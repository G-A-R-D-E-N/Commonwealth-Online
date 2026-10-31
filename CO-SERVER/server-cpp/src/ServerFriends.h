#pragma once

#include "ServerOutboundFrames.h"
#include "ServerProtocol.h"
#include "ServerSocialGraph.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace SessionTypes
	{
		struct Session;
	}

	using Session = SessionTypes::Session;

	struct FriendSessionState
	{
		std::map<std::string, std::string> tokenByProfile;
		std::map<std::string, std::string> profileByToken;
		std::uint32_t nextToken{ 1 };
	};

	class FriendService
	{
	public:
		using Json = nlohmann::json;
		using Sessions = std::unordered_map<std::uint32_t, Session>;

		struct Peer
		{
			std::uint32_t connectionId{};
			Session* session{};

			[[nodiscard]] explicit operator bool() const noexcept { return session != nullptr; }
		};

		static constexpr std::size_t kMaximumFriendsPerProfile{ 200 };
		static constexpr std::size_t kMaximumPendingPerTarget{ 5 };
		static constexpr std::size_t kMaximumRequestsPerWindow{ 10 };
		static constexpr double kRequestWindowSeconds{ 60.0 };
		static constexpr double kRequestLifetimeSeconds{ 60.0 };

		explicit FriendService(ProtocolStats& a_stats, SocialGraph& a_social) :
			stats(a_stats), social(a_social)
		{}

		[[nodiscard]] bool Enabled() const noexcept { return enabled; }
		bool Enable();
		void Disable() noexcept { enabled = false; }

		[[nodiscard]] static bool HandlesType(std::string_view a_type) noexcept;

		std::optional<std::vector<OutboundFrame>> Dispatch(
			std::uint32_t a_connectionId,
			Session& a_self,
			Sessions& a_sessions,
			std::string_view a_type,
			const Json& a_packet,
			double a_monotonicSeconds,
			double a_unixTimeSeconds,
			std::vector<std::string>& a_refreshProfiles);
		std::vector<OutboundFrame> JoinFrames(std::uint32_t a_connectionId, Sessions& a_sessions, double a_unixTimeSeconds);
		std::vector<OutboundFrame> DisconnectFrames(const std::string& a_profileId, Sessions& a_sessions, double a_unixTimeSeconds);
		std::vector<OutboundFrame> PollExpiry(Sessions& a_sessions, double a_monotonicSeconds, double a_unixTimeSeconds);
		std::vector<OutboundFrame> ForgetProfileFrames(const std::string& a_profileId, Sessions& a_sessions, double a_unixTimeSeconds, bool a_dropRequests = true);

	private:
		struct FriendRequest
		{
			std::string requestId;
			std::string fromProfileId;
			std::string toProfileId;
			std::string fromName;
			std::uint32_t fromPlayerId{};
			std::uint32_t toPlayerId{};
			double createdMonotonicSeconds{};
			double expiresAtUnixSeconds{};
		};

		std::vector<OutboundFrame> HandleRequest(std::uint32_t a_connectionId, Session& a_self, Sessions& a_sessions, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds, std::vector<std::string>& a_refreshProfiles);
		std::vector<OutboundFrame> HandleResponse(std::uint32_t a_connectionId, Session& a_self, Sessions& a_sessions, const Json& a_packet, double a_unixTimeSeconds, std::vector<std::string>& a_refreshProfiles);
		std::vector<OutboundFrame> HandleRemove(std::uint32_t a_connectionId, Session& a_self, Sessions& a_sessions, const Json& a_packet, double a_unixTimeSeconds);
		std::vector<OutboundFrame> AcceptRequest(std::uint32_t a_connectionId, Session& a_self, Sessions& a_sessions, std::map<std::string, FriendRequest>::iterator a_request, std::string_view a_op, double a_unixTimeSeconds, std::vector<std::string>& a_refreshProfiles);
		void ExpireRequests(Sessions& a_sessions, double a_monotonicSeconds, double a_unixTimeSeconds, std::vector<OutboundFrame>& a_frames);
		[[nodiscard]] bool RequestRateAllows(const std::string& a_profileId, double a_monotonicSeconds) const;
		[[nodiscard]] std::string TokenFor(Session& a_viewer, const std::string& a_profileId);
		[[nodiscard]] std::uint32_t WholeSecondsRemaining(double a_expiresAtUnixSeconds, double a_unixTimeSeconds) const;
		[[nodiscard]] Json StatePacket(Peer a_viewer, Sessions& a_sessions, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> StateFramesForProfiles(Sessions& a_sessions, const std::vector<std::string>& a_profileIds, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> PeerStateFrames(Sessions& a_sessions, const std::string& a_profileId, double a_unixTimeSeconds);
		[[nodiscard]] std::vector<OutboundFrame> RequestFailure(std::uint32_t a_connectionId, std::string_view a_op, std::string_view a_reason, double a_unixTimeSeconds);

		ProtocolStats& stats;
		SocialGraph& social;
		bool enabled{};
		std::uint32_t nextRequestNumber{ 1 };
		std::map<std::string, FriendRequest> requests;
		std::map<std::string, std::vector<double>> requestWindows;
	};
}
