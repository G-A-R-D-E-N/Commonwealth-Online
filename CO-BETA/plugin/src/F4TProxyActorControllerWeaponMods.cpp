#include "pch.h"

#include "F4TEquipmentInstance.h"
#include "F4TProxyActorControllerInternal.h"

using namespace F4T::ProxyActorController::detail;

namespace F4T::ProxyActorController::detail
{
	ModdedWeaponEquip EquipSyncedWeaponWithMods(
		RE::ActorEquipManager& a_equipManager,
		RE::Actor& a_proxy,
		RE::TESObjectWEAP& a_weapon,
		const std::vector<F4T::RemotePlayerState::RemoteItemMod>& a_mods,
		std::string_view a_logKey)
	{
		const auto resolved = F4T::EquipmentInstance::ResolveModdedStack(
			a_proxy,
			a_weapon,
			std::addressof(a_mods),
			[&](std::uint32_t a_modFormId) {
				LogThrottledWarning(
					std::string{ "proxy_weapon_mod_missing_" } + std::string{ a_logKey } + "_" + std::to_string(a_modFormId),
					std::format("Could not resolve weapon mod {:08X}; skipping recipe.", a_modFormId));
			});

		const auto decisionKey = std::string{ "proxy_weapon_mod_decision_" } + std::string{ a_logKey } + "_" + std::to_string(a_weapon.GetFormID());
		if (F4T::DebugLogging::IsEnabled()) {
			LogThrottledInfo(
				decisionKey + "_status",
				std::format("[RT-VERIFY 361-D] weapon {:08X} mods={} status={} (0 resolved, 1 base only, 2 mod missing, 3 stack missing)", a_weapon.GetFormID(), F4T::EquipmentState::DescribeMods(a_mods), static_cast<int>(resolved.status)),
				1s);
		}
		if (resolved.status != F4T::EquipmentInstance::ModdedStackStatus::kResolved) {
			return {};
		}
		if (F4T::DebugLogging::IsEnabled()) {
			LogThrottledInfo(
				decisionKey + "_extra",
				std::format("[RT-VERIFY 361-D] weapon {:08X} stackFound={} extraFound={}", a_weapon.GetFormID(), resolved.found.stack != nullptr, resolved.found.stack && resolved.found.stack->extra),
				1s);
		}
		if (!resolved.found.stack || !resolved.found.stack->extra) {
			return {};
		}

		ModdedWeaponEquip result;
		result.extra = resolved.found.stack->extra.get();
		if (F4T::DebugLogging::IsEnabled()) {
			LogThrottledInfo(
				decisionKey + "_instance",
				std::format("[RT-VERIFY 361-D] weapon {:08X} stackId={} instanceDataFound={}", a_weapon.GetFormID(), resolved.found.stackId, resolved.instanceData != nullptr),
				1s);
		}
		if (!resolved.instanceData) {
			return result;
		}

		RE::BGSObjectInstance objectInstance{ std::addressof(a_weapon), resolved.instanceData };
		result.equipped = a_equipManager.EquipObject(
			std::addressof(a_proxy),
			objectInstance,
			resolved.found.stackId,
			1,
			nullptr,
			false,
			true,
			false,
			true,
			false);
		// TODO-verify(#407)
		if (F4T::DebugLogging::IsEnabled()) {
			LogThrottledInfo(
				decisionKey + "_equip",
				std::format("[RT-VERIFY 361-I] equip formId={:08X} stackId={} mods={} equipped={}", a_weapon.GetFormID(), resolved.found.stackId, a_mods.size(), result.equipped),
				1s);
		}
		if (!result.equipped) {
			LogThrottledWarning(
				std::string{ "proxy_weapon_mod_equip_failed_" } + std::string{ a_logKey } + "_" + std::to_string(a_weapon.GetFormID()),
				std::format("ActorEquipManager failed to equip weapon {:08X} with mods on proxy {:08X}.", a_weapon.GetFormID(), a_proxy.GetFormID()));
		}
		result.instanceData = resolved.instanceData;
		return result;
	}
}
