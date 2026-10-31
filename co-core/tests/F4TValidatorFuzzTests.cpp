#include "F4TValidatorFuzzHarness.h"

#include "F4TAuthoritativePacketValidation.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "F4TNetworkValidation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace ValidatorFuzz;

	using Session = F4T::GnsProtocolAdapter::Session;
	using ReadyIdentity = F4T::GnsProtocolAdapter::ReadyIdentity;
	using EnvelopeResult = F4T::GnsSnapshotEnvelope::DecodeResult;

	const std::string kManifestFingerprint(64, '1');

	bool GameHour(const Json& a_value) { return InRange(a_value, 0.0, 24.0); }
	bool GameDays(const Json& a_value) { return InRange(a_value, 0.0, 10000000.0); }
	bool Damage(const Json& a_value) { return InRange(a_value, 0.000001, 10000.0); }

	bool NpcEntry(const Json& a_value)
	{
		return a_value.is_object() &&
		       OnlyKeys(a_value, { "npcId", "sourceFormId", "baseFormId", "cellId", "worldspaceId", "x", "y", "z",
		                           "angleZ", "isDead", "isMoving", "isSprinting", "isSneaking", "movementSpeed" }) &&
		       Field(a_value, "npcId", Positive32) &&
		       Field(a_value, "sourceFormId", HexNonZero, true) &&
		       Field(a_value, "baseFormId", HexNonZero) &&
		       Field(a_value, "cellId", HexNonZero, true) &&
		       Field(a_value, "worldspaceId", HexOrEmpty, true) &&
		       AllFields(a_value, { "x", "y", "z", "angleZ" }, Coordinate, true) &&
		       AllFields(a_value, { "isDead", "isMoving", "isSprinting", "isSneaking" }, Flag, false) &&
		       Field(a_value, "movementSpeed", Speed);
	}

	bool WorldStateInvariant(const Json&, const Json& a_output)
	{
		return a_output.is_object() &&
		       OnlyKeys(a_output, { "type", "gameHour", "gameDaysPassed", "weatherFormId", "timeSync", "clientTime" }) &&
		       TypeIs(a_output, "worldState") &&
		       Field(a_output, "gameHour", GameHour) &&
		       Field(a_output, "gameDaysPassed", GameDays) &&
		       Field(a_output, "weatherFormId", HexOrEmpty) &&
		       Field(a_output, "timeSync", Flag) &&
		       Field(a_output, "clientTime", ClientTime);
	}

	bool NpcStateInvariant(const Json&, const Json& a_output)
	{
		return a_output.is_object() &&
		       OnlyKeys(a_output, { "type", "npcs", "fullReplace", "clientTime", "authorityEpoch", "authorityCellId", "authorityWorldspaceId" }) &&
		       TypeIs(a_output, "npcState") &&
		       Field(a_output, "npcs", [](const Json& a_value) { return Elements(a_value, 64, NpcEntry); }, true) &&
		       Field(a_output, "fullReplace", Flag) &&
		       Field(a_output, "clientTime", ClientTime) &&
		       Field(a_output, "authorityEpoch", Positive32) &&
		       Field(a_output, "authorityCellId", HexNonZero) &&
		       Field(a_output, "authorityWorldspaceId", HexOrEmpty);
	}

	bool CombatHitInvariant(const Json&, const Json& a_output)
	{
		return a_output.is_object() &&
		       OnlyKeys(a_output, { "type", "targetPlayerId", "sequence", "attackSequence", "damage", "weaponFormId" }) &&
		       TypeIs(a_output, "combatHit") &&
		       Field(a_output, "targetPlayerId", Positive32, true) &&
		       Field(a_output, "sequence", Positive32, true) &&
		       Field(a_output, "attackSequence", Positive32, true) &&
		       Field(a_output, "damage", Damage, true) &&
		       Field(a_output, "weaponFormId", HexAny);
	}

	Json WorldStateSample()
	{
		return Json{
			{ "type", "worldState" },
			{ "gameHour", 13.5 },
			{ "gameDaysPassed", 42.25 },
			{ "weatherFormId", HexWord(10) },
			{ "timeSync", true },
			{ "clientTime", 1000.5 }
		};
	}

	Json NpcStateSample()
	{
		return Json{
			{ "type", "npcState" },
			{ "fullReplace", true },
			{ "clientTime", 2000.0 },
			{ "authorityEpoch", 3 },
			{ "authorityCellId", HexWord(51966) },
			{ "authorityWorldspaceId", HexWord(61453) },
			{ "npcs", Json::array({ Json{
				{ "npcId", 7 },
				{ "sourceFormId", HexWord(4660) },
				{ "baseFormId", HexWord(22136) },
				{ "cellId", HexWord(51966) },
				{ "worldspaceId", HexWord(61453) },
				{ "x", 1.0 },
				{ "y", -2.0 },
				{ "z", 3.5 },
				{ "angleZ", 90.0 },
				{ "isDead", false },
				{ "isMoving", true },
				{ "isSprinting", false },
				{ "isSneaking", true },
				{ "movementSpeed", 120.0 }
			} }) }
		};
	}

	Json CombatHitSample()
	{
		return Json{
			{ "type", "combatHit" },
			{ "targetPlayerId", 2 },
			{ "sequence", 9 },
			{ "attackSequence", 4 },
			{ "damage", 10.5 },
			{ "weaponFormId", HexWord(48879) }
		};
	}

	Json WelcomeFixture()
	{
		return Json{ { "type", "welcome" }, { "playerId", 7 }, { "protocolVersion", 2 } };
	}

	Json SessionReadyFixture()
	{
		return Json{
			{ "type", "sessionReady" },
			{ "playerId", 7 },
			{ "protocolVersion", 2 },
			{ "serverProtocolVersion", 2 },
			{ "worldStateHostPlayerId", 7 },
			{ "profileId", "co-12345678" },
			{ "clientVersion", std::string{ F4T::ClientCompatibility::kClientVersion } },
			{ "falloutRuntimeVersion", std::string{ F4T::ClientCompatibility::kCurrentRuntimeVersion } },
			{ "contentManifestFingerprint", kManifestFingerprint },
			{ "resumeToken", "rotated-token" },
			{ "resumeExpiresAt", 1234567890.5 }
		};
	}

	F4T::GnsProtocolAdapter::ClientIdentity IdentityFixture()
	{
		return F4T::GnsProtocolAdapter::ClientIdentity{
			.profileId = "co-12345678",
			.metadata = F4T::ClientCompatibility::Metadata{
				.buildTarget = F4T::ClientCompatibility::BuildTarget::Current,
				.clientVersion = std::string{ F4T::ClientCompatibility::kClientVersion },
				.falloutRuntimeVersion = std::string{ F4T::ClientCompatibility::kCurrentRuntimeVersion },
				.contentManifestFingerprint = kManifestFingerprint,
				.capabilities = {}
			},
			.resumeToken = std::optional<std::string>{ "previous-server-token" }
		};
	}

	Json SessionPacket(Rng& a_rng)
	{
		switch (a_rng.Below(5)) {
		case 0:
			return WelcomeFixture();
		case 1:
			return SessionReadyFixture();
		case 2:
			return NpcStateSample();
		case 3:
			return Json{ { "type", "sessionEnded" }, { "reason", "server" } };
		default:
			return Json{ { "type", "keepAlive" } };
		}
	}

	void FuzzSession(std::uint64_t a_seed)
	{
		Rng rng{ std::mt19937_64{ a_seed } };
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "GnsProtocolAdapter::Session::AdaptInbound", a_seed, iteration };
			Session session;
			session.SetClientIdentity(IdentityFixture(), [](const ReadyIdentity&, std::string&) { return true; });
			if (rng.Below(2) == 0) {
				const auto welcome = WelcomeFixture().dump();
				Invoke(trial, [&] { return session.AdaptInbound(welcome); });
			}
			if (rng.Below(2) == 0) {
				const auto ready = SessionReadyFixture().dump();
				Invoke(trial, [&] { return session.AdaptInbound(ready); });
			}
			auto packet = SessionPacket(rng);
			for (std::size_t mutations = rng.Below(4); mutations > 0; --mutations) {
				MutateOnce(packet, rng);
			}
			auto text = packet.dump();
			MutateText(text, rng);
			const auto inbound = Invoke(trial, [&] { return session.AdaptInbound(text); });
			if (inbound.readyIdentity) {
				const auto expiry = inbound.readyIdentity->resumeExpiresAt;
				if (!std::isfinite(expiry) || expiry <= 0.0) {
					Fail(trial, "ready identity carries a non-positive or non-finite resumeExpiresAt; input=" + text);
				}
			}
			Invoke(trial, [&] { return session.AdaptOutbound(text); });
		}
	}

	std::optional<float> OracleClock(std::string_view a_text)
	{
		if (a_text.size() != 4 || !std::all_of(a_text.begin(), a_text.end(), [](char a_ch) { return a_ch >= '0' && a_ch <= '9'; })) {
			return std::nullopt;
		}
		const int hours = (a_text[0] - '0') * 10 + (a_text[1] - '0');
		const int minutes = (a_text[2] - '0') * 10 + (a_text[3] - '0');
		if (hours > 23 || minutes > 59) {
			return std::nullopt;
		}
		return static_cast<float>(hours) + static_cast<float>(minutes) / 60.0F;
	}

	bool IsHexDigit(char a_ch)
	{
		return (a_ch >= '0' && a_ch <= '9') || (a_ch >= 'a' && a_ch <= 'f') || (a_ch >= 'A' && a_ch <= 'F');
	}

	bool OracleHex(std::string_view a_text, bool a_allowEmpty, bool a_allowZero)
	{
		if (a_text.empty()) {
			return a_allowEmpty;
		}
		if (a_text.size() > 8) {
			return false;
		}
		std::uint64_t value = 0;
		for (const char ch : a_text) {
			if (!IsHexDigit(ch)) {
				return false;
			}
			value = value * 16U + static_cast<std::uint64_t>(ch <= '9' ? ch - '0' : (ch | 0x20) - 'a' + 10);
		}
		return a_allowZero || value != 0;
	}

	void FuzzStringParsers(std::uint64_t a_seed)
	{
		Rng rng{ std::mt19937_64{ a_seed } };
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial clockTrial{ "NetworkValidation::ParseServerTimeHHmm", a_seed, iteration };
			const auto clock = ClockText(rng);
			const auto parsed = Invoke(clockTrial, [&] { return F4T::NetworkValidation::ParseServerTimeHHmm(clock); });
			const auto expected = OracleClock(clock);
			if (parsed.has_value() != expected.has_value() || (parsed && *parsed != *expected)) {
				Fail(clockTrial, "ParseServerTimeHHmm disagrees with oracle for input \"" + clock + "\"");
			}
			if (parsed && (*parsed < 0.0F || *parsed >= 24.0F)) {
				Fail(clockTrial, "ParseServerTimeHHmm returned out-of-range hour for input \"" + clock + "\"");
			}

			const Trial hexTrial{ "NetworkValidation::IsValidHexFormId", a_seed, iteration };
			std::string hex(rng.Below(11), '0');
			for (auto& ch : hex) {
				ch = kHexAlphabet[rng.Below(kHexAlphabet.size())];
			}
			const bool allowEmpty = rng.Below(2) == 0;
			const bool allowZero = rng.Below(2) == 0;
			const auto accepted = Invoke(hexTrial, [&] { return F4T::NetworkValidation::IsValidHexFormId(hex, allowEmpty, allowZero); });
			if (accepted != OracleHex(hex, allowEmpty, allowZero)) {
				Fail(hexTrial, "IsValidHexFormId disagrees with oracle for input \"" + hex + "\"");
			}
		}
	}

	F4T::NetworkValidation::MovementState RandomMovementState(Rng& a_rng)
	{
		static const std::string kCells[] = { HexWord(51966), HexWord(48879) };
		constexpr std::string_view kTypes[] = {
			"normal", "teleport", "cell_change", "worldspace_change", "load", "spawn", "fast_travel", "bogus"
		};
		F4T::NetworkValidation::MovementState state;
		state.x = a_rng.Below(2) == 0 ? RandomDouble(a_rng) : static_cast<double>(a_rng.Below(2001)) - 1000.0;
		state.y = a_rng.Below(2) == 0 ? RandomDouble(a_rng) : static_cast<double>(a_rng.Below(2001)) - 1000.0;
		state.z = a_rng.Below(2) == 0 ? RandomDouble(a_rng) : static_cast<double>(a_rng.Below(2001)) - 1000.0;
		state.cellId = kCells[a_rng.Below(std::size(kCells))];
		state.worldspaceId = kCells[a_rng.Below(std::size(kCells))];
		state.movementType = kTypes[a_rng.Below(std::size(kTypes))];
		return state;
	}

	void FuzzMovement(std::uint64_t a_seed)
	{
		Rng rng{ std::mt19937_64{ a_seed } };
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "NetworkValidation::ValidateMovement", a_seed, iteration };
			std::optional<F4T::NetworkValidation::MovementState> previous;
			if (rng.Below(4) != 0) {
				previous = RandomMovementState(rng);
			}
			std::optional<double> previousTime;
			if (rng.Below(4) != 0) {
				previousTime = RandomSeconds(rng);
			}
			const auto current = RandomMovementState(rng);
			const auto now = RandomSeconds(rng);
			F4T::NetworkValidation::MovementTransitionBudget budget;
			for (auto recorded = rng.Below(20); recorded > 0; --recorded) {
				budget.Record(RandomSeconds(rng));
			}

			const auto result = F4T::NetworkValidation::ValidateMovement(previous, previousTime, current, now, budget);
			using Verdict = F4T::NetworkValidation::MovementVerdict;
			const bool hasPrior = previous.has_value() && previousTime.has_value();
			if (result.verdict == Verdict::AcceptedFirstTransform && hasPrior) {
				Fail(trial, "first-transform verdict returned with a prior state");
			}
			if (result.verdict == Verdict::AcceptedExplicitTransition &&
			    !F4T::NetworkValidation::IsMovementTransitionType(current.movementType)) {
				Fail(trial, "explicit transition accepted for a non-transition movement type");
			}
			if (result.verdict == Verdict::AcceptedNormalMovement &&
			    (!std::isfinite(result.distance) || result.distance > result.allowedDistance ||
			     result.elapsedSeconds < 0.0 || result.elapsedSeconds > F4T::NetworkValidation::MaximumMovementValidationElapsedSeconds)) {
				Fail(trial, "normal movement accepted outside distance or elapsed bounds");
			}
			if (result.verdict == Verdict::RejectedScopeChange &&
			    (!hasPrior || F4T::NetworkValidation::IsMovementTransitionType(current.movementType) ||
			     (previous->cellId == current.cellId && previous->worldspaceId == current.worldspaceId))) {
				Fail(trial, "scope-change rejection without a cell or worldspace change");
			}
		}
	}

	void FuzzEnvelope(std::uint64_t a_seed)
	{
		Rng rng{ std::mt19937_64{ a_seed } };
		std::size_t decoded = 0;
		for (std::size_t iteration = 0; iteration < kIterations; ++iteration) {
			const Trial trial{ "GnsSnapshotEnvelope::Decode", a_seed, iteration };
			auto bytes = EnvelopeSeed(rng);
			for (std::size_t mutations = rng.Below(3); mutations > 0; --mutations) {
				MutateBytes(bytes, rng);
			}
			F4T::GnsSnapshotEnvelope::Header header{};
			if (F4T::GnsSnapshotEnvelope::Decode(bytes, header) != EnvelopeResult::Valid) {
				continue;
			}
			++decoded;
			std::vector<std::uint8_t> rebuilt(F4T::GnsSnapshotEnvelope::HeaderSize);
			if (!F4T::GnsSnapshotEnvelope::WriteHeader(rebuilt, header.family, header.sequence)) {
				Fail(trial, "WriteHeader rejected a header that Decode accepted");
			}
			rebuilt.insert(rebuilt.end(), bytes.begin() + F4T::GnsSnapshotEnvelope::HeaderSize, bytes.end());
			F4T::GnsSnapshotEnvelope::Header again{};
			if (rebuilt != bytes || F4T::GnsSnapshotEnvelope::Decode(rebuilt, again) != EnvelopeResult::Valid ||
			    again.family != header.family || again.sequence != header.sequence) {
				Fail(trial, "envelope re-encode does not round-trip through Decode");
			}
		}
		if (decoded == 0) {
			Fail(Trial{ "GnsSnapshotEnvelope::Decode", a_seed, 0 }, "no valid envelopes were decoded");
		}
	}

	const JsonTarget kJsonTargets[] = {
		{ "NormalizeWorldState", WorldStateSample, F4T::AuthoritativePacketValidation::NormalizeWorldState, WorldStateInvariant },
		{ "NormalizeNpcState", NpcStateSample, F4T::AuthoritativePacketValidation::NormalizeNpcState, NpcStateInvariant },
		{ "NormalizeCombatHit", CombatHitSample, F4T::AuthoritativePacketValidation::NormalizeCombatHit, CombatHitInvariant }
	};
}

int main()
{
	std::uint64_t seed = kBaseSeed;
	for (const auto& target : kJsonTargets) {
		FuzzJsonTarget(target, seed++);
	}
	FuzzSession(seed++);
	FuzzStringParsers(seed++);
	FuzzMovement(seed++);
	FuzzEnvelope(seed++);
	std::cout << "validator fuzz passed: json_targets=" << std::size(kJsonTargets)
	          << " iterations_per_target=" << kIterations << " base_seed=" << kBaseSeed << '\n';
	return 0;
}
