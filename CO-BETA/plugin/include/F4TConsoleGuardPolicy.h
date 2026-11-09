#pragma once

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <string_view>

namespace F4T::ConsoleGuard
{
	inline constexpr std::string_view kConsoleMenuName{ "Console" };
	inline constexpr std::string_view kConsoleBlockedNotice{ "Console is disabled in multiplayer" };
	inline constexpr std::string_view kConsoleLockNotice{ "Commonwealth Online could not lock the console" };
	inline constexpr std::uint32_t kMaxHookInstallAttempts{ 8 };

	enum class Decision
	{
		kAllow,
		kBlock
	};

	enum class CommandSource
	{
		kInternal,
		kPluginCode,
		kGameCode,
		kOtherModule
	};

	[[nodiscard]] constexpr Decision DecideConsoleUse(
		bool a_sessionActive,
		bool a_admin,
		CommandSource a_source) noexcept
	{
		if (!a_sessionActive || a_admin ||
			a_source == CommandSource::kInternal || a_source == CommandSource::kPluginCode) {
			return Decision::kAllow;
		}
		return Decision::kBlock;
	}

	inline constexpr std::uint32_t kMaxPendingAllowedCommands{ 64 };

	inline bool TryAddToken(std::atomic<std::uint32_t>& a_tokens) noexcept
	{
		auto pending = a_tokens.load(std::memory_order_acquire);
		while (pending < kMaxPendingAllowedCommands) {
			if (a_tokens.compare_exchange_weak(pending, pending + 1, std::memory_order_acq_rel)) {
				return true;
			}
		}
		return false;
	}

	inline bool TryTakeToken(std::atomic<std::uint32_t>& a_tokens) noexcept
	{
		auto pending = a_tokens.load(std::memory_order_acquire);
		while (pending > 0) {
			if (a_tokens.compare_exchange_weak(pending, pending - 1, std::memory_order_acq_rel)) {
				return true;
			}
		}
		return false;
	}

	inline bool ClaimLogOnce(std::atomic<bool>& a_flag) noexcept
	{
		return !a_flag.exchange(true, std::memory_order_acq_rel);
	}

	[[nodiscard]] constexpr CommandSource ClassifyCommandSource(
		bool a_internalCall,
		bool a_insidePluginImage,
		bool a_insideGameImage) noexcept
	{
		if (a_internalCall) {
			return CommandSource::kInternal;
		}
		if (a_insidePluginImage) {
			return CommandSource::kPluginCode;
		}
		return a_insideGameImage ? CommandSource::kGameCode : CommandSource::kOtherModule;
	}

	[[nodiscard]] constexpr bool InsideRange(
		std::uintptr_t a_address,
		std::uintptr_t a_begin,
		std::uintptr_t a_size) noexcept
	{
		return a_size != 0 && a_begin != 0 && a_address >= a_begin && a_address - a_begin < a_size;
	}

	[[nodiscard]] constexpr std::uint64_t HookRetryDelayTicks(std::uint32_t a_completedAttempts) noexcept
	{
		if (a_completedAttempts >= kMaxHookInstallAttempts) {
			return 0;
		}
		const auto shift = a_completedAttempts < 6 ? a_completedAttempts : 6;
		return std::uint64_t{ 1 } << shift;
	}

	[[nodiscard]] inline bool ReadConsoleAllowed(const nlohmann::json& a_packet)
	{
		const auto found = a_packet.find("consoleAllowed");
		return found != a_packet.end() && found->is_boolean() && found->get<bool>();
	}
}
