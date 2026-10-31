#include "IdentityGate.h"

#include "SaveAttestationAuthority.h"

#include "F4TSteamId.h"

#include <nlohmann/json.hpp>

#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr double kAttestationWindowSeconds = 60.0;
		constexpr std::uint32_t kMaximumAttestationsPerWindow = 64;
		constexpr std::uint32_t kLoopbackNetwork = 127;

		std::optional<Json> DecodeObject(std::span<const std::uint8_t> a_payload)
		{
			if (a_payload.empty()) {
				return std::nullopt;
			}
			try {
				const auto text = std::string{
					reinterpret_cast<const char*>(a_payload.data()),
					a_payload.size()
				};
				auto packet = Json::parse(text);
				return packet.is_object() ? std::optional{ std::move(packet) } : std::nullopt;
			} catch (...) {
				return std::nullopt;
			}
		}

		std::optional<std::string> PacketType(const Json& a_packet)
		{
			const auto found = a_packet.find("type");
			return found != a_packet.end() && found->is_string()
				? std::optional{ found->get<std::string>() }
				: std::nullopt;
		}

		std::vector<std::uint8_t> JsonBytes(const Json& a_packet)
		{
			const auto text = a_packet.dump();
			return { text.begin(), text.end() };
		}

		std::string ClaimedString(const Json& a_packet, std::string_view a_name, std::size_t a_maximum, bool a_digitsOnly)
		{
			const auto found = a_packet.find(a_name);
			if (found == a_packet.end() || !found->is_string()) {
				return {};
			}
			const auto value = found->get<std::string>();
			if (value.empty() || value.size() > a_maximum) {
				return {};
			}
			for (const unsigned char ch : value) {
				if (ch < 0x20 || ch == 0x7f) {
					return {};
				}
				if (a_digitsOnly && (ch < '0' || ch > '9')) {
					return {};
				}
			}
			return value;
		}

		bool FixedTimeStringEquals(std::string_view a_left, std::string_view a_right) noexcept
		{
			if (a_left.size() != a_right.size()) {
				return false;
			}
			volatile std::uint8_t difference = 0;
			for (std::size_t index = 0; index < a_left.size(); ++index) {
				difference = static_cast<std::uint8_t>(difference |
					(static_cast<std::uint8_t>(a_left[index]) ^ static_cast<std::uint8_t>(a_right[index])));
			}
			return difference == 0;
		}

		bool IsHex(std::string_view a_value) noexcept
		{
			for (const char ch : a_value) {
				if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))) {
					return false;
				}
			}
			return true;
		}

		std::string ToLowerHex(std::string a_value)
		{
			for (auto& ch : a_value) {
				if (ch >= 'A' && ch <= 'F') {
					ch = static_cast<char>(ch - 'A' + 'a');
				}
			}
			return a_value;
		}

		bool IsCleanSaveName(std::string_view a_value) noexcept
		{
			for (const unsigned char ch : a_value) {
				if (ch < 0x20 || ch == 0x7f) {
					return false;
				}
			}
			return true;
		}

		bool HasCapability(const std::vector<std::string>& a_capabilities, std::string_view a_capability)
		{
			for (const auto& capability : a_capabilities) {
				if (capability == a_capability) {
					return true;
				}
			}
			return false;
		}

		std::optional<std::string> PresentedProfileSecret(const Json& a_packet)
		{
			const auto found = a_packet.find("profileSecret");
			if (found == a_packet.end() || !found->is_string()) {
				return std::nullopt;
			}
			const auto& value = found->get_ref<const std::string&>();
			if (value.empty() || value.size() > 128) {
				return std::nullopt;
			}
			return value;
		}
	}

	IdentityGate::IdentityGate(
		F4T::Identity::Requirements a_requirements,
		F4T::Identity::TokenService& a_tokens,
		F4T::Identity::ResumeCredentialStore* a_resumeStore,
		ProfileBanLookup a_profileBanLookup,
		SaveAttestationAuthority* a_saveAuthority,
		ProfileAdminLookup a_profileAdminLookup,
		SteamIdentityHooks a_steamIdentity) :
		_requirements(a_requirements),
		_identities(std::move(a_requirements), a_tokens, a_resumeStore),
		_profileBanLookup(std::move(a_profileBanLookup)),
		_saveAuthority(a_saveAuthority),
		_profileAdminLookup(std::move(a_profileAdminLookup)),
		_steamIdentity(std::move(a_steamIdentity)),
		_passwordLockout(_steamIdentity.passwordLockout),
		_directPasswordLockout(_steamIdentity.passwordLockout)
	{}

	double IdentityGate::MonotonicNow() const
	{
		if (_steamIdentity.monotonicNow) return _steamIdentity.monotonicNow();
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	IdentityIngressDecision IdentityGate::InspectHello(
		std::uint32_t a_connectionId,
		const nlohmann::json& a_packet,
		double a_nowUnixSeconds,
		bool a_capacityAvailable)
	{
		const auto key = ConnectionKey(a_connectionId);
		auto claimedSteamId = ClaimedString(a_packet, "steamId", F4T::SteamId::kDecimalLength, true);
		if (!F4T::SteamId::ParseIndividual(claimedSteamId)) {
			claimedSteamId.clear();
		}
		std::optional<VerifiedSteamId> verifiedSteam;
		if (_steamIdentity.resolve) {
			verifiedSteam = _steamIdentity.resolve(a_connectionId, claimedSteamId);
		}
		_helloSteam = verifiedSteam;
		std::optional<std::uint32_t> directJoinIpv4;
		if (verifiedSteam) {
			if (auto refused = CheckSteamPassword(a_connectionId, *verifiedSteam)) return std::move(*refused);
		} else if (_steamIdentity.remoteIpv4) {
			const auto remoteIpv4 = _steamIdentity.remoteIpv4(a_connectionId);
			if (remoteIpv4 && (*remoteIpv4 >> 24) != kLoopbackNetwork) {
				directJoinIpv4 = remoteIpv4;
				if (auto refused = CheckDirectJoinPassword(a_connectionId, *remoteIpv4)) return std::move(*refused);
			}
		}
		const auto preflight = F4T::Identity::NormalizeHello(a_packet, _requirements);
		const auto outcomeCode = preflight.accepted ? std::string_view{} : std::string_view{ preflight.code };
		if (verifiedSteam) {
			RecordSteamPasswordOutcome(a_connectionId, *verifiedSteam, outcomeCode);
		} else if (directJoinIpv4) {
			RecordDirectJoinPasswordOutcome(a_connectionId, *directJoinIpv4, outcomeCode);
		}
		if (!preflight.accepted || !preflight.identity) {
			auto decision = IdentityIngressDecision{ .forward = false, .code = preflight.code, .reason = preflight.reason, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = preflight.requiredModpack };
			decision.modpackFingerprint = preflight.modpackFingerprint;
			decision.modpackChunks = preflight.modpackChunks;
			decision.modpackName = preflight.modpackName;
			return decision;
		}
		if (!HasCapability(preflight.identity->capabilities, F4T::Identity::SaveAttestationCapability)) {
			return IdentityIngressDecision{
				.forward = false,
				.code = "save_attestation_required",
				.reason = "This server requires clients that report multiplayer save attestation. Update Commonwealth Online.",
				.profileId = {},
				.evictConnectionId = std::nullopt,
				.requiredModpack = {}
			};
		}
		if (_profileBanLookup) {
			if (const auto banReason = _profileBanLookup(preflight.identity->profileId)) {
				return IdentityIngressDecision{ .forward = false, .code = "profile_banned", .reason = *banReason, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
			}
		}
		if (verifiedSteam && !claimedSteamId.empty() && claimedSteamId != verifiedSteam->steamId) {
			std::cerr << "Refused hello on connection " << a_connectionId << ": the claimed Steam ID does not match the "
			          << SteamIdSourceName(verifiedSteam->source) << " Steam ID.\n";
			return IdentityIngressDecision{ .forward = false, .code = "steam_identity_mismatch", .reason = "The Steam ID sent by this client does not match its Steam account.", .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
		}
		if (verifiedSteam && _steamIdentity.banLookup) {
			if (const auto banReason = _steamIdentity.banLookup(verifiedSteam->steamId)) {
				return IdentityIngressDecision{ .forward = false, .code = "steam_banned", .reason = *banReason, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
			}
		}
		const auto profileId = preflight.identity->profileId;
		const auto presentedSecret = PresentedProfileSecret(a_packet);
		const bool secretVerified = SecretMatches(profileId, presentedSecret);
		const bool isAdmin = _profileAdminLookup && _profileAdminLookup(profileId) && secretVerified;
		std::vector<F4T::SaveProfileLogic::AttestedSave> recorded;
		bool profileKnown = true;
		bool claimReadmission = false;
		try {
			if (_saveAuthority != nullptr) {
				recorded = _saveAuthority->GetSaveAttestations(profileId);
				profileKnown = _saveAuthority->IsKnownProfile(profileId);
			}
			claimReadmission = !_claimReadmitted.contains(profileId) && _identities.AwaitingConfirmation(profileId, a_nowUnixSeconds);
		} catch (const std::exception& error) {
			return IdentityIngressDecision{
				.forward = false,
				.code = "save_attestation_unavailable",
				.reason = std::string{ "The server could not read this profile's multiplayer save history: " } + error.what(),
				.profileId = {},
				.evictConnectionId = std::nullopt,
				.requiredModpack = {}
			};
		}
		const auto admission = F4T::SaveProfileLogic::AdmitAttestedSaveClaim(
			recorded,
			preflight.identity->saveAttestation,
			F4T::SaveProfileLogic::SaveClaimStanding{ isAdmin, secretVerified, profileKnown, claimReadmission });
		if (!admission.allowed) {
			return IdentityIngressDecision{ .forward = false, .code = admission.code, .reason = admission.reason, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
		}

		const auto result = _identities.ValidateAndReserve(key, a_packet, a_nowUnixSeconds, a_capacityAvailable);
		if (!result.accepted || !result.identity) {
			auto decision = IdentityIngressDecision{ .forward = false, .code = result.code, .reason = result.reason, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = result.requiredModpack };
			decision.modpackFingerprint = result.modpackFingerprint;
			decision.modpackChunks = result.modpackChunks;
			decision.modpackName = result.modpackName;
			return decision;
		}
		if (admission.recordClaim && _saveAuthority != nullptr && preflight.identity->saveAttestation) {
			try {
				(void)_saveAuthority->RecordSaveAttestation(profileId, *preflight.identity->saveAttestation);
			} catch (const std::exception& error) {
				_identities.ConnectionClosed(key);
				return IdentityIngressDecision{
					.forward = false,
					.code = "save_attestation_unavailable",
					.reason = std::string{ "The server could not store this profile's multiplayer save history: " } + error.what(),
					.profileId = {},
					.evictConnectionId = std::nullopt,
					.requiredModpack = {}
				};
			}
			if (!secretVerified) {
				std::cerr << "Recorded the first multiplayer save claim for profile " << profileId << " without a profile secret.\n";
			}
		}

		IdentityIngressDecision accepted{ .forward = true, .code = {}, .reason = {}, .profileId = profileId, .evictConnectionId = std::nullopt, .requiredModpack = {} };
		accepted.isAdmin = isAdmin;
		if (verifiedSteam) {
			accepted.steamId = verifiedSteam->steamId;
			accepted.steamIdSource = verifiedSteam->source;
		}
		if (!verifiedSteam && _steamIdentity.verifyTicket) {
			const auto ticket = a_packet.find("steamTicket");
			if (ticket != a_packet.end() && ticket->is_string() && F4T::SteamId::IsTicketHex(ticket->get_ref<const std::string&>())) {
				accepted.steamTicketQueued = _steamIdentity.verifyTicket(a_connectionId, ticket->get_ref<const std::string&>(), claimedSteamId, profileId);
			}
		}
		accepted.claimedSteamId = std::move(claimedSteamId);
		accepted.nickname = ClaimedString(a_packet, "nickname", 64, false);
		std::optional<std::string> issuedSecret;
		if (_saveAuthority != nullptr) {
			try {
				issuedSecret = _saveAuthority->EnsureProfileSecret(profileId);
			} catch (const std::exception& error) {
				_identities.ConnectionClosed(key);
				return IdentityIngressDecision{
					.forward = false,
					.code = "save_attestation_unavailable",
					.reason = std::string{ "The server could not issue a multiplayer save credential: " } + error.what(),
					.profileId = {},
					.evictConnectionId = std::nullopt,
					.requiredModpack = {}
				};
			}
		}
		_profileByConnection[a_connectionId] = profileId;
		if (issuedSecret) {
			_secretByConnection[a_connectionId] = *issuedSecret;
		}
		if (isAdmin) {
			_adminConnections.insert(a_connectionId);
		}
		if (admission.readmitted) {
			_claimReadmitted.insert(profileId);
		}
		if (result.evictConnectionKey) {
			accepted.evictConnectionId = ConnectionIdFromKey(*result.evictConnectionKey);
		}
		return accepted;
	}

	IdentityIngressDecision IdentityGate::InspectInbound(
		std::uint32_t a_connectionId,
		std::span<const std::uint8_t> a_payload,
		double a_nowUnixSeconds,
		bool a_capacityAvailable)
	{
		const auto packet = DecodeObject(a_payload);
		if (!packet) {
			return IdentityIngressDecision{ .forward = true, .code = {}, .reason = {}, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
		}

		const auto type = PacketType(*packet);
		const auto key = ConnectionKey(a_connectionId);
		if (type && *type == "hello") {
			_helloSteam.reset();
			auto decision = InspectHello(a_connectionId, *packet, a_nowUnixSeconds, a_capacityAvailable);
			if (_helloSteam) {
				std::cerr << "Steam admission: connection " << a_connectionId << " (" << SteamIdSourceName(_helloSteam->source) << ") hello "
				          << (decision.forward ? std::string{ "accepted" } : "refused: " + decision.code) << '\n';
			}
			return decision;
		}

		if (type && *type == "saveAttested") {
			return InspectSaveAttestation(a_connectionId, *packet, a_nowUnixSeconds);
		}

		if (type && *type == "keepAlive") {
			const auto ack = packet->find("sessionReadyAck");
			if (ack != packet->end() && ack->is_boolean() && ack->get<bool>()) {
				if (!_identities.IsValidated(key)) {
					return IdentityIngressDecision{
						.forward = false,
						.code = "handshake_required",
						.reason = "Protocol V2 hello and identity validation must complete before sessionReady acknowledgment.",
						.profileId = {},
						.evictConnectionId = std::nullopt,
						.requiredModpack = {}
					};
				}

				if (const auto pending = _identities.PendingForReady(key)) {
					const auto proof = packet->find("resumeTokenAck");
					if (proof == packet->end() || !proof->is_string() ||
						!FixedTimeStringEquals(proof->get_ref<const std::string&>(), pending->resumeToken)) {
						return IdentityIngressDecision{
							.forward = false,
							.code = "invalid_resume_ack",
							.reason = "sessionReady acknowledgment must echo the resume token delivered by this session.",
							.profileId = {},
							.evictConnectionId = std::nullopt,
							.requiredModpack = {}
						};
					}
				}

				if (!_identities.ConfirmReady(key)) {
					return IdentityIngressDecision{
						.forward = false,
						.code = "resume_storage_failed",
						.reason = "The server could not confirm the delivered resume credential. Reconnect with your previous token.",
						.profileId = {},
						.evictConnectionId = std::nullopt,
						.requiredModpack = {}
					};
				}
				if (const auto profile = _profileByConnection.find(a_connectionId); profile != _profileByConnection.end()) {
					_claimReadmitted.erase(profile->second);
				}
			}
			return IdentityIngressDecision{ .forward = true, .code = {}, .reason = {}, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
		}

		if (!_identities.IsValidated(key)) {
			return IdentityIngressDecision{
				.forward = false,
				.code = "handshake_required",
				.reason = "Protocol V2 hello and identity validation must complete before gameplay.",
				.profileId = {},
				.evictConnectionId = std::nullopt,
				.requiredModpack = {}
			};
		}
		return IdentityIngressDecision{ .forward = true, .code = {}, .reason = {}, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {} };
	}

	IdentityIngressDecision IdentityGate::RejectAttestation(std::string a_code, std::string a_reason)
	{
		++_rejectedAttestations;
		IdentityIngressDecision rejected{
			.forward = true,
			.code = std::move(a_code),
			.reason = std::move(a_reason),
			.profileId = {},
			.evictConnectionId = std::nullopt,
			.requiredModpack = {},
			.consumed = true,
			.rejected = true
		};
		rejected.response = Json{
			{ "type", "saveAttestedRejected" },
			{ "code", rejected.code },
			{ "reason", rejected.reason }
		};
		return rejected;
	}

	IdentityIngressDecision IdentityGate::InspectSaveAttestation(
		std::uint32_t a_connectionId,
		const nlohmann::json& a_packet,
		double a_nowUnixSeconds)
	{
		if (!_identities.IsValidated(ConnectionKey(a_connectionId))) {
			return IdentityIngressDecision{
				.forward = false,
				.code = "handshake_required",
				.reason = "Protocol V2 hello and identity validation must complete before save attestation.",
				.profileId = {},
				.evictConnectionId = std::nullopt,
				.requiredModpack = {}
			};
		}

		IdentityIngressDecision consumed{ .forward = true, .code = {}, .reason = {}, .profileId = {}, .evictConnectionId = std::nullopt, .requiredModpack = {}, .consumed = true };
		const auto profile = _profileByConnection.find(a_connectionId);
		if (profile == _profileByConnection.end()) {
			return consumed;
		}
		const auto expected = _secretByConnection.find(a_connectionId);
		const auto presented = PresentedProfileSecret(a_packet);
		if (_saveAuthority == nullptr || expected == _secretByConnection.end() || !presented ||
			!FixedTimeStringEquals(*presented, expected->second)) {
			return RejectAttestation(
				"save_attestation_unauthenticated",
				"saveAttested must carry the profile secret this session was issued.");
		}

		const auto saveFile = a_packet.find("saveFile");
		const auto sha256 = a_packet.find("sha256");
		if (saveFile == a_packet.end() || !saveFile->is_string() ||
			sha256 == a_packet.end() || !sha256->is_string()) {
			return consumed;
		}

		const auto saveName = saveFile->get<std::string>();
		const auto hash = sha256->get<std::string>();
		if (saveName.empty() || saveName.size() > 256 || !IsCleanSaveName(saveName) ||
			hash.size() != 64 || !IsHex(hash)) {
			return consumed;
		}

		F4T::SaveProfileLogic::AttestedSave attested{ saveName, ToLowerHex(hash), 0 };
		try {
			const auto history = _saveAuthority->GetSaveAttestations(profile->second);
			for (const auto& recorded : history) {
				if (F4T::SaveProfileLogic::SameAttestedSave(recorded, attested)) {
					consumed.response = Json{
						{ "type", "saveAttestedAck" },
						{ "saveFile", attested.saveFile },
						{ "sha256", attested.sha256Hex },
						{ "recorded", false }
					};
					return consumed;
				}
			}
			if (F4T::SaveProfileLogic::ContradictsRecordedSave(history, attested)) {
				return RejectAttestation(
					std::string{ F4T::SaveProfileLogic::SaveNotMultiplayerCode },
					"This save name is already recorded for this profile with a different hash; the recorded attestation was kept.");
			}
		} catch (const std::exception& error) {
			return RejectAttestation(
				"save_attestation_unavailable",
				std::string{ "The server could not read this profile's multiplayer save history: " } + error.what());
		}

		auto& window = _attestationWindows[a_connectionId];
		if (a_nowUnixSeconds - window.windowStart >= kAttestationWindowSeconds || window.windowStart <= 0.0) {
			window.windowStart = a_nowUnixSeconds;
			window.count = 0;
		}
		if (window.count >= kMaximumAttestationsPerWindow) {
			++_rateLimitedAttestations;
			return RejectAttestation(
				"save_attestation_rate_limited",
				"This connection announced too many save attestations; the record was not stored.");
		}
		++window.count;

		try {
			(void)_saveAuthority->RecordSaveAttestation(profile->second, attested);
		} catch (const std::exception& error) {
			return RejectAttestation(
				"save_attestation_unavailable",
				std::string{ "The server could not store this save attestation: " } + error.what());
		}
		consumed.response = Json{
			{ "type", "saveAttestedAck" },
			{ "saveFile", attested.saveFile },
			{ "sha256", attested.sha256Hex },
			{ "recorded", true }
		};
		return consumed;
	}

	bool IdentityGate::SecretMatches(std::string_view a_profileId, const std::optional<std::string>& a_presentedSecret) const
	{
		if (!a_presentedSecret || _saveAuthority == nullptr) {
			return false;
		}
		std::string expected;
		try {
			expected = _saveAuthority->ProfileSecret(a_profileId);
		} catch (const std::exception&) {
			return false;
		}
		return FixedTimeStringEquals(*a_presentedSecret, expected);
	}

	bool IdentityGate::EnrichSessionReady(OutboundFrame& a_frame)
	{
		if (a_frame.payload.empty()) {
			return false;
		}
		auto packet = DecodeObject(a_frame.payload);
		if (!packet || PacketType(*packet).value_or(std::string{}) != "sessionReady") {
			return false;
		}

		const auto pending = _identities.PendingForReady(ConnectionKey(a_frame.connectionId));
		if (!pending) {
			return false;
		}
		(*packet)["profileId"] = pending->profileId;
		(*packet)["clientVersion"] = pending->clientVersion;
		(*packet)["falloutRuntimeVersion"] = pending->falloutRuntimeVersion;
		(*packet)["contentManifestFingerprint"] = pending->contentManifestFingerprint;
		(*packet)["resumeToken"] = pending->resumeToken;
		(*packet)["resumeExpiresAt"] = pending->resumeExpiresAt;
		(*packet)["resumeAckRequired"] = true;
		(*packet)["resumeAckViaKeepAlive"] = true;
		if (const auto secret = _secretByConnection.find(a_frame.connectionId); secret != _secretByConnection.end()) {
			(*packet)["profileSecret"] = secret->second;
			_secretDelivered.insert(a_frame.connectionId);
		}
		if (_adminConnections.contains(a_frame.connectionId)) {
			(*packet)["multiplayerSaveAdmin"] = true;
		}
		a_frame.payload = JsonBytes(*packet);
		return true;
	}

	bool IdentityGate::CommitReady(std::uint32_t a_connectionId)
	{
		return _identities.CommitReady(ConnectionKey(a_connectionId));
	}

	void IdentityGate::ConnectionClosed(std::uint32_t a_connectionId)
	{
		_identities.ConnectionClosed(ConnectionKey(a_connectionId));
		if (const auto profile = _profileByConnection.find(a_connectionId); profile != _profileByConnection.end() && !_secretDelivered.contains(a_connectionId)) {
			_claimReadmitted.erase(profile->second);
		}
		_secretDelivered.erase(a_connectionId);
		_profileByConnection.erase(a_connectionId);
		_secretByConnection.erase(a_connectionId);
		_attestationWindows.erase(a_connectionId);
		_adminConnections.erase(a_connectionId);
	}

	bool IdentityGate::IsValidated(std::uint32_t a_connectionId) const
	{
		return _identities.IsValidated(ConnectionKey(a_connectionId));
	}

	bool IdentityGate::IsAdminConnection(std::uint32_t a_connectionId) const
	{
		return _adminConnections.contains(a_connectionId);
	}

	bool IdentityGate::TryResetResumeCredential(std::string_view a_profileId, std::string& a_reason)
	{
		const bool reset = _identities.TryResetResumeCredential(a_profileId, a_reason);
		if (reset) {
			_claimReadmitted.erase(std::string{ a_profileId });
		}
		return reset;
	}

	void IdentityGate::SetServerPasswordSha256(std::optional<std::string> a_passwordSha256)
	{
		_requirements.serverPasswordSha256 = std::move(a_passwordSha256);
	}

	void IdentityGate::SetResumeLifetimeSeconds(double a_seconds)
	{
		if (a_seconds > 0.0) {
			_requirements.resumeLifetimeSeconds = a_seconds;
		}
	}

	std::size_t IdentityGate::RejectedAttestationCount() const
	{
		return _rejectedAttestations;
	}

	std::size_t IdentityGate::RateLimitedAttestationCount() const
	{
		return _rateLimitedAttestations;
	}

	std::string IdentityGate::ConnectionKey(std::uint32_t a_connectionId)
	{
		return std::to_string(a_connectionId);
	}

	std::optional<std::uint32_t> IdentityGate::ConnectionIdFromKey(std::string_view a_connectionKey)
	{
		std::uint32_t parsed = 0;
		const auto result = std::from_chars(
			a_connectionKey.data(),
			a_connectionKey.data() + a_connectionKey.size(),
			parsed);
		if (result.ec != std::errc{} || result.ptr != a_connectionKey.data() + a_connectionKey.size()) {
			return std::nullopt;
		}
		return parsed;
	}
}
