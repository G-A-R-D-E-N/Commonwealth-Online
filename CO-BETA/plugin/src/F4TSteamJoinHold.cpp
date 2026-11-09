#include "pch.h"

#include "F4TSteamJoinHold.h"

#include "F4TSteamInvite.h"
#include "F4TSteamTunnelService.h"

#include <mutex>
#include <string>

namespace F4T::Steam
{
	namespace
	{
		std::mutex g_mutex;
		bool g_held{ false };
		bool g_pending{ false };
		std::string g_name;
		std::uint64_t g_code{ 0 };
	}

	void SetJoinHeld(bool a_held)
	{
		std::string name;
		std::uint64_t code = 0;
		{
			std::lock_guard lock{ g_mutex };
			if (g_held == a_held) return;
			g_held = a_held;
			if (a_held || !g_pending) return;
			name = g_name;
			code = g_code;
			g_pending = false;
		}
		if (!HostRunning()) return;
		AnnounceHosting(name);
		if (code != 0) RememberInviteCode(code);
	}

	void HoldJoinForNewHost()
	{
		std::lock_guard lock{ g_mutex };
		g_held = true;
		g_pending = false;
		g_name.clear();
		g_code = 0;
	}

	void AnnounceHostWhenOpen(std::string_view a_name, std::uint64_t a_friendCode)
	{
		{
			std::lock_guard lock{ g_mutex };
			if (g_held) {
				g_pending = true;
				g_name = std::string{ a_name };
				if (a_friendCode != 0) g_code = a_friendCode;
				return;
			}
		}
		AnnounceHosting(a_name);
		if (a_friendCode != 0) RememberInviteCode(a_friendCode);
	}
}
