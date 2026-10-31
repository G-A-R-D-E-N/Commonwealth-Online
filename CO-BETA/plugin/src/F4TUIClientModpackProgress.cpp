#include "pch.h"

#include "F4TUIClientModpackProgress.h"

#include "CommonwealthOnlineUI_API.h"
#include "F4TClientModpackProbe.h"

#include <atomic>
#include <format>
#include <string>

namespace
{
	std::atomic<bool> g_installed{ false };

	void ForwardProgress(const F4T::ClientModpackProbe::ProbeProgress& a_progress)
	{
		auto* ui = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI13>();
		if (ui == nullptr || ui->PushJoinProgress == nullptr) {
			return;
		}
		const auto message = a_progress.totalEntries == 0 ?
			std::string{ "Checking your modpack..." } :
			std::format(
				"Checking your modpack... {}/{} files ({} MiB hashed)",
				a_progress.checkedEntries,
				a_progress.totalEntries,
				a_progress.hashedBytes / (1024ull * 1024ull));
		ui->PushJoinProgress(message.c_str());
	}
}

namespace F4T::UIClientModpackProgress
{
	void Install()
	{
		if (g_installed.exchange(true, std::memory_order_acq_rel)) {
			return;
		}
		ClientModpackProbe::SetProbeProgressCallback(&ForwardProgress);
	}
}
