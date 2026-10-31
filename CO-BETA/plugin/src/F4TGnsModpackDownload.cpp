#include "pch.h"

#include "F4TGnsModpackDownload.h"

#include "F4TGnsWinsockShimInternal.h"
#include "F4TGnsWinsockShimState.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string_view>
#include <thread>
#include <utility>

namespace
{
	using F4T::GnsWinsockShimState::BridgeApi;
	using F4T::GnsWinsockShimState::GnsClientHandle;
	using F4T::GnsWinsockShimState::GnsEvent;
	using F4T::GnsWinsockShimState::WipeServerPassword;

	constexpr auto kConnectTimeout = std::chrono::seconds{ 10 };
	constexpr auto kIdleTimeout = std::chrono::seconds{ 30 };
	constexpr auto kPollSleep = std::chrono::milliseconds{ 2 };
	// constraint: delivery value mirrors CO_GNS_DELIVERY_RELIABLE_ORDERED in CO-BETA/plugin/gns-bridge/co_gns_client_bridge.h
	constexpr std::uint32_t kReliableOrdered = 1;

	class Connection
	{
	public:
		Connection(const std::string& a_host, std::uint16_t a_port, const std::atomic_bool* a_cancel) :
			cancel(a_cancel)
		{
			std::string bridgeError;
			api = F4T::GnsWinsockShim::GetBridgeApi(bridgeError);
			std::array<char, 512> errorBuffer{};
			if (api == nullptr || api->create(a_host.c_str(), a_port, std::addressof(client), errorBuffer.data(), errorBuffer.size()) != 1 || !client) {
				client = nullptr;
				return;
			}
			const auto deadline = std::chrono::steady_clock::now() + kConnectTimeout;
			while (std::chrono::steady_clock::now() < deadline && !Cancelled()) {
				GnsEvent event{};
				const auto result = api->poll(client, std::addressof(event), payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (result < 0) {
					return;
				}
				if (result == 0) {
					std::this_thread::sleep_for(kPollSleep);
					continue;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventConnected) {
					connected = true;
					return;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventDisconnected) {
					return;
				}
			}
		}

		~Connection()
		{
			if (api != nullptr && client) {
				api->disconnect(client, 0, "modpack download finished");
				api->destroy(client);
			}
		}

		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;

		[[nodiscard]] bool Connected() const noexcept
		{
			return connected;
		}

		[[nodiscard]] bool Cancelled() const noexcept
		{
			return cancel != nullptr && cancel->load(std::memory_order_acquire);
		}

		[[nodiscard]] bool SendHello(std::string_view a_field, nlohmann::json a_request, const std::optional<std::string>& a_password)
		{
			auto hello = nlohmann::json{
				{ "type", "hello" },
				{ "protocolVersion", 2 },
				{ "capabilities", nlohmann::json::array({ std::string{ F4T::ModpackDownload::kCapabilityName } }) },
				{ std::string{ a_field }, std::move(a_request) }
			};
			if (a_password && !a_password->empty()) {
				hello["serverPassword"] = *a_password;
			}
			auto text = hello.dump();
			if (hello.contains("serverPassword")) {
				WipeServerPassword(hello["serverPassword"].get_ref<std::string&>());
			}
			const auto sent = api->send(client, text.data(), static_cast<std::uint32_t>(text.size()), kReliableOrdered);
			WipeServerPassword(text);
			return sent == F4T::GnsWinsockShimState::kSendSent;
		}

		[[nodiscard]] std::optional<nlohmann::json> Next()
		{
			auto deadline = std::chrono::steady_clock::now() + kIdleTimeout;
			while (std::chrono::steady_clock::now() < deadline && !Cancelled()) {
				GnsEvent event{};
				const auto result = api->poll(client, std::addressof(event), payload.data(), static_cast<std::uint32_t>(payload.size()));
				if (result < 0) {
					return std::nullopt;
				}
				if (result == 0) {
					std::this_thread::sleep_for(kPollSleep);
					continue;
				}
				if (event.type == F4T::GnsWinsockShimState::kEventDisconnected) {
					return std::nullopt;
				}
				if (event.type != F4T::GnsWinsockShimState::kEventMessage) {
					continue;
				}
				if (event.payloadSize > payload.size()) {
					return std::nullopt;
				}
				auto packet = nlohmann::json::parse(
					std::string_view{ reinterpret_cast<const char*>(payload.data()), event.payloadSize },
					nullptr,
					false);
				if (packet.is_discarded() || !packet.is_object()) {
					return std::nullopt;
				}
				return packet;
			}
			return std::nullopt;
		}

	private:
		BridgeApi* api{};
		GnsClientHandle client{};
		const std::atomic_bool* cancel{};
		bool connected{};
		std::array<std::uint8_t, F4T::GnsWinsockShim::kMaximumMessageBytes> payload{};
	};

	std::string RefusalText(const nlohmann::json& a_packet)
	{
		const auto reason = a_packet.find("reason");
		if (reason == a_packet.end() || !reason->is_string()) {
			return "The server refused the request.";
		}
		return reason->get<std::string>().substr(0, 256);
	}

	bool IsSessionEnded(const nlohmann::json& a_packet)
	{
		const auto type = a_packet.find("type");
		return type != a_packet.end() && type->is_string() && type->get_ref<const std::string&>() == "sessionEnded";
	}
}

namespace F4T::GnsModpackDownload
{
	GnsTransport::GnsTransport(std::string a_host, std::uint16_t a_port, std::optional<std::string> a_serverPassword, const std::atomic_bool* a_cancel) :
		host(std::move(a_host)),
		port(a_port),
		serverPassword(std::move(a_serverPassword)),
		cancel(a_cancel)
	{}

	GnsTransport::~GnsTransport()
	{
		if (serverPassword) {
			WipeServerPassword(*serverPassword);
		}
	}

	std::optional<ModpackDownload::Offer> GnsTransport::FetchOffer(const ModpackDownload::OfferRequest& a_request)
	{
		try {
			Connection connection(host, port, cancel);
			if (!connection.Connected() || !connection.SendHello(ModpackDownload::kOfferRequestField, ModpackDownload::OfferRequestObject(a_request), serverPassword)) {
				lastRefusal = "Could not reach the server.";
				return std::nullopt;
			}
			const auto packet = connection.Next();
			if (!packet) {
				lastRefusal = "The server did not answer.";
				return std::nullopt;
			}
			if (IsSessionEnded(*packet)) {
				lastRefusal = RefusalText(*packet);
				return std::nullopt;
			}
			auto offer = ModpackDownload::ParseOfferPacket(*packet);
			if (!offer || offer->fingerprint != a_request.fingerprint) {
				lastRefusal = "The server's answer was not valid.";
				return std::nullopt;
			}
			return offer;
		} catch (...) {
			lastRefusal = "The download request failed.";
			return std::nullopt;
		}
	}

	std::optional<ModpackDownload::FileEnd> GnsTransport::FetchFile(const ModpackDownload::FileRequest& a_request, const ChunkHandler& a_onChunk)
	{
		try {
			Connection connection(host, port, cancel);
			if (!connection.Connected() || !connection.SendHello(ModpackDownload::kFileRequestField, ModpackDownload::FileRequestObject(a_request), serverPassword)) {
				lastRefusal = "Could not reach the server.";
				return std::nullopt;
			}
			while (true) {
				const auto packet = connection.Next();
				if (!packet) {
					lastRefusal = "The download stopped.";
					return std::nullopt;
				}
				if (IsSessionEnded(*packet)) {
					lastRefusal = RefusalText(*packet);
					return std::nullopt;
				}
				if (auto chunk = ModpackDownload::ParseChunkPacket(*packet)) {
					if (!a_onChunk(*chunk)) {
						return std::nullopt;
					}
					continue;
				}
				return ModpackDownload::ParseEndPacket(*packet);
			}
		} catch (...) {
			lastRefusal = "The download failed.";
			return std::nullopt;
		}
	}

	const std::string& GnsTransport::LastRefusal() const noexcept
	{
		return lastRefusal;
	}
}
