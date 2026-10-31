#include "pch.h"

#include "F4TNetworking.h"
#include "F4TDebugLogging.h"
#include "F4TNpcSync.h"

#include "F4TNpcSyncInternal.h"
#include "F4TRemoteNpcState.h"

#include <optional>
#include <string>

namespace F4T::NpcSync
{
	namespace
	{
		bool g_stalePuppetSweepDone{ false };
		bool g_wasHost{ false };
		std::uint32_t g_lastAuthorityHostPlayerId{};
		std::uint64_t g_lastNpcAuthorityEpoch{};
	}

	void Update()
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

		if (isHost != g_wasHost ||
			authorityHostPlayerId != g_lastAuthorityHostPlayerId ||
			authorityEpoch != g_lastNpcAuthorityEpoch) {
			Detail::ClearPuppets();
			Detail::RestoreSuppressedActors();
			RemoteNpcState::Clear();
			Detail::ClearHostNpcIds();
			Combat::Reset();
			Loot::Reset();
			Detail::g_nextNpcId = 1;
			Detail::g_lastHostSend = {};
			g_wasHost = isHost;
			g_lastAuthorityHostPlayerId = authorityHostPlayerId;
			g_lastNpcAuthorityEpoch = authorityEpoch;
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

		Detail::UpdatePuppets(*player);
		Health::Tick();
		Loot::Tick(isHost, authorityEpoch, cellId, worldspaceId);
		if (isHost) {
			Combat::Tick();
		}
		if (isHost) {
			Detail::UpdateHost(*player, authorityEpoch, cellId, worldspaceId);
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
		g_wasHost = false;
		g_lastAuthorityHostPlayerId = 0;
		g_lastNpcAuthorityEpoch = 0;
		g_stalePuppetSweepDone = false;
		Detail::PublishRegistrySnapshot();
	}
}
