#pragma once

#include "F4TInlineHookGuard.h"
#include "F4TVtableGuard.h"

#include <safetyhook.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace F4T::InlineHook
{
	using LogFn = void (*)(std::string_view);
	using PublishFn = void (*)(void* a_original);

	struct Request
	{
		std::uintptr_t target{ 0 };
		VtableGuard::AddressRange codeSection{};
		std::span<const std::uint8_t> expectedPrologue{};
		void* destination{ nullptr };
		PublishFn publishOriginal{ nullptr };
	};

	struct Result
	{
		InlineHookGuard::InstallVerdict verdict{ InlineHookGuard::InstallVerdict::kTargetUnresolved };
		std::optional<safetyhook::InlineHook> hook{};
	};

	struct MidResult
	{
		InlineHookGuard::InstallVerdict verdict{ InlineHookGuard::InstallVerdict::kTargetUnresolved };
		std::optional<safetyhook::MidHook> hook{};
	};

	void Install(const Request& a_request, LogFn a_log, Result& a_result) noexcept;
	void InstallMid(const Request& a_request, safetyhook::MidHookFn a_destination, LogFn a_log, MidResult& a_result) noexcept;
}
