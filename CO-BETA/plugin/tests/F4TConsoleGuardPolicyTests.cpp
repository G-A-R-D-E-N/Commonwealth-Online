#include "F4TConsoleGuardPolicy.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace
{
	using F4T::ConsoleGuard::ClassifyCommandSource;
	using F4T::ConsoleGuard::CommandSource;
	using F4T::ConsoleGuard::Decision;
	using F4T::ConsoleGuard::DecideConsoleUse;
	using F4T::ConsoleGuard::kMaxPendingAllowedCommands;
	using F4T::ConsoleGuard::TryAddToken;
	using F4T::ConsoleGuard::ClaimLogOnce;
	using F4T::ConsoleGuard::TryTakeToken;
	using F4T::ConsoleGuard::HookRetryDelayTicks;
	using F4T::ConsoleGuard::InsideRange;
	using F4T::ConsoleGuard::kMaxHookInstallAttempts;
	using F4T::ConsoleGuard::ReadConsoleAllowed;

	using Json = nlohmann::json;

	void CheckDecisionMatrix()
	{
		constexpr CommandSource sources[]{
			CommandSource::kInternal,
			CommandSource::kPluginCode,
			CommandSource::kGameCode,
			CommandSource::kOtherModule
		};
		for (const auto source : sources) {
			assert(DecideConsoleUse(false, false, source) == Decision::kAllow);
			assert(DecideConsoleUse(false, true, source) == Decision::kAllow);
			assert(DecideConsoleUse(true, true, source) == Decision::kAllow);
		}

		assert(DecideConsoleUse(true, false, CommandSource::kInternal) == Decision::kAllow);
		assert(DecideConsoleUse(true, false, CommandSource::kPluginCode) == Decision::kAllow);
		assert(DecideConsoleUse(true, false, CommandSource::kGameCode) == Decision::kBlock);
		assert(DecideConsoleUse(true, false, CommandSource::kOtherModule) == Decision::kBlock);
	}

	void CheckQueuedTokens()
	{
		std::atomic<std::uint32_t> tokens{};
		assert(!TryTakeToken(tokens));
		assert(TryAddToken(tokens));
		assert(TryTakeToken(tokens));
		assert(!TryTakeToken(tokens));

		for (std::uint32_t i = 0; i < kMaxPendingAllowedCommands; ++i) {
			assert(TryAddToken(tokens));
		}
		assert(!TryAddToken(tokens));
		assert(tokens.load() == kMaxPendingAllowedCommands);
	}

	void CheckSourceClassification()
	{
		assert(!InsideRange(0x1000, 0, 0));
		assert(!InsideRange(0x1000, 0, 0x100));
		assert(InsideRange(0x1000, 0x1000, 0x100));
		assert(InsideRange(0x10FF, 0x1000, 0x100));
		assert(!InsideRange(0x1100, 0x1000, 0x100));
		assert(!InsideRange(0x0FFF, 0x1000, 0x100));

		assert(ClassifyCommandSource(true, false, false) == CommandSource::kInternal);
		assert(ClassifyCommandSource(true, true, true) == CommandSource::kInternal);
		assert(ClassifyCommandSource(false, true, false) == CommandSource::kPluginCode);
		assert(ClassifyCommandSource(false, true, true) == CommandSource::kPluginCode);
		assert(ClassifyCommandSource(false, false, true) == CommandSource::kGameCode);
		assert(ClassifyCommandSource(false, false, false) == CommandSource::kOtherModule);

		const auto unresolved = ClassifyCommandSource(false, InsideRange(0x2000, 0, 0), InsideRange(0x2000, 0, 0));
		assert(unresolved == CommandSource::kOtherModule);
		assert(DecideConsoleUse(true, false, unresolved) == Decision::kBlock);
		assert(DecideConsoleUse(true, false, ClassifyCommandSource(true, false, false)) == Decision::kAllow);
	}

	void CheckInstallBackoff()
	{
		assert(HookRetryDelayTicks(0) == 1);
		assert(HookRetryDelayTicks(1) == 2);
		assert(HookRetryDelayTicks(2) == 4);
		assert(HookRetryDelayTicks(3) == 8);
		assert(HookRetryDelayTicks(4) == 16);
		assert(HookRetryDelayTicks(5) == 32);
		assert(HookRetryDelayTicks(6) == 64);
		assert(HookRetryDelayTicks(7) == 64);
		assert(HookRetryDelayTicks(kMaxHookInstallAttempts) == 0);
		assert(HookRetryDelayTicks(kMaxHookInstallAttempts + 100) == 0);
		assert(kMaxHookInstallAttempts > 0);
	}

	void CheckClaimLogOnce()
	{
		std::atomic<bool> flag{ false };
		assert(ClaimLogOnce(flag));
		assert(flag.load());
		assert(!ClaimLogOnce(flag));
		flag.store(false);
		assert(ClaimLogOnce(flag));
		assert(!ClaimLogOnce(flag));
		std::printf("missing trampoline warns once: first claim logs, later claims stay quiet\n");
	}

	void CheckConstants()
	{
		assert(F4T::ConsoleGuard::kConsoleMenuName == std::string_view{ "Console" });
		assert(F4T::ConsoleGuard::kConsoleBlockedNotice == std::string_view{ "Console is disabled in multiplayer" });
		assert(F4T::ConsoleGuard::kConsoleLockNotice == std::string_view{ "Commonwealth Online could not lock the console" });
	}

	void CheckConsoleAllowedParsing()
	{
		assert(ReadConsoleAllowed(Json{ { "consoleAllowed", true } }));
		assert(!ReadConsoleAllowed(Json{ { "consoleAllowed", false } }));
		assert(!ReadConsoleAllowed(Json{ { "playerId", 7 } }));
		assert(!ReadConsoleAllowed(Json{ { "consoleAllowed", "true" } }));
		assert(!ReadConsoleAllowed(Json{ { "consoleAllowed", nullptr } }));
		assert(!ReadConsoleAllowed(Json{ { "consoleAllowed", 1 } }));
		assert(!ReadConsoleAllowed(Json::object()));
		assert(!ReadConsoleAllowed(Json{ { "consoleAllowed", Json::array({ true }) } }));
		assert(ReadConsoleAllowed(Json{ { "playerId", 7 }, { "consoleAllowed", true } }));
	}
}

int main()
{
	CheckDecisionMatrix();
	CheckQueuedTokens();
	CheckSourceClassification();
	CheckInstallBackoff();
	CheckClaimLogOnce();
	CheckConstants();
	CheckConsoleAllowedParsing();
	return 0;
}
