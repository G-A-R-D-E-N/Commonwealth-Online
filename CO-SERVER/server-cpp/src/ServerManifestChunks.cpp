#include "ServerManifestChunks.h"

#include "F4TIdentityProtocol.h"
#include "F4TModpackChunks.h"
#include "ServerManifestChunkQueue.h"
#include "ServerRuntimeSupport.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr double kDeliveryTimeoutSeconds = 8.0;

		struct DeliveryKey
		{
			co_gns_server_handle server{};
			std::uint32_t connectionId{};

			[[nodiscard]] bool operator<(const DeliveryKey& a_other) const noexcept
			{
				if (server != a_other.server) {
					return server < a_other.server;
				}
				return connectionId < a_other.connectionId;
			}
		};

		struct ManifestChunkPackets
		{
			std::string fingerprint;
			std::vector<nlohmann::json> chunks;
			nlohmann::json endMarker;
			std::string failureReason;
		};

		struct ManifestChunkDelivery
		{
			std::shared_ptr<const ManifestChunkPackets> packets;
			ManifestChunkQueue queue;
		};

		std::map<DeliveryKey, ManifestChunkDelivery> g_deliveries;
		std::shared_ptr<const ManifestChunkPackets> g_packets;

		enum class SendOutcome
		{
			sent,
			backpressure,
			failed
		};

		SendOutcome SendJson(co_gns_server_handle a_server, std::uint32_t a_connectionId, const nlohmann::json& a_packet)
		{
			const auto payload = RuntimeSupport::JsonBytes(a_packet);
			switch (co_gns_server_send(
				a_server,
				a_connectionId,
				payload.data(),
				static_cast<std::uint32_t>(payload.size()),
				static_cast<std::uint32_t>(CO_GNS_DELIVERY_RELIABLE_ORDERED))) {
			case CO_GNS_SEND_SENT:
				return SendOutcome::sent;
			case CO_GNS_SEND_BACKPRESSURE:
				return SendOutcome::backpressure;
			default:
				return SendOutcome::failed;
			}
		}

		bool HasCapability(const nlohmann::json& a_capabilities, std::string_view a_capability)
		{
			if (!a_capabilities.is_array()) {
				return false;
			}
			for (const auto& item : a_capabilities) {
				if (item.is_string() && item.get_ref<const std::string&>() == a_capability) {
					return true;
				}
			}
			return false;
		}

		std::shared_ptr<const ManifestChunkPackets> BuildManifestChunkPackets(const std::string& a_fingerprint, std::string_view a_manifest)
		{
			auto packets = std::make_shared<ManifestChunkPackets>();
			packets->fingerprint = a_fingerprint;
			const auto description = F4T::ModpackChunks::Describe(a_manifest);
			const auto chunks = F4T::ModpackChunks::Split(a_manifest);
			if (!description || !chunks || chunks->size() != description->chunkCount) {
				packets->failureReason = "The server's modpack manifest could not be split for delivery.";
				return packets;
			}
			packets->chunks.reserve(chunks->size());
			for (const auto& chunk : *chunks) {
				packets->chunks.push_back(nlohmann::json{
					{ "type", std::string{ F4T::ModpackChunks::kChunkPacketType } },
					{ "fingerprint", a_fingerprint },
					{ "index", chunk.index },
					{ "total", description->chunkCount },
					{ "totalBytes", description->totalBytes },
					{ "sha256", description->sha256 },
					{ "data", chunk.payloadBase64 }
				});
			}
			packets->endMarker = nlohmann::json{
				{ "type", std::string{ F4T::ModpackChunks::kEndPacketType } },
				{ "fingerprint", a_fingerprint },
				{ "total", description->chunkCount },
				{ "totalBytes", description->totalBytes },
				{ "sha256", description->sha256 }
			};
			return packets;
		}

		void SendSessionEndedFrame(co_gns_server_handle a_server, std::uint32_t a_connectionId, double a_unixTimeSeconds, std::string_view a_code, std::string_view a_reason)
		{
			(void)SendJson(a_server, a_connectionId, nlohmann::json{
				{ "type", "sessionEnded" },
				{ "code", a_code },
				{ "reason", a_reason },
				{ "serverTime", a_unixTimeSeconds }
			});
		}
	}

	ManifestChunkRequest ServeManifestChunksIfRequested(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_message,
		const Configuration::Options& a_options,
		double a_unixTimeSeconds,
		const ManifestChunkDisconnectHandler& a_disconnect,
		F4T::Admission::AdmissionRegistry& a_admissions,
		std::uint64_t& a_admissionTicket)
	{
		if (!a_options.requiredModpackFingerprint || !a_options.requiredModpackManifestJson || a_message.empty()) {
			return ManifestChunkRequest::notRequest;
		}
		nlohmann::json request;
		try {
			request = nlohmann::json::parse(
				std::string_view{ reinterpret_cast<const char*>(a_message.data()), a_message.size() },
				nullptr,
				false);
		} catch (...) {
			return ManifestChunkRequest::notRequest;
		}
		if (request.is_discarded() || !request.is_object()) {
			return ManifestChunkRequest::notRequest;
		}
		const auto type = request.find("type");
		const auto fingerprint = request.find(std::string{ F4T::ModpackChunks::kRequestField });
		const auto capabilities = request.find("capabilities");
		if (type == request.end() || !type->is_string() || type->get_ref<const std::string&>() != "hello" ||
			fingerprint == request.end() || !fingerprint->is_string() ||
			capabilities == request.end() || !HasCapability(*capabilities, F4T::ModpackChunks::kCapabilityName) ||
			fingerprint->get_ref<const std::string&>() != *a_options.requiredModpackFingerprint) {
			return ManifestChunkRequest::notRequest;
		}

		if (const auto passwordRejection = F4T::Identity::CheckServerPassword(request, Configuration::IdentityRequirements(a_options))) {
			SendSessionEndedFrame(a_server, a_connectionId, a_unixTimeSeconds, passwordRejection->code, passwordRejection->reason);
			a_disconnect(a_connectionId, "Modpack manifest request refused");
			return ManifestChunkRequest::refused;
		}

		const DeliveryKey key{ a_server, a_connectionId };
		if (g_deliveries.contains(key)) {
			return ManifestChunkRequest::alreadyServing;
		}

		const auto& requiredFingerprint = *a_options.requiredModpackFingerprint;
		if (!g_packets || g_packets->fingerprint != requiredFingerprint) {
			g_packets = BuildManifestChunkPackets(requiredFingerprint, *a_options.requiredModpackManifestJson);
		}
		ManifestChunkDelivery delivery;
		delivery.packets = g_packets;
		delivery.queue.deadlineUnixSeconds = a_unixTimeSeconds + kDeliveryTimeoutSeconds;
		if (g_packets->failureReason.empty()) {
			delivery.queue.chunkCount = g_packets->chunks.size();
		} else {
			delivery.queue.failureReason = g_packets->failureReason;
		}
		g_deliveries.emplace(key, std::move(delivery));
		if (a_admissionTicket != 0) {
			(void)a_admissions.Reject(a_admissionTicket, "modpack_manifest_served");
			a_admissionTicket = 0;
		}
		return ManifestChunkRequest::started;
	}

	void AdvanceManifestChunkDeliveries(
		co_gns_server_handle a_server,
		double a_unixTimeSeconds,
		const ManifestChunkDisconnectHandler& a_disconnect)
	{
		for (auto it = g_deliveries.begin(); it != g_deliveries.end();) {
			if (it->first.server != a_server) {
				++it;
				continue;
			}
			auto& delivery = it->second;
			const auto connectionId = it->first.connectionId;
			const auto step = AdvanceManifestChunkQueue(delivery.queue, a_unixTimeSeconds, [&](ManifestChunkPacket a_packet, std::size_t a_index) {
				const auto& packet = a_packet == ManifestChunkPacket::chunk ? delivery.packets->chunks[a_index] : delivery.packets->endMarker;
				switch (SendJson(a_server, connectionId, packet)) {
				case SendOutcome::sent:
					return ManifestChunkSendResult::sent;
				case SendOutcome::backpressure:
					return ManifestChunkSendResult::blocked;
				default:
					return ManifestChunkSendResult::failed;
				}
			});
			if (step == ManifestChunkStep::blocked) {
				++it;
				continue;
			}
			const std::string reason = step == ManifestChunkStep::failed ? delivery.queue.failureReason : std::string{};
			it = g_deliveries.erase(it);
			if (step == ManifestChunkStep::complete) {
				a_disconnect(connectionId, "Modpack manifest served");
				continue;
			}
			SendSessionEndedFrame(a_server, connectionId, a_unixTimeSeconds, "modpack_manifest_unavailable", reason);
			a_disconnect(connectionId, reason.c_str());
		}
	}

	void ForgetManifestChunkDelivery(
		co_gns_server_handle a_server,
		std::uint32_t a_connectionId)
	{
		const auto found = g_deliveries.find(DeliveryKey{ a_server, a_connectionId });
		if (found != g_deliveries.end()) {
			g_deliveries.erase(found);
		}
	}
}
