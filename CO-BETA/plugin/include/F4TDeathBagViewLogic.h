#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::DeathBagView
{
	inline constexpr double kDiscoveryIntervalSeconds = 10.0;
	inline constexpr double kCheckIntervalSeconds = 0.25;
	inline constexpr double kInFlightSeconds = 15.0;
	inline constexpr std::size_t kMaximumInFlight = 256;
	inline constexpr std::uint64_t kMaximumStackCount = static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)());

	using KeyedCounts = std::map<std::string, std::uint64_t>;

	struct Take
	{
		std::string itemKey;
		std::uint64_t count{};
	};

	[[nodiscard]] inline std::optional<KeyedCounts> CountStacks(const nlohmann::json& a_stacks)
	{
		if (!a_stacks.is_array()) {
			return std::nullopt;
		}
		KeyedCounts counts;
		for (const auto& stack : a_stacks) {
			if (!stack.is_object() || stack.size() != 3 || !stack.contains("itemKey") || !stack.at("itemKey").is_string() ||
				!stack.contains("count") || !stack.at("count").is_number_unsigned() || !stack.contains("metadata")) {
				return std::nullopt;
			}
			const auto count = stack.at("count").get<std::uint64_t>();
			auto& total = counts[stack.at("itemKey").get<std::string>()];
			if (count == 0 || count > kMaximumStackCount || total > kMaximumStackCount - count) {
				return std::nullopt;
			}
			total += count;
		}
		return counts;
	}

	[[nodiscard]] inline bool ValidPosition(const nlohmann::json& a_position)
	{
		if (!a_position.is_array() || a_position.size() != 3) {
			return false;
		}
		for (const auto& value : a_position) {
			if (!value.is_number() || !std::isfinite(value.get<double>())) {
				return false;
			}
		}
		return true;
	}

	[[nodiscard]] inline bool IsTakeable(const nlohmann::json& a_bag)
	{
		if (!a_bag.is_object() || !a_bag.contains("deathId") || !a_bag.at("deathId").is_string() || !a_bag.contains("stacks") ||
			!a_bag.contains("location") || !a_bag.contains("canTake") || !a_bag.at("canTake").is_boolean() || !a_bag.at("canTake").get<bool>()) {
			return false;
		}
		const auto& location = a_bag.at("location");
		return location.is_object() && location.contains("position") && ValidPosition(location.at("position"));
	}

	[[nodiscard]] inline bool LocationMatches(
		const nlohmann::json& a_bag,
		const std::string& a_cellKey,
		const std::optional<std::string>& a_worldspaceKey)
	{
		const auto& location = a_bag.at("location");
		if (!location.contains("cellKey") || !location.contains("worldspaceKey") || location.at("cellKey") != a_cellKey) {
			return false;
		}
		const auto& worldspace = location.at("worldspaceKey");
		return worldspace.is_null() || (a_worldspaceKey.has_value() && worldspace == *a_worldspaceKey);
	}

	[[nodiscard]] inline std::vector<Take> DetectTakes(const KeyedCounts& a_built, const KeyedCounts& a_current)
	{
		std::vector<Take> takes;
		for (const auto& [itemKey, built] : a_built) {
			const auto found = a_current.find(itemKey);
			const std::uint64_t current = found == a_current.end() ? 0 : found->second;
			if (current < built) {
				takes.push_back(Take{ itemKey, built - current });
			}
		}
		return takes;
	}

	[[nodiscard]] inline bool CanWithdraw(const nlohmann::json& a_stacks, const std::string& a_itemKey)
	{
		std::size_t entries = 0;
		bool anyMetadata = false;
		for (const auto& stack : a_stacks) {
			const auto key = stack.find("itemKey");
			if (key == stack.end() || *key != a_itemKey) {
				continue;
			}
			++entries;
			const auto metadata = stack.find("metadata");
			if (metadata == stack.end() || !metadata->is_null()) {
				anyMetadata = true;
			}
		}
		return entries > 0 && (!anyMetadata || entries == 1);
	}

	[[nodiscard]] inline std::vector<nlohmann::json> CandidateMetadata(const nlohmann::json& a_stacks, const std::string& a_itemKey)
	{
		std::vector<nlohmann::json> candidates;
		for (const auto& stack : a_stacks) {
			const auto key = stack.find("itemKey");
			const auto metadata = stack.find("metadata");
			if (key == stack.end() || *key != a_itemKey || metadata == stack.end()) {
				continue;
			}
			if (std::find(candidates.begin(), candidates.end(), *metadata) == candidates.end()) {
				candidates.push_back(*metadata);
			}
		}
		return candidates;
	}

	[[nodiscard]] inline std::string OperationIdFor(std::uint64_t a_sequence)
	{
		return "dbv" + std::to_string(a_sequence);
	}

	class InFlight
	{
	public:
		[[nodiscard]] bool TryBegin(const std::string& a_deathId, const std::string& a_itemKey, double a_nowSeconds)
		{
			Expire(a_nowSeconds);
			const auto key = std::make_pair(a_deathId, a_itemKey);
			if (entries_.contains(key) || entries_.size() >= kMaximumInFlight) {
				return false;
			}
			entries_.emplace(key, a_nowSeconds + kInFlightSeconds);
			return true;
		}

		void Expire(double a_nowSeconds)
		{
			for (auto it = entries_.begin(); it != entries_.end();) {
				it = it->second <= a_nowSeconds ? entries_.erase(it) : std::next(it);
			}
		}

		void Clear()
		{
			entries_.clear();
		}

	private:
		std::map<std::pair<std::string, std::string>, double> entries_;
	};
}
