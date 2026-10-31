#pragma once

#include "PrismaUI_F4_API.h"

#include <cstddef>
#include <cstdint>

namespace F4T::Alerts
{
	struct AlertAction
	{
		const char* id{ nullptr };
		const char* label{ nullptr };
		const char* hotkey{ nullptr };
	};

	struct AlertRequest
	{
		const char*        id{ nullptr };
		const char*        category{ nullptr };
		const char*        title{ nullptr };
		const char*        message{ nullptr };
		const char*        tone{ nullptr };  // info | success | warning | error | invite
		std::uint32_t      durationMs{ 8000 };
		const AlertAction* actions{ nullptr };
		std::size_t        actionCount{ 0 };
	};

	using AlertResponseFn = void (*)(const char* a_alertId, const char* a_actionId, void* a_userData);

	void Initialize(PRISMA_UI_API::IVPrismaUI4* a_api);
	void Shutdown();

	void EnsureView();
	bool HasView();

	void AllowCreateRetry();

	bool Show(
		const AlertRequest& a_request,
		AlertResponseFn     a_onResponse = nullptr,
		void*               a_userData = nullptr,
		char*               a_idOut = nullptr,
		std::size_t         a_idOutSize = 0);

	void Dismiss(const char* a_alertId);
	void DismissAll();

	void SetMenuInteractionActive(bool a_active);

	bool IsPointerOverInteractive(std::int32_t a_x, std::int32_t a_y);

	PrismaView GetView();

	void FocusForPointer(const char* a_reason);
	void ClearPointerFocus(const char* a_reason);

	void DumpViewState();

	void PollHotkeys();

	void PollDemoHotkey();
}
