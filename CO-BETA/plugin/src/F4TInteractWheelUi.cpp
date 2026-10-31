#include "pch.h"

#include "F4TInteractWheelUi.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TInteractWheel.h"
#include "F4TUIClient.h"

#include <cstddef>
#include <cstring>
#include <string>

namespace
{
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16* g_uiWheel = nullptr;

	void CopyError(char* a_errorBuf, std::size_t a_errorBufSize, const char* a_error)
	{
		if (!a_errorBuf || a_errorBufSize == 0) {
			return;
		}
		const auto length = std::strlen(a_error);
		const auto copied = length < a_errorBufSize - 1 ? length : a_errorBufSize - 1;
		std::memcpy(a_errorBuf, a_error, copied);
		a_errorBuf[copied] = '\0';
	}

	bool Callback_GetInteractWheelStateJson(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize)
	{
		if (!a_jsonOut || a_jsonOutSize == 0) {
			CopyError(a_errorBuf, a_errorBufSize, "JSON output buffer is missing.");
			return false;
		}
		try {
			const auto state = F4T::InteractWheel::BuildStateJson();
			if (state.size() + 1 > a_jsonOutSize) {
				CopyError(a_errorBuf, a_errorBufSize, "Interact wheel state JSON exceeds output buffer.");
				return false;
			}
			std::memcpy(a_jsonOut, state.c_str(), state.size() + 1);
			return true;
		} catch (...) {
			CopyError(a_errorBuf, a_errorBufSize, "Interact wheel state JSON could not be built.");
			return false;
		}
	}

	void Callback_SendInteractWheelAction(const char* a_actionJson)
	{
		if (!a_actionJson) {
			return;
		}
		F4T::InteractWheel::HandleAction(a_actionJson);
	}

	const COMMONWEALTH_ONLINE_UI_API::InteractWheelCallbacks kInteractWheelCallbacks{
		Callback_GetInteractWheelStateJson,
		Callback_SendInteractWheelAction
	};
}

namespace F4T::InteractWheelUi
{
	void Register()
	{
		if (!g_uiWheel) {
			g_uiWheel = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI16>();
		}
		if (g_uiWheel && g_uiWheel->SetInteractWheelCallbacks) {
			g_uiWheel->SetInteractWheelCallbacks(&kInteractWheelCallbacks);
		}
	}

	void Open()
	{
		Register();
		if (g_uiWheel && g_uiWheel->OpenInteractWheel) {
			g_uiWheel->OpenInteractWheel();
		}
	}

	void Close()
	{
		if (g_uiWheel && g_uiWheel->CloseInteractWheel) {
			g_uiWheel->CloseInteractWheel();
		}
	}

	void NotifyStateChanged()
	{
		if (g_uiWheel && g_uiWheel->NotifyInteractWheelStateChanged) {
			g_uiWheel->NotifyInteractWheelStateChanged();
		}
	}

	bool IsVisible()
	{
		return g_uiWheel && g_uiWheel->IsInteractWheelVisible && g_uiWheel->IsInteractWheelVisible();
	}
}

namespace F4T::UIClient
{
	void OpenInteractWheel()
	{
		F4T::InteractWheelUi::Open();
	}

	void CloseInteractWheel()
	{
		F4T::InteractWheelUi::Close();
	}

	void PushInteractWheelState(const char* a_state_json)
	{
		(void)a_state_json;
		F4T::InteractWheelUi::NotifyStateChanged();
	}

	bool IsInteractWheelVisible()
	{
		return F4T::InteractWheelUi::IsVisible();
	}
}
