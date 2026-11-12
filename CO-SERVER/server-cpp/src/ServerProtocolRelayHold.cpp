#include "ServerProtocolImpl.h"
#include "ServerPacketDecode.h"
#include "ServerPacketRead.h"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace CommonwealthOnline::Server
{
	namespace
	{
		constexpr std::size_t kMaximumHeldCombatRelays = 64;

		bool IsCombatRelayType(const std::string& a_type)
		{
			return a_type == "combatHit" || a_type == "npcHit" || a_type == "npcDamage";
		}
	}

	bool ProtocolHost::Impl::HeldCombatRelayBlocks(const OutboundFrame& a_frame) const
	{
		const auto first = std::find_if(heldCombatRelays.begin(), heldCombatRelays.end(), [&a_frame](const HeldCombatRelay& a_held) {
			return a_held.frame.connectionId == a_frame.connectionId;
		});
		if (first == heldCombatRelays.end() || !IsCombatRelayType(PacketDecode::RawPacketType(a_frame.payload).value_or(std::string{}))) {
			return false;
		}
		return first->frame.payload != a_frame.payload;
	}

	void ProtocolHost::Impl::HoldCombatRelay(const OutboundFrame& a_frame)
	{
		const auto type = PacketDecode::RawPacketType(a_frame.payload).value_or(std::string{});
		const auto recipient = sessions.find(a_frame.connectionId);
		if (!IsCombatRelayType(type) || recipient == sessions.end()) {
			return;
		}
		const bool alreadyHeld = std::any_of(heldCombatRelays.begin(), heldCombatRelays.end(), [&a_frame](const HeldCombatRelay& a_held) {
			return a_held.frame.connectionId == a_frame.connectionId && a_held.frame.payload == a_frame.payload;
		});
		if (alreadyHeld) {
			return;
		}
		const auto heldForConnection = std::count_if(heldCombatRelays.begin(), heldCombatRelays.end(), [&a_frame](const HeldCombatRelay& a_held) {
			return a_held.frame.connectionId == a_frame.connectionId;
		});
		if (static_cast<std::size_t>(heldForConnection) >= kMaximumHeldCombatRelays) {
			std::cerr << "held combat relay refused for connection " << a_frame.connectionId << ": " << kMaximumHeldCombatRelays << " already held\n";
			return;
		}
		heldCombatRelays.push_back(HeldCombatRelay{ a_frame, recipient->second.playerId, type, std::chrono::steady_clock::now() });
	}

	void ProtocolHost::Impl::ReleaseHeldCombatRelay(const OutboundFrame& a_frame)
	{
		const auto held = std::find_if(heldCombatRelays.begin(), heldCombatRelays.end(), [&a_frame](const HeldCombatRelay& a_entry) {
			return a_entry.frame.connectionId == a_frame.connectionId && a_entry.frame.payload == a_frame.payload;
		});
		if (held != heldCombatRelays.end()) {
			heldCombatRelays.erase(held);
		}
	}

	std::vector<OutboundFrame> ProtocolHost::Impl::RetryHeldCombatRelays()
	{
		std::vector<OutboundFrame> frames;
		const auto now = std::chrono::steady_clock::now();
		std::erase_if(heldCombatRelays, [this, &frames, now](HeldCombatRelay& a_held) {
			const auto age = std::chrono::duration<double>(now - a_held.heldSince).count();
			const auto recipient = FindActiveSession(a_held.recipientPlayerId);
			bool authorityHeld = true;
			if (a_held.type == "npcHit") {
				const auto packet = nlohmann::json::parse(a_held.frame.payload.begin(), a_held.frame.payload.end(), nullptr, false);
				const F4T::NpcAuthority::Scope wire{ ReadString(packet, "cellId").value_or(""), ReadString(packet, "worldspaceId").value_or("") };
				const auto assignment = recipient ? npcAuthority.Get(ScopeKeys::Canonical(recipient->second->pluginList, wire)) : std::nullopt;
				authorityHeld = assignment && assignment->playerId == a_held.recipientPlayerId && assignment->epoch == ReadUInt32(packet, "authorityEpoch").value_or(0);
			}
			if (age > SessionTypes::kCombatActionLifetimeSeconds || !recipient || !authorityHeld) {
				std::cerr << "held " << a_held.type << " relay dropped for player " << a_held.recipientPlayerId << '\n';
				return true;
			}
			a_held.frame.connectionId = recipient->first;
			frames.push_back(a_held.frame);
			return false;
		});
		return frames;
	}
}
