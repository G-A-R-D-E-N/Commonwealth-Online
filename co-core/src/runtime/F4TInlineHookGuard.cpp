#include "F4TInlineHookGuard.h"

#include <algorithm>
#include <cstring>

namespace F4T::InlineHookGuard
{
	namespace
	{
		constexpr std::uint8_t kRel32JumpOpcode = 0xE9;
		constexpr std::uint8_t kIndirectJumpPrefix = 0xFF;
		constexpr std::uint8_t kIndirectJumpModrm = 0x25;
		constexpr std::uint8_t kInt3Opcode = 0xCC;
		constexpr std::size_t kRel32JumpBytes = 5;
		constexpr std::size_t kAbsoluteSlotOffset = 6;
	}

	InstallVerdict CheckTarget(
		std::uintptr_t a_target,
		std::size_t a_length,
		const VtableGuard::AddressRange& a_codeSection) noexcept
	{
		if (a_target == 0) {
			return InstallVerdict::kTargetUnresolved;
		}
		if (a_length == 0 ||
			!VtableGuard::Contains(a_codeSection, a_target) ||
			!VtableGuard::Contains(a_codeSection, a_target + a_length - 1)) {
			return InstallVerdict::kTargetNotExecutable;
		}
		return InstallVerdict::kOk;
	}

	InstallVerdict VerifyPrologue(
		std::span<const std::uint8_t> a_expected,
		std::span<const std::uint8_t> a_observed) noexcept
	{
		if (a_expected.size() < kMinPrologueBytes) {
			return InstallVerdict::kPrologueTooShort;
		}
		if (a_observed.size() < a_expected.size()) {
			return InstallVerdict::kPrologueMismatch;
		}

		const auto observed = a_observed.first(a_expected.size());
		if (std::ranges::equal(a_expected, observed)) {
			return InstallVerdict::kOk;
		}
		if (observed[0] == kRel32JumpOpcode) {
			return InstallVerdict::kPriorRel32Jump;
		}
		if (observed[0] == kIndirectJumpPrefix && observed[1] == kIndirectJumpModrm) {
			return InstallVerdict::kPriorIndirectJump;
		}
		if (observed[0] == kInt3Opcode) {
			return InstallVerdict::kPriorInt3;
		}
		return InstallVerdict::kPrologueMismatch;
	}

	std::optional<std::uintptr_t> Rel32JumpTarget(
		std::span<const std::uint8_t> a_observed,
		std::uintptr_t a_jumpAddress) noexcept
	{
		if (a_observed.size() < kRel32JumpBytes || a_observed[0] != kRel32JumpOpcode) {
			return std::nullopt;
		}
		std::int32_t displacement{ 0 };
		std::memcpy(&displacement, a_observed.data() + 1, sizeof(displacement));
		return a_jumpAddress + kRel32JumpBytes + static_cast<std::uintptr_t>(static_cast<std::intptr_t>(displacement));
	}

	bool AbsoluteJumpTo(std::span<const std::uint8_t> a_observed, std::uintptr_t a_destination) noexcept
	{
		if (a_observed.size() < kAbsoluteSlotOffset + sizeof(std::uint64_t) ||
			a_observed[0] != kIndirectJumpPrefix ||
			a_observed[1] != kIndirectJumpModrm ||
			!std::ranges::all_of(a_observed.subspan(2, 4), [](std::uint8_t a_byte) { return a_byte == 0; })) {
			return false;
		}
		std::uint64_t slot{ 0 };
		std::memcpy(&slot, a_observed.data() + kAbsoluteSlotOffset, sizeof(slot));
		return slot == static_cast<std::uint64_t>(a_destination);
	}

	std::string_view Describe(InstallVerdict a_verdict) noexcept
	{
		switch (a_verdict) {
		case InstallVerdict::kOk:
			return "inline hook installed and verified";
		case InstallVerdict::kTargetUnresolved:
			return "target address did not resolve";
		case InstallVerdict::kDestinationUnresolved:
			return "detour address is null";
		case InstallVerdict::kTargetNotExecutable:
			return "target is outside the module's executable code section";
		case InstallVerdict::kPrologueTooShort:
			return "expected prologue is shorter than the 14-byte absolute jump";
		case InstallVerdict::kPrologueMismatch:
			return "target bytes differ from the expected prologue; the function may have changed in this runtime";
		case InstallVerdict::kPriorRel32Jump:
			return "target already starts with a rel32 jmp (E9); another mod or patch has hooked this function";
		case InstallVerdict::kPriorIndirectJump:
			return "target already starts with an indirect jmp (FF 25); another mod or patch has hooked this function";
		case InstallVerdict::kPriorInt3:
			return "target starts with int3 (CC); a debugger or patch has altered this function";
		case InstallVerdict::kCreateFailed:
			return "SafetyHook could not create the inline hook";
		case InstallVerdict::kInstallNotVerified:
			return "target does not jump to the destination after install; hook was removed";
		default:
			return "unknown inline hook verdict";
		}
	}
}
