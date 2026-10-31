#include "pch.h"

#include "F4TUIApiGuard.h"

#include "CommonwealthOnlineUI_API.h"

#include "RE/S/SendHUDMessage.h"

#include <atomic>
#include <cstdint>
#include <string_view>

namespace F4T::UIApiGuard
{
	namespace
	{
		constexpr std::string_view kMismatchNotice =
			"Commonwealth Online: CommonwealthOnline.dll and CommonwealthOnlineUI.dll are from different builds. "
			"Some multiplayer UI is turned off. Reinstall both from the same release.";

		std::atomic_uint32_t g_refusedVersions{ 0 };
		std::atomic_bool     g_noticePending{ false };

		void OnInterfaceRefused(COMMONWEALTH_ONLINE_UI_API::InterfaceVersion a_interfaceVersion, std::uint32_t a_layout)
		{
			const auto version = static_cast<unsigned>(a_interfaceVersion);
			const auto bit = version < 32 ? (1u << version) : 0u;
			if (bit && (g_refusedVersions.fetch_or(bit, std::memory_order_acq_rel) & bit)) {
				return;
			}
			g_noticePending.store(true, std::memory_order_release);
			REX::CRITICAL(
				"[UIApiGuard] CommonwealthOnlineUI.dll refused or lacks interface V{} (CommonwealthOnline.dll layout {:#010x}). "
				"The two DLLs are from different builds; CommonwealthOnlineUI.log names the layout it has. "
				"A UI DLL older than the layout check has no RequestPluginAPIChecked export and refuses every interface.",
				version,
				a_layout);
		}
	}

	void Install()
	{
		COMMONWEALTH_ONLINE_UI_API::OnInterfaceRefused = OnInterfaceRefused;
	}

	void ShowPendingNotice()
	{
		if (g_noticePending.exchange(false, std::memory_order_acq_rel)) {
			RE::SendHUDMessage::ShowHUDMessage(kMismatchNotice.data(), nullptr, true, true);
		}
	}
}
