#include "pch.h"
// address source:E:/RE-work/co-sleep/sleep-native-functions.md

#include "F4TSleepController.h"

#include "F4TDebugLogging.h"
#include "F4TDownedGlue.h"
#include "F4TNetworking.h"
#include "F4TQuestFormKey.h"
#include "F4TSleepBedHook.h"
#include "F4TSleepBedIdentity.h"
#include "F4TSleepClock.h"
#include "F4TSleepView.h"
#include "F4TWorkshopReplicationClient.h"

#include "RE/A/AIProcess.h"
#include "RE/A/ActorValue.h"
#include "RE/C/Calendar.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/SIT_SLEEP_STATE.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <optional>
#include <string_view>

namespace F4T::SleepController
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		enum class State { Idle, Choosing, Entering, Sleeping };

		inline constexpr REL::ID kStopSleep{ 1551767, 2232890, 2232890 } /* E:/RE-work/co-sleep/sleep-native-functions.md */;
		inline constexpr REL::ID kInterruptSleep{ 674480, 2232889, 2232889 } /* E:/RE-work/co-sleep/sleep-native-functions.md */;
		inline constexpr REL::ID kEnterBed{ 1340894, 2232956, 2232956 } /* E:/RE-work/co-sleep/sleep-entry-functions.md */;

		constexpr std::uint32_t kMaxHours = 24;
		constexpr std::uint32_t kSleepStepSeconds = 900;
		constexpr auto kTickInterval = std::chrono::seconds(1);
		constexpr float kMaxPickerDistanceSquared = 512.0F * 512.0F;
		constexpr std::size_t kMaximumUnsentUses = 16;

		State g_state{ State::Idle };
		std::uint32_t g_nextToken{ 1 };
		std::uint32_t g_token{};
		std::uint32_t g_hours{};
		RE::ObjectRefHandle g_bed;
		std::optional<std::uint32_t> g_marker;
		std::optional<SleepBedIdentity::Identity> g_identity;
		bool g_enterSent{};
		std::deque<nlohmann::json> g_unsentUses;
		std::uint32_t g_pickerCell{};
		Clock::time_point g_nextTick{};
		Clock::time_point g_entryDeadline{};

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("Sleep", a_message);
		}

		void NativeStop(RE::PlayerCharacter* a_player)
		{
			REL::Relocation<void(RE::PlayerCharacter*)> func{ kStopSleep };
			func(a_player);
		}

		void RestoreHealth(RE::PlayerCharacter* a_player)
		{
			const auto* actorValues = RE::ActorValue::GetSingleton();
			if (!actorValues || !actorValues->health) {
				return;
			}
			const auto missing = a_player->GetPermanentActorValue(*actorValues->health) - a_player->GetActorValue(*actorValues->health);
			if (std::isfinite(missing) && missing > 0.0F) {
				a_player->RestoreActorValue(*actorValues->health, missing);
			}
		}

		void AdvanceCalendar(float a_seconds)
		{
			auto* calendar = RE::Calendar::GetSingleton();
			if (!calendar || !calendar->gameHour || !calendar->gameDaysPassed) {
				return;
			}
			const float hours = a_seconds / 3600.0F;
			const float hour = calendar->gameHour->value + hours;
			calendar->gameDaysPassed->value += hours / 24.0F;
			calendar->gameHour->value = hour >= 24.0F ? hour - 24.0F : hour;
		}

		void AdvanceSleep(RE::PlayerCharacter* a_player)
		{
			AdvanceCalendar(static_cast<float>((std::min)(a_player->sleepSeconds, kSleepStepSeconds)));
			if (a_player->sleepSeconds > kSleepStepSeconds) {
				a_player->sleepSeconds -= kSleepStepSeconds;
			} else {
				RestoreHealth(a_player);
				NativeStop(a_player);
				LogLine(std::format("sleep completed hours={}", g_hours));
			}
		}

		void NativeInterrupt(RE::PlayerCharacter* a_player)
		{
			REL::Relocation<void(RE::PlayerCharacter*)> func{ kInterruptSleep };
			func(a_player);
		}

		void ResetSession()
		{
			g_state = State::Idle;
			g_token = 0;
			g_hours = 0;
			g_bed = {};
			g_marker.reset();
			g_identity.reset();
			g_enterSent = false;
			g_pickerCell = 0;
		}

		void SendOrQueue(const nlohmann::json& a_packet)
		{
			if (!g_unsentUses.empty() || !F4T::Networking::SendSleepUsePacket(a_packet)) {
				if (g_unsentUses.size() >= kMaximumUnsentUses) {
					g_unsentUses.pop_front();
				}
				g_unsentUses.push_back(a_packet);
			}
		}

		void FlushUnsentUses()
		{
			while (!g_unsentUses.empty() && F4T::Networking::SendSleepUsePacket(g_unsentUses.front())) {
				g_unsentUses.pop_front();
			}
		}

		std::uint32_t CellOf(const RE::PlayerCharacter& a_player)
		{
			const auto* cell = a_player.GetParentCell();
			return cell && !cell->IsExterior() ? cell->GetFormID() : 0;
		}

		bool PickerMoved(const RE::PlayerCharacter& a_player, const RE::TESObjectREFR& a_bed)
		{
			if (CellOf(a_player) != g_pickerCell) {
				return true;
			}
			const auto delta = a_player.GetPosition() - a_bed.GetPosition();
			return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z > kMaxPickerDistanceSquared;
		}

		void Finish()
		{
			LogLine(std::format("sleep finished wasSleeping={} exitSent={}", g_state == State::Sleeping, g_enterSent));
			SleepView::Close();
			if (g_state == State::Sleeping) {
				SleepClock::End();
			}
			if (g_enterSent) {
				SendOrQueue(SleepBedIdentity::MakeSleepExit());
			}
			if (g_state == State::Entering || g_state == State::Sleeping) {
				if (auto* player = RE::PlayerCharacter::GetSingleton()) {
					auto* process = player->currentProcess;
					const auto occupied = process ? process->GetOccupiedFurniture().get() : nullptr;
					if ((occupied && occupied == g_bed.get()) ||
						(!occupied && process && process->middleHigh && process->middleHigh->currentFurniture == g_bed)) {
						player->InitiateGetUpPackage();
					}
				}
			}
			ResetSession();
		}

		std::optional<SleepBedIdentity::Identity> IdentityOf(const RE::TESObjectREFR& a_bed)
		{
			if (const auto placed = WorkshopReplicationClient::FindPlacedObject(a_bed)) {
				return SleepBedIdentity::WorkshopObject{ placed->buildZoneId, placed->objectId };
			}
			const auto key = QuestFormKey::ToKey(a_bed.GetFormID(), QuestFormKey::GetLoadOrder());
			const auto parsed = key ? QuestProtocol::ParseFormKey(*key) : std::nullopt;
			if (!parsed) {
				return std::nullopt;
			}
			return *parsed;
		}

		void BeginSleeping()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto bed = g_bed.get();
			auto* process = player ? player->currentProcess : nullptr;
			if (!bed || !process || !process->middleHigh) {
				LogLine("sleep not started: bed or player process unavailable");
				Finish();
				return;
			}

			SleepClock::Begin();
			process->middleHigh->currentFurniture = g_bed;
			F4T::SleepBedHook::CallNativeSleep(player, static_cast<std::int32_t>(g_hours));
			if (!player->isSleeping) {
				LogLine("sleep not started: engine refused the sleep request");
				SleepClock::Cancel();
				process->middleHigh->currentFurniture = {};
				Finish();
				return;
			}

			g_state = State::Sleeping;
			g_nextTick = Clock::now() + kTickInterval;

			if (g_identity && g_marker) {
				if (const auto packet = SleepBedIdentity::MakeSleepEnter(*g_identity, *g_marker)) {
					SendOrQueue(*packet);
					g_enterSent = true;
				}
			}
			LogLine(std::format("sleep started hours={} marker={} identity={} enterSent={}", g_hours,
				g_marker.value_or(0), g_identity.has_value(), g_enterSent));
		}

		void StartSleep(std::uint32_t a_hours)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			const auto bed = g_bed.get();
			if (!player || !bed) { SleepView::Close(); ResetSession(); return; }
			REL::Relocation<bool(RE::PlayerCharacter*, RE::TESObjectREFR*)> enter{ kEnterBed };
			if (!enter(player, bed.get())) { SleepView::Close(); ResetSession(); return; }
			g_hours = a_hours;
			g_state = State::Entering;
			g_entryDeadline = Clock::now() + std::chrono::seconds(30);
			SleepView::SetPhase(SleepView::Phase::Resting, a_hours);
		}

		void OnAction(SleepView::Action a_action, std::uint32_t a_token, std::uint32_t a_hours)
		{
			if (g_state == State::Idle || a_token != g_token) {
				return;
			}

			if (a_action == SleepView::Action::Cancel) {
				if (g_state == State::Sleeping || g_state == State::Entering) {
					if (auto* player = RE::PlayerCharacter::GetSingleton()) {
						if (g_state == State::Sleeping) NativeInterrupt(player);
					}
					Finish();
				} else {
					SleepView::Close();
					ResetSession();
				}
				return;
			}

			if (g_state == State::Choosing && a_hours >= 1 && a_hours <= kMaxHours) {
				StartSleep(a_hours);
			}
		}
	}

	bool TryBegin(RE::TESObjectREFR* a_bed)
	{
		if (g_state != State::Idle) {
			LogLine("sleep not started: a sleep session is already active");
			return true;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!a_bed || !player) {
			LogLine("sleep not started: bed or player unavailable");
			return false;
		}
		if (!F4T::Networking::IsConnectedToServer()) {
			LogLine("sleep not started: not connected to the server");
			return false;
		}
		if (F4T::Downed::IsLocalPlayerDowned() || F4T::Downed::IsLocalPlayerDead()) {
			LogLine("sleep not started: player is downed or dead");
			return false;
		}

		const auto identity = IdentityOf(*a_bed);
		if (!identity) {
			LogLine("bed has no server identity; sleep runs without a server sleep record");
		}

		const auto token = g_nextToken++;
		if (!SleepView::Open(token, kMaxHours, &OnAction)) {
			LogLine("sleep not started: sleep view did not open");
			return false;
		}

		g_state = State::Choosing;
		g_token = token;
		g_pickerCell = CellOf(*player);
		g_bed = a_bed->GetHandle();
		g_identity = identity;
		return true;
	}

	void Tick()
	{
		FlushUnsentUses();
		if (g_state == State::Idle) {
			return;
		}

		auto* player = RE::PlayerCharacter::GetSingleton();
		const bool usable = player && F4T::Networking::IsConnectedToServer() &&
			!F4T::Downed::IsLocalPlayerDowned() && !F4T::Downed::IsLocalPlayerDead();

		if (g_state == State::Choosing) {
			const auto bed = g_bed.get();
			if (!usable || !bed || PickerMoved(*player, *bed)) {
				SleepView::Close();
				ResetSession();
			}
			return;
		}
		if (g_state == State::Entering) {
			const auto bed = g_bed.get();
			if (!usable || !bed || Clock::now() >= g_entryDeadline) { Finish(); return; }
			auto* process = player->currentProcess;
			if (process && process->middleHigh && process->GetOccupiedFurniture() == g_bed) {
				const auto state = player->DoGetSitSleepState();
				if (state == RE::SIT_SLEEP_STATE::kIsSleeping) {
					const auto marker = process->middleHigh->occupiedFurnitureMarkerID;
					if (SleepBedIdentity::IsValidMarker(marker)) g_marker = marker;
					BeginSleeping();
				}
			}
			return;
		}

		if (!player || !player->isSleeping) {
			Finish();
			return;
		}
		if (!usable) {
			NativeInterrupt(player);
			Finish();
			return;
		}

		const auto now = Clock::now();
		if (now < g_nextTick) {
			return;
		}
		g_nextTick = now + kTickInterval;
		AdvanceSleep(player);
		if (!player->isSleeping) {
			Finish();
			return;
		}
		SleepView::SetProgress(static_cast<float>(g_hours) - static_cast<float>(player->sleepSeconds) / 3600.0F);
	}

	void OnDisconnect()
	{
		g_unsentUses.clear();
		if (g_state == State::Idle) {
			return;
		}
		if (g_state == State::Sleeping) {
			if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->isSleeping) {
				NativeInterrupt(player);
			}
		}
		g_enterSent = false;
		Finish();
	}
}
