#include "ServerResourceBridge.h"

#include "F4TPacketRateLimit.h"
#include "ServerPacketDecode.h"
#include "ServerPacketRead.h"
#include "ServerOutboundFrames.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace StateBagProtocol = F4T::StateBagProtocol;

	namespace
	{
		using Json = nlohmann::json;

		constexpr double kStateBagRetrySeconds = 1.0;

		std::vector<OutboundFrame> CapablePlayerFrames(
			StateBagSessions& a_sessions,
			std::uint32_t a_playerId,
			std::string_view a_resource,
			std::string_view a_view,
			bool a_open,
			const Json& a_packet)
		{
			std::vector<OutboundFrame> frames;
			for (auto& [connectionId, session] : a_sessions) {
				if (session.active && session.playerId == a_playerId && session.compatibilityCapabilities.Has(StateBagProtocol::kCapability)) {
					if (a_open) {
						session.openResourceViews[std::string{ a_resource }] = std::string{ a_view };
					} else {
						session.openResourceViews.erase(std::string{ a_resource });
					}
					frames.push_back(ReliableFrame(connectionId, a_packet));
				}
			}
			return frames;
		}
	}

	std::vector<OutboundFrame> ResourceViewOpenFrames(
		StateBagSessions& a_sessions,
		std::uint32_t a_playerId,
		std::string_view a_resource,
		std::string_view a_view,
		std::string& a_failure)
	{
		if (!StateBagProtocol::IsValidResourceName(a_resource)) {
			a_failure = "invalid resource name";
			return {};
		}
		if (!StateBagProtocol::IsValidViewPath(a_view)) {
			a_failure = "invalid view path";
			return {};
		}
		auto frames = CapablePlayerFrames(a_sessions, a_playerId, a_resource, a_view, true, Json{
			{ "type", std::string{ StateBagProtocol::kResourceViewPacketType } },
			{ "op", "open" },
			{ "resource", std::string{ a_resource } },
			{ "view", std::string{ a_view } }
		});
		if (frames.empty()) {
			a_failure = "player has no state-bag-v1 session";
			return {};
		}
		return frames;
	}

	std::vector<OutboundFrame> ResourceViewCloseFrames(
		StateBagSessions& a_sessions,
		std::uint32_t a_playerId,
		std::string_view a_resource)
	{
		if (!StateBagProtocol::IsValidResourceName(a_resource)) {
			return {};
		}
		return CapablePlayerFrames(a_sessions, a_playerId, a_resource, {}, false, Json{
			{ "type", std::string{ StateBagProtocol::kResourceViewPacketType } },
			{ "op", "close" },
			{ "resource", std::string{ a_resource } }
		});
	}

	bool IsResourceViewOpen(
		const StateBagSessions& a_sessions,
		std::uint32_t a_playerId,
		std::string_view a_resource)
	{
		for (const auto& [connectionId, session] : a_sessions) {
			if (session.active && session.playerId == a_playerId && session.openResourceViews.contains(std::string{ a_resource })) {
				return true;
			}
		}
		return false;
	}

	void RecordStateBagResync(StateBagSessions& a_sessions, const OutboundFrame& a_frame)
	{
		const auto session = a_sessions.find(a_frame.connectionId);
		if (session == a_sessions.end()) {
			return;
		}
		const auto packet = PacketDecode::DecodeJson(a_frame.payload);
		const auto type = packet ? ReadString(*packet, "type") : std::optional<std::string>{};
		if (!type) {
			return;
		}
		if (*type == StateBagProtocol::kPacketType) {
			session->second.stateBagResync = true;
		} else if (*type == StateBagProtocol::kResourceViewPacketType) {
			if (const auto resource = ReadString(*packet, "resource")) {
				session->second.resourceViewResync.insert(*resource);
			}
		}
	}

	std::vector<OutboundFrame> RetryStateBagResync(StateBagSessions& a_sessions, const StateBagStore& a_store, double a_monotonicSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (auto& [connectionId, session] : a_sessions) {
			const bool pending = session.stateBagResync || !session.resourceViewResync.empty();
			if (!pending || !session.active || !session.compatibilityCapabilities.Has(StateBagProtocol::kCapability) || a_monotonicSeconds < session.stateBagRetryAt) {
				continue;
			}
			session.stateBagRetryAt = a_monotonicSeconds + kStateBagRetrySeconds;
			if (session.stateBagResync) {
				session.stateBagResync = false;
				for (auto& frame : StateBagSnapshotFrames(connectionId, session, a_store)) {
					frames.push_back(std::move(frame));
				}
			}
			for (const auto& resource : session.resourceViewResync) {
				const auto view = session.openResourceViews.find(resource);
				if (view != session.openResourceViews.end()) {
					frames.push_back(ReliableFrame(connectionId, Json{
						{ "type", std::string{ StateBagProtocol::kResourceViewPacketType } },
						{ "op", "open" },
						{ "resource", resource },
						{ "view", view->second }
					}));
				} else {
					frames.push_back(ReliableFrame(connectionId, Json{
						{ "type", std::string{ StateBagProtocol::kResourceViewPacketType } },
						{ "op", "close" },
						{ "resource", resource }
					}));
				}
			}
			session.resourceViewResync.clear();
		}
		return frames;
	}

	std::vector<OutboundFrame> HandleResourceMessage(
		SessionTypes::Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		ProtocolStats& a_stats,
		const ResourceMessageHandler& a_handler)
	{
		const auto reject = [&a_stats]() {
			a_stats.packetsRejected++;
			return std::vector<OutboundFrame>{};
		};
		if (!a_session.compatibilityCapabilities.Has(StateBagProtocol::kCapability)) {
			return reject();
		}
		const auto resource = ReadString(a_packet, "resource");
		const auto data = a_packet.find("data");
		if (!resource || !StateBagProtocol::IsValidResourceName(*resource) || data == a_packet.end() || !StateBagProtocol::IsValidResourceData(*data)) {
			return reject();
		}
		if (!a_session.openResourceViews.contains(std::string{ *resource })) {
			return reject();
		}
		if (!F4T::PacketRateLimit::TakeToken(
				a_session.resourceMessages,
				a_monotonicSeconds,
				StateBagProtocol::kMaximumResourceMessagesPerSecond,
				StateBagProtocol::kMaximumResourceMessagesPerSecond)) {
			return reject();
		}
		if (a_handler) {
			a_handler(a_session.playerId, *resource, *data);
		}
		return {};
	}
}
