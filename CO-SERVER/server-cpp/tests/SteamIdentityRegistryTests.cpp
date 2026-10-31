#include "SteamIdentityRegistry.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <stdexcept>
#include <string>

namespace
{
	using CommonwealthOnline::Server::SteamIdentityFlagKind;
	using CommonwealthOnline::Server::SteamIdentityRecord;
	using CommonwealthOnline::Server::SteamIdentityRegistry;
	using CommonwealthOnline::Server::SteamIdSource;
	using Json = nlohmann::json;

	constexpr auto kProfile = "profile-alpha";
	constexpr auto kOtherProfile = "profile-bravo";
	constexpr auto kSteam = "76561198000000042";
	constexpr auto kOtherSteam = "76561198000000099";
	constexpr double kDay = 24.0 * 60.0 * 60.0;

	void TestObserveRecordsHistory()
	{
		SteamIdentityRegistry registry;
		auto seen = registry.Observe(kProfile, kSteam, SteamIdSource::SteamTunnel, "Nate", 100.0);
		assert(seen.stored && seen.newLink && seen.raised.empty());
		seen = registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, "Nora", 200.0);
		assert(seen.stored && !seen.newLink);
		(void)registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, "Nate", 300.0);
		(void)registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, "", 50.0);

		const auto records = registry.ForProfile(kProfile);
		assert(records.size() == 1 && registry.ForSteamId(kSteam).size() == 1);
		const auto& record = records.front();
		assert(record.firstSeen == 100.0 && record.lastSeen == 300.0);
		assert(record.source == SteamIdSource::WebApi);
		assert((record.nicknames == std::vector<std::string>{ "Nate", "Nora" }));

		for (int index = 0; index < 20; ++index) {
			(void)registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, "name-" + std::to_string(index), 400.0 + index);
		}
		const auto capped = registry.ForProfile(kProfile).front();
		assert(capped.nicknames.size() == SteamIdentityRegistry::kMaximumNicknames && capped.nicknames.front() == "name-19");
		const auto longName = std::string(100, 'x');
		(void)registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, longName, 500.0);
		assert(registry.ForProfile(kProfile).front().nicknames.front().size() == SteamIdentityRegistry::kMaximumNicknameLength);
	}

	void TestObserveRejectsUnverifiedOrMalformed()
	{
		SteamIdentityRegistry registry;
		assert(!registry.Observe(kProfile, kSteam, SteamIdSource::None, "n", 1.0).stored);
		assert(!registry.Observe(kProfile, "76561197960265728", SteamIdSource::WebApi, "n", 1.0).stored);
		assert(!registry.Observe(kProfile, "not-a-steam-id", SteamIdSource::WebApi, "n", 1.0).stored);
		assert(!registry.Observe("short", kSteam, SteamIdSource::WebApi, "n", 1.0).stored);
		assert(!registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, "n", -1.0).stored);
		assert(registry.RecordCount() == 0 && registry.Flags().empty());
	}

	void TestLinkingAnomaliesAreFlaggedNotBlocked()
	{
		SteamIdentityRegistry registry;
		(void)registry.Observe(kProfile, kSteam, SteamIdSource::SteamTunnel, "a", 1.0);

		auto shared = registry.Observe(kOtherProfile, kSteam, SteamIdSource::WebApi, "b", 2.0);
		assert(shared.stored && shared.raised.size() == 1);
		assert(shared.raised.front().kind == SteamIdentityFlagKind::SharedSteamId);
		assert(shared.raised.front().profileId == kOtherProfile && shared.raised.front().otherId == kProfile);

		auto multiple = registry.Observe(kProfile, kOtherSteam, SteamIdSource::WebApi, "a", 3.0);
		assert(multiple.stored && multiple.raised.size() == 1);
		assert(multiple.raised.front().kind == SteamIdentityFlagKind::MultipleSteamIds);
		assert(multiple.raised.front().steamId == kOtherSteam && multiple.raised.front().otherId == kSteam);

		assert(registry.Observe(kOtherProfile, kSteam, SteamIdSource::WebApi, "b", 4.0).raised.empty());
		assert(registry.Flags().size() == 2 && registry.RecordCount() == 3);

		assert(registry.ClearFlags(kProfile, kSteam) == 0);
		assert(registry.ClearFlags(kOtherProfile, kSteam) == 1);
		assert(registry.ClearFlags(kProfile, kOtherSteam) == 1);
		assert(registry.Flags().empty());
	}

	void TestSweepKeepsActiveFlaggedAndProtected()
	{
		SteamIdentityRegistry registry;
		const double now = 1000.0 * kDay;
		(void)registry.Observe(kProfile, kSteam, SteamIdSource::WebApi, "recent", now - 10.0 * kDay);
		(void)registry.Observe("profile-stale", "76561198000000111", SteamIdSource::WebApi, "stale", now - 400.0 * kDay);
		(void)registry.Observe("profile-banned", "76561198000000222", SteamIdSource::WebApi, "banned", now - 400.0 * kDay);
		(void)registry.Observe("profile-flag-a", "76561198000000333", SteamIdSource::WebApi, "x", now - 500.0 * kDay);
		(void)registry.Observe("profile-flag-b", "76561198000000333", SteamIdSource::WebApi, "y", now - 450.0 * kDay);
		(void)registry.Observe("profile-edge", "76561198000000444", SteamIdSource::WebApi, "edge", now - SteamIdentityRegistry::kRetentionSeconds);

		const auto removed = registry.Sweep(now, [](const SteamIdentityRecord& a_record) { return a_record.profileId == "profile-banned"; });
		assert(removed == 1);
		assert(registry.ForProfile("profile-stale").empty());
		assert(registry.ForProfile(kProfile).size() == 1 && registry.ForProfile("profile-banned").size() == 1);
		assert(registry.ForProfile("profile-flag-a").size() == 1 && registry.ForProfile("profile-flag-b").size() == 1);
		assert(registry.ForProfile("profile-edge").size() == 1);

		assert(registry.ClearFlags("profile-flag-b", "76561198000000333") == 1);
		assert(registry.Sweep(now, {}) == 3);
		assert(registry.RecordCount() == 2);
	}

	void TestJsonRoundTrip()
	{
		SteamIdentityRegistry registry;
		(void)registry.Observe(kProfile, kSteam, SteamIdSource::HostSelf, "Nate", 10.0);
		(void)registry.Observe(kOtherProfile, kSteam, SteamIdSource::SteamTunnel, "Nora", 20.0);
		const auto stored = registry.ToJson();
		assert(stored.at("records").size() == 2 && stored.at("flags").size() == 1);
		assert(stored.at("records").at(0).at("source") == "host-self");
		assert(stored.at("flags").at(0).at("kind") == "shared_steam_id");

		const SteamIdentityRegistry reloaded{ Json::parse(stored.dump()) };
		assert(reloaded.ToJson() == stored);
		Json flagOnly = stored;
		flagOnly["records"].erase(1);
		SteamIdentityRegistry restored{ flagOnly };
		assert(restored.Observe(kOtherProfile, kSteam, SteamIdSource::WebApi, "Nora", 30.0).raised.empty());
		assert(restored.Flags().size() == 1);
		assert(SteamIdentityRegistry{ Json(nullptr) }.RecordCount() == 0);
		assert(SteamIdentityRegistry{ Json::object() }.RecordCount() == 0);
	}

	void TestLoadRejectsMalformed()
	{
		const Json record{ { "profileId", kProfile }, { "steamId", kSteam }, { "source", "steam-web-api" }, { "firstSeen", 1.0 }, { "lastSeen", 2.0 }, { "nicknames", Json::array({ "a" }) } };
		const auto withRecord = [&](const char* a_key, Json a_value) {
			Json changed = record;
			changed[a_key] = std::move(a_value);
			return Json{ { "records", Json::array({ changed }) } };
		};
		assert((SteamIdentityRegistry{ Json{ { "records", Json::array({ record }) } } }.RecordCount() == 1));
		for (const auto& bad : {
				 Json::array(),
				 Json{ { "records", Json::object() } },
				 Json{ { "records", Json::array({ record, record }) } },
				 Json{ { "records", Json::array({ 1 }) } },
				 withRecord("steamId", Json(76561198000000042ull)),
				 withRecord("steamId", Json("76561197960265728")),
				 withRecord("profileId", Json("bad profile")),
				 withRecord("source", Json("claimed")),
				 withRecord("firstSeen", Json(3.0)),
				 withRecord("lastSeen", Json(-1.0)),
				 withRecord("lastSeen", Json(true)),
				 withRecord("nicknames", Json::array({ std::string(65, 'n') })),
				 withRecord("nicknames", Json("a")),
				 Json{ { "flags", Json::array({ Json{ { "kind", "other" }, { "profileId", kProfile }, { "steamId", kSteam }, { "otherId", kOtherProfile }, { "raisedAt", 1.0 } } }) } },
				 Json{ { "flags", Json::array({ Json{ { "kind", "multiple_steam_ids" }, { "profileId", kProfile }, { "steamId", kSteam }, { "otherId", kOtherProfile }, { "raisedAt", 1.0 } } }) } } }) {
			bool threw = false;
			try {
				(void)SteamIdentityRegistry{ bad };
			} catch (const std::invalid_argument&) {
				threw = true;
			}
			assert(threw);
		}
	}
}

int main()
{
	TestObserveRecordsHistory();
	TestObserveRejectsUnverifiedOrMalformed();
	TestLinkingAnomaliesAreFlaggedNotBlocked();
	TestSweepKeepsActiveFlaggedAndProtected();
	TestJsonRoundTrip();
	TestLoadRejectsMalformed();
	return 0;
}
