#pragma once

#include "PrismaUI_F4_API.h"

#include <cstdint>
#include <string_view>

namespace F4T::PrismaCapabilities
{
	enum class ApiVersion : std::uint8_t
	{
		None = 0,
		V6 = 6,
		V10 = 10,
		V11 = 11,
		V12 = 12,
	};

	[[nodiscard]] PRISMA_UI_API::IVPrismaUI6* Acquire();

	[[nodiscard]] ApiVersion GetVersion() noexcept;
	[[nodiscard]] const char* GetVersionName() noexcept;

	[[nodiscard]] PRISMA_UI_API::IVPrismaUI6* GetV6() noexcept;
	[[nodiscard]] PRISMA_UI_API::IVPrismaUI10* GetV10() noexcept;
	[[nodiscard]] PRISMA_UI_API::IVPrismaUI11* GetV11() noexcept;
	[[nodiscard]] PRISMA_UI_API::IVPrismaUI12* GetV12() noexcept;
	[[nodiscard]] bool HasV10() noexcept;
	[[nodiscard]] bool HasV11() noexcept;
	[[nodiscard]] bool HasV12() noexcept;
	[[nodiscard]] bool HasInputRegions() noexcept;

	bool TrySetViewRole(PrismaView a_view, PRISMA_UI_API::ViewRole a_role) noexcept;

	[[nodiscard]] PRISMA_UI_API::ViewRole TryGetViewRole(PrismaView a_view) noexcept;

	[[nodiscard]] PrismaView TryGetFocusedView() noexcept;

	[[nodiscard]] bool IsAnyExternalPanelVisible(PrismaView a_ignoreView) noexcept;

	[[nodiscard]] bool HasAnyActiveFocus() noexcept;

	[[nodiscard]] const char* RoleName(PRISMA_UI_API::ViewRole a_role) noexcept;
}
