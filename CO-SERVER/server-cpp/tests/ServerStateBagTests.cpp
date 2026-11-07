#include "ServerStateBag.h"
#include "ServerProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::ProtocolHost;
	using CommonwealthOnline::Server::StateBagStore;
	using CommonwealthOnline::Server::kMaximumStateBagFrameBytes;
	namespace StateBag = F4T::StateBagProtocol;
	using StateBag::BagId;
	using StateBag::PlayerBag;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodePacket(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{
			reinterpret_cast<const char*>(a_frame.payload.data()),
			a_frame.payload.size() });
	}

	std::vector<Json> StateBagPackets(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			if (frame.connectionId != a_connectionId || frame.payload.empty()) {
				continue;
			}
			auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == "stateBag") {
				packets.push_back(std::move(packet));
			}
		}
		return packets;
	}

	bool HasSessionReady(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == "sessionReady") {
				return true;
			}
		}
		return false;
	}

	struct Hello
	{
		std::uint32_t playerId{};
		std::vector<OutboundFrame> frames;
	};

	Hello Join(ProtocolHost& a_host, std::uint32_t a_connectionId, bool a_capable, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		Json hello{ { "type", "hello" }, { "protocolVersion", 2 } };
		if (a_capable) {
			hello["capabilities"] = Json::array({ std::string{ StateBag::kCapability } });
		}
		Hello joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.frames = a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(HasSessionReady(joined.frames, a_connectionId));
		return joined;
	}

	void TestKeyAndValueLimits()
	{
		StateBagStore store;
		assert(store.Set(BagId{}, "", Json(1)) == "invalid key");
		assert(store.Set(BagId{}, "has space", Json(1)) == "invalid key");
		assert(store.Set(BagId{}, std::string(StateBag::kMaximumKeyChars + 1, 'k'), Json(1)) == "invalid key");
		assert(store.Set(BagId{}, std::string(StateBag::kMaximumKeyChars, 'k'), Json(1)).empty());
		assert(store.Set(BagId{}, "ok", Json(std::string(4094, 'x'))).empty());
		assert(store.Set(BagId{}, "big", Json(std::string(4095, 'x'))) == "value too large");
		assert(!store.Get(BagId{}, "big").has_value());
		assert(store.Keys(BagId{}).size() == 2);
	}

	void TestKeyCountLimit()
	{
		StateBagStore store;
		for (std::size_t index = 0; index < StateBag::kMaximumKeysPerBag; ++index) {
			assert(store.Set(BagId{}, "k" + std::to_string(index), Json(index)).empty());
		}
		assert(store.Set(BagId{}, "overflow", Json(1)) == "too many keys");
		assert(store.Set(BagId{}, "k0", Json(2)).empty());
		assert(store.Keys(BagId{}).size() == StateBag::kMaximumKeysPerBag);
		assert(store.Set(PlayerBag(7), "overflow", Json(1)).empty());
	}

	void TestTotalBytesLimit()
	{
		StateBagStore store;
		const std::string value(3998, 'x');
		for (char letter = 'a'; letter < 'a' + 16; ++letter) {
			assert(store.Set(BagId{}, std::string{ 'k', letter }, Json(value)).empty());
		}
		assert(store.Set(BagId{}, "kq", Json(value)) == "bag too large");
		assert(store.Set(BagId{}, "ka", Json(1)).empty());
		assert(store.Set(BagId{}, "kq", Json(value)).empty());
	}

	void TestNullDeletesAndErase()
	{
		StateBagStore store;
		assert(store.Set(PlayerBag(7), "a", Json(1)).empty());
		assert(store.Get(PlayerBag(7), "a") == Json(1));
		assert(store.Set(PlayerBag(7), "a", Json(nullptr)).empty());
		assert(!store.Get(PlayerBag(7), "a").has_value());
		assert(!store.Erase(PlayerBag(7), "a"));
		assert(!store.DropPlayer(7));
		assert(store.Set(BagId{}, "missing", Json(nullptr)).empty());
		assert(store.Set(BagId{}, "kept", Json("v")).empty());
		assert(store.Erase(BagId{}, "kept"));
		assert(store.Keys(BagId{}).empty());
	}

	void TestBagsAreSeparateAndDropPlayer()
	{
		StateBagStore store;
		assert(store.Set(BagId{}, "shared", Json(1)).empty());
		assert(store.Set(PlayerBag(7), "shared", Json(2)).empty());
		assert(store.Set(PlayerBag(8), "own", Json(3)).empty());
		assert(store.Get(BagId{}, "shared") == Json(1));
		assert(store.Get(PlayerBag(7), "shared") == Json(2));
		assert(store.Keys(PlayerBag(7)) == std::vector<std::string>{ "shared" });
		assert(store.DropPlayer(7));
		assert(!store.Get(PlayerBag(7), "shared").has_value());
		assert(!store.DropPlayer(7));
		assert(store.Get(PlayerBag(8), "own") == Json(3));
		assert(store.Get(BagId{}, "shared") == Json(1));
	}

	void TestSnapshotShape()
	{
		StateBagStore store;
		const auto empty = store.SnapshotPackets(7);
		assert(empty.size() == 1);
		assert(empty.front() == (Json{ { "type", "stateBag" }, { "op", "snapshot" }, { "bags", Json{ { "global", Json::object() } } } }));
		assert(store.Set(BagId{}, "g", Json(1)).empty());
		assert(store.Set(PlayerBag(7), "a", Json("x")).empty());
		assert(store.Set(PlayerBag(8), "b", Json("y")).empty());
		const auto ownerSeven = store.SnapshotPackets(7);
		assert(ownerSeven.size() == 1);
		assert((ownerSeven.front().at("bags") == Json{ { "global", Json{ { "g", 1 } } }, { "player:7", Json{ { "a", "x" } } } }));
		assert(store.SnapshotPackets(8).front().at("bags").at("player:8").at("b") == "y");
		assert(!store.SnapshotPackets(8).front().at("bags").contains("player:7"));
		assert(!store.SnapshotPackets(9).front().at("bags").contains("player:7"));
	}

	void TestLargeSnapshotIsChunked()
	{
		StateBagStore store;
		const std::string value(3998, 'x');
		for (char letter = 'a'; letter < 'a' + 16; ++letter) {
			assert(store.Set(BagId{}, std::string{ 'k', letter }, Json(value)).empty());
		}
		assert(store.Set(PlayerBag(7), "own", Json(value)).empty());
		const auto packets = store.SnapshotPackets(7);
		assert(packets.size() == 18);
		for (const auto& packet : packets) {
			assert(static_cast<std::size_t>(packet.dump().size()) <= kMaximumStateBagFrameBytes);
		}
		assert(packets.front().at("op") == "snapshot");
		assert(packets.front().at("bags").at("global").empty());
		assert(packets.front().at("bags").at("player:7").empty());
		Json rebuilt = packets.front().at("bags");
		for (std::size_t index = 1; index < packets.size(); ++index) {
			const auto& packet = packets[index];
			assert(packet.at("op") == "set");
			rebuilt[packet.at("bag").get<std::string>()][packet.at("key").get<std::string>()] = packet.at("value");
		}
		assert(rebuilt.at("global").size() == 16);
		assert(rebuilt.at("player:7").size() == 1);
	}

	void TestChangeAndClearPackets()
	{
		assert((StateBagStore::ChangePacket(BagId{}, "k", Json(5)) == Json{ { "type", "stateBag" }, { "op", "set" }, { "bag", "global" }, { "key", "k" }, { "value", 5 } }));
		assert((StateBagStore::ChangePacket(PlayerBag(7), "k", Json(nullptr)) == Json{ { "type", "stateBag" }, { "op", "delete" }, { "bag", "player:7" }, { "key", "k" } }));
		assert((StateBagStore::ClearPacket(PlayerBag(7)) == Json{ { "type", "stateBag" }, { "op", "clear" }, { "bag", "player:7" } }));
	}

	void TestProtocolSnapshotAndBroadcastReachCapableSessions()
	{
		ProtocolHost host{ "State Bag" };
		const auto capable = Join(host, 10, true, 100.0, 1000.0);
		const auto initial = StateBagPackets(capable.frames, 10);
		assert(initial.size() == 1);
		assert(initial.front().at("op") == "snapshot");
		assert((initial.front().at("bags") == Json{ { "global", Json::object() } }));

		const auto plain = Join(host, 20, false, 100.2, 1000.2);
		assert(StateBagPackets(plain.frames, 20).empty());

		const auto second = Join(host, 30, true, 100.4, 1000.4);

		std::string failure;
		const auto global = host.SetStateBag(BagId{}, "motd", Json("hello"), failure);
		assert(failure.empty());
		assert(global.size() == 2);
		assert(StateBagPackets(global, 10).front() == (Json{ { "type", "stateBag" }, { "op", "set" }, { "bag", "global" }, { "key", "motd" }, { "value", "hello" } }));
		assert(StateBagPackets(global, 30).front().at("bag") == "global");
		assert(StateBagPackets(global, 20).empty());

		const auto personal = host.SetStateBag(PlayerBag(second.playerId), "faction", Json("red"), failure);
		assert(failure.empty());
		assert(personal.size() == 1);
		assert(personal.front().connectionId == 30);
		assert(StateBagPackets(personal, 10).empty());
		assert(StateBagPackets(personal, 30).front().at("bag") == "player:" + std::to_string(second.playerId));

		const auto rejected = host.SetStateBag(BagId{}, "bad key", Json(1), failure);
		assert(rejected.empty());
		assert(failure == "invalid key");

		const auto cleared = host.SetStateBag(BagId{}, "motd", Json(nullptr), failure);
		assert(failure.empty());
		assert(StateBagPackets(cleared, 10).front().at("op") == "delete");
		assert(StateBagPackets(cleared, 30).size() == 1);

		const auto late = Join(host, 40, true, 101.0, 1001.0);
		const auto lateSnapshot = StateBagPackets(late.frames, 40);
		assert(lateSnapshot.size() == 1);
		assert(lateSnapshot.front().at("bags").at("global").empty());
		assert(!lateSnapshot.front().at("bags").contains("player:" + std::to_string(second.playerId)));

		const auto left = host.HandleDisconnected(30, 1002.0);
		assert(StateBagPackets(left, 10).empty());
		assert(StateBagPackets(left, 30).empty());
		assert(StateBagPackets(left, 40).empty());

		const auto gone = host.SetStateBag(PlayerBag(second.playerId), "faction", Json("blue"), failure);
		assert(gone.empty());
		assert(failure == "player not connected");
	}

	void TestNoOpSetsAreNotBroadcast()
	{
		ProtocolHost host{ "State Bag" };
		Join(host, 10, true, 100.0, 1000.0);
		std::string failure;
		assert(host.SetStateBag(BagId{}, "motd", Json("hello"), failure).size() == 1);
		failure.clear();
		assert(host.SetStateBag(BagId{}, "motd", Json("hello"), failure).empty());
		assert(failure.empty());
		assert(host.SetStateBag(BagId{}, "motd", Json(nullptr), failure).size() == 1);
		failure.clear();
		assert(host.SetStateBag(BagId{}, "motd", Json(nullptr), failure).empty());
		assert(failure.empty());
		assert(host.SetStateBag(BagId{}, "bad", Json(std::string("\xED\xA0\x80", 3)), failure).empty());
		assert(failure == "invalid value");
	}

	void TestClientCannotWrite()
	{
		ProtocolHost host{ "State Bag" };
		Join(host, 10, true, 100.0, 1000.0);
		const auto before = host.Stats().packetsRejected;
		const auto forged = host.HandleMessage(
			10,
			EncodeJson(Json{ { "type", "stateBag" }, { "op", "set" }, { "bag", "global" }, { "key", "motd" }, { "value", "forged" } }),
			100.5,
			1000.5);
		assert(host.Stats().packetsRejected == before + 1);
		assert(StateBagPackets(forged, 10).empty());
		const auto late = Join(host, 30, true, 101.0, 1001.0);
		assert(StateBagPackets(late.frames, 30).front().at("bags").at("global").empty());
	}
	void TestResourceViewReachesOnlyCapableSessions()
	{
		ProtocolHost host{ "Resource" };
		const auto capable = Join(host, 10, true, 100.0, 1000.0);
		Join(host, 20, false, 100.0, 1000.0);
		std::string failure;
		const auto opened = host.OpenResourceView(capable.playerId, "quest-hub", "web/menu.html", failure);
		assert(failure.empty());
		assert(opened.size() == 1 && opened.front().connectionId == 10);
		assert(DecodePacket(opened.front()) == (Json{ { "type", "resourceView" }, { "op", "open" }, { "resource", "quest-hub" }, { "view", "web/menu.html" } }));
		const auto closed = host.CloseResourceView(capable.playerId, "quest-hub");
		assert(closed.size() == 1 && DecodePacket(closed.front()) == (Json{ { "type", "resourceView" }, { "op", "close" }, { "resource", "quest-hub" } }));
		failure.clear();
		assert(host.OpenResourceView(capable.playerId + 99, "quest-hub", "menu.html", failure).empty());
		assert(failure == "player has no state-bag-v1 session");
		failure.clear();
		assert(host.OpenResourceView(capable.playerId, "quest-hub", "../menu.html", failure).empty());
		assert(failure == "invalid view path");
		failure.clear();
		assert(host.OpenResourceView(capable.playerId, "Quest Hub", "menu.html", failure).empty());
		assert(failure == "invalid resource name");
		assert(host.CloseResourceView(capable.playerId, "Quest Hub").empty());
	}

	void TestResourceMessagesAreValidatedAndRateLimited()
	{
		ProtocolHost host{ "Resource" };
		const auto capable = Join(host, 10, true, 100.0, 1000.0);
		Join(host, 20, false, 100.0, 1000.0);
		const Json message{ { "type", "resourceMessage" }, { "resource", "quest-hub" }, { "data", { { "action", "buy" } } } };

		int calls = 0;
		std::uint32_t lastPlayer = 0;
		std::string lastResource;
		Json lastData;
		host.SetResourceMessageHandler([&](std::uint32_t a_playerId, std::string_view a_resource, const Json& a_data) {
			++calls;
			lastPlayer = a_playerId;
			lastResource = std::string{ a_resource };
			lastData = a_data;
		});
		std::string openFailure;
		assert(host.OpenResourceView(capable.playerId, "quest-hub", "web/menu.html", openFailure).size() == 1);
		assert(openFailure.empty());
		host.HandleMessage(10, EncodeJson(message), 200.0, 1000.5);
		assert(calls == 1 && lastPlayer == capable.playerId && lastResource == "quest-hub" && lastData.at("action") == "buy");
		host.HandleMessage(20, EncodeJson(message), 200.0, 1000.5);
		assert(calls == 1);
		const auto before = host.Stats().packetsRejected;
		host.HandleMessage(10, EncodeJson(Json{ { "type", "resourceMessage" }, { "resource", "Bad Name" }, { "data", 1 } }), 200.0, 1000.5);
		assert(host.Stats().packetsRejected == before + 1);
		for (int i = 0; i < 30; ++i) {
			host.HandleMessage(10, EncodeJson(message), 200.0, 1000.5);
		}
		assert(calls == 20);
		host.HandleMessage(10, EncodeJson(Json{ { "type", "resourceMessage" }, { "resource", "never-opened" }, { "data", { { "action", "buy" } } } }), 200.0, 1010.0);
		assert(calls == 20);
		assert(host.CloseResourceView(capable.playerId, "quest-hub").size() == 1);
		host.HandleMessage(10, EncodeJson(message), 200.0, 1020.0);
		assert(calls == 20);
	}
}

int main()
{
	TestKeyAndValueLimits();
	TestKeyCountLimit();
	TestTotalBytesLimit();
	TestNullDeletesAndErase();
	TestBagsAreSeparateAndDropPlayer();
	TestSnapshotShape();
	TestLargeSnapshotIsChunked();
	TestChangeAndClearPackets();
	TestProtocolSnapshotAndBroadcastReachCapableSessions();
	TestNoOpSetsAreNotBroadcast();
	TestClientCannotWrite();
	TestResourceViewReachesOnlyCapableSessions();
	TestResourceMessagesAreValidatedAndRateLimited();
	return 0;
}
