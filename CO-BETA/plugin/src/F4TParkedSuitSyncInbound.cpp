#include "pch.h"

#include "F4TParkedSuit.h"
#include "F4TParkedSuitInternal.h"
#include "F4TParkedSuitSync.h"
#include "F4TParkedSuitSyncInternal.h"
#include "F4TParkedSuitUi.h"
#include "F4TPowerArmorScript.h"

#include "F4TDebugLogging.h"
#include "F4TJsonFloat.h"

#include "RE/T/TESForm.h"
#include "RE/T/TESObjectREFR.h"

#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace F4T::ParkedSuitSync
{
	using namespace detail;

	namespace
	{
		std::uint32_t NonceOf(const nlohmann::json& a_packet)
		{
			const auto found = a_packet.find("nonce");
			return found != a_packet.end() && found->is_number_unsigned() ? found->get<std::uint32_t>() : 0;
		}

		void StorePark(std::uint32_t a_suitId, const nlohmann::json& a_packet)
		{
			const auto owner = a_packet.find("ownerKey");
			const auto position = a_packet.find("position");
			if (owner == a_packet.end() || !owner->is_string() || position == a_packet.end() || !position->is_object()) {
				return;
			}
			const auto x = position->find("x");
			const auto y = position->find("y");
			const auto z = position->find("z");
			const auto coordinate = [&](auto it) { return it == position->end() ? std::optional<float>{} : F4T::JsonFloat::ReadFloat(*it); };
			const auto xValue = coordinate(x);
			const auto yValue = coordinate(y);
			const auto zValue = coordinate(z);
			if (!xValue || !yValue || !zValue) {
				return;
			}
			const auto powerArmor = a_packet.find("powerArmor");
			if (powerArmor == a_packet.end() || !powerArmor->is_object()) {
				return;
			}
			const auto frameBaseFormId = ParseHexId(*powerArmor, "frameBaseFormId");
			if (!frameBaseFormId) {
				return;
			}
			const auto decoded = F4T::PowerArmorSync::Decode(*powerArmor);
			if (!decoded) {
				return;
			}
			const auto angleField = a_packet.find("angleZ");
			const auto angle = angleField == a_packet.end() ? std::optional<float>{} : F4T::JsonFloat::ReadFloat(*angleField);
			if (!angle) {
				return;
			}
			const auto mineField = a_packet.find("mine");
			const auto mine = mineField != a_packet.end() && mineField->is_boolean() && mineField->get<bool>();
			const auto localId = ParseHexId(a_packet, "localId");
			{
				const std::scoped_lock lock(g_mutex);
				const auto existing = g_owners.find(a_suitId);
				if (existing != g_owners.end() && existing->second.mine && !mine) {
					g_hackResults.erase(a_suitId);
				}
				auto keptLocalId = localId;
				if (!keptLocalId && existing != g_owners.end()) {
					keptLocalId = existing->second.localId;
				}
				const auto adopting = g_adopting.find(a_suitId);
				if (mine && localId && adopting != g_adopting.end() && adopting->second) {
					g_adopting.erase(adopting);
				}
				if (mine && localId) {
					const auto graduated = g_graduated.find(*localId);
					if (graduated != g_graduated.end() && graduated->second == a_suitId) {
						g_graduated.erase(graduated);
					}
					const auto carried = g_graduatedBeforeReconnect.find(*localId);
					if (carried != g_graduatedBeforeReconnect.end() && carried->second == a_suitId) {
						g_graduatedBeforeReconnect.erase(carried);
					}
				}
				g_owners[a_suitId] = Placed{
					owner->get<std::string>(),
					keptLocalId,
					ParseHexId(*position, "cellId"),
					ParseHexId(*position, "worldspaceId"),
					*xValue,
					*yValue,
					*zValue,
					mine,
					*frameBaseFormId,
					*decoded,
					*angle
				};
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
		}

		void StoreChallenge(std::uint32_t a_suitId, const nlohmann::json& a_packet)
		{
			const auto seed = a_packet.find("seed");
			const auto length = a_packet.find("length");
			if (seed == a_packet.end() || length == a_packet.end()) {
				return;
			}
			const auto validSeed = AsInteger(*seed, 1, std::numeric_limits<std::uint32_t>::max());
			const auto validLength = AsInteger(*length, kBreachMinimumLength, kBreachMaximumLength);
			if (!validSeed || !validLength) {
				return;
			}
			{
				const std::scoped_lock lock(g_mutex);
				g_challenge = Challenge{ a_suitId, static_cast<std::uint32_t>(*validSeed), static_cast<std::uint32_t>(*validLength) };
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
		}

		void StoreHackResult(std::uint32_t a_suitId, const nlohmann::json& a_packet)
		{
			const auto hacked = a_packet.find("hacked");
			const auto broken = hacked != a_packet.end() && hacked->is_boolean() && hacked->get<bool>();
			{
				const std::scoped_lock lock(g_mutex);
				g_hackResults[a_suitId] = broken;
				if (broken) {
					g_appliedLocked.erase(a_suitId);
				}
				ForgetChallenge(a_suitId);
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
		}

		// TODO-verify: DisableFrame hides a released frame for the previous owner in game; source:CodeNotes-GARDEN power-armor-sync.
		void ReleaseSuit(std::uint32_t a_suitId, std::optional<std::uint32_t> a_localId)
		{
			std::optional<std::uint32_t> released = a_localId;
			std::optional<std::uint32_t> expectedBase;
			{
				const std::scoped_lock lock(g_mutex);
				const auto owner = g_owners.find(a_suitId);
				if (owner != g_owners.end()) {
					if (!released) {
						released = owner->second.localId;
					}
					if (owner->second.localId == released) {
						expectedBase = owner->second.frameBaseFormId;
						owner->second.localId = std::nullopt;
					}
				}
				const auto applied = g_appliedLocked.find(a_suitId);
				if (applied != g_appliedLocked.end() && applied->second.localId == released) {
					g_appliedLocked.erase(applied);
				}
				g_hackResults.erase(a_suitId);
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
			if (!released) {
				return;
			}
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				F4T::DebugLogging::Log("ParkedSuitSync", "task interface unavailable; released parked suit frame {:08X} keeps its lock", *released);
				return;
			}
			taskInterface->AddTask([localId = *released, expectedBase]() {
				auto* frame = RE::TESForm::GetFormByID<RE::TESObjectREFR>(localId);
				if (!frame || !expectedBase || !IsPowerArmorFrame(*frame) || !FrameResolvesToBase(localId, *expectedBase)) {
					return;
				}
				(void)F4T::PowerArmorScript::SetFrameLocked(*frame, false);
				(void)F4T::PowerArmorScript::DisableFrame(*frame);
			});
		}

		void HandleAck(const nlohmann::json& a_packet)
		{
			const auto op = a_packet.find("op");
			const auto ok = a_packet.find("ok");
			const auto localId = ParseHexId(a_packet, "localId");
			if (op == a_packet.end() || !op->is_string() || ok == a_packet.end() || !ok->is_boolean() || !localId) {
				return;
			}
			const auto name = op->get<std::string>();
			const auto accepted = ok->get<bool>();
			const auto nonce = NonceOf(a_packet);
			const auto reason = a_packet.find("reason");
			const std::string refusal = reason != a_packet.end() && reason->is_string() ? reason->get<std::string>() : std::string{};
			const std::scoped_lock lock(g_mutex);
			if (nonce == 0 || g_pending.empty() || g_pending.front().op != name || g_pending.front().localId != *localId || NonceOf(g_pending.front().packet) != nonce) {
				return;
			}
			if (accepted) {
				if (name == "adopt") {
					const auto suitId = ParseHexId(g_pending.front().packet, "suitId");
					const auto adopting = suitId ? g_adopting.find(*suitId) : g_adopting.end();
					if (adopting != g_adopting.end()) {
						adopting->second = true;
					}
				}
				if (g_pending.front().consoleSpawned) {
					g_consoleParked.insert(g_pending.front().localId);
					g_consoleUnsent.erase(g_pending.front().localId);
				}
				g_pending.pop_front();
				return;
			}
			if (g_pending.front().staleWorld) {
				g_pending.pop_front();
				return;
			}
			g_pending.front().nacked = true;
			g_pending.front().refusal = refusal;
			g_pending.front().inFlight = false;
			if (g_pending.front().op == "park" && IsDefinitiveRefusal(refusal)) {
				F4T::DebugLogging::Log("ParkedSuitSync", "park for localId {:08X} refused by the server: {}", *localId, refusal);
				g_pending.pop_front();
				return;
			}
			g_pending.front().nextSend = std::chrono::steady_clock::now() + NackDelay(g_pending.front());
			if (g_pending.front().op == "park") {
				auto moved = std::move(g_pending.front());
				g_pending.pop_front();
				g_pending.push_back(std::move(moved));
			}
		}
	}

	bool TryHandlePacket(std::string_view a_packetType, const nlohmann::json& a_packet)
	{
		if (a_packetType != F4T::ParkedSuit::kPacketType) {
			return false;
		}
		const auto action = a_packet.find("action");
		if (action == a_packet.end() || !action->is_string()) {
			return true;
		}
		const auto name = action->get<std::string>();
		if (name == "ack") {
			HandleAck(a_packet);
			return true;
		}
		const auto suitId = ReadSuitId(a_packet);
		if (!suitId) {
			return true;
		}
		if (name == "park") {
			StorePark(*suitId, a_packet);
			return true;
		}
		if (name == "remove") {
			{
				const std::scoped_lock lock(g_mutex);
				g_owners.erase(*suitId);
				g_hackResults.erase(*suitId);
			}
			F4T::ParkedSuitUi::NotifyStateChanged();
			return true;
		}
		if (name == "release") {
			ReleaseSuit(*suitId, ParseHexId(a_packet, "localId"));
			return true;
		}

		if (name == "breachChallenge") {
			StoreChallenge(*suitId, a_packet);
			return true;
		}
		if (name == "hackResult") {
			StoreHackResult(*suitId, a_packet);
			return true;
		}
		if (name == "hackAlert") {
			const auto failed = a_packet.find("failed");
			const auto reported = failed != a_packet.end() && failed->is_boolean() && failed->get<bool>();
			F4T::ParkedSuitUi::NotifyHackAttempt(*suitId, reported);
			return true;
		}
		return true;
	}

	std::optional<Record> Find(std::uint32_t a_suitId)
	{
		const std::scoped_lock lock(g_mutex);
		const auto found = g_owners.find(a_suitId);
		if (found == g_owners.end()) {
			return std::nullopt;
		}
		return Record{ found->first, found->second.ownerKey };
	}

	std::optional<bool> LastHackResult(std::uint32_t a_suitId)
	{
		const std::scoped_lock lock(g_mutex);
		const auto found = g_hackResults.find(a_suitId);
		if (found == g_hackResults.end()) {
			return std::nullopt;
		}
		return found->second;
	}
}
