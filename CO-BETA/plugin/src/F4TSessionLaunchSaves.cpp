#include "pch.h"

#include "F4TSessionLaunch.h"

#include "F4TNetworking.h"
#include "F4TSaveProfileLogic.h"
#include "F4TServerProfileStore.h"
#include "F4TSessionLaunchDiagnostics.h"
#include "F4TSessionLaunchInternal.h"
#include "F4TSessionSaveGuard.h"
#include "F4TSessionSaveQueue.h"

#include <format>
#include <optional>
#include <string>
#include <string_view>

	using namespace F4T::SessionLaunchDiagnostics;

namespace F4T::SessionLaunch::Internal
{
	bool g_profileSaveQueued = false;
	bool g_profileSaveRequeued = false;

	bool QueueProfileSave()
	{
		if (F4T::SessionSaveGuard::IsSessionExitPending() ||
			!F4T::SessionSaveQueue::StartQueuedSave(F4T::SessionSaveGuard::ArmOwner::kFirstJoin, "queued first-join save")) {
			LogInfo("First-join save not queued: save manager missing or a session exit save is pending.");
			return false;
		}
		g_profileSaveQueued = true;
		LogInfo("Armed the first multiplayer save; it queues once the save gate passes.");
		return true;
	}

	std::optional<std::string> BindableCharacterId(const std::string& a_savedName)
	{
		const auto characterId = SaveProfileLogic::ExtractCharacterId(a_savedName);
		if (!characterId || characterId->empty()) {
			LogWarning(std::format(
				"Refusing to bind the server profile to save '{}': the save name carries no character id.",
				a_savedName));
			return std::nullopt;
		}
		return characterId;
	}

}

namespace F4T::SessionLaunch
{
	using namespace Internal;
	using namespace F4T::SessionLaunchDiagnostics;

	void TryCompletePendingProfileBind(const std::optional<std::string>& a_savedName)
	{
		if (!a_savedName ||
			g_pendingProfileBindServerKey.empty() ||
			!g_pendingProfileBind ||
			!g_profileSaveQueued ||
			!F4T::Networking::IsConnectedToServer() ||
			g_charGenPhase != CharGenPhase::WaitingForProfileBind) {
			return;
		}

		if (!F4T::Networking::IsConnectedToServer()) { return; }

		const auto characterId = BindableCharacterId(*a_savedName);
		if (!characterId) {
			return;
		}

		const auto boundServerKey = g_pendingProfileBindServerKey;
		if (!ServerProfileStore::BindSaveFile(boundServerKey, *a_savedName)) {
			FailFirstTimeJoin("Gameplay save completed, but the server profile could not be persisted.");
			return;
		}
		g_activeProfileServerKey = boundServerKey;
		g_activeProfileCharacterId = *characterId;
		g_pendingProfileBindServerKey.clear();
		g_pendingProfileBind = false;
		g_profileSaveQueued = false;
		LogInfo(std::format(
			"Bound server profile to gameplay-ready save '{}' for server {}.",
			*a_savedName,
			boundServerKey));
		CompletePendingJoin(true, "gameplay save completed and server profile binding succeeded");
	}

}
