#pragma once

#include "F4TVtableGuard.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace F4T::InlineHookGuard
{
	enum class InstallVerdict
	{
		kOk,
		kTargetUnresolved,
		kDestinationUnresolved,
		kTargetNotExecutable,
		kPrologueTooShort,
		kPrologueMismatch,
		kPriorRel32Jump,
		kPriorIndirectJump,
		kPriorInt3,
		kCreateFailed,
		kInstallNotVerified,
	};

	inline constexpr std::size_t kMinPrologueBytes = 14;

	[[nodiscard]] InstallVerdict CheckTarget(
		std::uintptr_t a_target,
		std::size_t a_length,
		const VtableGuard::AddressRange& a_codeSection) noexcept;

	[[nodiscard]] InstallVerdict VerifyPrologue(
		std::span<const std::uint8_t> a_expected,
		std::span<const std::uint8_t> a_observed) noexcept;

	[[nodiscard]] std::optional<std::uintptr_t> Rel32JumpTarget(
		std::span<const std::uint8_t> a_observed,
		std::uintptr_t a_jumpAddress) noexcept;

	[[nodiscard]] bool AbsoluteJumpTo(
		std::span<const std::uint8_t> a_observed,
		std::uintptr_t a_destination) noexcept;

	[[nodiscard]] std::string_view Describe(InstallVerdict a_verdict) noexcept;
}
