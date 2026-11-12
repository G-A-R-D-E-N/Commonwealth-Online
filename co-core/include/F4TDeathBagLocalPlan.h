#pragma once
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
namespace F4T::DeathBagLocal
{
	inline constexpr std::uint32_t VanillaPipboyGlove = 0x00021B3B; // source:RE-Docs/ReverseEng__F4VR-CommonFramework.txt (PipboyArmor 0x21B3B); NG/AE TODO-verify
	inline constexpr std::uint32_t PipboyDustyBase = 448440;
	inline constexpr std::uint32_t PipboyVault81Base = 1809287;
	inline constexpr std::array<std::uint32_t, 3> PipboyArmorBases{ VanillaPipboyGlove, PipboyDustyBase, PipboyVault81Base };
	inline constexpr std::string_view PipboyArmorItemKey = "fallout4.esm:00021B3B";
	inline constexpr bool Enabled = true;
	inline constexpr std::uint32_t NibbleMask = 15;
	inline constexpr int NibbleBits = 4;
	inline constexpr int NibbleTopShift = 28;
	[[nodiscard]] inline std::string PipboyArmorKey(std::uint32_t a_base)
	{
		constexpr std::string_view digits = "0123456789ABCDEF";
		std::string key = "fallout4.esm:";
		for (int shift = NibbleTopShift; shift >= 0; shift -= NibbleBits) key.push_back(digits[(a_base >> shift) & NibbleMask]);
		return key;
	}
	[[nodiscard]] inline bool IsPipboyArmor(std::uint32_t a_base)
	{
		for (const auto base : PipboyArmorBases) if (base == a_base) return true;
		return false;
	}
	struct Stack { std::uint32_t base; std::uint32_t index; std::uint32_t count; bool questObject = false; };
	[[nodiscard]] inline bool IsRetained(const Stack& a_stack)
	{
		return IsPipboyArmor(a_stack.base) || a_stack.questObject;
	}
	[[nodiscard]] inline std::optional<std::vector<Stack>> Plan(const std::vector<Stack>& a_stacks)
	{
		if (a_stacks.size() > 4096) return std::nullopt;
		std::vector<Stack> result;
		for (const auto& stack : a_stacks) {
			if (!stack.base || !stack.count || stack.count > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) return std::nullopt;
			if (!IsRetained(stack)) result.push_back(stack);
		}
		return result;
	}
	using Counts = std::map<std::uint32_t, std::uint64_t>;
	[[nodiscard]] inline bool MatchesSource(const std::vector<Stack>& a_stacks, const Counts& a_counts)
	{
		Counts totals;
		std::set<std::pair<std::uint32_t, std::uint32_t>> seen;
		for (const auto& stack : a_stacks) {
			if (!stack.base || !stack.count || !seen.emplace(stack.base, stack.index).second) return false;
			auto& total = totals[stack.base];
			if (total > (std::numeric_limits<std::uint64_t>::max)() - stack.count) return false;
			total += stack.count;
		}
		return totals == a_counts;
	}
	[[nodiscard]] inline bool CountsMatch(const Counts& a_before, const Counts& a_after, const Counts& a_bag, const std::vector<Stack>& a_plan)
	{
		Counts expected;
		for (const auto& entry : a_plan) {
			auto& count = expected[entry.base];
			if (count > (std::numeric_limits<std::uint64_t>::max)() - entry.count) return false;
			count += entry.count;
		}
		if (a_bag != expected) return false;
		for (const auto& [id, count] : expected) if (!a_before.contains(id) || a_before.at(id) < count) return false;
		for (const auto& [id, before] : a_before) {
			const auto removed = expected.contains(id) ? expected.at(id) : 0;
			const auto after = a_after.contains(id) ? a_after.at(id) : 0;
			if (removed > before || after != before - removed) return false;
		}
		for (const auto& [id, after] : a_after) if (!a_before.contains(id) && after) return false;
		return true;
	}

}
