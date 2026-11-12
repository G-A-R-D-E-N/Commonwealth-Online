#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace
{
	using Json = nlohmann::json;
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

	OutboundFrame RelayFrame(std::uint32_t a_connectionId, const Json& a_packet)
	{
		OutboundFrame frame;
		frame.connectionId = a_connectionId;
		frame.payload = EncodeJson(a_packet);
		return frame;
	}

	Json CombatHit(std::uint32_t a_sequence)
	{
		return Json{ { "type", "combatHit" }, { "sequence", a_sequence } };
	}

	Json NpcHit(std::uint32_t a_authorityEpoch)
	{
		return Json{
			{ "type", "npcHit" },
			{ "npcId", 42 },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "sequence", 1 },
			{ "authorityEpoch", a_authorityEpoch }
		};
	}

	Json Transform(double a_x)
	{
		return Json{
			{ "type", "transform" },
			{ "cellId", "CAFE" },
			{ "worldspaceId", "F00D" },
			{ "x", a_x },
			{ "y", 0.0 },
			{ "z", 0.0 },
			{ "angleZ", 0.0 },
			{ "movementType", "spawn" }
		};
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		(void)a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		const auto ready = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(FindFrame(ready, a_connectionId, "sessionReady") != nullptr);
	}

	void TestFailedRelayHoldsLaterRelayUntilSent()
	{
		ProtocolHost host{ "Combat Relay Hold" };
		Join(host, 10, 1.0, 100.0);
		Join(host, 20, 1.1, 100.2);

		const auto first = RelayFrame(20, CombatHit(1));
		const auto second = RelayFrame(20, CombatHit(2));
		host.FrameDeliveryCompleted(first, false);
		assert(host.HeldCombatRelayBlocks(second));
		assert(!host.HeldCombatRelayBlocks(RelayFrame(10, CombatHit(3))));
		assert(!host.HeldCombatRelayBlocks(RelayFrame(20, Json{ { "type", "chat" } })));

		host.FrameDeliveryCompleted(second, false);
		const auto retry = host.RetryHeldCombatRelays();
		assert(retry.size() == 2);
		assert(retry[0].connectionId == 20 && retry[0].payload == first.payload);
		assert(retry[1].payload == second.payload);

		host.FrameDeliveryCompleted(retry[0], true);
		assert(!host.HeldCombatRelayBlocks(retry[1]));
		host.FrameDeliveryCompleted(retry[1], true);
		const auto drained = host.RetryHeldCombatRelays();
		assert(drained.empty());
	}

	void TestRecipientLeavingDropsHeldRelay()
	{
		ProtocolHost host{ "Combat Relay Hold Departure" };
		Join(host, 10, 1.0, 100.0);
		Join(host, 20, 1.1, 100.2);
		host.FrameDeliveryCompleted(RelayFrame(20, CombatHit(1)), false);
		(void)host.HandleDisconnected(20, 101.0);
		const auto retry = host.RetryHeldCombatRelays();
		assert(retry.empty());
	}

	void TestHeldRelayExpiresAfterActionLifetime()
	{
		ProtocolHost host{ "Combat Relay Hold Expiry" };
		Join(host, 10, 1.0, 100.0);
		Join(host, 20, 1.1, 100.2);
		host.FrameDeliveryCompleted(RelayFrame(20, CombatHit(1)), false);
		const auto fresh = host.RetryHeldCombatRelays();
		assert(fresh.size() == 1);
		std::this_thread::sleep_for(std::chrono::milliseconds(2100));
		const auto expired = host.RetryHeldCombatRelays();
		assert(expired.empty());
	}

	void TestNpcHitWithReassignedAuthorityIsDropped()
	{
		ProtocolHost host{ "Combat Relay Hold Authority" };
		Join(host, 10, 1.0, 100.0);
		const auto spawnA = host.HandleMessage(10, EncodeSnapshot(Transform(0.0), Family::Transform, 1), 2.0, 101.0);
		const auto* authorityA = FindFrame(spawnA, 10, "npcAuthority");
		assert(authorityA != nullptr);
		const auto epoch = DecodePacket(*authorityA).at("authorityEpoch").get<std::uint32_t>();

		Join(host, 20, 1.1, 100.2);
		(void)host.HandleMessage(20, EncodeSnapshot(Transform(20.0), Family::Transform, 1), 2.1, 101.1);
		host.FrameDeliveryCompleted(RelayFrame(20, NpcHit(epoch)), false);

		const auto departure = host.HandleDisconnected(10, 102.0);
		const auto* authorityB = FindFrame(departure, 20, "npcAuthority");
		assert(authorityB != nullptr);
		assert(DecodePacket(*authorityB).at("authorityEpoch").get<std::uint32_t>() == epoch + 1);
		const auto stale = host.RetryHeldCombatRelays();
		assert(stale.empty());

		host.FrameDeliveryCompleted(RelayFrame(20, NpcHit(epoch + 1)), false);
		const auto current = host.RetryHeldCombatRelays();
		assert(current.size() == 1 && current[0].connectionId == 20);
	}

	void TestCongestedConnectionDoesNotRefuseOtherRecipient()
	{
		ProtocolHost host{ "Combat Relay Hold Congestion" };
		Join(host, 10, 1.0, 100.0);
		Join(host, 20, 1.1, 100.2);
		for (std::uint32_t sequence = 1; sequence <= 65; ++sequence) {
			host.FrameDeliveryCompleted(RelayFrame(20, CombatHit(sequence)), false);
		}
		host.FrameDeliveryCompleted(RelayFrame(10, CombatHit(1)), false);
		const auto retry = host.RetryHeldCombatRelays();
		assert(retry.size() == 65);
		const auto otherRecipient = std::count_if(retry.begin(), retry.end(), [](const OutboundFrame& a_frame) {
			return a_frame.connectionId == 10;
		});
		assert(otherRecipient == 1);
	}
}

int main()
{
	try {
		TestFailedRelayHoldsLaterRelayUntilSent();
		TestRecipientLeavingDropsHeldRelay();
		TestHeldRelayExpiresAfterActionLifetime();
		TestNpcHitWithReassignedAuthorityIsDropped();
		TestCongestedConnectionDoesNotRefuseOtherRecipient();
	} catch (const std::exception& error) {
		std::fprintf(stderr, "ServerCombatRelayHoldTests unexpected exception: %s\n", error.what());
		return 1;
	}
	return 0;
}
