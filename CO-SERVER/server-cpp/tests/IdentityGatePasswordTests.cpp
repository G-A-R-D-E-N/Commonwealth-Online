#include "IdentityGate.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using F4T::Identity::TokenHash;

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "password-gate-token";
		}

		TokenHash HashToken(std::string_view) override
		{
			return TokenHash{};
		}
	};

	SteamIdentityHooks Hooks(const double* a_clock, double a_cooldownSeconds)
	{
		return SteamIdentityHooks{
			.passwordLockout = F4T::SteamAdmission::LockoutOptions{ .threshold = 3, .cooldownSeconds = a_cooldownSeconds, .maximumTracked = 4 },
			.monotonicNow = [a_clock] { return *a_clock; }
		};
	}

	void FailTimes(IdentityGate& a_gate, std::uint64_t a_source, std::uint32_t a_times)
	{
		for (std::uint32_t index = 0; index < a_times; ++index) {
			a_gate.RecordManifestPasswordOutcome(1, a_source, "password_invalid");
		}
	}

	void TestLockoutAfterThresholdAndCooldown()
	{
		FakeTokenService tokens;
		F4T::Identity::Requirements requirements;
		double clock = 1000.0;
		IdentityGate gate(requirements, tokens, nullptr, {}, nullptr, {}, Hooks(&clock, 60.0));
		constexpr std::uint64_t source = 0x0A000001ULL;

		assert(!gate.CheckManifestPassword(1, source).has_value());
		FailTimes(gate, source, 2);
		assert(!gate.CheckManifestPassword(1, source).has_value());
		FailTimes(gate, source, 1);

		const auto locked = gate.CheckManifestPassword(2, source);
		assert(locked.has_value());
		assert(!locked->forward);
		assert(locked->code == "password_locked");
		assert(locked->reason == "Too many wrong join passwords. Try again in 1 minute.");

		clock += 59.0;
		assert(gate.CheckManifestPassword(2, source).has_value());
		clock += 1.0;
		assert(!gate.CheckManifestPassword(2, source).has_value());
	}

	void TestLockoutMessagePluralizesMinutes()
	{
		FakeTokenService tokens;
		F4T::Identity::Requirements requirements;
		double clock = 1000.0;
		IdentityGate gate(requirements, tokens, nullptr, {}, nullptr, {}, Hooks(&clock, 120.0));
		constexpr std::uint64_t source = 0x0A000002ULL;

		FailTimes(gate, source, 3);
		const auto locked = gate.CheckManifestPassword(1, source);
		assert(locked.has_value());
		assert(locked->reason == "Too many wrong join passwords. Try again in 2 minutes.");
	}

	void TestSuccessClearsFailureCount()
	{
		FakeTokenService tokens;
		F4T::Identity::Requirements requirements;
		double clock = 1000.0;
		IdentityGate gate(requirements, tokens, nullptr, {}, nullptr, {}, Hooks(&clock, 60.0));
		constexpr std::uint64_t source = 0x0A000003ULL;

		FailTimes(gate, source, 2);
		gate.RecordManifestPasswordOutcome(1, source, "");
		FailTimes(gate, source, 2);
		assert(!gate.CheckManifestPassword(1, source).has_value());

		FailTimes(gate, source, 1);
		const auto locked = gate.CheckManifestPassword(1, source);
		assert(locked.has_value() && locked->code == "password_locked");
	}

	void TestOtherCodesAndZeroSourceAreNotCounted()
	{
		FakeTokenService tokens;
		F4T::Identity::Requirements requirements;
		double clock = 1000.0;
		IdentityGate gate(requirements, tokens, nullptr, {}, nullptr, {}, Hooks(&clock, 60.0));
		constexpr std::uint64_t source = 0x0A000004ULL;

		for (std::uint32_t index = 0; index < 5; ++index) {
			gate.RecordManifestPasswordOutcome(1, source, "password_locked");
			gate.RecordManifestPasswordOutcome(1, 0, "password_invalid");
		}
		assert(!gate.CheckManifestPassword(1, source).has_value());
		assert(!gate.CheckManifestPassword(1, 0).has_value());
	}

	void TestLockoutIsPerSource()
	{
		FakeTokenService tokens;
		F4T::Identity::Requirements requirements;
		double clock = 1000.0;
		IdentityGate gate(requirements, tokens, nullptr, {}, nullptr, {}, Hooks(&clock, 60.0));
		constexpr std::uint64_t locked = 0x0A000005ULL;
		constexpr std::uint64_t other = 0x0A000006ULL;

		FailTimes(gate, locked, 3);
		assert(gate.CheckManifestPassword(1, locked).has_value());
		assert(!gate.CheckManifestPassword(1, other).has_value());
	}
}

int main()
{
	TestLockoutAfterThresholdAndCooldown();
	TestLockoutMessagePluralizesMinutes();
	TestSuccessClearsFailureCount();
	TestOtherCodesAndZeroSourceAreNotCounted();
	TestLockoutIsPerSource();
	return 0;
}
