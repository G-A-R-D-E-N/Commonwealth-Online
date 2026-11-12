#include "AdminRoster.h"
#include "IdentityGate.h"
#include "SaveAttestationAuthority.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
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

	std::filesystem::remove_all(scratch);
	return 0;
}
