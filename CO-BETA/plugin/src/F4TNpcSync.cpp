#include "pch.h"

#include "F4TNetworking.h"
#include "F4TDebugLogging.h"
#include "F4TNpcSync.h"

#include "F4TCrashBreadcrumbs.h"
#include "F4TNpcSyncInternal.h"
#include "F4TNpcSyncSweepPolicy.h"
#include "F4TRemoteNpcState.h"

#include <chrono>
#include <optional>
#include <string>

namespace F4T::NpcSync
{
	namespace
	{
		constexpr std::uint64_t kAuthorityHoldMs = 1000;

		bool g_stalePuppetSweepDone{ false };
		std::uint32_t g_lastCellFormId{};
		NpcSyncSweep::AuthorityFlipGate g_authorityGate;

		std::uint64_t MonotonicMs()
		{
			return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
		}
	}

	void Update(const std::function<void(const std::function<void()>&)>& a_runGameplay)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !Networking::IsConnectedToServer()) {
			Reset();
			return;
		}

		const auto* cell = player->GetParentCell();
		const auto cellId = cell ? Detail::FormatFormId(cell->GetFormID()) : std::string{};
		const auto worldspaceId = cell && cell->IsExterior() && cell->worldSpace ?
		                              Detail::FormatFormId(cell->worldSpace->GetFormID()) :
		                              std::string{};
		const auto authority = cellId.empty() ?
		                           std::optional<Networking::NpcAuthorityAssignment>{} :
		                           Networking::GetNpcAuthorityForScope(cellId, worldspaceId);
		const auto localPlayerId = RemotePlayerState::GetAssignedPlayerId();
		const auto authorityHostPlayerId = authority ? authority->playerId : 0U;
		const auto authorityEpoch = authority ? authority->epoch : 0ULL;
		const bool isHost = authority && localPlayerId &&
		                    authority->playerId == *localPlayerId && authority->epoch != 0;

		const auto cellFormId = cell ? cell->GetFormID() : 0U;
		if (cellFormId != g_lastCellFormId) {
			g_lastCellFormId = cellFormId;
			F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kCellChange, cellFormId, 0);
		}

		const NpcSyncSweep::AuthoritySnapshot currentAuthority{ isHost, authorityHostPlayerId, authorityEpoch };
		const auto flip = g_authorityGate.Observe(currentAuthority, authority.has_value(), MonotonicMs(), kAuthorityHoldMs);
		if (flip == NpcSyncSweep::AuthorityFlipGate::Action::kApply) {
			F4T::CrashBreadcrumbs::Record(F4T::CrashBreadcrumbs::Code::kAuthorityEpoch, authorityHostPlayerId, static_cast<std::uint32_t>(authorityEpoch));
			Detail::ClearHostNpcIds();
			Combat::Reset();
			Loot::Reset();
			Detail::g_nextNpcId = 1;
			Detail::g_lastHostSend = {};
			F4T::DebugLogging::Log(
				"NpcSync",
				"scope authority changed cell={} worldspace={} hostPlayerId={} epoch={} localIsHost={}",
				cellId.empty() ? "<none>" : cellId,
				worldspaceId.empty() ? "<none>" : worldspaceId,
				authorityHostPlayerId,
				authorityEpoch,
				isHost);
		}

		if (!g_stalePuppetSweepDone && cell) {
			Detail::PurgeStalePuppets();
			g_stalePuppetSweepDone = true;
		}

		a_runGameplay([&] {
			Detail::UpdatePuppets(*player);
			Health::Tick();
		});
		Loot::Tick(isHost, authorityEpoch, cellId, worldspaceId, a_runGameplay);
		if (isHost) {
			a_runGameplay([&] {
				Combat::Tick();
				Detail::UpdateHost(*player, authorityEpoch, cellId, worldspaceId);
			});
		}
		Detail::PublishRegistrySnapshot();
	}

	void Reset()
	{
		Detail::ClearPuppets();
		Detail::RestoreSuppressedActors();
		RemoteNpcState::Clear();
		Detail::ClearHostNpcIds();
		Combat::Reset();
		Loot::Reset();
		Detail::g_nextNpcId = 1;
		Detail::g_lastHostSend = {};
		Detail::g_lastSummaryLog = {};
		g_authorityGate.Reset();
		g_stalePuppetSweepDone = false;
		Detail::PublishRegistrySnapshot();
	}
}
