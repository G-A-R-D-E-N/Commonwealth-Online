#include "pch.h"

#include "F4TNetStatsReporter.h"

#include "F4TGnsWinsockShim.h"
#include "F4TNetStatsPayload.h"
#include "F4TNetworking.h"
#include "F4TNetworkingSocketLifecycle.h"
#include "F4TSteamTunnelService.h"
#include "F4TUIPrefs.h"

#include <chrono>
#include <string>

namespace F4T::NetStatsReporter
{
	namespace
	{
		constexpr auto kPushInterval = std::chrono::seconds{ 1 };

		std::chrono::steady_clock::time_point g_nextPush{};
		bool g_pushedStats = false;

		void PushEmpty()
		{
			if (g_pushedStats) {
				F4T::UIPrefs::PushNetStats(std::string{});
				g_pushedStats = false;
			}
		}
	}

	void Tick()
	{
		const auto now = std::chrono::steady_clock::now();
		if (now < g_nextPush) {
			return;
		}
		g_nextPush = now + kPushInterval;

		const auto prefs = F4T::UIPrefs::Get();
		if (!F4T::Networking::IsConnectedToServer() || (!prefs.showPing && !prefs.networkStatsOverlay)) {
			PushEmpty();
			return;
		}
		const auto sample = F4T::GnsWinsockShim::ReadConnectionStats(F4T::Networking::Detail::GetSocket());
		if (!sample) {
			PushEmpty();
			return;
		}
		const auto steamTunneled = F4T::Steam::HostRunning() || F4T::Steam::GuestRunning();
		F4T::UIPrefs::PushNetStats(F4T::NetStatsPayload::BuildJson(*sample, steamTunneled));
		g_pushedStats = true;
	}
}
