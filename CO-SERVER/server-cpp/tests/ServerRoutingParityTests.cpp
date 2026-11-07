#include "ServerProtocol.h"

#include "F4TGnsSnapshotEnvelope.h"
#include "F4TInterestScope.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::GnsSnapshotEnvelope::DecodeResult;
	using F4T::GnsSnapshotEnvelope::Family;
	using F4T::InterestScope::StateScope;
	using F4T::InterestScope::StatesShareInterest;

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

	std::vector<Json> FramesTo(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		std::vector<Json> out;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == a_type) {
				out.push_back(packet);
			}
		}
		return out;
	}

	bool HasFrameTo(
		const std::vector<OutboundFrame>& a_frames,
		std::uint32_t a_connectionId,
		std::string_view a_type)
	{
		return !FramesTo(a_frames, a_connectionId, a_type).empty();
	}

	// Test-side mirror of a player's last accepted position, used to compute the OLD-predicate
	// expected recipient set independently of the server's internal membership.
	struct Pos
	{
		std::string cell;
		std::string world;
		double x{};
		double y{};
	};

	bool Share(const Pos& a_left, const Pos& a_right)
	{
		return StatesShareInterest(
			std::optional<StateScope>{ StateScope{ a_left.cell, a_left.world, a_left.x, a_left.y } },
			std::optional<StateScope>{ StateScope{ a_right.cell, a_right.world, a_right.x, a_right.y } });
	}

	struct Harness
	{
		ProtocolHost host{ "Routing Parity Test" };
		std::uint32_t nextConn = 1;
		double clock = 100.0;
		std::map<std::uint32_t, std::uint32_t> seq;
		Harness() { assert(host.EnableSocial(nullptr)); }

		double NextTime() { return clock += 2.0; }

		std::uint32_t Connect()
		{
			const auto conn = nextConn++;
			const auto welcome = host.HandleConnected(conn, NextTime());
			seq[conn] = 0;
			return conn;
		}

		std::uint32_t Activate(std::uint32_t& out_playerId)
		{
			const auto conn = nextConn++;
			const auto welcome = host.HandleConnected(conn, NextTime());
			host.BindSessionProfile(conn, "route-" + std::to_string(conn));
			out_playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
			const auto ready = host.HandleMessage(
				conn, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), NextTime(), NextTime());
			assert(!ready.empty());
			seq[conn] = 0;
			return conn;
		}

		std::vector<OutboundFrame> SendTransform(
			std::uint32_t a_conn, const Pos& a_pos, std::string_view a_movementType)
		{
			const auto s = ++seq[a_conn];
			Json packet{
				{ "type", "transform" },
				{ "cellId", a_pos.cell },
				{ "worldspaceId", a_pos.world },
				{ "x", a_pos.x },
				{ "y", a_pos.y },
				{ "z", 0.0 },
				{ "angleZ", 0.0 },
				{ "movementType", std::string{ a_movementType } }
			};
			const auto mono = NextTime();
			return host.HandleMessage(a_conn, EncodeSnapshot(packet, Family::Transform, s), mono, NextTime());
		}
	};

	const std::string kCellX = "00000100";
	const std::string kCellY = "00000200";
	const std::string kCellW = "00000300";
	const std::string kWorld = "0000F00D";
	const std::string kOtherWorld = "0000DEAD";
	const std::string kIntA = "0000CAFE";
	const std::string kIntB = "0000BEEF";
	const std::string kInterior = "";
}

int main()
{
	// 1. Same-cell mutual visibility.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		const Pos posB{ kCellX, kWorld, 1000.0, 0.0 };
		h.SendTransform(ca, posA, "spawn");
		const auto frB = h.SendTransform(cb, posB, "spawn");
		// B's first transform relays B to A (same cell) and replays A to B.
		assert(HasFrameTo(frB, ca, "transform"));
		assert(HasFrameTo(frB, cb, "transform"));   // replay of A -> B
		const auto frA = h.SendTransform(ca, posA, "normal");
		assert(HasFrameTo(frA, cb, "transform"));   // A -> B live relay
		assert(Share(posA, posB));
	}

	// 2. Exterior radius include / exclude.
	{
		Harness h;
		std::uint32_t pa = 0, pn = 0, pf = 0;
		const auto ca = h.Activate(pa);
		const auto cn = h.Activate(pn);
		const auto cf = h.Activate(pf);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		const Pos near{ kCellY, kWorld, 8000.0, 0.0 };   // dist 8000 <= 8192
		const Pos far{ kCellW, kWorld, 9000.0, 0.0 };    // dist 9000 >  8192
		h.SendTransform(ca, posA, "spawn");
		h.SendTransform(cn, near, "spawn");
		h.SendTransform(cf, far, "spawn");
		const auto fr = h.SendTransform(ca, posA, "normal");
		assert(Share(posA, near) && HasFrameTo(fr, cn, "transform"));
		assert(!Share(posA, far) && !HasFrameTo(fr, cf, "transform"));
	}

	// 3. First-transform population / replay: a new peer receives exactly the existing visible peers.
	{
		Harness h;
		std::uint32_t pa = 0, pc = 0, pd = 0;
		const auto ca = h.Activate(pa);
		const auto cc = h.Activate(pc);
		const auto cd = h.Activate(pd);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		const Pos posC{ kCellX, kWorld, 500.0, 0.0 };   // same cell as A -> visible
		const Pos posD{ kOtherWorld, kOtherWorld, 0.0, 0.0 };  // different world -> not visible
		h.SendTransform(ca, posA, "spawn");
		h.SendTransform(cc, posC, "spawn");
		h.SendTransform(cd, posD, "spawn");
		const Pos posB{ kCellX, kWorld, 100.0, 0.0 };
		std::uint32_t pb = 0;
		const auto cb = h.Activate(pb);
		const auto frB = h.SendTransform(cb, posB, "spawn");
		const auto replay = FramesTo(frB, cb, "transform");
		std::vector<std::uint32_t> gotPlayers;
		for (const auto& p : replay) gotPlayers.push_back(p.at("playerId").get<std::uint32_t>());
		std::sort(gotPlayers.begin(), gotPlayers.end());
		std::vector<std::uint32_t> expected{ pa, pc };   // A and C share B's cell; D does not
		std::sort(expected.begin(), expected.end());
		assert(gotPlayers == expected);
	}

	// 4. Boundary crossing updates membership in both directions.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const Pos shared{ kCellX, kWorld, 0.0, 0.0 };
		const Pos withB{ kCellX, kWorld, 200.0, 0.0 };
		const Pos away{ kOtherWorld, kOtherWorld, 0.0, 0.0 };
		h.SendTransform(ca, shared, "spawn");
		h.SendTransform(cb, withB, "spawn");
		assert(HasFrameTo(h.SendTransform(ca, shared, "normal"), cb, "transform"));   // shared -> visible
		assert(!HasFrameTo(h.SendTransform(ca, away, "cell_change"), cb, "transform"));   // left interest
		assert(!HasFrameTo(h.SendTransform(cb, withB, "normal"), ca, "transform"));   // reverse edge gone
		assert(HasFrameTo(h.SendTransform(ca, shared, "cell_change"), cb, "transform"));   // returned
		assert(HasFrameTo(h.SendTransform(cb, withB, "normal"), ca, "transform"));   // reverse edge restored
	}

	// 5. Missing lastMovement: an active peer with no transform is in nobody's set.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);   // never sends a transform
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		h.SendTransform(ca, posA, "spawn");
		assert(!HasFrameTo(h.SendTransform(ca, posA, "normal"), cb, "transform"));
	}

	// 6. Inactive session exclusion: connected-but-not-hello is never a peer/entity.
	{
		Harness h;
		std::uint32_t pa = 0;
		const auto ca = h.Activate(pa);
		const auto cInactive = h.Connect();   // no hello
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		h.SendTransform(ca, posA, "spawn");
		const auto fr = h.SendTransform(ca, posA, "normal");
		assert(!HasFrameTo(fr, cInactive, "transform"));
	}

	// 7. Self never visible: a sender never receives its own transform mirror.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		const Pos posB{ kCellX, kWorld, 300.0, 0.0 };
		h.SendTransform(ca, posA, "spawn");
		h.SendTransform(cb, posB, "spawn");
		const auto fr = h.SendTransform(ca, posA, "normal");
		for (const auto& p : FramesTo(fr, ca, "transform")) {
			assert(p.at("playerId").get<std::uint32_t>() != pa);
		}
	}

	// 8. Repeated reconciliation is idempotent: unchanged scope keeps the same recipients.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		const Pos posB{ kCellX, kWorld, 400.0, 0.0 };
		h.SendTransform(ca, posA, "spawn");
		h.SendTransform(cb, posB, "spawn");
		const bool first = HasFrameTo(h.SendTransform(ca, posA, "normal"), cb, "transform");
		const bool second = HasFrameTo(h.SendTransform(ca, posA, "normal"), cb, "transform");
		const bool third = HasFrameTo(h.SendTransform(ca, posA, "normal"), cb, "transform");
		assert(first && second && third);
	}

	// 9. Different peers get different sets.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0, pc = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const auto cc = h.Activate(pc);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		const Pos posB{ kCellX, kWorld, 100.0, 0.0 };   // with A
		const Pos posC{ kOtherWorld, kOtherWorld, 0.0, 0.0 };   // isolated
		h.SendTransform(ca, posA, "spawn");
		h.SendTransform(cb, posB, "spawn");
		h.SendTransform(cc, posC, "spawn");
		const auto frA = h.SendTransform(ca, posA, "normal");
		assert(HasFrameTo(frA, cb, "transform"));
		assert(!HasFrameTo(frA, cc, "transform"));
		const auto frC = h.SendTransform(cc, posC, "normal");
		assert(!HasFrameTo(frC, ca, "transform"));
		assert(!HasFrameTo(frC, cb, "transform"));
	}

	// 10. Disconnect removes the entity from every peer.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0, pc = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const auto cc = h.Activate(pc);
		const Pos co{ kCellX, kWorld, 0.0, 0.0 };
		h.SendTransform(ca, co, "spawn");
		h.SendTransform(cb, { kCellX, kWorld, 100.0, 0.0 }, "spawn");
		h.SendTransform(cc, { kCellX, kWorld, 200.0, 0.0 }, "spawn");
		assert(HasFrameTo(h.SendTransform(ca, co, "normal"), cb, "transform"));
		h.host.HandleDisconnected(cb, h.NextTime());
		const auto frA = h.SendTransform(ca, co, "normal");
		assert(!HasFrameTo(frA, cb, "transform"));   // B gone as a recipient
		assert(HasFrameTo(frA, cc, "transform"));    // C still visible, unperturbed
	}

	// 11. Exact recipient parity with the old StatesShareInterest predicate, across a mixed matrix.
	{
		Harness h;
		struct Peer { std::uint32_t conn; std::uint32_t player; Pos pos; };
		std::vector<Peer> peers;
		auto add = [&](const Pos& p) {
			std::uint32_t id = 0;
			const auto conn = h.Activate(id);
			peers.push_back({ conn, id, p });
		};
		add({ kCellX, kWorld, 0.0, 0.0 });            // sender base
		add({ kCellX, kWorld, 1500.0, 0.0 });         // same cell
		add({ kCellY, kWorld, 8000.0, 0.0 });         // near exterior
		add({ kCellW, kWorld, 9000.0, 0.0 });         // far exterior
		add({ kOtherWorld, kOtherWorld, 0.0, 0.0 });  // other world
		add({ kIntA, kInterior, 0.0, 0.0 });          // interior A
		add({ kIntB, kInterior, 0.0, 0.0 });          // interior B (diff interior cell)
		for (const auto& p : peers) h.SendTransform(p.conn, p.pos, "spawn");

		const auto& sender = peers.front();
		const auto fr = h.SendTransform(sender.conn, sender.pos, "normal");
		for (std::size_t i = 1; i < peers.size(); ++i) {
			const bool expected = Share(sender.pos, peers[i].pos);
			assert(HasFrameTo(fr, peers[i].conn, "transform") == expected);
		}
	}

	// 12. Durable playerState stays global while actionEvents remain interest-gated.
	{
		Harness h;
		std::uint32_t pa = 0, pb = 0, pc = 0;
		const auto ca = h.Activate(pa);
		const auto cb = h.Activate(pb);
		const auto cc = h.Activate(pc);
		const Pos posA{ kCellX, kWorld, 0.0, 0.0 };
		h.SendTransform(ca, posA, "spawn");
		h.SendTransform(cb, { kCellX, kWorld, 100.0, 0.0 }, "spawn");   // shares A
		h.SendTransform(cc, { kOtherWorld, kOtherWorld, 0.0, 0.0 }, "spawn");   // isolated from A
		const Json playerState{
			{ "type", "playerState" },
			{ "characterName", "Nomad" },
			{ "actionEvents", Json::array({ Json{
				{ "sequence", 1 }, { "type", 3 }, { "eventName", "fireSingle" } } }) }
		};
		const auto fr = h.host.HandleMessage(ca, EncodeJson(playerState), h.NextTime(), h.NextTime());
		const auto toB = FramesTo(fr, cb, "playerState");
		const auto toC = FramesTo(fr, cc, "playerState");
		assert(toB.size() == 1 && toB.front().value("displayName", std::string{}) == "Stranger");
		assert(!toB.front().contains("characterName"));
		assert(toB.front().contains("actionEvents"));            // in interest -> keeps actionEvents
		assert(toC.size() == 1 && toC.front().value("displayName", std::string{}) == "Stranger");
		assert(!toC.front().contains("characterName"));
		assert(!toC.front().contains("actionEvents"));           // out of interest -> durable only
	}

	return 0;
}
