#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "ServerCombatChecks.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::CombatChecks::CapForAction;
	using CommonwealthOnline::Server::CombatChecks::ClampCombatDamageClaim;
	using CommonwealthOnline::Server::CombatChecks::kMaximumCombatDamageClaim;
	using CommonwealthOnline::Server::CombatChecks::kMaximumUnarmedCombatDamageClaim;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<std::uint8_t> EncodeSnapshot(const Json& a_packet, Family a_family, std::uint32_t a_sequence)
	{
		const auto json = a_packet.dump();
		std::vector<std::uint8_t> output(F4T::GnsSnapshotEnvelope::HeaderSize + json.size());
		assert(F4T::GnsSnapshotEnvelope::WriteHeader(std::span<std::uint8_t>{ output }, a_family, a_sequence));
		std::copy(json.begin(), json.end(), output.begin() + static_cast<std::ptrdiff_t>(F4T::GnsSnapshotEnvelope::HeaderSize));
		return output;
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		F4T::GnsSnapshotEnvelope::Header header{};
		const auto decode = F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header);
		std::span<const std::uint8_t> payload{ a_frame.payload };
		if (decode == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		} else {
			assert(decode == DecodeResult::NotEnvelope);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size()
		});
	}

	const OutboundFrame* FindFrame(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && !frame.payload.empty() &&
				DecodePacket(frame).value("type", std::string{}) == a_type) {
				return &frame;
			}
		}
		return nullptr;
	}

	std::uint32_t ConnectPlayer(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		assert(welcome.size() == 1);
		const auto playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		const auto ready = a_host.HandleMessage(
			a_connectionId,
			EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }),
			a_monotonicSeconds,
			a_unixTimeSeconds + 0.1);
		assert(FindFrame(ready, a_connectionId, "sessionReady") != nullptr);
		return playerId;
	}

	Json Transform(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "cell" "Id", "CAFE" },
			{ "worldspace" "Id", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	Json RangedPlayerState(std::uint32_t a_actionSequence)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "form" "Id", "1234" } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 3 },
				{ "eventName", "fireSingle" }
			} }) }
		};
	}

	Json Hit(std::uint32_t a_target, std::uint32_t a_sequence, double a_damage, std::uint32_t a_attackSequence)
	{
		return Json{
			{ "type", "combatHit" },
			{ "targetPlayerId", a_target },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence },
			{ "damage", a_damage },
			{ "weaponForm" "Id", "1234" }
		};
	}

	Json UnarmedPlayerState(std::uint32_t a_actionSequence)
	{
		return Json{
			{ "type", "playerState" },
			{ "equippedItems", Json::array({ Json{ { "slot", "rightHand" }, { "form" "Id", "" } } }) },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", a_actionSequence },
				{ "type", 2 },
				{ "eventName", "meleeattackStart" }
			} }) }
		};
	}

	Json UnarmedHit(std::uint32_t a_target, std::uint32_t a_sequence, double a_damage, std::uint32_t a_attackSequence)
	{
		return Json{
			{ "type", "combatHit" },
			{ "targetPlayerId", a_target },
			{ "sequence", a_sequence },
			{ "attackSequence", a_attackSequence },
			{ "damage", a_damage }
		};
	}

	void TestClampRejectsNonPositiveAndNonFinite()
	{
		assert(ClampCombatDamageClaim(0.0F) == 0.0F);
		assert(ClampCombatDamageClaim(-1.0F) == 0.0F);
		assert(ClampCombatDamageClaim(std::numeric_limits<float>::quiet_NaN()) == 0.0F);
		assert(ClampCombatDamageClaim(std::numeric_limits<float>::infinity()) == 0.0F);
		assert(ClampCombatDamageClaim(-std::numeric_limits<float>::infinity()) == 0.0F);
	}

	void TestClampKeepsPlausibleDamage()
	{
		assert(ClampCombatDamageClaim(0.5F) == 0.5F);
		assert(ClampCombatDamageClaim(25.0F) == 25.0F);
		assert(ClampCombatDamageClaim(kMaximumCombatDamageClaim) == kMaximumCombatDamageClaim);
	}

	void TestClampBoundsAbsurdDamage()
	{
		assert(ClampCombatDamageClaim(kMaximumCombatDamageClaim + 1.0F) == kMaximumCombatDamageClaim);
		assert(ClampCombatDamageClaim(10'000.0F) == kMaximumCombatDamageClaim);
		assert(ClampCombatDamageClaim(std::numeric_limits<float>::max()) == kMaximumCombatDamageClaim);
	}

	void TestUnarmedClampUsesLowerCap()
	{
		assert(ClampCombatDamageClaim(200.0F, kMaximumUnarmedCombatDamageClaim) == kMaximumUnarmedCombatDamageClaim);
		assert(ClampCombatDamageClaim(40.0F, kMaximumUnarmedCombatDamageClaim) == 40.0F);
	}

	void TestCapForActionPicksPerActionCap()
	{
		assert(CapForAction(2U) == kMaximumUnarmedCombatDamageClaim);
		assert(CapForAction(1U) == kMaximumCombatDamageClaim);
		assert(CapForAction(3U) == kMaximumCombatDamageClaim);
	}

	void TestCombatHitDamageClaimIsBounded()
	{
		ProtocolHost host{ "Combat Damage Claim" };
		const auto attackerId = ConnectPlayer(host, 10, 1.0, 100.0);
		const auto victimId = ConnectPlayer(host, 20, 1.1, 100.2);
		assert(attackerId != victimId);
		host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		host.HandleMessage(20, EncodeSnapshot(Transform(100.0), Family::Transform, 1), 2.1, 101.1);
		host.HandleMessage(10, EncodeJson(RangedPlayerState(10)), 2.2, 101.2);

		const auto before = host.Stats();
		const auto frames = host.HandleMessage(10, EncodeJson(Hit(victimId, 1, 10'000.0, 10)), 2.21, 101.21);
		const auto relay = FindFrame(frames, 20, "combatHit");
		assert(relay != nullptr);
		assert(DecodePacket(*relay).at("damage").get<double>() == static_cast<double>(kMaximumCombatDamageClaim));
		assert(host.Stats().combatDamageClaimsClamped == before.combatDamageClaimsClamped + 1);
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
		assert(host.Stats().combatHitsRejected == before.combatHitsRejected);
	}

	void TestCombatHitPlausibleDamagePassesUnchanged()
	{
		ProtocolHost host{ "Combat Damage Claim Unchanged" };
		const auto attackerId = ConnectPlayer(host, 10, 1.0, 100.0);
		const auto victimId = ConnectPlayer(host, 20, 1.1, 100.2);
		assert(attackerId != victimId);
		host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		host.HandleMessage(20, EncodeSnapshot(Transform(100.0), Family::Transform, 1), 2.1, 101.1);
		host.HandleMessage(10, EncodeJson(RangedPlayerState(10)), 2.2, 101.2);

		const auto before = host.Stats();
		const auto frames = host.HandleMessage(10, EncodeJson(Hit(victimId, 1, 25.0, 10)), 2.21, 101.21);
		const auto relay = FindFrame(frames, 20, "combatHit");
		assert(relay != nullptr);
		assert(DecodePacket(*relay).at("damage").get<double>() == 25.0);
		assert(host.Stats().combatDamageClaimsClamped == before.combatDamageClaimsClamped);
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}

	void TestUnarmedCombatHitDamageIsBounded()
	{
		ProtocolHost host{ "Combat Damage Claim Unarmed" };
		const auto attackerId = ConnectPlayer(host, 10, 1.0, 100.0);
		const auto victimId = ConnectPlayer(host, 20, 1.1, 100.2);
		assert(attackerId != victimId);
		host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		host.HandleMessage(20, EncodeSnapshot(Transform(100.0), Family::Transform, 1), 2.1, 101.1);
		host.HandleMessage(10, EncodeJson(UnarmedPlayerState(10)), 2.2, 101.2);

		const auto before = host.Stats();
		const auto frames = host.HandleMessage(10, EncodeJson(UnarmedHit(victimId, 1, 200.0, 10)), 2.21, 101.21);
		const auto relay = FindFrame(frames, 20, "combatHit");
		assert(relay != nullptr);
		assert(DecodePacket(*relay).at("damage").get<double>() == static_cast<double>(kMaximumUnarmedCombatDamageClaim));
		assert(host.Stats().combatDamageClaimsClamped == before.combatDamageClaimsClamped + 1);
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}
}

int main()
{
	TestClampRejectsNonPositiveAndNonFinite();
	TestClampKeepsPlausibleDamage();
	TestClampBoundsAbsurdDamage();
	TestUnarmedClampUsesLowerCap();
	TestCapForActionPicksPerActionCap();
	TestCombatHitDamageClaimIsBounded();
	TestCombatHitPlausibleDamagePassesUnchanged();
	TestUnarmedCombatHitDamageIsBounded();
	return 0;
}
