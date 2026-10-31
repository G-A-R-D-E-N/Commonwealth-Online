#include "pch.h"

#include "F4TGnsManifestFetch.h"

#include "F4TGnsWinsockShimInternal.h"
#include "F4TGnsWinsockShimState.h"
#include "F4TModpackChunks.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace
{
	using F4T::GnsWinsockShimState::BridgeApi;
	using F4T::GnsWinsockShimState::GnsEvent;
	using F4T::GnsWinsockShimState::GnsClientHandle;
	using F4T::GnsWinsockShimState::WipeServerPassword;

	constexpr auto kFetchTimeout = std::chrono::seconds{ 10 };
	constexpr auto kPollSleep = std::chrono::milliseconds{ 5 };
	constexpr std::size_t kMaximumMessages = 256;
	// constraint: delivery value mirrors CO_GNS_DELIVERY_RELIABLE_ORDERED in CO-BETA/plugin/gns-bridge/co_gns_client_bridge.h
	constexpr std::uint32_t kReliableOrdered = 1;

	bool MatchesChunkInfo(const nlohmann::json& a_packet, const F4T::GnsProtocolAdapter::ModpackChunksInfo& a_chunks, const std::string& a_fingerprint)
	{
		const auto fingerprint = a_packet.find("fingerprint");
		const auto total = a_packet.find("total");
		const auto totalBytes = a_packet.find("totalBytes");
		const auto sha = a_packet.find("sha256");
		return fingerprint != a_packet.end() && fingerprint->is_string() &&
			fingerprint->get_ref<const std::string&>() == a_fingerprint &&
			total != a_packet.end() && total->is_number_unsigned() && total->get<std::uint64_t>() == a_chunks.chunkCount &&
			totalBytes != a_packet.end() && totalBytes->is_number_unsigned() && totalBytes->get<std::uint64_t>() == a_chunks.totalBytes &&
			sha != a_packet.end() && sha->is_string() && sha->get_ref<const std::string&>() == a_chunks.sha256;
	}
}

namespace F4T::GnsManifestFetch
{
	std::optional<ModpackManifest::Manifest> FetchManifest(
		const std::string& a_host,
		std::uint16_t a_port,
		const std::string& a_fingerprint,
		const std::string& a_serverPassword,
		const GnsProtocolAdapter::ModpackChunksInfo& a_chunks,
		const std::atomic_bool* a_cancelRequested)
	{
		const auto cancelled = [a_cancelRequested] {
			return a_cancelRequested != nullptr && a_cancelRequested->load(std::memory_order_acquire);
		};
		try {
			if (a_host.empty() || a_port == 0 || a_fingerprint.size() != 64 || cancelled()) {
				return std::nullopt;
			}
			std::string bridgeError;
			auto* api = F4T::GnsWinsockShim::GetBridgeApi(bridgeError);
			if (api == nullptr) {
				return std::nullopt;
			}
			std::array<char, 512> errorBuffer{};
			GnsClientHandle client = nullptr;
			if (api->create(a_host.c_str(), a_port, std::addressof(client), errorBuffer.data(), errorBuffer.size()) != 1 || !client) {
				return std::nullopt;
			}
			struct ClientGuard
			{
				BridgeApi* api;
				GnsClientHandle client;
				~ClientGuard()
				{
					api->disconnect(client, 0, "modpack manifest fetch finished");
					api->destroy(client);
				}
			} guard{ api, client };

			const auto deadline = std::chrono::steady_clock::now() + kFetchTimeout;
			std::array<std::uint8_t, F4T::GnsWinsockShim::kMaximumMessageBytes> payload{};
			bool connected = false;
			while (std::chrono::steady_clock::now() < deadline) {
				if (cancelled()) {
					return std::nullopt;
				}
				GnsEvent event{};
				const auto pollResult = api->poll(client, std::addressof(event), payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (pollResult < 0) {
					return std::nullopt;
				}
				if (pollResult == 0) {
					std::this_thread::sleep_for(kPollSleep);
					continue;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventConnected) {
					connected = true;
					break;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventDisconnected) {
					return std::nullopt;
				}
			}
			if (!connected) {
				return std::nullopt;
			}
			auto hello = nlohmann::json{
				{ "type", "hello" },
				{ "protocolVersion", 2 },
				{ "capabilities", nlohmann::json::array({ std::string{ F4T::ModpackChunks::kCapabilityName } }) },
				{ std::string{ F4T::ModpackChunks::kRequestField }, a_fingerprint }
			};
			if (!a_serverPassword.empty()) {
				hello["serverPassword"] = a_serverPassword;
			}
			auto request = hello.dump();
			if (hello.contains("serverPassword")) {
				WipeServerPassword(hello["serverPassword"].get_ref<std::string&>());
			}
			const auto sent = api->send(client, request.data(), static_cast<std::uint32_t>(request.size()), kReliableOrdered);
			WipeServerPassword(request);
			if (sent != F4T::GnsWinsockShimState::kSendSent) {
				return std::nullopt;
			}

			F4T::ModpackChunks::Reassembler reassembler(F4T::ModpackChunks::Description{
				a_chunks.totalBytes,
				a_chunks.chunkCount,
				a_chunks.sha256
			});
			std::size_t messages = 0;
			while (std::chrono::steady_clock::now() < deadline) {
				if (cancelled()) {
					return std::nullopt;
				}
				GnsEvent event{};
				const auto pollResult = api->poll(client, std::addressof(event), payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (pollResult < 0) {
					return std::nullopt;
				}
				if (pollResult == 0) {
					std::this_thread::sleep_for(kPollSleep);
					continue;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventDisconnected) {
					break;
				}
				if (event.type != F4T::GnsWinsockShimState::kEventMessage) {
					continue;
				}
				if (++messages > kMaximumMessages || event.payloadSize > payload.size()) {
					return std::nullopt;
				}
				const auto packet = nlohmann::json::parse(
					std::string_view{ reinterpret_cast<const char*>(payload.data()), event.payloadSize },
					nullptr,
					false);
				if (packet.is_discarded() || !packet.is_object()) {
					return std::nullopt;
				}
				const auto type = packet.find("type");
				if (type == packet.end() || !type->is_string()) {
					return std::nullopt;
				}
				const auto& typeText = type->get_ref<const std::string&>();
				if (typeText == F4T::ModpackChunks::kChunkPacketType) {
					if (!MatchesChunkInfo(packet, a_chunks, a_fingerprint)) {
						return std::nullopt;
					}
					const auto index = packet.find("index");
					const auto data = packet.find("data");
					if (index == packet.end() || !index->is_number_unsigned() || index->get<std::uint64_t>() >= a_chunks.chunkCount || data == packet.end() || !data->is_string()) {
						return std::nullopt;
					}
					if (reassembler.Add(static_cast<std::uint32_t>(index->get<std::uint64_t>()), data->get_ref<const std::string&>()) != F4T::ModpackChunks::AddResult::accepted) {
						return std::nullopt;
					}
					continue;
				}
				if (typeText == F4T::ModpackChunks::kEndPacketType) {
					if (!MatchesChunkInfo(packet, a_chunks, a_fingerprint) || !reassembler.Complete()) {
						return std::nullopt;
					}
					const auto manifest = reassembler.Assemble();
					if (!manifest) {
						return std::nullopt;
					}
					return ModpackManifest::ParseManifest(*manifest);
				}
				if (typeText == "sessionEnded") {
					return std::nullopt;
				}
			}
			return std::nullopt;
		} catch (...) {
			return std::nullopt;
		}
	}
}
