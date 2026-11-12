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
			"Commonwealth Online: the multiplayer UI interface was refused. "
			"Some multiplayer UI is turned off. Reinstall CommonwealthOnline.dll from one release.";

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
				"[UIApiGuard] CommonwealthOnline.dll refused or lacks interface V{} (layout {:#010x}). "
				"The UI code and the plugin code in this DLL disagree on the interface layout.",
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
