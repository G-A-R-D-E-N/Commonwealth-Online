#pragma once

#include "F4TQuestProtocol.h"

#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <limits>
#include <optional>
#include <string_view>
#include <variant>

namespace F4T::SleepBedIdentity
{
	inline constexpr std::string_view kCapabilityName = "sleep-bed-v1";
	[[nodiscard]] constexpr bool IsValidMarker(std::uint32_t a_marker)
	{
		return a_marker < 22;
	}
	[[nodiscard]] constexpr bool IsEnabledMarker(std::uint32_t a_flags, std::uint32_t a_marker)
	{
		return IsValidMarker(a_marker) && (a_flags & (std::uint32_t{ 1 } << a_marker)) != 0;
	}
	struct WorkshopObject
	{
		std::uint64_t zone{};
		std::uint64_t object{};
		bool operator==(const WorkshopObject&) const = default;
	};

	using Identity = std::variant<QuestProtocol::FormKey, WorkshopObject>;
	struct RelayedSleep
	{
		std::uint32_t playerId{};
		std::optional<Identity> bed;
		std::uint32_t marker{};
	};
	[[nodiscard]] std::optional<Identity> ParseBedIdentity(const nlohmann::json& a_value);
	[[nodiscard]] std::optional<RelayedSleep> ParseRelayedSleep(const nlohmann::json& a_value);
	[[nodiscard]] std::optional<nlohmann::json> MakeSleepEnter(const Identity& a_bed, std::uint32_t a_marker);
	[[nodiscard]] nlohmann::json MakeSleepExit();
	class RemoteStates
	{
	public:
		bool Apply(const RelayedSleep& a_state);
		[[nodiscard]] std::optional<RelayedSleep> Find(std::uint32_t a_playerId) const;
		void Erase(std::uint32_t a_playerId);
		void Clear();
		[[nodiscard]] std::optional<std::uint64_t> Revision(std::uint32_t a_playerId) const;
	private:
		struct Entry { RelayedSleep state; std::uint64_t revision; };
		std::map<std::uint32_t, Entry> states_;
		std::uint64_t revision_{};
	};
}
