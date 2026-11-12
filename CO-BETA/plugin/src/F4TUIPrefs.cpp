#include "pch.h"

#include "F4TUIPrefs.h"

#include "F4TNetworkingInternal.h"
#include "F4TUIClient.h"

#include <atomic>
#include <chrono>
#include <mutex>

namespace F4T::UIPrefs
{
	namespace
	{
		std::mutex g_mutex;
		MultiplayerPreferences g_cached{};
		bool g_hasCached = false;
		std::uint64_t g_cachedJoin = 0;
		std::int64_t g_cachedAtMs = 0;
		MultiplayerPreferences g_consumed{};
		bool g_hasConsumed = false;
		bool g_hasSent = false;
		std::uint64_t g_sentGeneration = 0;
		nlohmann::json g_sentPrivacy;

		std::int64_t NowMs()
		{
			return std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now().time_since_epoch())
				.count();
		}

		COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI26* Api()
		{
			static std::atomic<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI26*> cached{ nullptr };
			auto* api = cached.load(std::memory_order_acquire);
			if (!api) {
				api = COMMONWEALTH_ONLINE_UI_API::RequestPluginAPI<COMMONWEALTH_ONLINE_UI_API::IVCommonwealthOnlineUI26>();
				cached.store(api, std::memory_order_release);
			}
			return api;
		}

		MultiplayerPreferences Fetch()
		{
			auto prefs = DefaultPreferences();
			MultiplayerPreferences fetched{};
			const auto* api = Api();
			if (api && api->GetMultiplayerPreferences && api->GetMultiplayerPreferences(&fetched)) {
				prefs = fetched;
			}
			return prefs;
		}
	}

	MultiplayerPreferences Get()
	{
		std::scoped_lock lock(g_mutex);
		const auto join = F4T::UIClient::JoinGeneration();
		const auto now = NowMs();
		if (PreferencesStale(g_hasCached, g_cachedJoin, join, g_cachedAtMs, now)) {
			g_cached = Fetch();
			g_hasCached = true;
			g_cachedJoin = join;
			g_cachedAtMs = now;
		}
		return g_cached;
	}

	void PushNetStats(const std::string& a_json)
	{
		auto* api = Api();
		if (api && api->PushNetStats) {
			api->PushNetStats(a_json.c_str());
		}
	}

	bool ConsumeChanged(MultiplayerPreferences& a_out)
	{
		a_out = Get();
		std::scoped_lock lock(g_mutex);
		const bool changed = !g_hasConsumed || ValuesOf(g_consumed) != ValuesOf(a_out);
		g_consumed = a_out;
		g_hasConsumed = true;
		return changed;
	}

	// Constraint: game-thread only; called from the replication tick while connected.
	void SendPrivacyIfChanged(std::uint64_t a_generation)
	{
		MultiplayerPreferences current{};
		const bool changed = ConsumeChanged(current);
		const auto packet = PrivacySetPacket(current);
		if (g_hasSent && a_generation == g_sentGeneration && (!changed || packet["privacy"] == g_sentPrivacy)) {
			return;
		}
		g_hasSent = true;
		g_sentGeneration = a_generation;
		g_sentPrivacy = packet["privacy"];
		if (!F4T::Networking::Detail::SendPacketBytes(packet.dump())) {
			g_hasSent = false;
		}
	}
}
