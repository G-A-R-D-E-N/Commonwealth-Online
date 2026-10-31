#pragma once

#include "AdminCommandDispatcher.h"
#include "PersistenceGate.h"
#include "ServerProtocol.h"

#include "F4TCharacterSlots.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	using QuestFrameSender = std::function<void(std::vector<OutboundFrame>&)>;
	using QuestProfileResolver = std::function<std::optional<std::string>(std::string_view)>;

	inline constexpr std::string_view kQuestSyncDisabledMessage = "Quest sync is not enabled on this server.";

	template <typename State>
	[[nodiscard]] std::optional<std::string> QuestAccountForConnection(std::uint32_t a_connectionId, const State& a_state, PersistenceGate* a_persistence)
	{
		// Constraint: quest state is keyed by the session account, not the active slot.
		if (a_persistence != nullptr) {
			if (auto profile = a_persistence->ProfileForConnection(a_connectionId)) {
				if (const auto slotKey = F4T::CharacterSlots::ParseSlotKey(*profile)) return slotKey->first;
				return profile;
			}
		}
		if (a_state.profileId.empty()) return std::nullopt;
		return a_state.profileId;
	}

	template <typename Connections>
	[[nodiscard]] bool IsKnownQuestProfile(std::string_view a_profileId, PersistenceGate* a_persistence, const Connections& a_connections)
	{
		for (const auto& [connectionId, state] : a_connections) {
			if (state.active && QuestAccountForConnection(connectionId, state, a_persistence) == a_profileId) return true;
		}
		if (a_persistence == nullptr) return false;
		auto& store = a_persistence->Store();
		return store.GetProfileSecret(a_profileId).has_value() || store.GetResumeCredential(a_profileId).has_value();
	}

	inline AdminActionResult RunQuestModeAdminAction(
		ProtocolHost& a_protocol,
		std::string_view a_mode,
		double a_unixTimeSeconds,
		const QuestFrameSender& a_send)
	{
		if (!a_protocol.QuestSyncEnabled()) {
			return AdminActionResult{ false, std::string{ kQuestSyncDisabledMessage }, nullptr };
		}
		if (a_mode.empty()) {
			return AdminActionResult{
				true,
				"Quest mode is " + std::string{ F4T::QuestProtocol::ModeName(a_protocol.QuestMode()) } + ".",
				a_protocol.QuestStatus()
			};
		}
		const auto mode = F4T::QuestProtocol::ParseMode(a_mode);
		if (!mode) {
			return AdminActionResult{
				false,
				"Invalid quest mode '" + std::string{ a_mode } +
					"'; use coop-story, faction-war, instanced-story or free-roam.",
				nullptr
			};
		}
		const auto name = std::string{ F4T::QuestProtocol::ModeName(*mode) };
		if (a_protocol.QuestMode() == *mode) {
			return AdminActionResult{ true, "Quest mode is already " + name + ".", a_protocol.QuestStatus() };
		}
		auto frames = a_protocol.SetQuestMode(*mode, a_unixTimeSeconds);
		if (a_protocol.QuestMode() != *mode) {
			return AdminActionResult{ false, "Quest mode could not be saved; the durable quest write failed.", nullptr };
		}
		a_send(frames);
		return AdminActionResult{ true, "Quest mode set to " + name + ".", a_protocol.QuestStatus() };
	}

	inline AdminActionResult RunQuestGateAdminAction(
		ProtocolHost& a_protocol,
		const F4T::QuestProtocol::FormKey& a_quest,
		std::uint16_t a_stage,
		bool a_release,
		double a_unixTimeSeconds,
		const QuestFrameSender& a_send)
	{
		if (!a_protocol.QuestSyncEnabled()) {
			return AdminActionResult{ false, std::string{ kQuestSyncDisabledMessage }, nullptr };
		}
		const auto quest = F4T::QuestProtocol::FormatFormKey(a_quest);
		const auto label = quest + " stage " + std::to_string(a_stage);
		const auto data = nlohmann::json{ { "quest", quest }, { "stage", a_stage }, { "release", a_release } };
		const bool gated = a_protocol.IsQuestGated(a_quest, a_stage);
		if (a_release) {
			if (!gated) {
				return AdminActionResult{ false, "No quest gate is set for " + label + ".", nullptr };
			}
			auto frames = a_protocol.SetQuestGate(a_quest, a_stage, true, a_unixTimeSeconds);
			if (a_protocol.IsQuestGated(a_quest, a_stage)) {
				return AdminActionResult{
					false,
					"Quest gate for " + label + " could not be released; the durable quest write failed.",
					nullptr
				};
			}
			a_send(frames);
			return AdminActionResult{ true, "Released quest gate " + label + ".", data };
		}
		if (gated) {
			return AdminActionResult{ true, "Quest gate for " + label + " was already set.", data };
		}
		auto frames = a_protocol.SetQuestGate(a_quest, a_stage, false, a_unixTimeSeconds);
		if (!a_protocol.IsQuestGated(a_quest, a_stage)) {
			return AdminActionResult{
				false,
				"Quest gate for " + label + " could not be applied; the gate table is full or the durable quest write failed.",
				nullptr
			};
		}
		a_send(frames);
		return AdminActionResult{ true, "Gated quest stage " + label + ".", data };
	}

	inline AdminActionResult RunQuestSoloAdminAction(
		ProtocolHost& a_protocol,
		std::string_view a_profileId,
		const F4T::QuestProtocol::FormKey& a_quest,
		bool a_solo,
		double a_unixTimeSeconds,
		const QuestFrameSender& a_send)
	{
		if (!a_protocol.QuestSyncEnabled()) {
			return AdminActionResult{ false, std::string{ kQuestSyncDisabledMessage }, nullptr };
		}
		const auto profile = std::string{ a_profileId };
		const auto quest = F4T::QuestProtocol::FormatFormKey(a_quest);
		const auto data = nlohmann::json{ { "profileId", profile }, { "quest", quest }, { "solo", a_solo } };
		if (a_protocol.IsQuestSolo(a_profileId, a_quest) == a_solo) {
			return AdminActionResult{
				true,
				a_solo ? "Quest " + quest + " is already solo for " + profile + "."
					   : "Quest " + quest + " is not solo for " + profile + ".",
				data
			};
		}
		auto frames = a_protocol.SetQuestSolo(a_profileId, a_quest, a_solo, a_unixTimeSeconds);
		if (a_protocol.IsQuestSolo(a_profileId, a_quest) != a_solo) {
			return AdminActionResult{
				false,
				a_solo ? "Solo quest " + quest + " could not be set for " + profile +
							 "; the solo table is full or the durable quest write failed."
					   : "Solo quest " + quest + " could not be cleared for " + profile +
							 "; the durable quest write failed.",
				nullptr
			};
		}
		a_send(frames);
		return AdminActionResult{
			true,
			a_solo ? "Quest " + quest + " is now solo for " + profile + "."
				   : "Cleared solo quest " + quest + " for " + profile + ".",
			data
		};
	}

	inline AdminActionResult RunQuestSoloSelectorAdminAction(
		ProtocolHost& a_protocol,
		std::string_view a_player,
		const F4T::QuestProtocol::FormKey& a_quest,
		bool a_solo,
		double a_unixTimeSeconds,
		const QuestFrameSender& a_send,
		const QuestProfileResolver& a_resolve)
	{
		if (!a_protocol.QuestSyncEnabled()) {
			return AdminActionResult{ false, std::string{ kQuestSyncDisabledMessage }, nullptr };
		}
		const auto profileId = a_resolve(a_player);
		if (!profileId) {
			const bool profileSelector = a_player.substr(0, kProfileSelectorPrefix.size()) == kProfileSelectorPrefix;
			return AdminActionResult{ false, (profileSelector ? "No known profile matching " : "No connected player matching ") + std::string{ a_player } + ".", nullptr };
		}
		return RunQuestSoloAdminAction(a_protocol, *profileId, a_quest, a_solo, a_unixTimeSeconds, a_send);
	}

	template <typename Connections, typename Clock>
	AdminCommandHooks WithQuestAdminHooks(
		AdminCommandHooks a_hooks,
		ProtocolHost& a_protocol,
		PersistenceGate* a_persistence,
		const Connections& a_connections,
		const QuestFrameSender& a_send,
		Clock a_now)
	{
		const QuestProfileResolver resolve = [a_persistence, connections = &a_connections](std::string_view a_selector) {
			return ResolveProfileSelector(
				a_selector,
				[a_persistence, connections](std::uint32_t a_playerId) -> std::optional<std::string> {
					for (const auto& [connectionId, state] : *connections) {
						if (state.active && state.playerId == a_playerId) return QuestAccountForConnection(connectionId, state, a_persistence);
					}
					return std::nullopt;
				},
				[a_persistence, connections](std::string_view a_profileId) { return IsKnownQuestProfile(a_profileId, a_persistence, *connections); });
		};
		a_hooks.questMode = [&a_protocol, a_send, a_now](std::string_view a_mode) {
			return RunQuestModeAdminAction(a_protocol, a_mode, a_now(), a_send);
		};
		a_hooks.questGate = [&a_protocol, a_send, a_now](const F4T::QuestProtocol::FormKey& a_quest, std::uint16_t a_stage, bool a_release) {
			return RunQuestGateAdminAction(a_protocol, a_quest, a_stage, a_release, a_now(), a_send);
		};
		a_hooks.questSolo = [&a_protocol, a_send, a_now, resolve](std::string_view a_player, const F4T::QuestProtocol::FormKey& a_quest, bool a_solo) {
			return RunQuestSoloSelectorAdminAction(a_protocol, a_player, a_quest, a_solo, a_now(), a_send, resolve);
		};
		return a_hooks;
	}
}
