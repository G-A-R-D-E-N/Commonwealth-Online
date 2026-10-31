#include "pch.h"

#include "F4TDownedGlueInternal.h"

namespace
{
	using namespace F4T::Downed::GlueInternal;
	using namespace F4T::Downed::Engine;
}

namespace F4T::Downed::GlueInternal
{
	RespawnPoint FallbackRespawnPoint()
	{
		return HasLastBedCell() ? RespawnPoint::kLastBed : RespawnPoint::kSanctuary;
	}

	RespawnPoint AutomaticRespawnPoint()
	{
		return F4T::Downed::PreferredRespawnPoint(g_checkpoint.has_value(), HasLastBedCell());
	}

	void RecordCheckpoint(const char* a_why)
	{
		F4T::Downed::Checkpoint checkpoint;
		if (!CaptureCheckpoint(checkpoint)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_checkpoint_capture",
				"Could not capture the last-save position for respawn.");
			return;
		}
		if (!F4T::Downed::SendCheckpointReport(checkpoint)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_checkpoint_report",
				"Could not send the Commonwealth Online checkpoint report; no respawn checkpoint is held.");
			g_checkpoint.reset();
			return;
		}
		g_checkpoint = checkpoint;
		F4T::DebugLogging::Log(
			kLogComponent,
			"checkpoint recorded on {}: {} {:08X} ({:.0f},{:.0f},{:.0f}) heading={:.2f}",
			a_why,
			checkpoint.interior ? "cell" : "worldspace",
			checkpoint.space,
			checkpoint.x,
			checkpoint.y,
			checkpoint.z,
			checkpoint.rotZ);
	}

	void RequestRespawn(RespawnPoint a_point, std::optional<std::uint32_t> a_friendId)
	{
		if (!F4T::Downed::IsRespawnRequestValid(a_point, a_friendId)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_respawn_request_invalid",
				"Respawn request for 'friend' carried no friend id; falling back to the last bed.");
			a_point = FallbackRespawnPoint();
			a_friendId.reset();
		}
		g_pendingRespawnPoint = a_point;
		g_pendingRespawnFriendId = a_friendId;
		g_respawnRequestAttempts = 0;
		g_nextRespawnRequestSeconds = NowSeconds() + g_state.GetConfig().respawnRequestRetrySeconds;
		if (!F4T::Downed::SendRespawnRequest(a_point, a_friendId)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_respawn_request",
				"Could not send Commonwealth Online respawnRequest.");
		}
	}

	std::vector<F4T::Downed::RespawnFriend> CollectFriends()
	{
		std::vector<F4T::Downed::RespawnFriend> friends;
		for (const auto& remote : F4T::RemotePlayerState::GetRemotePlayerSnapshot()) {
			if (remote.playerId == 0 || remote.characterName.empty()) {
				continue;
			}
			friends.push_back(F4T::Downed::RespawnFriend{ remote.playerId, remote.characterName });
		}
		return friends;
	}

	void ShowRespawnMenu()
	{
		std::vector<std::string> points;
		if (g_checkpoint) {
			points.emplace_back(F4T::Downed::GetRespawnPointName(RespawnPoint::kCheckpoint));
		}
		points.emplace_back(F4T::Downed::GetRespawnPointName(RespawnPoint::kSanctuary));
		if (HasLastBedCell()) {
			points.emplace_back(F4T::Downed::GetRespawnPointName(RespawnPoint::kLastBed));
		}
		auto friends = CollectFriends();
		if (!friends.empty()) {
			points.emplace_back(F4T::Downed::GetRespawnPointName(RespawnPoint::kFriend));
		}
		auto* host = g_uiHost.load();
		if (!host) {
			LogInfo("No respawn menu host registered; requesting a direct respawn.");
			RequestRespawn(AutomaticRespawnPoint(), std::nullopt);
			return;
		}
		g_respawnMenuVisible = true;
		host->ShowRespawnMenu(points, friends);
		F4T::DebugLogging::Log(
			kLogComponent,
			"respawn menu shown: points={} friends={}",
			points.size(),
			friends.size());
	}

	void RetryPendingRespawn(double a_nowSeconds)
	{
		if (!g_pendingRespawnPoint || g_state.GetPhase() != Phase::kDead) {
			return;
		}
		if (a_nowSeconds < g_nextRespawnRequestSeconds) {
			return;
		}
		if (!F4T::Downed::ShouldRetryRespawnRequest(g_respawnRequestAttempts)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_respawn_retry_exhausted",
				"Gave up re-sending Commonwealth Online respawnRequest; pick a respawn point again to retry.");
			return;
		}
		g_nextRespawnRequestSeconds = a_nowSeconds + F4T::Downed::ComputeRespawnRetryDelay(
			g_state.GetConfig().respawnRequestRetrySeconds,
			g_respawnRequestAttempts);
		++g_respawnRequestAttempts;
		if (!F4T::Downed::SendRespawnRequest(*g_pendingRespawnPoint, g_pendingRespawnFriendId)) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_respawn_request",
				"Could not re-send Commonwealth Online respawnRequest.");
		}
	}

	void ApplyRespawnGrantEvent(const DownedEvent& a_event)
	{
		if (!g_state.ApplyRespawnGrant()) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"ignoring respawnGrant at '{}' while phase is {}",
				F4T::Downed::GetRespawnPointName(a_event.point),
				static_cast<int>(g_state.GetPhase()));
			return;
		}
		HideRespawnMenu();
		HideOverlay();
		g_pendingRespawnPoint.reset();
		g_pendingRespawnFriendId.reset();
		if (auto* player = LocalPlayer(); player && CanTouchWorld() && player->IsDead(true)) {
			player->Resurrect(false, true);
			LogInfo("respawn granted while the local player was dead; revived the player.");
		}
		LeaveIncapacitated(false);
		if (CanTouchWorld()) {
			TeleportForRespawn(a_event.point, a_event.friendId, a_event.checkpoint);
		}
		RestoreLocalPlayerHealth();
		F4T::DebugLogging::Log(
			kLogComponent,
			"respawn granted at '{}' friendId={}",
			F4T::Downed::GetRespawnPointName(a_event.point),
			a_event.friendId.value_or(0));
	}

	void ApplyRespawnChoiceEvent(const DownedEvent& a_event)
	{
		if (g_state.GetPhase() != Phase::kDead) {
			F4T::DebugLogging::Log(
				kLogComponent,
				"ignoring respawn choice '{}' while phase is {}",
				F4T::Downed::GetRespawnPointName(a_event.point),
				static_cast<int>(g_state.GetPhase()));
			return;
		}
		RequestRespawn(a_event.point, a_event.friendId);
	}
}

namespace F4T::Downed
{
	bool SendRespawnRequest(RespawnPoint a_point, std::optional<std::uint32_t> a_friendId)
	{
		const auto request = EncodeRespawnRequest(a_point, a_friendId);
		auto* transport = g_transport.load();
		if (!request || !transport) {
			return false;
		}
		return transport->Send(request->dump());
	}

	bool SendCheckpointReport(const Checkpoint& a_checkpoint)
	{
		const auto report = EncodeCheckpointReport(a_checkpoint);
		auto* transport = g_transport.load();
		if (!report || !transport) {
			return false;
		}
		return transport->Send(report->dump());
	}

	void OnWorldSaved()
	{
		RecordCheckpoint("the shared world was saved");
	}

	void OnWorldLoaded()
	{
		RecordCheckpoint("the shared world was loaded");
	}

	bool HasCheckpoint()
	{
		return g_checkpoint.has_value();
	}

	void OnRespawnChosen(std::string_view a_point, std::optional<std::uint32_t> a_friendId)
	{
		const auto point = ParseRespawnPoint(a_point);
		if (!point) {
			F4T::Networking::Detail::LogThrottledWarning(
				"downed_respawn_point",
				std::format("Ignoring unknown respawn point '{}'.", std::string{ a_point }));
			return;
		}
		QueueEvent(DownedEvent{ EventKind::kRespawnChoice, 0, false, 0, 1.0, *point, a_friendId });
	}
}

