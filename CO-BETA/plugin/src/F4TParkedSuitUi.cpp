#include "pch.h"

#include "F4TParkedSuitUi.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TParkedSuitSync.h"
#include <format>

#include <cstddef>
#include <cstring>
#include <string>

namespace
{
	COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI30* g_uiParkedSuit = nullptr;

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

	bool Callback_GetParkedSuitStateJson(
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
			const auto state = F4T::ParkedSuitSync::BuildUiStateJson();
			if (state.size() + 1 > a_jsonOutSize) {
				CopyError(a_errorBuf, a_errorBufSize, "Parked suit state JSON exceeds output buffer.");
				return false;
			}
			std::memcpy(a_jsonOut, state.c_str(), state.size() + 1);
			return true;
		} catch (...) {
			CopyError(a_errorBuf, a_errorBufSize, "Parked suit state JSON could not be built.");
			return false;
		}
	}

	void Callback_SendParkedSuitAction(const char* a_actionJson)
	{
		if (!a_actionJson) {
			return;
		}
		F4T::ParkedSuitSync::HandleUiAction(a_actionJson);
	}

	const COMMONWEALTH_ONLINE_UI_API::ParkedSuitCallbacks kParkedSuitCallbacks{
		Callback_GetParkedSuitStateJson,
		Callback_SendParkedSuitAction
	};
}

namespace F4T::ParkedSuitUi
{
	void Register()
	{
		if (!g_uiParkedSuit) {
			g_uiParkedSuit = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI30>();
		}
		if (g_uiParkedSuit && g_uiParkedSuit->SetParkedSuitCallbacks) {
			g_uiParkedSuit->SetParkedSuitCallbacks(&kParkedSuitCallbacks);
		}
	}

	void Open()
	{
		Register();
		if (g_uiParkedSuit && g_uiParkedSuit->OpenParkedSuitMenu) {
			g_uiParkedSuit->OpenParkedSuitMenu();
		}
	}

	void Close()
	{
		if (g_uiParkedSuit && g_uiParkedSuit->CloseParkedSuitMenu) {
			g_uiParkedSuit->CloseParkedSuitMenu();
		}
	}

	void NotifyStateChanged()
	{
		if (g_uiParkedSuit && g_uiParkedSuit->NotifyParkedSuitStateChanged) {
			g_uiParkedSuit->NotifyParkedSuitStateChanged();
		}
	}

	void NotifyRemovalFailed(std::uint32_t a_refFormId)
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
		if (!ui || !ui->ShowAlert) return;
		const auto id = std::format("parkedSuit:{:08X}:removeFailed", a_refFormId);
		const auto message = std::format("The server could not remove your parked power armor ({:08X}). Contact the server administrator.", a_refFormId);
		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = id.c_str();
		request.category = "Workshop";
		request.title = "Power armor";
		request.message = message.c_str();
		request.tone = "warning";
		request.durationMs = 8000;
		(void)ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0);
	}

	void NotifyHackAttempt(std::uint32_t a_refFormId, bool a_failed)
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI1>();
		if (!ui || !ui->ShowAlert) {
			return;
		}
		const auto alertId = a_failed
			? std::format("parkedSuit:{:08X}:failed", a_refFormId)
			: std::format("parkedSuit:{:08X}:broken", a_refFormId);
		const auto message = a_failed
			? std::format("Someone tried to break into your parked power armor and failed ({:08X}).", a_refFormId)
			: std::format("Someone broke into your parked power armor and took it ({:08X}).", a_refFormId);
		COMMONWEALTH_ONLINE_UI_API::AlertRequest request{};
		request.id = alertId.c_str();
		request.category = "Workshop";
		request.title = "Power armor";
		request.message = message.c_str();
		request.tone = "warning";
		request.durationMs = 8000;
		(void)ui->ShowAlert(&request, nullptr, nullptr, nullptr, 0);
	}
}
