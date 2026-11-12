#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "ServerFactionTestSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;
	using CommonwealthOnline::Server::ProtocolHost;
	using FactionTestSupport::ArmRangedAttacker;
	using FactionTestSupport::AssertResult;
	using FactionTestSupport::Disband;
	using FactionTestSupport::FormFaction;
	using FactionTestSupport::HasPacket;
	using FactionTestSupport::Join;
	using FactionTestSupport::Kick;
	using FactionTestSupport::Leave;
	using FactionTestSupport::Send;

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
		CommonwealthOnline::Server::ProtocolHost& a_host,
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

	struct HitOutcome
	{
		std::vector<OutboundFrame> frames;
		std::uint32_t targetConnectionId{};
		std::uint32_t targetPlayerId{};
	};

	HitOutcome SendRangedHit(CommonwealthOnline::Server::ProtocolHost& a_host)
	{
		const auto playerA = ConnectPlayer(a_host, 10, 1.0, 100.0);
		const auto playerB = ConnectPlayer(a_host, 20, 1.1, 100.2);
		a_host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		a_host.HandleMessage(20, EncodeSnapshot(Transform(100.0), Family::Transform, 1), 2.1, 101.1);
		assert(playerA != playerB);
		a_host.HandleMessage(10, EncodeJson(RangedPlayerState(10)), 2.2, 101.2);
		return HitOutcome{
			a_host.HandleMessage(10, EncodeJson(Hit(playerB, 1, 25.0, 10)), 2.21, 101.21),
			20,
			playerB
		};
	}

	void TestMemberHitOnLeaderDropped()
	{
		ProtocolHost host{ "Faction Friendly Fire Off Leader" };
		assert(host.EnableFactions(nullptr));
		const auto leader = Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 20, 10);

		const auto before = host.Stats();
		assert(FactionTestSupport::SendRangedHit(host, 20, leader.playerId, 1, 2.5).empty());
		assert(host.Stats().combatHitsRejected == before.combatHitsRejected + 1);
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted);
	}

	void TestSameFactionHitRoutedAfterMemberLeaves()
	{
		ProtocolHost host{ "Faction Friendly Fire Off Leave" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 10, 20);
		assert(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 1, 2.5).empty());

		AssertResult(Send(host, 20, Leave(), 2.3, 102.3), 20, "leave", true);
		const auto before = host.Stats();
		assert(HasPacket(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 2, 2.6), 20, "combatHit"));
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}

	void TestSameFactionHitRoutedAfterDisband()
	{
		ProtocolHost host{ "Faction Friendly Fire Off Disband" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 10, 20);
		assert(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 1, 2.5).empty());

		AssertResult(Send(host, 10, Disband(), 2.3, 102.3), 10, "disband", true);
		const auto before = host.Stats();
		assert(HasPacket(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 2, 2.6), 20, "combatHit"));
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}

	void TestSameFactionHitRoutedAfterLeaderLeaves()
	{
		ProtocolHost host{ "Faction Friendly Fire Off Leader Leave" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 10, 20);
		assert(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 1, 2.5).empty());

		AssertResult(Send(host, 10, Leave(), 2.3, 102.3), 10, "leave", true);
		const auto before = host.Stats();
		assert(HasPacket(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 2, 2.6), 20, "combatHit"));
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}

	void TestSameFactionHitRoutedAfterKick()
	{
		ProtocolHost host{ "Faction Friendly Fire Off Kick" };
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "p10", 1.0, 100.0);
		const auto member = Join(host, 20, "p20", 1.1, 100.2);
		FormFaction(host, 10, 20, member.playerId, "Alpha Team");
		ArmRangedAttacker(host, 10, 20);
		assert(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 1, 2.5).empty());

		AssertResult(Send(host, 10, Kick("p20"), 2.3, 102.3), 10, "kick", true);
		const auto before = host.Stats();
		assert(HasPacket(FactionTestSupport::SendRangedHit(host, 10, member.playerId, 2, 2.6), 20, "combatHit"));
		assert(host.Stats().combatHitsRouted == before.combatHitsRouted + 1);
	}
}

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost friendly{ "Friendly Fire On" };
	const auto friendlyWelcome = DecodePacket(friendly.HandleConnected(10, 100.0).front());
	assert(friendlyWelcome.at("friendlyFire").get<bool>());

	ProtocolHost hostilePvp{ "Friendly Fire Off", {}, {}, false };
	const auto hostileWelcome = DecodePacket(hostilePvp.HandleConnected(10, 100.0).front());
	assert(!hostileWelcome.at("friendlyFire").get<bool>());
	hostilePvp.HandleDisconnected(10, 100.1);

	const auto friendlyHit = SendRangedHit(friendly);
	const auto* routedFrame = FindFrame(friendlyHit.frames, friendlyHit.targetConnectionId, "combatHit");
	assert(routedFrame != nullptr);
	const auto routed = DecodePacket(*routedFrame);
	assert(routed.at("targetPlayerId") == friendlyHit.targetPlayerId);
	assert(friendly.Stats().combatHitsRouted == 1);

	const auto beforeHostile = hostilePvp.Stats();
	const auto hostileHit = SendRangedHit(hostilePvp);
	assert(hostileHit.frames.empty());
	const auto afterHostile = hostilePvp.Stats();
	assert(afterHostile.combatHitsRejected == beforeHostile.combatHitsRejected + 1);
	assert(afterHostile.combatHitsRouted == beforeHostile.combatHitsRouted);

	TestMemberHitOnLeaderDropped();
	TestSameFactionHitRoutedAfterMemberLeaves();
	TestSameFactionHitRoutedAfterDisband();
	TestSameFactionHitRoutedAfterLeaderLeaves();
	TestSameFactionHitRoutedAfterKick();

	return 0;
}
