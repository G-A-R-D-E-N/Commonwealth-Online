#pragma once
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace F4T::DeathBagPlaceRecovery
{
	enum class Holding { kFull, kNone, kPartial };
	enum class Resolution { kRecordRestoredFailure, kReplace, kKeep, kResendPlaced, kResendUnplaced };
	struct PendingAge
	{
		std::string deathId;
		std::int64_t createdAt{};
	};
	inline constexpr std::int64_t MaximumPendingAgeSeconds = 48 * 60 * 60;
	inline constexpr std::size_t MaximumPendingPlaces = 8;
	[[nodiscard]] inline Holding Classify(const nlohmann::json& a_persisted, const nlohmann::json& a_carried)
	{
		if (!a_persisted.is_array() || !a_carried.is_array() || a_persisted.empty()) return Holding::kPartial;
		bool anyHeld = false;
		bool allHeld = true;
		for (const auto& stack : a_persisted) {
			std::uint64_t held = 0;
			for (const auto& carried : a_carried) {
				if (carried.at("itemKey") == stack.at("itemKey")) held += carried.at("count").get<std::uint64_t>();
			}
			anyHeld = anyHeld || held > 0;
			allHeld = allHeld && held >= stack.at("count").get<std::uint64_t>();
		}
		if (allHeld) return Holding::kFull;
		return anyHeld ? Holding::kPartial : Holding::kNone;
	}
	[[nodiscard]] inline bool Exact(const nlohmann::json& a_persisted, const nlohmann::json& a_carried)
	{
		if (!a_persisted.is_array() || !a_carried.is_array() || a_persisted.empty()) return false;
		std::vector<std::string> persisted;
		std::vector<std::string> carried;
		for (const auto& stack : a_persisted) persisted.push_back(stack.dump());
		for (const auto& stack : a_carried) carried.push_back(stack.dump());
		std::sort(persisted.begin(), persisted.end());
		std::sort(carried.begin(), carried.end());
		return persisted == carried;
	}
	[[nodiscard]] inline bool MayBePlaced(const std::string& a_phase)
	{
		return a_phase == "placed" || a_phase == "placing";
	}
	[[nodiscard]] inline Resolution Resolve(const std::string& a_phase, Holding a_holding, bool a_exact)
	{
		if (a_holding == Holding::kPartial) return Resolution::kKeep;
		if (a_holding == Holding::kFull) {
			if (!MayBePlaced(a_phase)) return Resolution::kRecordRestoredFailure;
			return a_exact ? Resolution::kReplace : Resolution::kKeep;
		}
		return MayBePlaced(a_phase) ? Resolution::kResendPlaced : Resolution::kResendUnplaced;
	}
	[[nodiscard]] inline std::vector<std::string> Expired(const std::vector<PendingAge>& a_entries, std::int64_t a_now)
	{
		std::vector<std::string> expired;
		for (const auto& entry : a_entries) {
			if (a_now - entry.createdAt >= MaximumPendingAgeSeconds) expired.push_back(entry.deathId);
		}
		return expired;
	}
	[[nodiscard]] inline std::vector<std::string> Overflow(std::vector<PendingAge> a_entries, std::size_t a_room)
	{
		const auto limit = MaximumPendingPlaces - (std::min)(a_room, MaximumPendingPlaces);
		std::vector<std::string> dropped;
		if (a_entries.size() <= limit) return dropped;
		std::sort(a_entries.begin(), a_entries.end(), [](const PendingAge& a_left, const PendingAge& a_right) {
			return a_left.createdAt != a_right.createdAt ? a_left.createdAt < a_right.createdAt : a_left.deathId < a_right.deathId;
		});
		for (std::size_t index = 0; index < a_entries.size() - limit; ++index) dropped.push_back(a_entries[index].deathId);
		return dropped;
	}
}
