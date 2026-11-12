#include "pch.h"

#include "F4TPrismaCapabilities.h"
#include "SessionCallbacks.h"

#include <format>
#include <string>

namespace F4T::PrismaCapabilities
{
	namespace
	{
		PRISMA_UI_API::IVPrismaUI6*  g_apiV6 = nullptr;
		PRISMA_UI_API::IVPrismaUI10* g_apiV10 = nullptr;
		PRISMA_UI_API::IVPrismaUI11* g_apiV11 = nullptr;
		PRISMA_UI_API::IVPrismaUI12* g_apiV12 = nullptr;
		ApiVersion                   g_version = ApiVersion::None;

		std::string GetLogPrefix()
		{
			return std::string{ COUI::Session::GetLogPrefix() } + " [PrismaUI]";
		}

		void LogInfo(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}

		void LogWarn(std::string_view a_message)
		{
			const auto message = GetLogPrefix() + " " + std::string(a_message);
			REX::WARN(std::string_view{ message });
		}
	}

	PRISMA_UI_API::IVPrismaUI6* Acquire()
	{
		if (g_apiV6) return g_apiV6;
		g_apiV6 = nullptr;
		g_apiV10 = nullptr;
		g_apiV11 = nullptr;
		g_apiV12 = nullptr;
		g_version = ApiVersion::None;

		if (auto* apiV12 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI12>()) {
			g_apiV12 = apiV12;
			g_apiV11 = apiV12;
			g_apiV10 = apiV12;
			g_apiV6 = apiV12;
			g_version = ApiVersion::V12;
			LogInfo("Acquired IVPrismaUI12");
			return g_apiV6;
		}

		if (auto* apiV11 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI11>()) {
			g_apiV11 = apiV11;
			g_apiV10 = apiV11;
			g_apiV6 = apiV11;
			g_version = ApiVersion::V11;
			LogInfo("Acquired IVPrismaUI11");
			return g_apiV6;
		}

		if (auto* apiV10 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI10>()) {
			g_apiV10 = apiV10;
			g_apiV6 = apiV10;
			g_version = ApiVersion::V10;
			LogInfo("Acquired IVPrismaUI10");
			return g_apiV6;
		}

		if (auto* apiV6 = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI6>()) {
			g_apiV6 = apiV6;
			g_version = ApiVersion::V6;
			LogInfo("Acquired IVPrismaUI6");
			return g_apiV6;
		}

		LogWarn(
			"PrismaUI_F4 V6+ API not found. Install/update the separate PrismaUI_F4 runtime. Vanilla menus remain usable.");
		return nullptr;
	}

	ApiVersion GetVersion() noexcept
	{
		return g_version;
	}

	const char* GetVersionName() noexcept
	{
		switch (g_version) {
		case ApiVersion::V12:
			return "V12";
		case ApiVersion::V11:
			return "V11";
		case ApiVersion::V10:
			return "V10";
		case ApiVersion::V6:
			return "V6";
		default:
			return "None";
		}
	}

	PRISMA_UI_API::IVPrismaUI6* GetV6() noexcept
	{
		return g_apiV6;
	}

	PRISMA_UI_API::IVPrismaUI10* GetV10() noexcept
	{
		return g_apiV10;
	}

	PRISMA_UI_API::IVPrismaUI11* GetV11() noexcept
	{
		return g_apiV11;
	}

	PRISMA_UI_API::IVPrismaUI12* GetV12() noexcept
	{
		return g_apiV12;
	}

	bool HasV10() noexcept
	{
		return g_apiV10 != nullptr;
	}

	bool HasV11() noexcept
	{
		return g_apiV11 != nullptr;
	}

	bool HasV12() noexcept
	{
		return g_apiV12 != nullptr;
	}

	bool HasInputRegions() noexcept
	{
		return g_apiV10 && PRISMA_UI_API::HasPrismaCapability(PRISMA_UI_API::PrismaCapability::InputRegions);
	}

	bool TrySetViewRole(PrismaView a_view, PRISMA_UI_API::ViewRole a_role) noexcept
	{
		if (!g_apiV10 || a_view == 0) {
			return false;
		}

		g_apiV10->SetViewRole(a_view, a_role);
		return g_apiV10->GetViewRole(a_view) == a_role;
	}

	PRISMA_UI_API::ViewRole TryGetViewRole(PrismaView a_view) noexcept
	{
		if (!g_apiV10 || a_view == 0) {
			return PRISMA_UI_API::ViewRole::kUnspecified;
		}
		return g_apiV10->GetViewRole(a_view);
	}

	PrismaView TryGetFocusedView() noexcept
	{
		if (!g_apiV10) {
			return 0;
		}
		return g_apiV10->GetFocusedView();
	}

	bool IsAnyExternalPanelVisible(PrismaView a_ignoreView) noexcept
	{
		if (!g_apiV10) {
			return false;
		}
		return g_apiV10->IsAnyPanelVisible(a_ignoreView);
	}

	bool HasAnyActiveFocus() noexcept
	{
		if (!g_apiV6) {
			return false;
		}
		return g_apiV6->HasAnyActiveFocus();
	}

	const char* RoleName(PRISMA_UI_API::ViewRole a_role) noexcept
	{
		switch (a_role) {
		case PRISMA_UI_API::ViewRole::kWidget:
			return "kWidget";
		case PRISMA_UI_API::ViewRole::kPanel:
			return "kPanel";
		case PRISMA_UI_API::ViewRole::kUnspecified:
		default:
			return "kUnspecified";
		}
	}
}
