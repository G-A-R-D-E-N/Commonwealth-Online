#include "pch.h"

#include "RE/S/Setting.h"

#include "F4TSurvivalEnforcer.h"
#include "F4TSurvivalPolicyClient.h"
#include "F4TSurvivalLogPolicy.h"
#include "F4TNetworking.h"
#include "F4TUIClient.h"

namespace F4T::SurvivalEnforcer
{
	namespace { std::uint64_t g_lastNotified{}; bool g_lastLoadAmbiguous{}; bool g_dismissPending{}; std::atomic<std::uint64_t> g_failureConnection{}; std::atomic<std::uint64_t> g_failureEpoch{}; }

	namespace
	{
		std::string StoredDifficultyText()
		{
			auto* setting = RE::GetINISetting("iDifficulty:GamePlay");
			if (!setting) {
				setting = RE::GetINISetting("iDifficulty");
			}
			if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kInt) {
				return "missing";
			}
			return std::to_string(setting->GetInt());
		}

		std::string RequiredDifficultyName(std::int32_t a_difficulty)
		{
			switch (a_difficulty) {
			case 0: return "Very Easy";
			case 1: return "Easy";
			case 2: return "Normal";
			case 3: return "Hard";
			case 4: return "Very Hard";
			case 5: return "Survival";
			case 6: return "True Survival";
			default: return "level " + std::to_string(a_difficulty);
			}
		}

		void LogGateDecision(std::uint64_t a_connection, std::uint64_t a_epoch, std::string_view a_outcome, std::int32_t a_before, std::int32_t a_after)
		{
			static SurvivalLogPolicy::Latch latch;
			if (!latch.Claim(a_connection, a_epoch, a_outcome)) {
				return;
			}
			REX::INFO("[SurvivalGate] decision outcome={} difficulty={} afterDifficulty={} storedDifficulty={} connection={} epoch={}",
				a_outcome, a_before, a_after, StoredDifficultyText(), a_connection, a_epoch);
		}
	}

	bool Tick(std::uint64_t a_connection, std::uint64_t a_load, bool a_loaded, const std::function<void()>& a_work)
	{
		static std::uint64_t attemptConnection{};
		static std::uint64_t attemptEpoch{};
		static bool attempted{};
		return F4T::SurvivalPolicy::g_clientState.Enforce(a_connection, a_load, a_loaded, [a_connection] {
			const auto epoch = F4T::SurvivalPolicy::g_clientState.LoadEpoch();
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			if (version.major() != 1 || version.build() != 0 ||
				!((version.minor() == 10 && (version.patch() == 163 || version.patch() == 984)) ||
				  (version.minor() == 11 && version.patch() == 240))) {
				LogGateDecision(a_connection, epoch, "unsupported-runtime", -1, -1);
				g_failureConnection = a_connection;
				g_failureEpoch = epoch;
				return false;
			}
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player || !player->GetParentCell()) {
				LogGateDecision(a_connection, epoch, "player-not-ready", -1, -1);
				return false;
			}
			const auto required = F4T::SurvivalPolicy::g_clientState.RequiredDifficulty(a_connection);
			const auto requiredLevel = static_cast<RE::DifficultyLevel>(required);
			const auto before = static_cast<std::int32_t>(player->GetDifficultyLevel());
			if (player->GetDifficultyLevel() == requiredLevel) {
				attempted = false;
				g_failureConnection = 0;
				LogGateDecision(a_connection, epoch, "already-very-hard", before, before);
				return true;
			}
			if (attempted && attemptConnection == a_connection && attemptEpoch == epoch) {
				LogGateDecision(a_connection, epoch, "attempt-latched", before, before);
				return false;
			}
			const REL::ID objectId{ 303410, 2698073, 2698073 /* E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md */ };
			const REL::ID transitionId{ 657344, 2233057, 2233057 /* E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md */ };
			if (!objectId.address() || !transitionId.address()) {
				LogGateDecision(a_connection, epoch, "unresolved-address", before, before);
				g_failureConnection = a_connection;
				g_failureEpoch = epoch;
				return false;
			}
			REL::Relocation<void**> object{ objectId };
			if (!*object) {
				LogGateDecision(a_connection, epoch, "object-null", before, before);
				return false;
			}
			attempted = true;
			attemptConnection = a_connection;
			attemptEpoch = epoch;
			REL::Relocation<void (*)(void*, std::int32_t, bool, bool)> transition{ transitionId };
			transition(*object, required, false, true);
			const auto after = static_cast<std::int32_t>(player->GetDifficultyLevel());
			const bool confirmed = player->GetDifficultyLevel() == requiredLevel;
			LogGateDecision(a_connection, epoch, confirmed ? "set-very-hard" : "transition-failed", before, after);
			if (confirmed) {
				g_failureConnection = 0;
			} else {
				g_failureConnection = a_connection;
				g_failureEpoch = epoch;
			}
			return confirmed;
		}, a_work);
	}

	void NotifyUnavailable(std::uint64_t a_connection)
	{
		if (a_connection == 0 || g_failureConnection != a_connection || g_failureEpoch != F4T::SurvivalPolicy::g_clientState.LoadEpoch()) return;
		const bool ambiguous = F4T::SurvivalPolicy::g_clientState.LoadAmbiguous();
		if (a_connection == 0 || (!g_dismissPending && a_connection == g_lastNotified && ambiguous == g_lastLoadAmbiguous) ||
			F4T::Networking::GetConnectionGeneration() != a_connection || !F4T::Networking::IsConnectedToServer()) return;
		auto* ui = F4T::UIClient::GetCachedUI();
		if (!ui || !ui->ShowAlert) return;
		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = "survival-unavailable";
		request.category = "connection";
		const auto name = RequiredDifficultyName(F4T::SurvivalPolicy::g_clientState.RequiredDifficulty(a_connection));
		const std::string title = name + " difficulty could not be confirmed";
		const std::string message = ambiguous ?
			std::string("Load status is uncertain. Disconnect normally and contact the server administrator before restarting the game.") :
			std::string("Set the game difficulty to ") + name + ", then reconnect. If that fails, contact the server administrator.";
		request.title = title.c_str();
		request.message = message.c_str();
		request.tone = "error";
		request.durationMs = 0;
		if (ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0)) {
			g_lastNotified = a_connection;
			g_lastLoadAmbiguous = ambiguous;
			g_dismissPending = false;
			REX::INFO("[SurvivalGate] alert survival-unavailable shown ambiguous={} connection={}", ambiguous, a_connection);
		}
	}

	void ClearNotice()
	{
		if (g_lastNotified == 0) return;
		auto* ui = F4T::UIClient::GetCachedUI();
		if (!ui || !ui->DismissAlert) { g_dismissPending = true; return; }
		ui->DismissAlert("survival-unavailable");
		g_lastNotified = 0;
		g_dismissPending = false;
	}
}
