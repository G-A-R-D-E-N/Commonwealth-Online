#include "IdentityGate.h"

#include "F4TContentManifest.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::Delivery;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::SteamIdSource;
	using F4T::Identity::TokenHash;

	class FakeTokenService final : public F4T::Identity::TokenService
	{
	public:
		std::string CreateToken() override
		{
			return "gate-token-" + std::to_string(++counter);
		}

		TokenHash HashToken(std::string_view a_token) override
		{
			TokenHash hash{};
			for (std::size_t index = 0; index < hash.size(); ++index) {
				hash[index] = static_cast<std::uint8_t>(
					(index * 29u + a_token.size() * 17u + (a_token.empty() ? 0u : static_cast<unsigned char>(a_token[index % a_token.size()]))) & 0xffu);
			}
			return hash;
		}

		std::uint32_t counter{};
	};

	std::vector<std::uint8_t> Encode(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size()
		});
	}

	Json Hello(std::string_view a_profileId, std::string_view a_token = {}, std::string_view a_secret = {})
	{
		Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "clientVersion", "1.2.0" },
			{ "falloutRuntimeVersion", "1.11.240" },
			{ "profileId", a_profileId },
			{ "contentManifestFingerprint", std::string(64, 'a') },
			{ "capabilities", Json::array({ "interest-v1", "player-state-v1", "save-attestation-v1" }) }
		};
		if (!a_token.empty()) {
			hello["resumeToken"] = a_token;
		}
		if (!a_secret.empty()) {
			hello["profileSecret"] = a_secret;
		}
		return hello;
	}
}

void TestVerifiedSteamIdentity(const F4T::Identity::Requirements& a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	constexpr auto kGuest = "76561198000000042";
	constexpr auto kOther = "76561198000000099";
	constexpr auto kBanned = "76561198000000666";
	constexpr auto kHostSelf = "76561198000000001";
	std::map<std::uint32_t, VerifiedSteamId> verified{
		{ 300, { kGuest, SteamIdSource::SteamTunnel } },
		{ 301, { kGuest, SteamIdSource::SteamTunnel } },
		{ 302, { kGuest, SteamIdSource::SteamTunnel } },
		{ 303, { kBanned, SteamIdSource::SteamTunnel } },
		{ 304, { kHostSelf, SteamIdSource::HostSelf } },
		{ 306, { kOther, SteamIdSource::SteamTunnel } }
	};
	std::vector<std::string> resolvedClaims;
	IdentityGate gate(
		a_requirements,
		a_tokens,
		nullptr,
		{},
		nullptr,
		[](std::string_view) { return false; },
		SteamIdentityHooks{
			.resolve = [&](std::uint32_t a_connection, std::string_view a_claimed) -> std::optional<VerifiedSteamId> {
				resolvedClaims.emplace_back(a_claimed);
				const auto found = verified.find(a_connection);
				return found == verified.end() ? std::nullopt : std::optional{ found->second };
			},
			.banLookup = [&](std::string_view a_steamId) -> std::optional<std::string> {
				if (a_steamId == kBanned) return std::string{ "cheating" };
				return std::nullopt;
			},
			.verifyTicket = {},
			.passwordLockout = {},
			.monotonicNow = {}
		});

	Json hello = Hello("verified-guest-01");
	const auto noClaim = gate.InspectInbound(300, Encode(hello), 5000.0);
	assert(noClaim.forward);
	assert(noClaim.steamId == kGuest);
	assert(noClaim.steamIdSource == SteamIdSource::SteamTunnel);
	assert(noClaim.claimedSteamId.empty());

	hello = Hello("verified-guest-02");
	hello["steamId"] = kGuest;
	const auto matching = gate.InspectInbound(301, Encode(hello), 5000.1);
	assert(matching.forward);
	assert(matching.steamId == kGuest);
	assert(matching.claimedSteamId == kGuest);

	hello = Hello("verified-guest-03");
	hello["steamId"] = kOther;
	const auto spoofed = gate.InspectInbound(302, Encode(hello), 5000.2);
	assert(!spoofed.forward);
	assert(spoofed.code == "steam_identity_mismatch");
	assert(spoofed.steamId.empty());
	assert(!gate.IsValidated(302));

	hello = Hello("banned-steam-001");
	const auto banned = gate.InspectInbound(303, Encode(hello), 5000.3);
	assert(!banned.forward);
	assert(banned.code == "steam_banned");
	assert(banned.reason == "cheating");
	assert(!gate.IsValidated(303));

	hello = Hello("host-self-00001");
	hello["steamId"] = kHostSelf;
	const auto hostSelf = gate.InspectInbound(304, Encode(hello), 5000.4);
	assert(hostSelf.forward);
	assert(hostSelf.steamId == kHostSelf);
	assert(hostSelf.steamIdSource == SteamIdSource::HostSelf);

	hello = Hello("claims-only-0001");
	hello["steamId"] = kHostSelf;
	const auto unverified = gate.InspectInbound(305, Encode(hello), 5000.5);
	assert(unverified.forward);
	assert(unverified.steamId.empty());
	assert(unverified.claimedSteamId == kHostSelf);

	const auto saveAdminFlag = [&gate](std::uint32_t a_connection) {
		OutboundFrame frame{ a_connection, Delivery::ReliableOrdered, Encode(Json{ { "type", "sessionReady" }, { "playerId", 7 }, { "protocolVersion", 2 } }) };
		assert(gate.EnrichSessionReady(frame));
		return Decode(frame).value("multiplayerSaveAdmin", false);
	};
	assert(!saveAdminFlag(304));
	assert(!saveAdminFlag(305));
	assert(!saveAdminFlag(301));

	hello = Hello("verified-guest-01");
	gate.ConnectionClosed(300);
	const auto rebound = gate.InspectInbound(306, Encode(hello), 5000.6);
	assert(rebound.forward);
	assert(rebound.steamId == kOther);

	resolvedClaims.clear();
	hello = Hello("malformed-claim-1");
	hello["steamId"] = "7656119800000000x";
	(void)gate.InspectInbound(307, Encode(hello), 5000.7);
	assert(resolvedClaims.size() == 1 && resolvedClaims.front().empty());
}

void TestSteamTicketQueuedOnlyWithoutVerifiedSource(const F4T::Identity::Requirements& a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	constexpr auto kGuest = "76561198000000042";
	constexpr auto kTicket = "14000000ABCDEF01";
	struct Queued
	{
		std::uint32_t connection;
		std::string ticket;
		std::string claim;
		std::string profile;
	};
	std::vector<Queued> queued;
	bool accept = true;
	IdentityGate gate(
		a_requirements,
		a_tokens,
		nullptr,
		{},
		nullptr,
		[](std::string_view) { return false; },
		SteamIdentityHooks{
			.resolve = [&](std::uint32_t a_connection, std::string_view) -> std::optional<VerifiedSteamId> {
				if (a_connection == 400) return VerifiedSteamId{ kGuest, SteamIdSource::SteamTunnel };
				return std::nullopt;
			},
			.banLookup = {},
			.verifyTicket = [&](std::uint32_t a_connection, std::string_view a_ticket, std::string_view a_claim, std::string_view a_profile) {
				queued.push_back({ a_connection, std::string{ a_ticket }, std::string{ a_claim }, std::string{ a_profile } });
				return accept;
			},
			.passwordLockout = {},
			.monotonicNow = {}
		});

	Json hello = Hello("ticket-tunnel-001");
	hello["steamTicket"] = kTicket;
	const auto tunnel = gate.InspectInbound(400, Encode(hello), 6000.0);
	assert(tunnel.forward && tunnel.steamId == kGuest && !tunnel.steamTicketQueued);
	assert(queued.empty());

	hello = Hello("ticket-direct-001");
	hello["steamTicket"] = kTicket;
	hello["steamId"] = kGuest;
	const auto direct = gate.InspectInbound(401, Encode(hello), 6000.1);
	assert(direct.forward && direct.steamId.empty() && direct.steamTicketQueued);
	assert(queued.size() == 1 && queued[0].connection == 401 && queued[0].ticket == kTicket && queued[0].claim == kGuest);
	assert(queued[0].profile == direct.profileId && !direct.profileId.empty());

	for (const Json& malformed : { Json("00&key=x"), Json("ABC"), Json(1234), Json(std::string(4098, 'A')), Json::array({ kTicket }) }) {
		hello = Hello("ticket-malformed1");
		hello["steamTicket"] = malformed;
		const auto ignored = gate.InspectInbound(402, Encode(hello), 6000.2);
		assert(ignored.forward && !ignored.steamTicketQueued);
		gate.ConnectionClosed(402);
	}
	assert(queued.size() == 1);

	accept = false;
	hello = Hello("ticket-queue-full");
	hello["steamTicket"] = kTicket;
	const auto full = gate.InspectInbound(403, Encode(hello), 6000.3);
	assert(full.forward && !full.steamTicketQueued);
}

void TestSteamPasswordLockout(F4T::Identity::Requirements a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	constexpr auto kFriendId = "76561198000000042";
	constexpr auto kStrangerId = "76561198000000099";
	constexpr auto kOtherId = "76561198000000123";
	constexpr auto kHostId = "76561198000000001";
	a_requirements.serverPasswordSha256 = F4T::ContentManifest::Sha256Hex("letmein");
	double clock = 100.0;
	std::map<std::uint32_t, VerifiedSteamId> verified;
	const auto gateHooks = [&] {
		return SteamIdentityHooks{
			.resolve = [&](std::uint32_t a_connection, std::string_view) -> std::optional<VerifiedSteamId> {
				const auto found = verified.find(a_connection);
				return found == verified.end() ? std::nullopt : std::optional{ found->second };
			},
			.banLookup = {},
			.verifyTicket = {},
			.passwordLockout = F4T::SteamAdmission::LockoutOptions{ .threshold = 3, .cooldownSeconds = 60.0, .maximumTracked = 16 },
			.monotonicNow = [&clock] { return clock; }
		};
	};
	IdentityGate gate(a_requirements, a_tokens, nullptr, {}, nullptr, [](std::string_view) { return false; }, gateHooks());

	std::uint32_t connection = 500;
	const auto attempt = [&](std::string_view a_steamId, bool a_passwordRequired, std::optional<std::string> a_password) {
		const auto id = connection++;
		verified[id] = VerifiedSteamId{ std::string{ a_steamId }, SteamIdSource::SteamTunnel, a_passwordRequired };
		Json hello = Hello("lockout-profile-" + std::to_string(id));
		if (a_password) hello["serverPassword"] = *a_password;
		return gate.InspectInbound(id, Encode(hello), 7000.0);
	};

	assert(attempt(kFriendId, false, "letmein").forward);
	assert(attempt(kFriendId, false, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, "letmein").forward);
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, std::nullopt).code == "password_required");
	assert(attempt(kStrangerId, true, std::nullopt).code == "password_required");
	assert(attempt(kStrangerId, true, "wrong-again").code == "password_invalid");
	assert(attempt(kStrangerId, true, "letmein").forward);
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	const auto third = attempt(kStrangerId, true, "wrong-again");
	assert(!third.forward && third.code == "password_invalid");

	const auto locked = attempt(kStrangerId, true, "letmein");
	assert(!locked.forward && locked.code == "password_locked" && locked.reason.find("1 minute") != std::string::npos);
	assert(!gate.IsValidated(connection - 1));
	assert(attempt(kOtherId, true, "letmein").forward);
	assert(attempt(kFriendId, false, "letmein").forward);

	clock += 59.0;
	assert(attempt(kStrangerId, true, "letmein").code == "password_locked");
	clock += 2.0;
	assert(attempt(kStrangerId, true, "letmein").forward);
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, "letmein").forward);
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, "wrong").code == "password_invalid");
	assert(attempt(kStrangerId, true, "letmein").forward);

	for (int index = 0; index < 5; ++index) {
		Json hello = Hello("unverified-pass-" + std::to_string(index));
		hello["serverPassword"] = "wrong";
		assert(gate.InspectInbound(connection++, Encode(hello), 7000.0).code == "password_invalid");
	}
	Json unverified = Hello("unverified-pass-ok");
	unverified["serverPassword"] = "letmein";
	assert(gate.InspectInbound(connection++, Encode(unverified), 7000.0).forward);

	F4T::Identity::Requirements open = a_requirements;
	open.serverPasswordSha256.reset();
	IdentityGate openGate(open, a_tokens, nullptr, {}, nullptr, [](std::string_view) { return false; }, gateHooks());
	verified[900] = VerifiedSteamId{ kStrangerId, SteamIdSource::SteamTunnel, true };
	verified[901] = VerifiedSteamId{ kFriendId, SteamIdSource::SteamTunnel, false };
	const auto friendsOnly = openGate.InspectInbound(900, Encode(Hello("open-gate-stranger")), 7000.0);
	assert(!friendsOnly.forward && friendsOnly.code == "steam_friends_only" && !openGate.IsValidated(900));
	assert(openGate.InspectInbound(901, Encode(Hello("open-gate-friend01")), 7000.0).forward);

	for (int index = 0; index < 3; ++index) {
		const auto id = connection++;
		verified[id] = VerifiedSteamId{ kHostId, SteamIdSource::HostSelf, true };
		Json wrongHost = Hello("host-lockout-wrong-" + std::to_string(index));
		wrongHost["serverPassword"] = "wrong";
		assert(gate.InspectInbound(id, Encode(wrongHost), 7000.0).code == "password_invalid");
	}
	const auto hostLockedId = connection++;
	verified[hostLockedId] = VerifiedSteamId{ kHostId, SteamIdSource::HostSelf, true };
	Json hostLockedHello = Hello("host-lockout-blocked");
	hostLockedHello["serverPassword"] = "letmein";
	const auto hostLocked = gate.InspectInbound(hostLockedId, Encode(hostLockedHello), 7000.0);
	assert(!hostLocked.forward && hostLocked.code == "password_locked");
	clock += 61.0;
	const auto hostRetryId = connection++;
	verified[hostRetryId] = VerifiedSteamId{ kHostId, SteamIdSource::HostSelf, true };
	Json hostHello = Hello("host-lockout-ok");
	hostHello["serverPassword"] = "letmein";
	assert(gate.InspectInbound(hostRetryId, Encode(hostHello), 7000.0).forward);
}

void TestDirectPeerPasswordLockout(F4T::Identity::Requirements a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	a_requirements.serverPasswordSha256 = F4T::ContentManifest::Sha256Hex("letmein");
	constexpr std::uint32_t kFirstAddress = 0x0A000001u;
	constexpr std::uint32_t kSecondAddress = 0x0A000002u;
	constexpr std::uint32_t kThirdAddress = 0x0A000003u;
	constexpr std::uint32_t kLoopbackAddress = 0x7F000001u;
	double clock = 200.0;
	std::map<std::uint32_t, std::uint32_t> remoteAddresses;
	IdentityGate gate(
		a_requirements,
		a_tokens,
		nullptr,
		{},
		nullptr,
		[](std::string_view) { return false; },
		SteamIdentityHooks{
			.resolve = [](std::uint32_t, std::string_view) -> std::optional<VerifiedSteamId> { return std::nullopt; },
			.remoteIpv4 = [&remoteAddresses](std::uint32_t a_connection) -> std::optional<std::uint32_t> {
				const auto found = remoteAddresses.find(a_connection);
				return found == remoteAddresses.end() ? std::nullopt : std::optional{ found->second };
			},
			.banLookup = {},
			.verifyTicket = {},
			.passwordLockout = F4T::SteamAdmission::LockoutOptions{ .threshold = 3, .cooldownSeconds = 60.0, .maximumTracked = 2 },
			.monotonicNow = [&clock] { return clock; }
		});

	std::uint32_t connection = 700;
	const auto attempt = [&](std::uint32_t a_address, std::optional<std::string> a_password) {
		const auto id = connection++;
		remoteAddresses[id] = a_address;
		Json hello = Hello("direct-join-" + std::to_string(id));
		if (a_password) hello["serverPassword"] = *a_password;
		return gate.InspectInbound(id, Encode(hello), 9000.0);
	};

	assert(attempt(kFirstAddress, "wrong").code == "password_invalid");
	assert(attempt(kFirstAddress, "wrong").code == "password_invalid");
	assert(attempt(kFirstAddress, "wrong").code == "password_invalid");
	const auto locked = attempt(kFirstAddress, "letmein");
	assert(!locked.forward && locked.code == "password_locked");
	assert(locked.reason.find("1 minute") != std::string::npos);
	assert(attempt(kFirstAddress, "wrong").code == "password_locked");

	assert(attempt(kSecondAddress, "letmein").forward);

	for (int index = 0; index < 5; ++index) {
		assert(attempt(kLoopbackAddress, "wrong").code == "password_invalid");
	}
	assert(attempt(kLoopbackAddress, "letmein").forward);

	clock += 61.0;
	assert(attempt(kFirstAddress, "letmein").forward);

	for (int index = 0; index < 3; ++index) {
		assert(attempt(kFirstAddress, "wrong").code == "password_invalid");
		assert(attempt(kSecondAddress, "wrong").code == "password_invalid");
	}
	assert(attempt(kFirstAddress, "letmein").code == "password_locked");
	assert(attempt(kSecondAddress, "letmein").code == "password_locked");
	for (int index = 0; index < 4; ++index) {
		assert(attempt(kThirdAddress, "wrong").code == "password_invalid");
	}
	assert(attempt(kFirstAddress, "letmein").code == "password_locked");
	assert(attempt(kSecondAddress, "letmein").code == "password_locked");

	clock += 61.0;
	assert(attempt(kFirstAddress, "letmein").forward);
}

int main()
{
	using CommonwealthOnline::Server::IdentityGate;

	FakeTokenService tokens;
	F4T::Identity::Requirements requirements;

	requirements.resumeLifetimeSeconds = 100.0;

	IdentityGate steamGate(requirements, tokens);
	Json steamHello = Hello("steam-profile-000");
	assert(steamGate.InspectInbound(200, Encode(steamHello), 4000.0).steamId.empty());
	std::uint32_t steamConnection = 201;
	for (const auto* claimed : { "76561197960265728", "76561202255233024", "7656119800000000", "765611980000000000", "103582791429521408", "7656119800000000a", "" }) {
		steamHello = Hello("steam-profile-" + std::to_string(steamConnection));
		steamHello["steamId"] = claimed;
		const auto malformedSteam = steamGate.InspectInbound(steamConnection++, Encode(steamHello), 4000.1);
		assert(malformedSteam.forward);
		assert(malformedSteam.steamId.empty());
	}
	steamHello = Hello("steam-profile-numeric");
	steamHello["steamId"] = 76561198000000000ull;
	assert(steamGate.InspectInbound(steamConnection++, Encode(steamHello), 4000.2).steamId.empty());
	steamHello = Hello("steam-profile-valid");
	steamHello["steamId"] = "76561198000000000";
	const auto claimOnly = steamGate.InspectInbound(steamConnection, Encode(steamHello), 4000.3);
	assert(claimOnly.forward);
	assert(claimOnly.steamId.empty());
	assert(claimOnly.steamIdSource == SteamIdSource::None);
	assert(claimOnly.claimedSteamId == "76561198000000000");

	TestVerifiedSteamIdentity(requirements, tokens);
	TestSteamTicketQueuedOnlyWithoutVerifiedSource(requirements, tokens);
	TestSteamPasswordLockout(requirements, tokens);
	TestDirectPeerPasswordLockout(requirements, tokens);
	return 0;
}
