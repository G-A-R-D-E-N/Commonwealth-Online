#include "F4TSteamId.h"
#include "IdentityGate.h"

#include "F4TContentManifest.h"
#include "ServerAfk.h"

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

	for (const Json& malformed : { Json("00&key=x"), Json("ABC"), Json(1234), Json(std::string(F4T::SteamId::kMaximumTicketBytes * 2 + 2, 'A')), Json::array({ kTicket }) }) {
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

	{
		auto steamRequired = a_requirements;
		steamRequired.requireSteamIdentity = true;
		double monotonic = 100.0;
		IdentityGate required(
			steamRequired,
			a_tokens,
			nullptr,
			{},
			nullptr,
			{},
			SteamIdentityHooks{
				.verifyTicket = [](std::uint32_t, std::string_view, std::string_view, std::string_view) { return true; },
				.monotonicNow = [&monotonic] { return monotonic; }
			});
		Json plain = Hello("steam-required-none");
		const auto refused = required.InspectInbound(500, Encode(plain), 7000.0);
		assert(!refused.forward && refused.code == "steam_required");

		Json ticketed = Hello("steam-required-held");
		ticketed["steamTicket"] = kTicket;
		const auto held = required.InspectInbound(501, Encode(ticketed), 7000.0);
		assert(held.forward && held.steamTicketQueued && held.steamId.empty() && held.steamVerdictHeld);
		Json gameplay{ { "type", "move" } };
		const auto dropped = required.InspectInbound(501, Encode(gameplay), 7000.1);
		assert(dropped.forward && dropped.consumed);
		const auto duplicate = required.InspectInbound(501, Encode(ticketed), 7000.1);
		assert(duplicate.forward && duplicate.consumed);
		const auto parkedHello = required.TakeParkedHello(501);
		assert(parkedHello.has_value() && Json::parse(std::string(parkedHello->begin(), parkedHello->end())).at("profileId").get<std::string>() == "steam-required-held");
		const auto parkedAgain = required.TakeParkedHello(501);
		assert(!parkedAgain.has_value());
		const bool releasedHold = required.ReleaseSteamVerdictHold(501);
		const bool releasedAgain = required.ReleaseSteamVerdictHold(501);
		assert(releasedHold && !releasedAgain);
		const auto released = required.InspectInbound(501, Encode(gameplay), 7000.2);
		assert(released.forward && !released.consumed);

		Json expiring = Hello("steam-required-expiry");
		expiring["steamTicket"] = kTicket;
		const auto admitted = required.InspectInbound(502, Encode(expiring), 7000.0);
		assert(admitted.forward && admitted.steamVerdictHeld);
		monotonic = 129.9;
		const auto early = required.TakeExpiredSteamVerdictHolds();
		assert(early.empty());
		monotonic = 130.0;
		const auto expired = required.TakeExpiredSteamVerdictHolds();
		assert(expired.size() == 1 && expired[0] == 502);

		Json closing = Hello("steam-required-closed");
		closing["steamTicket"] = kTicket;
		const auto closed = required.InspectInbound(503, Encode(closing), 7000.0);
		assert(closed.forward && closed.steamVerdictHeld);
		required.ConnectionClosed(503);
		const auto closedParked = required.TakeParkedHello(503);
		assert(!closedParked.has_value());
		const bool closedHold = required.ReleaseSteamVerdictHold(503);
		assert(!closedHold);

		IdentityGate queueFull(
			steamRequired,
			a_tokens,
			nullptr,
			{},
			nullptr,
			{},
			SteamIdentityHooks{
				.verifyTicket = [](std::uint32_t, std::string_view, std::string_view, std::string_view) { return false; },
				.monotonicNow = [&monotonic] { return monotonic; }
			});
		Json queuedOut = Hello("steam-required-queue-full");
		queuedOut["steamTicket"] = kTicket;
		const auto refusedQueue = queueFull.InspectInbound(504, Encode(queuedOut), 7000.0);
		assert(!refusedQueue.forward && refusedQueue.code == "steam_required");
		assert(!queueFull.IsValidated(504));
	}
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
	// (b) A full table of active locks refuses a new source, and its correct password too (fail closed).
	for (int index = 0; index < 4; ++index) {
		assert(attempt(kThirdAddress, "wrong").code == "password_locked");
	}
	assert(attempt(kThirdAddress, "letmein").code == "password_locked");
	assert(attempt(kFirstAddress, "letmein").code == "password_locked");
	assert(attempt(kSecondAddress, "letmein").code == "password_locked");

	clock += 61.0;
	assert(attempt(kFirstAddress, "letmein").forward);

	// (a) A full table of unlocked failures evaluates a new source normally and evicts the oldest unlocked entry.
	assert(attempt(kSecondAddress, "wrong").code == "password_invalid");
	clock += 1.0;
	assert(attempt(kFirstAddress, "wrong").code == "password_invalid");
	clock += 1.0;
	assert(attempt(kThirdAddress, "letmein").forward);
	assert(attempt(kThirdAddress, "wrong").code == "password_invalid");
	clock += 1.0;
	// Second was evicted as the oldest unlocked entry, so its earlier failure no longer counts toward the lock.
	assert(attempt(kSecondAddress, "wrong").code == "password_invalid");
	assert(attempt(kSecondAddress, "wrong").code == "password_invalid");
	assert(attempt(kSecondAddress, "letmein").forward);

	clock += 61.0;
	assert(attempt(kFirstAddress, "letmein").forward);
}

// Slots 0..6 are sources A..G; both gates in TestLockedSourcesAreNeverEvicted use maximumTracked = 2 and threshold = 3.
template <typename Attempt>
void RunFullLockoutTableScenario(const Attempt& a_attempt)
{
	for (int index = 0; index < 3; ++index) {
		assert(a_attempt(0, "wrong").code == "password_invalid");
	}
	assert(a_attempt(0, "letmein").code == "password_locked");

	// B is the only unlocked entry when C needs a slot, so C evicts B and never A.
	assert(a_attempt(1, "wrong").code == "password_invalid");
	assert(a_attempt(2, "wrong").code == "password_invalid");
	assert(a_attempt(0, "letmein").code == "password_locked");

	// B was evicted, so two more wrong guesses leave it unlocked and its correct password is admitted.
	assert(a_attempt(1, "wrong").code == "password_invalid");
	assert(a_attempt(1, "wrong").code == "password_invalid");
	assert(a_attempt(1, "letmein").forward);

	// Each new source evicts the previous unlocked one; F is tracked through its three wrong guesses and locks.
	assert(a_attempt(3, "wrong").code == "password_invalid");
	assert(a_attempt(4, "wrong").code == "password_invalid");
	for (int index = 0; index < 3; ++index) {
		assert(a_attempt(5, "wrong").code == "password_invalid");
	}
	assert(a_attempt(5, "letmein").code == "password_locked");
	assert(a_attempt(0, "letmein").code == "password_locked");

	// The table now holds two active locks (A and F). A new source is refused, even with the correct password, and evicts neither lock.
	for (int index = 0; index < 4; ++index) {
		assert(a_attempt(6, "wrong").code == "password_locked");
	}
	assert(a_attempt(0, "letmein").code == "password_locked");
	assert(a_attempt(5, "letmein").code == "password_locked");
	assert(a_attempt(6, "letmein").code == "password_locked");
}

void TestLockedSourcesAreNeverEvicted(F4T::Identity::Requirements a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	a_requirements.serverPasswordSha256 = F4T::ContentManifest::Sha256Hex("letmein");
	double clock = 300.0;
	const auto options = F4T::SteamAdmission::LockoutOptions{ .threshold = 3, .cooldownSeconds = 60.0, .maximumTracked = 2 };
	constexpr const char* kSteamIds[] = {
		"76561198000000101", "76561198000000102", "76561198000000103", "76561198000000104",
		"76561198000000105", "76561198000000106", "76561198000000107"
	};

	std::map<std::uint32_t, std::uint32_t> remoteAddresses;
	IdentityGate directGate(
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
			.passwordLockout = options,
			.monotonicNow = [&clock] { return clock; }
		});

	std::map<std::uint32_t, VerifiedSteamId> verified;
	IdentityGate steamGate(
		a_requirements,
		a_tokens,
		nullptr,
		{},
		nullptr,
		[](std::string_view) { return false; },
		SteamIdentityHooks{
			.resolve = [&verified](std::uint32_t a_connection, std::string_view) -> std::optional<VerifiedSteamId> {
				const auto found = verified.find(a_connection);
				return found == verified.end() ? std::nullopt : std::optional{ found->second };
			},
			.banLookup = {},
			.verifyTicket = {},
			.passwordLockout = options,
			.monotonicNow = [&clock] { return clock; }
		});

	std::uint32_t connection = 1300;
	const auto directAttempt = [&](int a_slot, std::string a_password) {
		const auto id = connection++;
		remoteAddresses[id] = 0x0A000101u + static_cast<std::uint32_t>(a_slot);
		Json hello = Hello("locked-direct-" + std::to_string(id));
		hello["serverPassword"] = a_password;
		return directGate.InspectInbound(id, Encode(hello), 9000.0);
	};
	const auto steamAttempt = [&](int a_slot, std::string a_password) {
		const auto id = connection++;
		verified[id] = VerifiedSteamId{ kSteamIds[a_slot], SteamIdSource::SteamTunnel, true };
		Json hello = Hello("locked-steam-" + std::to_string(id));
		hello["serverPassword"] = a_password;
		return steamGate.InspectInbound(id, Encode(hello), 9000.0);
	};

	RunFullLockoutTableScenario(directAttempt);
	RunFullLockoutTableScenario(steamAttempt);
}

void TestSteamHostSourceDecidesTheAfkExemption(F4T::Identity::Requirements a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::Afk::Evaluate;
	using CommonwealthOnline::Server::Afk::Exemption;
	using CommonwealthOnline::Server::Afk::Verdict;
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	constexpr auto kGuest = "76561198000000042";
	constexpr auto kHostSelf = "76561198000000001";
	std::map<std::uint32_t, VerifiedSteamId> verified{
		{ 500, { kHostSelf, SteamIdSource::HostSelf } },
		{ 501, { kGuest, SteamIdSource::SteamTunnel } },
		{ 502, { kHostSelf, SteamIdSource::HostSelf } }
	};
	IdentityGate gate(
		a_requirements,
		a_tokens,
		nullptr,
		{},
		nullptr,
		[](std::string_view) { return false; },
		SteamIdentityHooks{
			.resolve = [&verified](std::uint32_t a_connection, std::string_view) -> std::optional<VerifiedSteamId> {
				const auto found = verified.find(a_connection);
				return found == verified.end() ? std::nullopt : std::optional{ found->second };
			},
			.remoteIpv4 = {},
			.banLookup = {},
			.verifyTicket = {},
			.passwordLockout = {},
			.monotonicNow = {}
		});

	// loopback must not decide the host: only the hello identity separates the host from a tunnel guest
	assert(gate.InspectInbound(500, Encode(Hello("steam-host-000001")), 8000.0).forward);
	assert(gate.InspectInbound(501, Encode(Hello("steam-guest-00001")), 8000.1).forward);
	const auto hostSource = gate.SteamSourceOf(500);
	const auto guestSource = gate.SteamSourceOf(501);
	assert(hostSource == SteamIdSource::HostSelf);
	assert(guestSource == SteamIdSource::SteamTunnel);

	// the runtime must mark a connection as the Steam host only for HostSelf
	const bool hostIsSteamHost = hostSource == SteamIdSource::HostSelf;
	const bool guestIsSteamHost = guestSource == SteamIdSource::HostSelf;
	assert(hostIsSteamHost && !guestIsSteamHost);

	std::optional<double> hostClock{ 8000.0 };
	std::optional<double> guestClock{ 8000.0 };
	assert(Evaluate(hostClock, Exemption{}, 8901.0, true, hostIsSteamHost) == Verdict::none);
	assert(Evaluate(guestClock, Exemption{}, 8901.0, true, guestIsSteamHost) == Verdict::kick);

	// a refused hello must leave no record, so a reused connection id cannot inherit the host identity
	Json spoofed = Hello("steam-host-spoof");
	spoofed["steamId"] = kGuest;
	assert(gate.InspectInbound(502, Encode(spoofed), 8002.0).code == "steam_identity_mismatch");
	assert(!gate.SteamSourceOf(502).has_value());
	verified.erase(502);
	assert(gate.InspectInbound(502, Encode(Hello("steam-guest-00002")), 8003.0).forward);
	const auto reusedSource = gate.SteamSourceOf(502);
	assert(!reusedSource.has_value());
	std::optional<double> reusedClock{ 8003.0 };
	assert(Evaluate(reusedClock, Exemption{}, 8904.0, true, reusedSource == SteamIdSource::HostSelf) == Verdict::kick);

	// closing the connection must drop the record too
	gate.ConnectionClosed(500);
	assert(!gate.SteamSourceOf(500).has_value());
}

void TestDirectJoinFromBannedSteamHistoryIsRefused(const F4T::Identity::Requirements& a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	constexpr auto kGuest = "76561198000000042";
	constexpr auto kBanned = "76561198000000666";
	IdentityGate gate(
		a_requirements,
		a_tokens,
		nullptr,
		{},
		nullptr,
		[](std::string_view) { return false; },
		SteamIdentityHooks{
			.resolve = [](std::uint32_t a_connection, std::string_view) -> std::optional<VerifiedSteamId> {
				if (a_connection != 401) return std::nullopt;
				return VerifiedSteamId{ kGuest, SteamIdSource::SteamTunnel };
			},
			.banLookup = [](std::string_view a_steamId) -> std::optional<std::string> {
				if (a_steamId == kBanned) return std::string{ "cheating" };
				return std::nullopt;
			},
			.verifyTicket = {},
			.passwordLockout = {},
			.monotonicNow = {},
			.profileSteamIds = [](std::string_view a_profileId) -> std::vector<std::string> {
				if (a_profileId == "history-banned-01") return { kBanned };
				return {};
			}
		});

	const auto refused = gate.InspectInbound(400, Encode(Hello("history-banned-01")), 6000.0);
	assert(!refused.forward);
	assert(refused.code == "steam_banned");
	assert(refused.reason == "cheating");

	const auto clean = gate.InspectInbound(402, Encode(Hello("history-clean-01")), 6000.1);
	assert(clean.forward);

	const auto verified = gate.InspectInbound(401, Encode(Hello("history-banned-01")), 6000.2);
	assert(verified.forward);
	assert(verified.steamId == kGuest);
}

void TestVerifiedSteamPassesRequiredGate(const F4T::Identity::Requirements& a_requirements, FakeTokenService& a_tokens)
{
	using CommonwealthOnline::Server::IdentityGate;
	using CommonwealthOnline::Server::SteamIdentityHooks;
	using CommonwealthOnline::Server::VerifiedSteamId;

	constexpr auto kGuest = "76561198000000042";
	constexpr auto kHost = "76561198000000043";
	auto steamRequired = a_requirements;
	steamRequired.requireSteamIdentity = true;
	IdentityGate gate(
		steamRequired,
		a_tokens,
		nullptr,
		{},
		nullptr,
		{},
		SteamIdentityHooks{
			.resolve = [&](std::uint32_t a_connection, std::string_view) -> std::optional<VerifiedSteamId> {
				if (a_connection == 600) return VerifiedSteamId{ kGuest, SteamIdSource::SteamTunnel };
				if (a_connection == 601) return VerifiedSteamId{ kHost, SteamIdSource::HostSelf };
				return std::nullopt;
			}
		});

	const auto tunnel = gate.InspectInbound(600, Encode(Hello("steam-required-tunnel")), 8000.0);
	assert(tunnel.forward && !tunnel.consumed && !tunnel.steamVerdictHeld && tunnel.steamId == kGuest);
	const auto host = gate.InspectInbound(601, Encode(Hello("steam-required-host")), 8000.0);
	assert(host.forward && !host.consumed && !host.steamVerdictHeld && host.steamId == kHost);
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
	TestVerifiedSteamPassesRequiredGate(requirements, tokens);
	TestSteamPasswordLockout(requirements, tokens);
	TestDirectPeerPasswordLockout(requirements, tokens);
	TestDirectJoinFromBannedSteamHistoryIsRefused(requirements, tokens);
	TestLockedSourcesAreNeverEvicted(requirements, tokens);
	TestSteamHostSourceDecidesTheAfkExemption(requirements, tokens);
	return 0;
}
