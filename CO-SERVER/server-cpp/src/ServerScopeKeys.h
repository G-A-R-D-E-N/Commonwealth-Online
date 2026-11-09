#pragma once

#include "F4TFormIdentity.h"
#include "F4TInterestScope.h"
#include "F4TInventoryContract.h"
#include "F4TLoadOrder.h"
#include "F4TNpcAuthority.h"
#include "ServerSessionTypes.h"

#include <charconv>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <system_error>

namespace CommonwealthOnline::Server::ScopeKeys
{
	using Plugins = std::optional<F4T::LoadOrder::PluginList>;
	using Wires = std::map<F4T::NpcAuthority::Scope, F4T::NpcAuthority::Scope>;

	inline std::optional<std::string> BaseGameKey(std::uint32_t a_formId)
	{
		const auto split = F4T::FormIdentity::SplitFormId(a_formId);
		if (split.kind != F4T::FormIdentity::Kind::Regular || split.fileIndex != 0) {
			return std::nullopt;
		}
		return F4T::InventoryContract::MakeItemKey("Fallout4.esm", split.localId, split.kind);
	}

	inline std::string FormKey(const Plugins& a_plugins, const std::string& a_formId)
	{
		if (a_formId.empty()) {
			return a_formId;
		}
		std::uint32_t formId{};
		const char* end = a_formId.data() + a_formId.size();
		const auto parsed = std::from_chars(a_formId.data(), end, formId, 16);
		if (parsed.ec != std::errc{} || parsed.ptr != end) {
			return a_formId;
		}
		if (a_plugins) {
			return F4T::LoadOrder::ResolveValidatedFormKey(*a_plugins, formId).value_or(a_formId);
		}
		return BaseGameKey(formId).value_or(a_formId);
	}

	inline F4T::NpcAuthority::Scope Canonical(const Plugins& a_plugins, const F4T::NpcAuthority::Scope& a_raw)
	{
		return F4T::NpcAuthority::Scope{ FormKey(a_plugins, a_raw.cellId), FormKey(a_plugins, a_raw.worldspaceId) };
	}

	inline void Stamp(
		SessionTypes::StoredMovementState& a_movement,
		const Plugins& a_plugins,
		const std::optional<SessionTypes::StoredMovementState>& a_previous = std::nullopt)
	{
		if (a_previous && a_previous->cellId == a_movement.cellId && a_previous->worldspaceId == a_movement.worldspaceId) {
			a_movement.cellKey = a_previous->cellKey;
			a_movement.worldspaceKey = a_previous->worldspaceKey;
			return;
		}
		a_movement.cellKey = FormKey(a_plugins, a_movement.cellId);
		a_movement.worldspaceKey = FormKey(a_plugins, a_movement.worldspaceId);
	}

	inline std::string CellKey(const SessionTypes::StoredMovementState& a_movement)
	{
		return a_movement.cellKey.empty() ? a_movement.cellId : a_movement.cellKey;
	}

	inline std::string WorldspaceKey(const SessionTypes::StoredMovementState& a_movement)
	{
		return a_movement.worldspaceKey.empty() ? a_movement.worldspaceId : a_movement.worldspaceKey;
	}

	inline F4T::NpcAuthority::Scope MovementScope(const SessionTypes::StoredMovementState& a_movement)
	{
		return F4T::NpcAuthority::Scope{ CellKey(a_movement), WorldspaceKey(a_movement) };
	}

	inline std::optional<F4T::InterestScope::StateScope> InterestScopeOf(const std::optional<SessionTypes::StoredMovementState>& a_movement)
	{
		if (!a_movement) {
			return std::nullopt;
		}
		return F4T::InterestScope::StateScope{ CellKey(*a_movement), WorldspaceKey(*a_movement), a_movement->x, a_movement->y };
	}

	inline std::optional<F4T::NpcAuthority::Scope> KeyOfWire(const Wires& a_wires, const F4T::NpcAuthority::Scope& a_wire)
	{
		for (const auto& [key, wire] : a_wires) {
			if (wire == a_wire) {
				return key;
			}
		}
		return std::nullopt;
	}

	inline F4T::NpcAuthority::Scope WireOf(const Wires& a_wires, const F4T::NpcAuthority::Scope& a_key)
	{
		const auto found = a_wires.find(a_key);
		return found == a_wires.end() ? a_key : found->second;
	}
}
