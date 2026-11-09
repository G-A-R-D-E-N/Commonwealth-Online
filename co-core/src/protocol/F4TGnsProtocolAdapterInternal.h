#pragma once

#include "F4TGnsProtocolAdapter.h"
#include "F4TDifficultyReport.h"
#include "F4TInventoryContract.h"
#include "F4TGnsProtocolAdapterJson.h"
#include "F4TGnsProtocolAdapterModpack.h"
#include "F4TGnsProtocolAdapterSlots.h"
#include "F4TNpcAuthority.h"
#include "F4TRemotePlayerCapacity.h"
#include "F4TSecretWipe.h"
#include "F4TSurvivalPolicyState.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace F4T::GnsProtocolAdapter::Internal
{
	using Json = nlohmann::json;
	using Delivery = F4T::GnsProtocolAdapter::Delivery;
	using OutboundMessage = F4T::GnsProtocolAdapter::OutboundMessage;
	using NpcAuthorityAssignment = F4T::GnsProtocolAdapter::NpcAuthorityAssignment;
	using F4T::GnsProtocolAdapter::Detail::ReadBoolean;
	using F4T::GnsProtocolAdapter::Detail::ReadPositiveFiniteDouble;
	using F4T::GnsProtocolAdapter::Detail::ReadString;
	using F4T::GnsProtocolAdapter::Detail::ReadUnsigned32;
	using F4T::GnsProtocolAdapter::Detail::ReadUnsigned64;
	constexpr std::size_t kMaximumPendingRemoteActionEvents = 64;
	constexpr std::size_t kMaximumRosterEntries = F4T::RemotePlayerCapacity::MaximumTrackedRemotePlayers;
	constexpr std::size_t kMaximumNpcAuthorityScopes = F4T::NpcAuthority::MaximumTrackedEpochScopes;
	constexpr std::array<const char*, 7> kStickyPlayerStateFields{ "equippedItems", "appearance", "characterName", "displayName", "factionTag", "playerKey", "powerArmor" };
	constexpr std::chrono::seconds kStickyPlayerStateResendInterval{ 5 };

	struct ScopeKey
	{
		std::string cellId;
		std::string worldspaceId;
		bool operator==(const ScopeKey&) const = default;
	};

	struct ScopeKeyHash
	{
		std::size_t operator()(const ScopeKey& a_scope) const noexcept
		{
			const auto first = std::hash<std::string>{}(a_scope.cellId);
			const auto second = std::hash<std::string>{}(a_scope.worldspaceId);
			return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
		}
	};

	inline std::string TrimLegacyPayload(std::string_view a_payload)
	{
		while (!a_payload.empty() &&
		       (a_payload.back() == '\n' || a_payload.back() == '\r' || a_payload.back() == ' ' || a_payload.back() == '\t')) {
			a_payload.remove_suffix(1);
		}
		while (!a_payload.empty() &&
		       (a_payload.front() == '\n' || a_payload.front() == '\r' || a_payload.front() == ' ' || a_payload.front() == '\t')) {
			a_payload.remove_prefix(1);
		}
		return std::string{ a_payload };
	}

	inline std::optional<ScopeKey> ReadAuthorityScope(const Json& a_packet)
	{
		const auto cellId = ReadString(a_packet, "authorityCellId");
		if (!cellId || cellId->empty()) {
			return std::nullopt;
		}
		return ScopeKey{ *cellId, ReadString(a_packet, "authorityWorldspaceId").value_or("") };
	}

	inline std::string TranslateFormIdText(
		const F4T::GnsProtocolAdapter::FormIdTranslator& a_translator,
		std::uint32_t a_playerId,
		const std::string& a_text)
	{
		if (a_text.empty() || a_text.size() > 8) {
			return a_text;
		}
		std::uint32_t value = 0;
		const auto [end, error] = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);
		if (error != std::errc{} || end != a_text.data() + a_text.size()) {
			return a_text;
		}
		const auto translated = a_translator(a_playerId, value);
		if (translated == value) {
			return a_text;
		}
		char buffer[9]{};
		std::snprintf(buffer, sizeof(buffer), "%08X", translated);
		return buffer;
	}

	inline std::optional<ScopeKey> ReadNpcScope(const Json& a_npc)
	{
		const auto cellId = ReadString(a_npc, "cellId");
		if (!cellId || cellId->empty()) {
			return std::nullopt;
		}
		return ScopeKey{ *cellId, ReadString(a_npc, "worldspaceId").value_or("") };
	}

	inline void MergeStickyPlayerState(Json& a_transform, const Json& a_sticky)
	{
		for (const char* field : kStickyPlayerStateFields) {
			if (const auto iterator = a_sticky.find(field); iterator != a_sticky.end()) {
				a_transform[field] = *iterator;
			}
		}
	}

	inline void AppendPendingActionEvents(Json& a_pending, const Json& a_actions)
	{
		if (!a_pending.is_array()) {
			a_pending = Json::array();
		}
		for (const auto& action : a_actions) {
			if (a_pending.size() >= kMaximumPendingRemoteActionEvents) {
				a_pending.erase(a_pending.begin());
			}
			a_pending.push_back(action);
		}
	}
}
