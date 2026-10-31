#include "F4TInlineHook.h"

#include <format>
#include <utility>

namespace F4T::InlineHook
{
	namespace
	{
		std::span<const std::uint8_t> BytesAt(std::uintptr_t a_address, std::size_t a_length) noexcept
		{
			return { reinterpret_cast<const std::uint8_t*>(a_address), a_length };
		}

		bool LandsOnDestination(std::uintptr_t a_target, std::size_t a_length, std::uintptr_t a_destination) noexcept
		{
			if (InlineHookGuard::AbsoluteJumpTo(BytesAt(a_target, a_length), a_destination)) {
				return true;
			}
			const auto stub = InlineHookGuard::Rel32JumpTarget(BytesAt(a_target, a_length), a_target);
			if (!stub) {
				return false;
			}
			return InlineHookGuard::AbsoluteJumpTo(BytesAt(*stub, InlineHookGuard::kMinPrologueBytes), a_destination);
		}
	}

	void Install(const Request& a_request, LogFn a_log, Result& a_result) noexcept
	{
		using Verdict = InlineHookGuard::InstallVerdict;

		a_result = Result{};
		try {
			const auto prologueLength = a_request.expectedPrologue.size();
			a_result.verdict = a_request.destination
				? InlineHookGuard::CheckTarget(a_request.target, prologueLength, a_request.codeSection)
				: Verdict::kDestinationUnresolved;
			if (a_result.verdict == Verdict::kOk) {
				a_result.verdict = InlineHookGuard::VerifyPrologue(
					a_request.expectedPrologue, BytesAt(a_request.target, prologueLength));
			}
			if (a_result.verdict == Verdict::kOk) {
				auto created = safetyhook::InlineHook::create(
					reinterpret_cast<void*>(a_request.target),
					a_request.destination,
					safetyhook::InlineHook::StartDisabled);
				if (created) {
					a_result.hook.emplace(std::move(*created));
					if (a_request.publishOriginal) {
						a_request.publishOriginal(a_result.hook->original<void*>());
					}
					if (a_result.hook->enable()) {
						const auto destination = reinterpret_cast<std::uintptr_t>(a_request.destination);
						a_result.verdict = LandsOnDestination(a_request.target, prologueLength, destination)
							? Verdict::kOk
							: Verdict::kInstallNotVerified;
					} else {
						a_result.verdict = Verdict::kCreateFailed;
					}
				} else {
					a_result.verdict = Verdict::kCreateFailed;
				}
			}
		} catch (...) {
			a_result.verdict = Verdict::kCreateFailed;
		}

		if (a_result.verdict != Verdict::kOk) {
			a_result.hook.reset();
			if (a_request.publishOriginal) {
				a_request.publishOriginal(nullptr);
			}
		}
		if (a_log) {
			try {
				a_log(std::format(
					"inline hook at {:#x}: {}", a_request.target, InlineHookGuard::Describe(a_result.verdict)));
			} catch (...) {
			}
		}
	}

	void InstallMid(const Request& a_request, safetyhook::MidHookFn a_destination, LogFn a_log, MidResult& a_result) noexcept
	{
		using Verdict = InlineHookGuard::InstallVerdict;

		a_result = MidResult{};
		try {
			const auto prologueLength = a_request.expectedPrologue.size();
			a_result.verdict = a_destination
				? InlineHookGuard::CheckTarget(a_request.target, prologueLength, a_request.codeSection)
				: Verdict::kDestinationUnresolved;
			if (a_result.verdict == Verdict::kOk) {
				a_result.verdict = InlineHookGuard::VerifyPrologue(
					a_request.expectedPrologue, BytesAt(a_request.target, prologueLength));
			}
			if (a_result.verdict == Verdict::kOk) {
				auto created = safetyhook::MidHook::create(
					reinterpret_cast<void*>(a_request.target),
					a_destination,
					safetyhook::MidHook::StartDisabled);
				if (created) {
					a_result.hook.emplace(std::move(*created));
					a_result.verdict = a_result.hook->enable() ? Verdict::kOk : Verdict::kCreateFailed;
				} else {
					a_result.verdict = Verdict::kCreateFailed;
				}
			}
		} catch (...) {
			a_result.verdict = Verdict::kCreateFailed;
		}

		if (a_result.verdict != Verdict::kOk) {
			a_result.hook.reset();
		}
		if (a_log) {
			try {
				a_log(std::format(
					"mid hook at {:#x}: {}", a_request.target, InlineHookGuard::Describe(a_result.verdict)));
			} catch (...) {
			}
		}
	}
}
