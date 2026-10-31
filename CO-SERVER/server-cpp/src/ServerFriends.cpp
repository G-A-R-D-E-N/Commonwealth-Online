#include "ServerFriends.h"

#include "F4TFriendValidation.h"
#include "F4TIdentityProtocol.h"
#include "ServerPacketRead.h"
#include "ServerProtocolImpl.h"
#include "ServerSessionTypes.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;
		using Peer = FriendService::Peer;

		bool HasFriends(const Session& a_session)
		{
			return a_session.compatibilityCapabilities.Has(F4T::Identity::FriendsCapability);
		}

		std::optional<Peer> PeerForProfile(FriendService::Sessions& a_sessions, const std::string& a_profileId)
		{
			if (a_profileId.empty()) return std::nullopt;
			for (auto& [connectionId, session] : a_sessions) {
				if (session.active && session.profileId == a_profileId) return Peer{ connectionId, &session };
			}
			return std::nullopt;
		}

		std::optional<Peer> PeerForPlayer(FriendService::Sessions& a_sessions, std::uint32_t a_playerId)
		{
			if (a_playerId == 0) return std::nullopt;
			for (auto& [connectionId, session] : a_sessions) {
				if (session.active && session.playerId == a_playerId) return Peer{ connectionId, &session };
			}
			return std::nullopt;
		}

		Json ResultPacket(std::string_view a_op, bool a_ok, std::string_view a_reason, double a_unixTimeSeconds)
		{
			Json packet{ { "type", "friendResult" }, { "op", a_op }, { "ok", a_ok } };
			if (!a_ok && !a_reason.empty()) packet["error"] = a_reason;
			packet["serverTime"] = a_unixTimeSeconds;
			return packet;
		}

		Json RequestReceivedPacket(
			const std::string& a_requestId,
			const SocialGraph::NameView& a_view,
			std::uint32_t a_fromPlayerId,
			std::uint32_t a_expiresInSeconds,
			double a_unixTimeSeconds)
		{
			Json packet{
				{ "type", "friendRequestReceived" },
				{ "requestId", a_requestId },
				{ "fromPlayerId", a_fromPlayerId },
				{ "displayName", a_view.displayName },
				{ "expiresInSeconds", a_expiresInSeconds },
				{ "serverTime", a_unixTimeSeconds }
			};
			const auto& view = a_view;
			if (view.disclosedName) packet["fromName"] = *view.disclosedName;
			return packet;
		}
	}

	bool FriendService::Enable()
	{
		if (!social.Enabled()) return false;
		enabled = true;
		return true;
	}

	bool FriendService::HandlesType(std::string_view a_type) noexcept
	{
		return a_type == "friendRequest" || a_type == "friendRequestResponse" || a_type == "friendRemove" || a_type == "friendListRequest";
	}

	std::uint32_t FriendService::WholeSecondsRemaining(double a_expiresAtUnixSeconds, double a_unixTimeSeconds) const
	{
		return static_cast<std::uint32_t>(std::clamp(std::ceil(a_expiresAtUnixSeconds - a_unixTimeSeconds), 0.0, kRequestLifetimeSeconds));
	}

	bool FriendService::RequestRateAllows(const std::string& a_profileId, double a_monotonicSeconds) const
	{
		const auto recent = requestWindows.find(a_profileId);
		if (recent == requestWindows.end()) return true;
		const auto inWindow = std::count_if(recent->second.begin(), recent->second.end(), [a_monotonicSeconds](double a_sent) {
			return a_monotonicSeconds - a_sent <= kRequestWindowSeconds;
		});
		return static_cast<std::size_t>(inWindow) < kMaximumRequestsPerWindow;
	}

	std::string FriendService::TokenFor(Session& a_viewer, const std::string& a_profileId)
	{
		if (const auto existing = a_viewer.friends.tokenByProfile.find(a_profileId); existing != a_viewer.friends.tokenByProfile.end()) {
			return existing->second;
		}
		const auto token = "f" + std::to_string(a_viewer.friends.nextToken++);
		a_viewer.friends.tokenByProfile[a_profileId] = token;
		a_viewer.friends.profileByToken[token] = a_profileId;
		return token;
	}

	Json FriendService::StatePacket(Peer a_viewer, Sessions& a_sessions, double a_unixTimeSeconds)
	{
		auto& viewer = *a_viewer.session;
		Json friends = Json::array();
		Json incoming = Json::array();
		Json outgoing = Json::array();
		if (!viewer.profileId.empty()) {
			if (const auto* list = social.FriendsOf(viewer.profileId)) {
				for (const auto& entry : *list) {
					const auto online = PeerForProfile(a_sessions, entry.profileId);
					Json row{
						{ "friendId", TokenFor(viewer, entry.profileId) },
						{ "name", online ? std::string{ SessionNameView(*online->session) } : entry.name },
						{ "online", online.has_value() }
					};
					if (online) row["playerId"] = online->session->playerId;
					friends.push_back(std::move(row));
				}
			}
			for (const auto& [requestId, request] : requests) {
				if (request.toProfileId == viewer.profileId) {
					const auto from = PeerForProfile(a_sessions, request.fromProfileId);
					const auto view = social.NameViewFor(viewer.profileId, request.fromProfileId, request.fromName);
					incoming.push_back(Json{
						{ "requestId", requestId },
						{ "fromPlayerId", from ? from->session->playerId : request.fromPlayerId },
						{ "displayName", view.displayName }
					});
				} else if (request.fromProfileId == viewer.profileId) {
					const auto target = PeerForProfile(a_sessions, request.toProfileId);
					outgoing.push_back(target ? target->session->playerId : request.toPlayerId);
				}
			}
		}
		return Json{
			{ "type", "friendState" },
			{ "friends", std::move(friends) },
			{ "pendingIncoming", std::move(incoming) },
			{ "pendingOutgoingPlayerIds", std::move(outgoing) },
			{ "serverTime", a_unixTimeSeconds }
		};
	}

	std::vector<OutboundFrame> FriendService::PeerStateFrames(Sessions& a_sessions, const std::string& a_profileId, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		const auto peer = PeerForProfile(a_sessions, a_profileId);
		if (!peer) return frames;
		// Constraint: a client without friends-v1 must never receive a friend packet.
		if (!HasFriends(*peer->session)) return frames;
		frames.push_back(ReliableFrame(peer->connectionId, StatePacket(*peer, a_sessions, a_unixTimeSeconds)));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::StateFramesForProfiles(Sessions& a_sessions, const std::vector<std::string>& a_profileIds, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		for (const auto& profileId : a_profileIds) AppendFrames(frames, PeerStateFrames(a_sessions, profileId, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::RequestFailure(std::uint32_t a_connectionId, std::string_view a_op, std::string_view a_reason, double a_unixTimeSeconds)
	{
		stats.packetsRejected++;
		return { ReliableFrame(a_connectionId, ResultPacket(a_op, false, a_reason, a_unixTimeSeconds)) };
	}

	void FriendService::ExpireRequests(Sessions& a_sessions, double a_monotonicSeconds, double a_unixTimeSeconds, std::vector<OutboundFrame>& a_frames)
	{
		std::vector<std::string> expired;
		for (const auto& [requestId, request] : requests) {
			const auto age = a_monotonicSeconds - request.createdMonotonicSeconds;
			if (!(age >= 0.0) || age > kRequestLifetimeSeconds || request.expiresAtUnixSeconds <= a_unixTimeSeconds) expired.push_back(requestId);
		}
		for (const auto& requestId : expired) {
			const auto request = requests.find(requestId);
			if (request == requests.end()) continue;
			const auto fromProfileId = request->second.fromProfileId;
			const auto toProfileId = request->second.toProfileId;
			requests.erase(request);
			if (const auto from = PeerForProfile(a_sessions, fromProfileId)) {
				// Constraint: expiry must not disclose a name; the requester only learns the request ended.
				a_frames.push_back(ReliableFrame(from->connectionId, ResultPacket("friendRequest", false, "requestExpired", a_unixTimeSeconds)));
			}
			AppendFrames(a_frames, StateFramesForProfiles(a_sessions, { fromProfileId, toProfileId }, a_unixTimeSeconds));
		}
	}

	std::vector<OutboundFrame> FriendService::PollExpiry(Sessions& a_sessions, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		if (!enabled) return frames;
		ExpireRequests(a_sessions, a_monotonicSeconds, a_unixTimeSeconds, frames);
		return frames;
	}

	std::optional<std::vector<OutboundFrame>> FriendService::Dispatch(
		std::uint32_t a_connectionId,
		Session& a_self,
		Sessions& a_sessions,
		std::string_view a_type,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::string>& a_refreshProfiles)
	{
		if (!enabled || !HandlesType(a_type)) return std::nullopt;
		if (!HasFriends(a_self)) return RequestFailure(a_connectionId, a_type, "unsupported", a_unixTimeSeconds);
		std::vector<OutboundFrame> frames;
		ExpireRequests(a_sessions, a_monotonicSeconds, a_unixTimeSeconds, frames);
		std::optional<std::vector<OutboundFrame>> result;
		if (a_type == "friendRequest") {
			result = HandleRequest(a_connectionId, a_self, a_sessions, a_packet, a_monotonicSeconds, a_unixTimeSeconds, a_refreshProfiles);
		} else if (a_type == "friendRequestResponse") {
			result = HandleResponse(a_connectionId, a_self, a_sessions, a_packet, a_unixTimeSeconds, a_refreshProfiles);
		} else if (a_type == "friendRemove") {
			result = HandleRemove(a_connectionId, a_self, a_sessions, a_packet, a_unixTimeSeconds);
		} else {
			result = PeerStateFrames(a_sessions, a_self.profileId, a_unixTimeSeconds);
		}
		if (result) AppendFrames(frames, std::move(*result));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::HandleRequest(
		std::uint32_t a_connectionId,
		Session& a_self,
		Sessions& a_sessions,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds,
		std::vector<std::string>& a_refreshProfiles)
	{
		const auto normalized = F4T::FriendValidation::NormalizeFriendRequest(a_packet);
		if (!normalized) return {};
		const auto targetPlayerId = ReadUInt32(*normalized, "targetPlayerId").value_or(0);
		if (targetPlayerId == 0 || targetPlayerId == a_self.playerId || a_self.profileId.empty()) {
			return RequestFailure(a_connectionId, "friendRequest", "invalidTarget", a_unixTimeSeconds);
		}
		const auto target = PeerForPlayer(a_sessions, targetPlayerId);
		if (!target || target->session->profileId.empty() || target->session->profileId == a_self.profileId) {
			return RequestFailure(a_connectionId, "friendRequest", "targetUnavailable", a_unixTimeSeconds);
		}
		if (!HasFriends(*target->session)) return RequestFailure(a_connectionId, "friendRequest", "targetUnsupported", a_unixTimeSeconds);
		const auto targetProfileId = target->session->profileId;
		if (social.AreFriends(a_self.profileId, targetProfileId)) return RequestFailure(a_connectionId, "friendRequest", "alreadyFriends", a_unixTimeSeconds);

		for (auto pending = requests.begin(); pending != requests.end(); ++pending) {
			if (pending->second.fromProfileId != targetProfileId || pending->second.toProfileId != a_self.profileId) continue;
			// Constraint: two players asking each other at once is an accept, not a second request.
			return AcceptRequest(a_connectionId, a_self, a_sessions, pending, "friendRequest", a_unixTimeSeconds, a_refreshProfiles);
		}
		const auto sameDirection = std::any_of(requests.begin(), requests.end(), [&](const auto& a_entry) {
			return a_entry.second.fromProfileId == a_self.profileId && a_entry.second.toProfileId == targetProfileId;
		});
		if (sameDirection) return RequestFailure(a_connectionId, "friendRequest", "requestExists", a_unixTimeSeconds);
		if (social.FriendCount(a_self.profileId) >= kMaximumFriendsPerProfile || social.FriendCount(targetProfileId) >= kMaximumFriendsPerProfile) {
			return RequestFailure(a_connectionId, "friendRequest", "friendListFull", a_unixTimeSeconds);
		}
		const auto pendingForTarget = std::count_if(requests.begin(), requests.end(), [&targetProfileId](const auto& a_entry) {
			return a_entry.second.toProfileId == targetProfileId;
		});
		if (static_cast<std::size_t>(pendingForTarget) >= kMaximumPendingPerTarget) {
			return RequestFailure(a_connectionId, "friendRequest", "requestCapacity", a_unixTimeSeconds);
		}
		if (!RequestRateAllows(a_self.profileId, a_monotonicSeconds)) {
			return RequestFailure(a_connectionId, "friendRequest", "rateLimited", a_unixTimeSeconds);
		}

		const auto requestId = "fr-" + std::to_string(nextRequestNumber++);
		const auto fromName = std::string{ SessionNameView(a_self) };
		requests.emplace(requestId, FriendRequest{
			requestId,
			a_self.profileId,
			targetProfileId,
			fromName,
			a_self.playerId,
			target->session->playerId,
			a_monotonicSeconds,
			a_unixTimeSeconds + kRequestLifetimeSeconds
		});
		requestWindows[a_self.profileId].push_back(a_monotonicSeconds);
		std::erase_if(requestWindows, [a_monotonicSeconds](auto& a_entry) {
			std::erase_if(a_entry.second, [a_monotonicSeconds](double a_sent) { return a_monotonicSeconds - a_sent > kRequestWindowSeconds; });
			return a_entry.second.empty();
		});

		std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, ResultPacket("friendRequest", true, {}, a_unixTimeSeconds)) };
		// Constraint: a stranger's real name must never reach the target of a friend request.
		const auto view = social.NameViewFor(targetProfileId, a_self.profileId, fromName);
		frames.push_back(ReliableFrame(
			target->connectionId,
			RequestReceivedPacket(requestId, view, a_self.playerId, WholeSecondsRemaining(a_unixTimeSeconds + kRequestLifetimeSeconds, a_unixTimeSeconds), a_unixTimeSeconds)));
		AppendFrames(frames, StateFramesForProfiles(a_sessions, { a_self.profileId, targetProfileId }, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::AcceptRequest(
		std::uint32_t a_connectionId,
		Session& a_self,
		Sessions& a_sessions,
		std::map<std::string, FriendRequest>::iterator a_request,
		std::string_view a_op,
		double a_unixTimeSeconds,
		std::vector<std::string>& a_refreshProfiles)
	{
		const auto request = a_request->second;
		const auto from = PeerForProfile(a_sessions, request.fromProfileId);
		if (!from) {
			requests.erase(a_request);
			return RequestFailure(a_connectionId, a_op, "targetUnavailable", a_unixTimeSeconds);
		}
		const auto& fromProfileId = request.fromProfileId;
		if (social.FriendCount(a_self.profileId) >= kMaximumFriendsPerProfile || social.FriendCount(fromProfileId) >= kMaximumFriendsPerProfile) {
			requests.erase(a_request);
			return RequestFailure(a_connectionId, a_op, "friendListFull", a_unixTimeSeconds);
		}

		const auto selfFriends = social.FriendListCopy(a_self.profileId);
		const auto fromFriends = social.FriendListCopy(fromProfileId);
		const auto selfKnown = social.KnownListCopy(a_self.profileId);
		const auto fromKnown = social.KnownListCopy(fromProfileId);
		if (!social.AddFriend(a_self.profileId, SessionNameView(a_self), fromProfileId, SessionNameView(*from->session))) {
			return RequestFailure(a_connectionId, a_op, "friendListFull", a_unixTimeSeconds);
		}
		social.NoteKnown(a_self.profileId, fromProfileId);
		if (!social.SaveNow()) {
			social.RestoreFriendList(a_self.profileId, selfFriends);
			social.RestoreFriendList(fromProfileId, fromFriends);
			// Constraint: a failed save must also unlearn the names, or both players stay unmasked.
			social.RestoreKnownList(a_self.profileId, selfKnown);
			social.RestoreKnownList(fromProfileId, fromKnown);
			return RequestFailure(a_connectionId, a_op, "persistenceUnavailable", a_unixTimeSeconds);
		}
		requests.erase(a_request);
		std::erase_if(requests, [&](const auto& a_entry) {
			return (a_entry.second.fromProfileId == a_self.profileId && a_entry.second.toProfileId == fromProfileId) ||
				(a_entry.second.fromProfileId == fromProfileId && a_entry.second.toProfileId == a_self.profileId);
		});
		a_refreshProfiles.push_back(fromProfileId);
		std::vector<OutboundFrame> frames{
			ReliableFrame(a_connectionId, ResultPacket(a_op, true, {}, a_unixTimeSeconds)),
			ReliableFrame(from->connectionId, ResultPacket("friendRequest", true, {}, a_unixTimeSeconds))
		};
		AppendFrames(frames, StateFramesForProfiles(a_sessions, { a_self.profileId, fromProfileId }, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::HandleResponse(
		std::uint32_t a_connectionId,
		Session& a_self,
		Sessions& a_sessions,
		const Json& a_packet,
		double a_unixTimeSeconds,
		std::vector<std::string>& a_refreshProfiles)
	{
		const auto normalized = F4T::FriendValidation::NormalizeFriendRequestResponse(a_packet);
		if (!normalized) return {};
		const auto requestId = ReadString(*normalized, "requestId").value_or("");
		auto request = requests.find(requestId);
		if (request == requests.end()) return RequestFailure(a_connectionId, "friendRequestResponse", "requestNotFound", a_unixTimeSeconds);
		if (request->second.toProfileId != a_self.profileId) return RequestFailure(a_connectionId, "friendRequestResponse", "requestNotFound", a_unixTimeSeconds);
		if (normalized->at("accept").get<bool>()) {
			return AcceptRequest(a_connectionId, a_self, a_sessions, request, "friendRequestResponse", a_unixTimeSeconds, a_refreshProfiles);
		}
		const auto fromProfileId = request->second.fromProfileId;
		requests.erase(request);
		std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, ResultPacket("friendRequestResponse", true, {}, a_unixTimeSeconds)) };
		if (const auto from = PeerForProfile(a_sessions, fromProfileId)) {
			// Constraint: a decline carries no name, only the end of the request.
			frames.push_back(ReliableFrame(from->connectionId, ResultPacket("friendRequest", false, "declined", a_unixTimeSeconds)));
		}
		AppendFrames(frames, StateFramesForProfiles(a_sessions, { a_self.profileId, fromProfileId }, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::HandleRemove(
		std::uint32_t a_connectionId,
		Session& a_self,
		Sessions& a_sessions,
		const Json& a_packet,
		double a_unixTimeSeconds)
	{
		const auto normalized = F4T::FriendValidation::NormalizeFriendRemove(a_packet);
		if (!normalized) return {};
		const auto token = ReadString(*normalized, "friendId").value_or("");
		const auto resolved = a_self.friends.profileByToken.find(token);
		if (resolved == a_self.friends.profileByToken.end()) return RequestFailure(a_connectionId, "friendRemove", "notFriends", a_unixTimeSeconds);
		const auto friendProfileId = resolved->second;
		if (!social.AreFriends(a_self.profileId, friendProfileId)) return RequestFailure(a_connectionId, "friendRemove", "notFriends", a_unixTimeSeconds);

		const auto selfFriends = social.FriendListCopy(a_self.profileId);
		const auto friendFriends = social.FriendListCopy(friendProfileId);
		social.RemoveFriend(a_self.profileId, friendProfileId);
		if (!social.SaveNow()) {
			social.RestoreFriendList(a_self.profileId, selfFriends);
			social.RestoreFriendList(friendProfileId, friendFriends);
			return RequestFailure(a_connectionId, "friendRemove", "persistenceUnavailable", a_unixTimeSeconds);
		}
		std::vector<OutboundFrame> frames{ ReliableFrame(a_connectionId, ResultPacket("friendRemove", true, {}, a_unixTimeSeconds)) };
		AppendFrames(frames, StateFramesForProfiles(a_sessions, { a_self.profileId, friendProfileId }, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::JoinFrames(std::uint32_t a_connectionId, Sessions& a_sessions, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		if (!enabled) return frames;
		const auto found = a_sessions.find(a_connectionId);
		if (found == a_sessions.end() || !found->second.active || found->second.profileId.empty()) return frames;
		const Peer viewer{ a_connectionId, &found->second };
		if (!HasFriends(*viewer.session)) return frames;
		const auto profileId = viewer.session->profileId;
		const auto name = std::string{ SessionNameView(*viewer.session) };
		if (name != "Unknown" && social.NoteFriendName(profileId, name)) (void)social.SaveNow();
		frames.push_back(ReliableFrame(a_connectionId, StatePacket(viewer, a_sessions, a_unixTimeSeconds)));
		for (auto& [connectionId, peer] : a_sessions) {
			if (!peer.active || peer.profileId.empty() || peer.profileId == profileId) continue;
			if (!HasFriends(peer)) continue;
			if (!social.AreFriends(profileId, peer.profileId)) continue;
			frames.push_back(ReliableFrame(connectionId, StatePacket(Peer{ connectionId, &peer }, a_sessions, a_unixTimeSeconds)));
		}
		return frames;
	}

	std::vector<OutboundFrame> FriendService::DisconnectFrames(const std::string& a_profileId, Sessions& a_sessions, double a_unixTimeSeconds)
	{
		std::vector<OutboundFrame> frames;
		if (!enabled || a_profileId.empty()) return frames;
		AppendFrames(frames, ForgetProfileFrames(a_profileId, a_sessions, a_unixTimeSeconds));
		return frames;
	}

	std::vector<OutboundFrame> FriendService::ForgetProfileFrames(const std::string& a_profileId, Sessions& a_sessions, double a_unixTimeSeconds, bool a_dropRequests)
	{
		std::vector<OutboundFrame> frames;
		if (!enabled || a_profileId.empty()) return frames;
		std::vector<std::string> affected;
		if (a_dropRequests) {
			for (auto request = requests.begin(); request != requests.end();) {
				if (request->second.fromProfileId != a_profileId && request->second.toProfileId != a_profileId) {
					++request;
					continue;
				}
				affected.push_back(request->second.fromProfileId == a_profileId ? request->second.toProfileId : request->second.fromProfileId);
				request = requests.erase(request);
			}
			requestWindows.erase(a_profileId);
		}
		for (auto& entry : a_sessions) {
			const auto& peer = entry.second;
			if (!peer.active || peer.profileId.empty() || peer.profileId == a_profileId) continue;
			if (social.AreFriends(a_profileId, peer.profileId)) affected.push_back(peer.profileId);
		}
		std::sort(affected.begin(), affected.end());
		affected.erase(std::unique(affected.begin(), affected.end()), affected.end());
		AppendFrames(frames, StateFramesForProfiles(a_sessions, affected, a_unixTimeSeconds));
		return frames;
	}

	std::optional<std::vector<OutboundFrame>> ProtocolHost::Impl::DispatchFriendPacket(
		const std::string& a_type,
		std::uint32_t a_connectionId,
		Session& a_session,
		const Json& a_packet,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		if (!friendService.Enabled() || !FriendService::HandlesType(a_type)) return std::nullopt;
		std::vector<std::string> refresh;
		auto frames = friendService.Dispatch(a_connectionId, a_session, sessions, a_type, a_packet, a_monotonicSeconds, a_unixTimeSeconds, refresh);
		if (!frames) return std::nullopt;
		for (const auto& profileId : refresh) {
			const auto peer = PeerForProfile(sessions, profileId);
			if (!peer) continue;
			AppendFrames(*frames, PairRefreshFrames(a_connectionId, a_session, peer->connectionId, *peer->session, a_unixTimeSeconds));
		}
		return frames;
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::ExistingFriendFrames(std::uint32_t a_connectionId, const Session& a_target, double a_unixTimeSeconds)
	{
		(void)a_target;
		if (!friendService.Enabled()) return {};
		return friendService.JoinFrames(a_connectionId, sessions, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::FriendDisconnectFrames(const std::string& a_profileId, double a_unixTimeSeconds)
	{
		if (!friendService.Enabled()) return {};
		return friendService.DisconnectFrames(a_profileId, sessions, a_unixTimeSeconds);
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::PollFriendRequests(double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		if (!friendService.Enabled()) return {};
		return friendService.PollExpiry(sessions, a_monotonicSeconds, a_unixTimeSeconds);
	}
}
