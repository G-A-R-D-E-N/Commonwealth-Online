#include "F4TInlineHookGuard.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <span>

namespace
{
	using F4T::InlineHookGuard::InstallVerdict;
	using F4T::VtableGuard::AddressRange;

	constexpr AddressRange kText{ 0x1'4100'0000, 0x1'4200'0000 };
	constexpr std::uintptr_t kTarget = 0x1'4100'5000;
	constexpr std::uintptr_t kDestination = 0x7FF0'0000'1000;

	const std::array<std::uint8_t, 16> kExpected{
		0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48,
		0x89, 0x70, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20
	};
}

int main()
{
	using namespace F4T::InlineHookGuard;

	assert(CheckTarget(kTarget, kExpected.size(), kText) == InstallVerdict::kOk);
	assert(CheckTarget(0, kExpected.size(), kText) == InstallVerdict::kTargetUnresolved);
	assert(CheckTarget(0x1'4000'1000, kExpected.size(), kText) == InstallVerdict::kTargetNotExecutable);
	assert(CheckTarget(kText.end - 4, kExpected.size(), kText) == InstallVerdict::kTargetNotExecutable);
	assert(CheckTarget(kTarget, 0, kText) == InstallVerdict::kTargetNotExecutable);
	assert(CheckTarget(kTarget, kExpected.size(), AddressRange{}) == InstallVerdict::kTargetNotExecutable);

	assert(VerifyPrologue(kExpected, kExpected) == InstallVerdict::kOk);

	{
		auto observed = kExpected;
		observed[11] = 0x90;
		assert(VerifyPrologue(kExpected, observed) == InstallVerdict::kPrologueMismatch);
	}

	{
		auto observed = kExpected;
		observed[0] = 0xE9;
		assert(VerifyPrologue(kExpected, observed) == InstallVerdict::kPriorRel32Jump);
	}

	{
		auto observed = kExpected;
		observed[0] = 0xFF;
		observed[1] = 0x25;
		assert(VerifyPrologue(kExpected, observed) == InstallVerdict::kPriorIndirectJump);
	}

	{
		auto observed = kExpected;
		observed[0] = 0xCC;
		assert(VerifyPrologue(kExpected, observed) == InstallVerdict::kPriorInt3);
	}

	{
		const std::array<std::uint8_t, 13> shortExpected{};
		assert(VerifyPrologue(shortExpected, shortExpected) == InstallVerdict::kPrologueTooShort);
	}

	assert(VerifyPrologue(kExpected, std::span{ kExpected }.first(8)) == InstallVerdict::kPrologueMismatch);

	{
		const std::array<std::uint8_t, 5> rel32{ 0xE9, 0x00, 0x10, 0x00, 0x00 };
		assert(Rel32JumpTarget(rel32, kTarget) == kTarget + 5 + 0x1000);
		assert(!Rel32JumpTarget(kExpected, kTarget));
		assert(!Rel32JumpTarget(std::span<const std::uint8_t>{}, kTarget));
	}

	{
		std::array<std::uint8_t, 14> absolute{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
		const auto destination = static_cast<std::uint64_t>(kDestination);
		for (std::size_t i = 0; i < sizeof(destination); ++i) {
			absolute[6 + i] = static_cast<std::uint8_t>(destination >> (8 * i));
		}
		assert(AbsoluteJumpTo(absolute, kDestination));
		assert(!AbsoluteJumpTo(absolute, kDestination + 1));
		absolute[2] = 0x01;
		assert(!AbsoluteJumpTo(absolute, kDestination));
	}

	assert(!AbsoluteJumpTo(kExpected, kDestination));
	assert(!AbsoluteJumpTo(std::span<const std::uint8_t>{}, kDestination));

	assert(Describe(InstallVerdict::kPriorRel32Jump).find("E9") != std::string_view::npos);
	return 0;
}
