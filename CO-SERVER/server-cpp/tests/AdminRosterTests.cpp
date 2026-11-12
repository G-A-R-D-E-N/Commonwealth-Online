#include "AdminRoster.h"
#include "IdentityGate.h"
#include "SaveAttestationAuthority.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>

namespace
{
	using namespace CommonwealthOnline::Server;


	class Tokens final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override { return "case-test-token-" + std::to_string(++counter); }
		F4T::Identity::TokenHash HashToken(std::string_view token) override {
			F4T::Identity::TokenHash hash{};
			for (std::size_t i = 0; i < hash.size(); ++i) hash[i] = static_cast<std::uint8_t>(i + token.size());
			return hash;
		}
		unsigned counter{};
	};


	class MemoryResumeStore final : public F4T::Identity::ResumeCredentialStore {
	public:
		std::map<std::string, F4T::Identity::PersistedResumeCredential> records;
		std::optional<F4T::Identity::PersistedResumeCredential> GetResumeCredential(std::string_view id) override {
			const auto it = records.find(std::string{id});
			return it == records.end() ? std::nullopt : std::optional{it->second};
		}
		void SaveResumeCredential(std::string_view id, const F4T::Identity::TokenHash& hash, const std::optional<F4T::Identity::TokenHash>& previous, bool confirmed, double expires) override {
			records[std::string{id}] = {hash, expires, previous, confirmed};
		}
		bool DeleteResumeCredential(std::string_view id) override { return records.erase(std::string{id}) != 0; }
	};

	std::vector<std::uint8_t> Hello(std::string_view profile, std::string_view secret)
	{
		const auto text = nlohmann::json{
			{ "type", "hello" }, { "protocolVersion", 2 }, { "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" }, { "profileId", profile }, { "profileSecret", secret },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", nlohmann::json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		}.dump();
		return { text.begin(), text.end() };
	}

	std::filesystem::path Scratch()
	{
		const auto suffix = std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count()));
		return std::filesystem::temp_directory_path() / ("co-admin-roster-tests-" + suffix);
	}
}

int main()
{
	const auto scratch = Scratch();
	std::filesystem::create_directories(scratch);
	const auto path = scratch / "admins.json";
	constexpr auto kConfigured = "config-admin-0001";
	constexpr auto kGranted = "granted-player-0002";
	constexpr auto kHost = "host-player-0003";

	{
		AdminRoster roster{ { kConfigured }, path };
		assert(roster.Contains(kConfigured));
		assert(!roster.Contains("CONFIG-ADMIN-0001"));
		assert(!roster.ContainsStored("CONFIG-ADMIN-0001"));
		assert(roster.Grant("CONFIG-ADMIN-0001") == AdminChange::Changed);
		assert(roster.Contains("CONFIG-ADMIN-0001"));
		assert(roster.Revoke("CONFIG-ADMIN-0001") == AdminChange::Changed);
		assert(!roster.Contains(kGranted));
		assert(roster.Grant("bad") == AdminChange::Invalid);
		assert(roster.Grant(kConfigured) == AdminChange::Unchanged);
		assert(roster.Grant(kGranted) == AdminChange::Changed);
		assert(roster.Grant(kGranted) == AdminChange::Unchanged);
		assert(roster.Contains(kGranted));
		assert(roster.Revoke(kConfigured) == AdminChange::Fixed);
		assert(roster.Contains(kConfigured));

		roster.NoteHost(false, kHost);
		assert(!roster.Contains(kHost));
		roster.NoteHost(true, kHost);
		assert(roster.Contains(kHost));
		assert(roster.Revoke(kHost) == AdminChange::Fixed);
		assert(roster.List()["admins"].size() == 3);
	}

	{
		AdminRoster reloaded{ {}, path };
		assert(reloaded.Contains(kGranted));
		assert(!reloaded.Contains(kHost));
		assert(reloaded.Revoke(kGranted) == AdminChange::Changed);
		assert(reloaded.Revoke(kGranted) == AdminChange::Unchanged);
		assert(!reloaded.Contains(kGranted));
	}

	{
		AdminRoster afterRevoke{ {}, path };
		assert(!afterRevoke.Contains(kGranted));
		assert(!afterRevoke.TakeLoadWarning());
	}

	{
		std::ofstream(path, std::ios::trunc) << "{ not json";
		AdminRoster corrupt{ {}, path };
		assert(!corrupt.Contains(kGranted));
		assert(corrupt.TakeLoadWarning());
		assert(!corrupt.TakeLoadWarning());
		assert(!std::filesystem::exists(path));
	}

	{
		const auto corruptPath = scratch / "corrupt-admins.json";
		std::ofstream(corruptPath, std::ios::trunc) << "{ not json";
		AdminRoster unbound{ { kConfigured }, corruptPath, [](std::string_view) { return false; } };
		assert(unbound.TakeLoadWarning());
		assert(unbound.IsUnbound(kConfigured));
		std::size_t quarantined = 0;
		for (const auto& entry : std::filesystem::directory_iterator(scratch)) {
			const auto name = entry.path().filename().string();
			if (name.rfind("corrupt-admins.json.quarantine.", 0) != 0) continue;
			std::ifstream in(entry.path(), std::ios::binary);
			std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			assert(text == "{ not json");
			++quarantined;
		}
		assert(quarantined == 1);
	}



	{
		AdminRoster roster{ { "admin-profile-01" }, {} };
		Tokens tokens;
		MemorySaveAttestationAuthority authority([](std::string_view profile) {
			return std::string(32, profile.front() == 'a' ? 'a' : 'b');
		});
		const auto adminSecret = authority.EnsureProfileSecret("admin-profile-01");
		const auto attackerSecret = authority.EnsureProfileSecret("ADMIN-PROFILE-01");
		assert(adminSecret != attackerSecret);
		F4T::Identity::Requirements requirements;
		IdentityGate gate(requirements, tokens, nullptr, {}, &authority,
			[&](std::string_view id) { return roster.ContainsStored(id); });
		const auto admin = gate.InspectInbound(501, Hello("admin-profile-01", adminSecret), 1000.0);
		assert(admin.forward && admin.secretVerified && admin.isAdmin);
		const auto attacker = gate.InspectInbound(502, Hello("ADMIN-PROFILE-01", attackerSecret), 1000.0);
		assert(attacker.forward && attacker.secretVerified && !attacker.isAdmin);
		ProtocolHost host{ "Admin test", "", { "admin-profile-01" } };
		host.SetAdminLookup([&](std::string_view id) { return roster.Contains(id); });
		for (const auto& [connection, decision] : { std::pair{ 501u, admin }, std::pair{ 502u, attacker } }) {
			(void)host.HandleConnected(connection, 1000.0);
			host.BindSessionProfile(connection, decision.profileId);
			host.SetSessionAdminVerified(connection, decision.secretVerified);
			(void)host.HandleMessage(connection, Hello(decision.profileId, {}), 1000.1, 1000.1);
		}
		assert(host.IsLiveAdmin(501));
		assert(!host.IsLiveAdmin(502));
		const auto actionText = nlohmann::json{ { "type", "adminAction" }, { "action", "ping" } }.dump();
		const std::vector<std::uint8_t> action{ actionText.begin(), actionText.end() };
		const auto rejectedBefore = host.Stats().adminRejected;
		(void)host.HandleMessage(502, action, 1001.0, 1001.0);
		assert(host.Stats().adminRejected == rejectedBefore + 1);
		const auto adminReplies = host.HandleMessage(501, action, 1001.0, 1001.0);
		assert(!adminReplies.empty());

	}

	{
		const auto collisionPath = scratch / "case-collision.json";
		AdminRoster roster{ {}, collisionPath };
		assert(roster.Grant("Mixed-Case-Admin") == AdminChange::Changed);
		assert(roster.Grant("mixed-case-admin") == AdminChange::Changed);
		AdminRoster reloaded{ {}, collisionPath };
		assert(reloaded.ContainsStored("Mixed-Case-Admin"));
		assert(reloaded.ContainsStored("mixed-case-admin"));
		assert(!reloaded.ContainsStored("MIXED-CASE-ADMIN"));
		assert(reloaded.Revoke("mixed-case-admin") == AdminChange::Changed);
		AdminRoster afterRevoke{ {}, collisionPath };
		assert(afterRevoke.ContainsStored("Mixed-Case-Admin"));
		assert(!afterRevoke.ContainsStored("mixed-case-admin"));
	}

	{
		AdminRoster full{ {}, scratch / "full.json" };
		for (std::size_t i = 0; i < AdminRoster::kMaxGranted; ++i) {
			assert(full.Grant("player-id-" + std::to_string(1000 + i)) == AdminChange::Changed);
		}
		bool refused = false;
		try {
			(void)full.Grant("player-id-overflow");
		} catch (const std::length_error&) {
			refused = true;
		}
		assert(refused);
	}

	{
		AdminRoster roster{ {}, scratch / "hooks.json" };
		AdminCommandHooks hooks;
		std::string announced;
		InstallAdminRosterHooks(
			hooks, roster,
			[&](std::uint32_t id) -> std::optional<std::string> { return id == 7 ? std::optional<std::string>{ kGranted } : std::nullopt; },
			[&](std::string_view id) { announced = std::string{ id }; });
		AdminCommandDispatcher dispatcher{ hooks };

		auto reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "playerId", 9 } });
		assert(!reply.value("ok", true));
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "playerId", 7 } });
		assert(reply.value("ok", false));
		assert(announced == kGranted && roster.Contains(kGranted));
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "profileId", kGranted } });
		assert(!reply.value("ok", true));
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_list" } });
		assert(reply.value("ok", false));
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_revoke" } });
		assert(!reply.value("ok", true));
		announced.clear();
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_revoke" }, { "profileId", kGranted } });
		assert(reply.value("ok", false));
		assert(announced == kGranted && !roster.Contains(kGranted));
	}


	{
		Tokens tokens;
		std::string steamId = "76561198000000042";
		SteamIdentityHooks hooks;
		hooks.resolve = [&](std::uint32_t, std::string_view, std::string_view) -> std::optional<VerifiedSteamId> {
			return VerifiedSteamId{ steamId, SteamIdSource::SteamTunnel };
		};
		F4T::Identity::Requirements requirements;
		MemoryResumeStore store;
		store.SaveResumeCredential("existing-profile", tokens.HashToken("existing-token"), std::nullopt, true, 5000.0);
		IdentityGate gate(requirements, tokens, &store, {}, nullptr, {}, hooks);
		for (std::uint32_t i = 1; i <= 32; ++i) {
			const auto result = gate.InspectInbound(i, Hello("rate-profile-" + std::to_string(i), {}), 1000.0);
			assert(result.forward);
			gate.ConnectionClosed(i);
		}
		const auto limited = gate.InspectInbound(33, Hello("rate-profile-over", {}), 1000.0);
		assert(!limited.forward && limited.code == "reservation_rate_limited");
		const auto helloBytes = Hello("existing-profile", {});
		auto returning = nlohmann::json::parse(helloBytes.begin(), helloBytes.end());
		returning["resumeToken"] = "existing-token";
		const auto returningText = returning.dump();
		const std::vector<std::uint8_t> returningBytes{ returningText.begin(), returningText.end() };
		assert(gate.InspectInbound(36, returningBytes, 1000.0).forward);
		gate.ConnectionClosed(36);

		steamId = "76561198000000043";
		assert(gate.InspectInbound(34, Hello("rate-profile-other", {}), 1000.0).forward);
		gate.ConnectionClosed(34);
		steamId = "76561198000000042";
		assert(gate.InspectInbound(35, Hello("rate-profile-later", {}), 4601.0).forward);
	}


	{
		Tokens tokens;
		SteamIdentityHooks hooks;
		hooks.remoteIpv4 = [](std::uint32_t) -> std::optional<std::uint32_t> { return 0xc0000201; };
		F4T::Identity::Requirements requirements;
		IdentityGate gate(requirements, tokens, nullptr, {}, nullptr, {}, hooks);
		for (std::uint32_t i = 100; i < 132; ++i) {
			assert(gate.InspectInbound(i, Hello("direct-profile-" + std::to_string(i), {}), 1000.0).forward);
			gate.ConnectionClosed(i);
		}
		const auto limited = gate.InspectInbound(132, Hello("direct-profile-over", {}), 1000.0);
		assert(!limited.forward && limited.code == "reservation_rate_limited");
	}

	{
		constexpr auto kSteam = "76561198000000101";
		constexpr auto kOther = "76561198000000102";
		const auto bindPath = scratch / "bind.json";
		std::set<std::string> known{ "known-admin-0001" };
		const auto isKnown = [&](std::string_view id) { return known.contains(std::string{ id }); };
		{
			AdminRoster roster{ { "config-unknown-0001", "known-admin-0001" }, bindPath, isKnown };
			assert(roster.IsUnbound("config-unknown-0001") && !roster.Honours("config-unknown-0001", ""));
			assert(!roster.IsUnbound("known-admin-0001") && roster.Honours("known-admin-0001", ""));
			assert(roster.Grant("grant-unknown-0002") == AdminChange::Changed);
			assert(roster.IsUnbound("grant-unknown-0002") && !roster.Honours("grant-unknown-0002", kSteam));
			known.insert("grant-known-0003");
			assert(roster.Grant("grant-known-0003") == AdminChange::Changed && !roster.IsUnbound("grant-known-0003"));
			assert(roster.Honours("grant-known-0003", ""));
			assert(roster.Grant("grant-bad-0004", "not-a-steam-id") == AdminChange::Invalid);
			assert(roster.Grant("grant-steam-0005", kSteam) == AdminChange::Changed);
			assert(!roster.IsUnbound("grant-steam-0005"));
			assert(!roster.Honours("grant-steam-0005", "") && !roster.Honours("grant-steam-0005", kOther) && roster.Honours("grant-steam-0005", kSteam));
			assert(roster.Grant("grant-steam-0005", kSteam) == AdminChange::Unchanged);
			known.insert("grant-conn-0006");
			assert(roster.Grant("grant-conn-0006", {}, true) == AdminChange::Changed);
			assert(!roster.IsUnbound("grant-conn-0006") && roster.Honours("grant-conn-0006", ""));
			assert(roster.Grant("grant-unknown-0002", kSteam) == AdminChange::Changed);
			assert(roster.Honours("grant-unknown-0002", kSteam) && !roster.Honours("grant-unknown-0002", kOther));
			assert(roster.Grant("grant-late-0007") == AdminChange::Changed && roster.IsUnbound("grant-late-0007"));
			assert(roster.Grant("grant-late-0007", {}, true) == AdminChange::Changed);
			assert(roster.Honours("grant-late-0007", ""));
			assert(roster.Grant("grant-late-0008") == AdminChange::Changed && roster.IsUnbound("grant-late-0008"));
		}
		known.insert("config-unknown-0001");
		known.insert("grant-late-0008");
		{
			AdminRoster reloaded{ { "config-unknown-0001", "known-admin-0001" }, bindPath, isKnown };
			assert(reloaded.IsUnbound("config-unknown-0001") && !reloaded.Honours("config-unknown-0001", kSteam));
			assert(reloaded.IsUnbound("grant-late-0008") && !reloaded.Honours("grant-late-0008", ""));
			assert(reloaded.Honours("grant-steam-0005", kSteam) && !reloaded.Honours("grant-steam-0005", ""));
			assert(reloaded.Honours("grant-unknown-0002", kSteam) && !reloaded.IsUnbound("grant-unknown-0002"));
			assert(reloaded.Honours("grant-conn-0006", "") && reloaded.Honours("known-admin-0001", ""));
			bool listedUnbound = false;
			const auto listing = reloaded.List();
			for (const auto& entry : listing["admins"]) {
				if (entry["profileId"] == "grant-late-0008") listedUnbound = entry.value("unbound", false);
			}
			assert(listedUnbound);
			assert(reloaded.Revoke("grant-steam-0005") == AdminChange::Changed);
			assert(reloaded.Grant("grant-steam-0005") == AdminChange::Changed && reloaded.IsUnbound("grant-steam-0005"));
			assert(!reloaded.Honours("grant-steam-0005", kSteam));
		}
	}

	{
		constexpr auto kSteam = "76561198000000201";
		constexpr auto kOther = "76561198000000202";
		const auto bindPath = scratch / "gate-bind.json";
		Tokens tokens;
		MemorySaveAttestationAuthority authority([](std::string_view) { return std::string(32, 'c'); });
		AdminRoster roster{ {}, bindPath };
		for (const auto* profile : { "steam-admin-0001", "steam-admin-0002", "steam-admin-0003", "steam-admin-0004", "steam-admin-0005" }) {
			assert(roster.Grant(profile, kSteam) == AdminChange::Changed);
		}
		const auto secretOf = [&](std::string_view profile) { return authority.EnsureProfileSecret(profile); };
		std::string resolved;
		SteamIdentityHooks hooks;
		hooks.resolve = [&](std::uint32_t, std::string_view, std::string_view) -> std::optional<VerifiedSteamId> {
			if (resolved.empty()) return std::nullopt;
			return VerifiedSteamId{ resolved, SteamIdSource::SteamTunnel };
		};
		F4T::Identity::Requirements requirements;
		IdentityGate gate(requirements, tokens, nullptr, {}, &authority,
			[&](std::string_view id) { return roster.ContainsStored(id); }, hooks);
		gate.SetAdminSteamCheck([&](std::string_view id, std::string_view steam) { return roster.Honours(id, steam); });
		const auto noSteam = gate.InspectInbound(601, Hello("steam-admin-0001", secretOf("steam-admin-0001")), 1000.0);
		assert(noSteam.forward && noSteam.secretVerified && !noSteam.isAdmin);
		gate.ConnectionClosed(601);
		resolved = kOther;
		const auto wrongSteam = gate.InspectInbound(602, Hello("steam-admin-0002", secretOf("steam-admin-0002")), 1000.0);
		assert(wrongSteam.forward && !wrongSteam.isAdmin);
		gate.ConnectionClosed(602);
		resolved = kSteam;
		const auto rightSteam = gate.InspectInbound(603, Hello("steam-admin-0003", secretOf("steam-admin-0003")), 1000.0);
		assert(rightSteam.forward && rightSteam.isAdmin);
		assert(gate.IsAdminConnection(603));

		resolved.clear();
		const auto pending = gate.InspectInbound(604, Hello("steam-admin-0004", secretOf("steam-admin-0004")), 1000.0);
		assert(pending.forward && pending.secretVerified && !pending.isAdmin && !gate.IsAdminConnection(604));
		assert(!gate.NoteSteamVerified(604, kOther) && !gate.IsAdminConnection(604));
		assert(gate.NoteSteamVerified(604, kSteam) && gate.IsAdminConnection(604));
		gate.ConnectionClosed(604);
		assert(!gate.IsAdminConnection(604) && !gate.NoteSteamVerified(604, kSteam));

		const auto noSecret = gate.InspectInbound(605, Hello("steam-admin-0005", {}), 1000.0);
		assert(!noSecret.secretVerified && !noSecret.isAdmin);
		assert(!gate.NoteSteamVerified(605, kSteam) && !gate.IsAdminConnection(605));

		ProtocolHost host{ "Admin test", "", {} };
		host.SetAdminLookup([&](std::string_view id) { return roster.Contains(id); });
		host.SetAdminSteamCheck([&](std::string_view id, std::string_view steam) { return roster.Honours(id, steam); });
		for (const auto& [connection, steam] : { std::pair{ 611u, std::string{} }, std::pair{ 612u, std::string{ kOther } }, std::pair{ 613u, std::string{ kSteam } } }) {
			const auto profile = "steam-admin-000" + std::to_string(connection - 610);
			(void)host.HandleConnected(connection, 1000.0);
			host.BindSessionProfileVerified(connection, profile, true, steam);
			(void)host.HandleMessage(connection, Hello(profile, {}), 1000.1, 1000.1);
		}
		assert(!host.IsLiveAdmin(611) && !host.IsLiveAdmin(612) && host.IsLiveAdmin(613));
	}

	{
		AdminRoster roster{ {}, scratch / "dispatch-bind.json", [](std::string_view) { return false; } };
		AdminCommandHooks hooks;
		InstallAdminRosterHooks(hooks, roster, [](std::uint32_t id) -> std::optional<std::string> { return id == 7 ? std::optional<std::string>{ "online-admin-0009" } : std::nullopt; }, [](std::string_view) {});
		AdminCommandDispatcher dispatcher{ hooks };
		auto reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "profileId", "offline-admin-0010" } });
		assert(reply.value("ok", false) && roster.IsUnbound("offline-admin-0010"));
		assert(reply.dump().find("WARNING") != std::string::npos);
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "profileId", "offline-admin-0010" }, { "steamId", "nope" } });
		assert(!reply.value("ok", true));
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "profileId", "offline-admin-0010" }, { "steamId", "76561198000000301" } });
		assert(reply.value("ok", false) && !roster.IsUnbound("offline-admin-0010"));
		assert(roster.Honours("offline-admin-0010", "76561198000000301") && !roster.Honours("offline-admin-0010", ""));
		reply = dispatcher.Dispatch(nlohmann::json{ { "cmd", "admin_grant" }, { "playerId", 7 } });
		assert(reply.value("ok", false) && roster.Honours("online-admin-0009", ""));
	}

	std::filesystem::remove_all(scratch);
	return 0;
}
