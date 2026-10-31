#include "ServerProtocol.h"
#include "ServerSocialGraph.h"

#include "DurableStateFileStore.h"
#include "F4TGnsSnapshotEnvelope.h"
#include "PersistenceGate.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using CommonwealthOnline::Server::ProtocolStats;
	using CommonwealthOnline::Server::SocialGraph;
	using F4T::GnsSnapshotEnvelope::DecodeResult;

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	Json DecodeFrame(const OutboundFrame& a_frame)
	{
		std::span<const std::uint8_t> payload{ a_frame.payload };
		F4T::GnsSnapshotEnvelope::Header header{};
		if (F4T::GnsSnapshotEnvelope::Decode(a_frame.payload, header) == DecodeResult::Valid) {
			payload = payload.subspan(F4T::GnsSnapshotEnvelope::HeaderSize);
		}
		return Json::parse(std::string{
			reinterpret_cast<const char*>(payload.data()),
			payload.size() });
	}

	const OutboundFrame* FindFrame(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodeFrame(frame).value("type", std::string{}) == std::string{ a_type }) {
				return &frame;
			}
		}
		return nullptr;
	}

	Json RequireFrame(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		const auto* frame = FindFrame(a_frames, a_connectionId, a_type);
		assert(frame != nullptr);
		return DecodeFrame(*frame);
	}

	struct Joined
	{
		std::uint32_t playerId{};
		std::vector<OutboundFrame> hello;
	};

	Joined Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodeFrame(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(FindFrame(joined.hello, a_connectionId, "sessionReady") != nullptr);
		return joined;
	}

	Json PlayerName(const std::string& a_name)
	{
		return Json{ { "type", "playerState" }, { "characterName", a_name } };
	}

	Joined JoinLegacy(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Joined joined;
		joined.playerId = DecodeFrame(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = a_host.HandleMessage(a_connectionId, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		assert(FindFrame(joined.hello, a_connectionId, "sessionReady") != nullptr);
		return joined;
	}

	std::filesystem::path TempDirectory(std::string_view a_suffix)
	{
		auto directory = std::filesystem::temp_directory_path() /
			("co-server-social-" + std::string{ a_suffix } + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		return directory;
	}

	void TestGraphSemantics()
	{
		ProtocolStats stats{};
		SocialGraph graph{ stats };
		assert(!graph.Knows("pa", "pb"));
		assert(!graph.Knows("", "pb") && !graph.Knows("pa", ""));
		assert(graph.Knows("pa", "pa"));
		assert(graph.NameViewFor("pb", "pa", "", false).displayName.empty());
		assert(graph.NameViewFor("pb", "pa", "Stranger", false).displayName == "Stranger");
		const auto stranger = graph.NameViewFor("pb", "pa", "Alice", false);
		assert(stranger.displayName == "Stranger" && !stranger.disclosedName);
		graph.NoteKnown("pa", "pb");
		assert(graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
		const auto disclosed = graph.NameViewFor("pb", "pa", "Alice", false);
		assert(disclosed.displayName == "Alice" && disclosed.disclosedName && *disclosed.disclosedName == "Alice");
		graph.NoteKnown("", "pb");
		graph.NoteKnown("pa", "pa");
		assert(!graph.Knows("", "pb"));
		for (std::size_t index = 0; index < SocialGraph::kMaximumKnownPerProfile + 5; ++index) {
			graph.NoteKnown("pa", "other-" + std::to_string(index));
		}
		assert(!graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
		assert(graph.Knows("pa", "other-260"));
	}

	void TestGraphAdminBypass()
	{
		ProtocolStats stats{};
		SocialGraph graph{ stats };
		graph.SetAdminCheck([](const std::string& a_profileId) { return a_profileId == "admin"; });
		assert(graph.NameViewFor("admin", "pa", "Alice", false).displayName == "Alice");
		assert(graph.NameViewFor("pb", "pa", "Alice", false).displayName == "Stranger");
	}

	void TestLoadKnownNonObjectLogsUnreadable()
	{
		const auto directory = TempDirectory("unreadable");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveKnownPeople(Json::array());
			store.WaitForCommits();
		}
		ProtocolStats stats{};
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		SocialGraph graph{ stats };
		graph.NoteKnown("pa", "pb");
		std::ostringstream captured;
		auto* const previous = std::cerr.rdbuf(captured.rdbuf());
		assert(graph.Enable(&gate));
		std::cerr.rdbuf(previous);
		assert(!graph.Knows("pa", "pb"));
		assert(captured.str().find("unreadable") != std::string::npos);
		std::filesystem::remove_all(directory);
	}

	void TestGraphPersistenceRoundTrip()
	{
		const auto directory = TempDirectory("persist");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			ProtocolStats stats{};
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			graph.NoteKnown("pa", "pb");
			assert(graph.SaveNow());
			store.WaitForCommits();
		}
		{
			ProtocolStats stats{};
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
			assert(!graph.Knows("pa", "pc"));
		}
		{
			ProtocolStats stats{};
			DurableStateFileStore store(path, clock, instanceId);
			store.SaveKnownPeople(Json{ { "knownPeople", Json::array() } });
			store.WaitForCommits();
			PersistenceGate gate(store);
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(!graph.Knows("pa", "pb"));
		}
		std::filesystem::remove_all(directory);
	}

	void TestLiveRelayMasked()
	{
		ProtocolHost host{ "Social Relay" };
		assert(host.EnableSocial(nullptr));
		Join(host, 10, "pa", 1.0, 100.0);
		Join(host, 20, "pb", 1.1, 100.1);
		const auto toBob = host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		assert(RequireFrame(toBob, 20, "playerState").at("displayName") == "Stranger");
		assert(!RequireFrame(toBob, 20, "playerState").contains("characterName"));
		const auto toAlice = host.HandleMessage(20, EncodeJson(PlayerName("Bob")), 1.6, 100.6);
		assert(RequireFrame(toAlice, 10, "playerState").at("displayName") == "Stranger");
		assert(!RequireFrame(toAlice, 10, "playerState").contains("characterName"));
	}

	void TestCatchUpMasked()
	{
		ProtocolHost host{ "Social CatchUp" };
		assert(host.EnableSocial(nullptr));
		Join(host, 10, "pa", 1.0, 100.0);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		const auto bob = Join(host, 20, "pb", 2.0, 101.0);
		assert(RequireFrame(bob.hello, 20, "playerState").at("displayName") == "Stranger");
		assert(!RequireFrame(bob.hello, 20, "playerState").contains("characterName"));
	}

	void TestChatLiveAndHistoryMasked()
	{
		ProtocolHost host{ "Social Chat" };
		host.SetChatEnabled(true);
		assert(host.EnableSocial(nullptr));
		Join(host, 10, "pa", 1.0, 100.0);
		Join(host, 20, "pb", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		const auto chat = host.HandleMessage(10, EncodeJson(Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "hello" } }), 2.0, 101.0);
		assert(RequireFrame(chat, 10, "chatMessage").at("displayName") == "Alice");
		assert(RequireFrame(chat, 10, "chatMessage").at("fromName") == "Alice");
		assert(RequireFrame(chat, 20, "chatMessage").at("displayName") == "Stranger");
		assert(!RequireFrame(chat, 20, "chatMessage").contains("fromName"));
		const auto history = host.HandleMessage(20, EncodeJson(Json{ { "type", "chatHistoryRequest" } }), 2.5, 101.5);
		const auto messages = RequireFrame(history, 20, "chatHistory").at("messages");
		assert(messages.size() == 1 && messages.at(0).at("displayName") == "Stranger");
		assert(!messages.at(0).contains("fromName"));
		assert(!messages.at(0).contains("fromProfileId"));
	}

	void TestAdminSeesRealNames()
	{
		ProtocolHost host{ "Social Admin", {}, { "pb" } };
		host.SetChatEnabled(true);
		assert(host.EnableSocial(nullptr));
		Join(host, 10, "pa", 1.0, 100.0);
		Join(host, 20, "pb", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		const auto relay = host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.6, 100.6);
		assert(RequireFrame(relay, 20, "playerState").at("displayName") == "Alice");
		assert(RequireFrame(relay, 20, "playerState").at("characterName") == "Alice");
		const auto chat = host.HandleMessage(10, EncodeJson(Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "hi" } }), 2.0, 101.0);
		assert(RequireFrame(chat, 20, "chatMessage").at("displayName") == "Alice");
		assert(RequireFrame(chat, 20, "chatMessage").at("fromName") == "Alice");
		const auto history = host.HandleMessage(20, EncodeJson(Json{ { "type", "chatHistoryRequest" } }), 2.5, 101.5);
		assert(RequireFrame(history, 20, "chatHistory").at("messages").at(0).at("displayName") == "Alice");
		assert(RequireFrame(history, 20, "chatHistory").at("messages").at(0).at("fromName") == "Alice");
	}

	void TestRosterMaskedPerViewer()
	{
		ProtocolHost host{ "Social Roster" };
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "pa", 1.0, 100.0);
		Join(host, 20, "pb", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		const auto roster = host.HandleMessage(20, EncodeJson(Json{ { "type", "playerRosterRequest" } }), 2.0, 101.0);
		bool sawAlice = false;
		for (const auto frame = RequireFrame(roster, 20, "playerRoster"); const auto& player : frame.at("players")) {
			if (player.at("playerId") == 1) {
				sawAlice = true;
				assert(player.at("displayName") == "Stranger");
				assert(!player.contains("name"));
			}
		}
		assert(sawAlice);
	}

	void TestUnboundViewerRosterShape()
	{
		ProtocolHost host{ "Social Unbound" };
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		Join(host, 10, "pa", 1.0, 100.0);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		host.HandleConnected(30, 100.6);
		host.HandleMessage(30, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "capabilities", Json::array({ "social-names-v1" }) } }), 1.6, 100.7);
		host.HandleConnected(40, 100.8);
		host.HandleMessage(40, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 } }), 1.7, 100.9);
		const auto capable = host.HandleMessage(30, EncodeJson(Json{ { "type", "playerRosterRequest" } }), 2.0, 101.0);
		const auto legacy = host.HandleMessage(40, EncodeJson(Json{ { "type", "playerRosterRequest" } }), 2.1, 101.1);
		bool sawCapable = false;
		for (const auto frame = RequireFrame(capable, 30, "playerRoster"); const auto& player : frame.at("players")) {
			if (player.at("playerId") == 1) {
				sawCapable = true;
				assert(player.at("displayName") == "Stranger");
				assert(!player.contains("name"));
			}
		}
		assert(sawCapable);
		bool sawLegacy = false;
		for (const auto frame = RequireFrame(legacy, 40, "playerRoster"); const auto& player : frame.at("players")) {
			if (player.at("playerId") == 1) {
				sawLegacy = true;
				assert(player.at("displayName") == "Stranger");
				assert(player.at("name") == "Stranger");
			}
		}
		assert(sawLegacy);
	}

	void TestFactionJoinWritesKnownPair()
	{
		const auto directory = TempDirectory("learn");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Social Learn" };
			assert(host.EnableSocial(&gate));
			assert(host.EnableFactions(&gate));
			const auto alice = Join(host, 10, "pa", 1.0, 100.0);
			const auto bob = Join(host, 20, "pb", 1.1, 100.1);
			host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
			host.HandleMessage(20, EncodeJson(PlayerName("Bob")), 1.6, 100.6);
			host.HandleMessage(10, EncodeJson(Json{ { "type", "factionCreate" }, { "name", "Team" } }), 2.0, 101.0);
			const auto invite = host.HandleMessage(10, EncodeJson(Json{ { "type", "factionInvite" }, { "targetPlayerId", bob.playerId } }), 2.1, 101.1);
			assert(RequireFrame(invite, 20, "factionInviteReceived").at("displayName") == "Stranger");
			const auto inviteId = RequireFrame(invite, 20, "factionInviteReceived").at("inviteId").get<std::string>();
			const auto accept = host.HandleMessage(20, EncodeJson(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }), 2.2, 101.2);
			assert(RequireFrame(accept, 20, "playerState").at("displayName") == "Alice");
			assert(RequireFrame(accept, 20, "playerState").at("characterName") == "Alice");
			assert(RequireFrame(accept, 10, "playerState").at("displayName") == "Bob");
			assert(RequireFrame(accept, 10, "playerState").at("characterName") == "Bob");
			bool rosterShowsAlice = false;
			for (const auto frame = RequireFrame(accept, 20, "playerRoster"); const auto& player : frame.at("players")) {
				if (player.at("playerId") == alice.playerId) {
					rosterShowsAlice = player.at("displayName") == "Alice";
				}
			}
			assert(rosterShowsAlice);
			store.WaitForCommits();
			const auto stored = store.GetKnownPeople();
			const auto& people = stored.at("knownPeople");
			assert(people.at("pa").size() == 1 && people.at("pa").at(0) == "pb");
			assert(people.at("pb").size() == 1 && people.at("pb").at(0) == "pa");
		}
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolStats stats{};
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(graph.Knows("pa", "pb") && graph.Knows("pb", "pa"));
			assert(!graph.Knows("pa", "pc"));
		}
		std::filesystem::remove_all(directory);
	}

	void TestLoadedFactionMatesSeeRealNames()
	{
		const auto directory = TempDirectory("loaded-faction");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		{
			DurableStateFileStore store(path, clock, instanceId);
			PersistenceGate gate(store);
			ProtocolHost host{ "Social Seed" };
			assert(host.EnableSocial(&gate));
			assert(host.EnableFactions(&gate));
			Join(host, 10, "fa", 1.0, 100.0);
			const auto bob = Join(host, 20, "fb", 1.1, 100.1);
			host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
			host.HandleMessage(20, EncodeJson(PlayerName("Bob")), 1.6, 100.6);
			host.HandleMessage(10, EncodeJson(Json{ { "type", "factionCreate" }, { "name", "Team" } }), 2.0, 101.0);
			const auto invite = host.HandleMessage(10, EncodeJson(Json{ { "type", "factionInvite" }, { "targetPlayerId", bob.playerId } }), 2.1, 101.1);
			const auto inviteId = RequireFrame(invite, 20, "factionInviteReceived").at("inviteId").get<std::string>();
			host.HandleMessage(20, EncodeJson(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }), 2.2, 101.2);
			store.WaitForCommits();
		}
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Social Loaded" };
		host.SetChatEnabled(true);
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(&gate));
		Join(host, 10, "fa", 1.0, 200.0);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 200.5);
		const auto bob = Join(host, 20, "fb", 2.0, 201.0);
		assert(RequireFrame(bob.hello, 20, "playerState").at("displayName") == "Alice");
		assert(RequireFrame(bob.hello, 20, "playerState").at("characterName") == "Alice");
		host.HandleMessage(20, EncodeJson(PlayerName("Bob")), 2.1, 201.1);
		const auto relay = host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 2.2, 201.2);
		assert(RequireFrame(relay, 20, "playerState").at("displayName") == "Alice");
		assert(RequireFrame(relay, 20, "playerState").at("characterName") == "Alice");
		const auto chat = host.HandleMessage(10, EncodeJson(Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "hello" } }), 2.3, 201.3);
		assert(RequireFrame(chat, 20, "chatMessage").at("displayName") == "Alice");
		assert(RequireFrame(chat, 20, "chatMessage").at("fromName") == "Alice");
		const auto history = host.HandleMessage(20, EncodeJson(Json{ { "type", "chatHistoryRequest" } }), 2.4, 201.4);
		const auto messages = RequireFrame(history, 20, "chatHistory").at("messages");
		assert(messages.size() == 1 && messages.at(0).at("displayName") == "Alice");
		assert(messages.at(0).at("fromName") == "Alice");
		std::filesystem::remove_all(directory);
	}

	void TestForgetProfileUnit()
	{
		ProtocolStats stats{};
		SocialGraph graph{ stats };
		graph.NoteKnown("pa", "pb");
		graph.NoteKnown("pa", "pc");
		graph.NoteKnown("pb", "pc");
		graph.ForgetProfile("pb");
		assert(!graph.Knows("pa", "pb") && !graph.Knows("pb", "pa"));
		assert(!graph.Knows("pb", "pc") && !graph.Knows("pc", "pb"));
		assert(graph.Knows("pa", "pc") && graph.Knows("pc", "pa"));
		graph.ForgetProfile("");
		graph.ForgetProfile("nobody");
		assert(graph.Knows("pa", "pc"));
		graph.NoteKnown("pa", "acct-dddd");
		graph.NoteKnown("acct-dddd", "pe");
		graph.ForgetProfile("acct-dddd#0");
		assert(!graph.Knows("pa", "acct-dddd") && !graph.Knows("acct-dddd", "pa"));
		assert(!graph.Knows("acct-dddd", "pe") && !graph.Knows("pe", "acct-dddd"));
		assert(graph.Knows("pa", "pc"));
	}

	void TestPurgeForgetsKnownProfile()
	{
		const auto directory = TempDirectory("forget");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Social Forget" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3, 7));
		// Constraint: production sessions bind bare accounts, so the learned pair is bare too.
		Join(host, 10, "purge-acct-a", 1.0, 100.0);
		const auto bob = Join(host, 20, "purge-acct-b", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(Json{ { "type", "factionCreate" }, { "name", "Team" } }), 2.0, 101.0);
		const auto invite = host.HandleMessage(10, EncodeJson(Json{ { "type", "factionInvite" }, { "targetPlayerId", bob.playerId } }), 2.1, 101.1);
		const auto inviteId = RequireFrame(invite, 20, "factionInviteReceived").at("inviteId").get<std::string>();
		host.HandleMessage(20, EncodeJson(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }), 2.2, 101.2);
		store.WaitForCommits();
		assert(store.GetKnownPeople().at("knownPeople").at("purge-acct-b").size() == 1);
		// Constraint: disconnect stale conn before slot delete (active selection blocks it).
		host.HandleDisconnected(20, 102.0);
		host.HandleConnected(30, 102.0);
		host.BindSessionProfile(30, "purge-acct-b");
		const auto hello30 = host.HandleMessage(
			30, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 } }), 3.0, 102.1);
		assert(FindFrame(hello30, 30, "sessionReady") != nullptr);
		const auto created = host.HandleMessage(30, EncodeJson(Json{ { "type", "characterCreate" }, { "slot", 0 }, { "name", "Gone" } }), 3.1, 102.0);
		assert(RequireFrame(created, 30, "characterResult").at("ok") == true);
		const auto deleted = host.HandleMessage(30, EncodeJson(Json{ { "type", "characterDelete" }, { "slot", 0 } }), 3.2, 102.0);
		const auto challenge = RequireFrame(deleted, 30, "characterDeleteChallenge");
		const auto confirmed = host.HandleMessage(
			30,
			EncodeJson(Json{ { "type", "characterDelete" }, { "slot", 0 }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }),
			3.3,
			102.0);
		assert(RequireFrame(confirmed, 30, "characterResult").at("ok") == true);
		const double afterRetention = 102.0 + 8.0 * 86400.0;
		host.HandleMessage(30, EncodeJson(Json{ { "type", "characterList" } }), 4.0, afterRetention);
		assert(!store.GetPlayerState("purge-acct-b#0").has_value());
		store.WaitForCommits();
		const auto stored = store.GetKnownPeople();
		const auto& people = stored.at("knownPeople");
		assert(!people.contains("purge-acct-b"));
		assert(!people.contains("purge-acct-b#0"));
		for (const auto& [profileId, list] : people.items()) {
			(void)profileId;
			assert(std::find(list.begin(), list.end(), "purge-acct-b") == list.end());
			assert(std::find(list.begin(), list.end(), "purge-acct-b#0") == list.end());
		}
		{
			ProtocolStats stats{};
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(!graph.Knows("purge-acct-a", "purge-acct-b") && !graph.Knows("purge-acct-b", "purge-acct-a"));
		}
		std::filesystem::remove_all(directory);
	}

	void TestPurgeKeepsKnownWhileSiblingSlotLives()
	{
		const auto directory = TempDirectory("forget-sibling");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Social Forget Sibling" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3, 7));
		Join(host, 10, "keep-acct-a", 1.0, 100.0);
		const auto bob = Join(host, 20, "keep-acct-b", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(Json{ { "type", "factionCreate" }, { "name", "Team" } }), 2.0, 101.0);
		const auto invite = host.HandleMessage(10, EncodeJson(Json{ { "type", "factionInvite" }, { "targetPlayerId", bob.playerId } }), 2.1, 101.1);
		const auto inviteId = RequireFrame(invite, 20, "factionInviteReceived").at("inviteId").get<std::string>();
		host.HandleMessage(20, EncodeJson(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }), 2.2, 101.2);
		store.WaitForCommits();
		assert(store.GetKnownPeople().at("knownPeople").at("keep-acct-b").size() == 1);
		host.HandleDisconnected(20, 102.0);
		host.HandleConnected(30, 102.0);
		host.BindSessionProfile(30, "keep-acct-b");
		const auto hello30 = host.HandleMessage(
			30, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 } }), 3.0, 102.1);
		assert(FindFrame(hello30, 30, "sessionReady") != nullptr);
		const auto make = [&](std::uint32_t a_slot, const char* a_name, double a_time) {
			const auto created = host.HandleMessage(30, EncodeJson(Json{ { "type", "characterCreate" }, { "slot", a_slot }, { "name", a_name } }), a_time, 102.0);
			assert(RequireFrame(created, 30, "characterResult").at("ok") == true);
		};
		make(0, "GoneA", 3.1);
		make(1, "KeepsB", 3.15);
		const auto drop = [&](std::uint32_t a_slot, double a_time) {
			const auto deleted = host.HandleMessage(30, EncodeJson(Json{ { "type", "characterDelete" }, { "slot", a_slot } }), a_time, 102.0);
			const auto challenge = RequireFrame(deleted, 30, "characterDeleteChallenge");
			const auto confirmed = host.HandleMessage(
				30,
				EncodeJson(Json{ { "type", "characterDelete" }, { "slot", a_slot }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }),
				a_time + 0.1,
				102.0);
			assert(RequireFrame(confirmed, 30, "characterResult").at("ok") == true);
		};
		drop(0, 3.2);
		const double afterRetention = 102.0 + 8.0 * 86400.0;
		host.HandleMessage(30, EncodeJson(Json{ { "type", "characterList" } }), 4.0, afterRetention);
		assert(!store.GetPlayerState("keep-acct-b#0").has_value());
		assert(store.GetPlayerState("keep-acct-b#1").has_value());
		store.WaitForCommits();
		assert(store.GetKnownPeople().at("knownPeople").at("keep-acct-b").size() == 1);
		{
			ProtocolStats stats{};
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(graph.Knows("keep-acct-a", "keep-acct-b") && graph.Knows("keep-acct-b", "keep-acct-a"));
		}
		drop(1, 4.1);
		host.HandleMessage(30, EncodeJson(Json{ { "type", "characterList" } }), 5.0, afterRetention + 8.0 * 86400.0);
		assert(!store.GetPlayerState("keep-acct-b#1").has_value());
		store.WaitForCommits();
		assert(!store.GetKnownPeople().at("knownPeople").contains("keep-acct-b"));
		{
			ProtocolStats stats{};
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(!graph.Knows("keep-acct-a", "keep-acct-b") && !graph.Knows("keep-acct-b", "keep-acct-a"));
		}
		std::filesystem::remove_all(directory);
	}

	void TestEraseProfileByKeyParentScrub()
	{
		const auto directory = TempDirectory("erase-key");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		store.SaveKnownPeople(Json{ { "knownPeople", Json{ { "acct-aaaa", Json::array({ "pb" }) }, { "pb", Json::array({ "acct-aaaa", "pc" }) } } } });
		store.WaitForCommits();
		assert(store.EraseProfileByKey("acct-aaaa#0"));
		const auto dropped = store.GetKnownPeople().at("knownPeople");
		assert(!dropped.contains("acct-aaaa") && !dropped.contains("acct-aaaa#0"));
		assert(dropped.at("pb").size() == 1 && dropped.at("pb").at(0) == "pc");
		store.SaveKnownPeople(Json{ { "knownPeople", Json{ { "acct-aaaa", Json::array({ "pb" }) }, { "pb", Json::array({ "acct-aaaa" }) } } } });
		store.WaitForCommits();
		assert(!store.EraseProfileByKey("acct-aaaa#0", false));
		assert(store.GetKnownPeople().at("knownPeople").contains("acct-aaaa"));
		std::filesystem::remove_all(directory);
	}

	void TestPurgeKeepsKnownWhileSoftDeletedSiblingHoldsState()
	{
		const auto directory = TempDirectory("forget-soft-sibling");
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Social Forget Soft Sibling" };
		assert(host.EnableSocial(&gate));
		assert(host.EnableFactions(&gate));
		assert(host.EnableCharacterSlots(&gate, 3, 7));
		Join(host, 10, "soft-acct-a", 1.0, 100.0);
		const auto bob = Join(host, 20, "soft-acct-b", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(Json{ { "type", "factionCreate" }, { "name", "Team" } }), 2.0, 101.0);
		const auto invite = host.HandleMessage(10, EncodeJson(Json{ { "type", "factionInvite" }, { "targetPlayerId", bob.playerId } }), 2.1, 101.1);
		const auto inviteId = RequireFrame(invite, 20, "factionInviteReceived").at("inviteId").get<std::string>();
		host.HandleMessage(20, EncodeJson(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }), 2.2, 101.2);
		store.WaitForCommits();
		host.HandleDisconnected(20, 102.0);
		host.HandleConnected(30, 102.0);
		host.BindSessionProfile(30, "soft-acct-b");
		const auto hello30 = host.HandleMessage(
			30, EncodeJson(Json{ { "type", "hello" }, { "protocolVersion", 2 }, { "slotProtocol", 1 } }), 3.0, 102.1);
		assert(FindFrame(hello30, 30, "sessionReady") != nullptr);
		const auto make = [&](std::uint32_t a_slot, const char* a_name, double a_mono, double a_unix) {
			const auto created = host.HandleMessage(30, EncodeJson(Json{ { "type", "characterCreate" }, { "slot", a_slot }, { "name", a_name } }), a_mono, a_unix);
			assert(RequireFrame(created, 30, "characterResult").at("ok") == true);
		};
		const auto dropAt = [&](std::uint32_t a_slot, double a_mono, double a_unix) {
			const auto deleted = host.HandleMessage(30, EncodeJson(Json{ { "type", "characterDelete" }, { "slot", a_slot } }), a_mono, a_unix);
			const auto challenge = RequireFrame(deleted, 30, "characterDeleteChallenge");
			const auto confirmed = host.HandleMessage(
				30,
				EncodeJson(Json{ { "type", "characterDelete" }, { "slot", a_slot }, { "confirmToken", challenge.at("confirmToken").get<std::string>() } }),
				a_mono + 0.1,
				a_unix);
			assert(RequireFrame(confirmed, 30, "characterResult").at("ok") == true);
		};
		constexpr double kDay = 86400.0;
		make(0, "GoneA", 3.1, 102.0);
		make(1, "SoftB", 3.15, 102.0);
		dropAt(0, 3.2, 102.0);
		dropAt(1, 3.8, 102.0 + 6.0 * kDay);
		host.HandleMessage(30, EncodeJson(Json{ { "type", "characterList" } }), 4.4, 102.0 + 8.0 * kDay);
		assert(!store.GetPlayerState("soft-acct-b#0").has_value());
		assert(store.GetPlayerState("soft-acct-b#1").has_value());
		store.WaitForCommits();
		assert(store.GetKnownPeople().at("knownPeople").contains("soft-acct-b"));
		{
			ProtocolStats stats{};
			SocialGraph graph{ stats };
			assert(graph.Enable(&gate));
			assert(graph.Knows("soft-acct-a", "soft-acct-b"));
		}
		host.HandleMessage(30, EncodeJson(Json{ { "type", "characterList" } }), 5.0, 102.0 + 14.0 * kDay);
		assert(!store.GetPlayerState("soft-acct-b#1").has_value());
		store.WaitForCommits();
		assert(!store.GetKnownPeople().at("knownPeople").contains("soft-acct-b"));
		std::filesystem::remove_all(directory);
	}

	void TestLegacyClientReceivesDisplayText()
	{
		ProtocolHost host{ "Social Legacy" };
		host.SetChatEnabled(true);
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		JoinLegacy(host, 10, "acct-aaaa", 1.0, 100.0);
		const auto bob = JoinLegacy(host, 20, "pb", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		const auto toBob = host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.6, 100.6);
		assert(RequireFrame(toBob, 20, "playerState").at("displayName") == "Stranger");
		assert(RequireFrame(toBob, 20, "playerState").at("characterName") == "Stranger");
		const auto chat = host.HandleMessage(10, EncodeJson(Json{ { "type", "chatSend" }, { "channel", "global" }, { "text", "hi" } }), 2.0, 101.0);
		assert(RequireFrame(chat, 20, "chatMessage").at("displayName") == "Stranger");
		assert(RequireFrame(chat, 20, "chatMessage").at("fromName") == "Stranger");
		const auto history = host.HandleMessage(20, EncodeJson(Json{ { "type", "chatHistoryRequest" } }), 2.5, 101.5);
		const auto messages = RequireFrame(history, 20, "chatHistory").at("messages");
		assert(messages.size() == 1 && messages.at(0).at("displayName") == "Stranger");
		assert(messages.at(0).at("fromName") == "Stranger");
		const auto roster = host.HandleMessage(10, EncodeJson(Json{ { "type", "playerRosterRequest" } }), 2.6, 101.6);
		bool sawBob = false;
		for (const auto frame = RequireFrame(roster, 10, "playerRoster"); const auto& player : frame.at("players")) {
			if (player.at("playerId") == bob.playerId) {
				sawBob = true;
				assert(player.at("displayName") == "Stranger");
				assert(player.at("name") == "Stranger");
			}
		}
		assert(sawBob);
	}

	void TestSameFactionSeesRealNames()
	{
		ProtocolHost host{ "Social Faction" };
		assert(host.EnableSocial(nullptr));
		assert(host.EnableFactions(nullptr));
		const auto alice = Join(host, 10, "acct-aaaa", 1.0, 100.0);
		const auto bob = Join(host, 20, "pb", 1.1, 100.1);
		host.HandleMessage(10, EncodeJson(PlayerName("Alice")), 1.5, 100.5);
		host.HandleMessage(20, EncodeJson(PlayerName("Bob")), 1.6, 100.6);
		host.HandleMessage(10, EncodeJson(Json{ { "type", "factionCreate" }, { "name", "Team" } }), 2.0, 101.0);
		const auto invite = host.HandleMessage(10, EncodeJson(Json{ { "type", "factionInvite" }, { "targetPlayerId", bob.playerId } }), 2.1, 101.1);
		assert(RequireFrame(invite, 20, "factionInviteReceived").at("displayName") == "Stranger");
		assert(!RequireFrame(invite, 20, "factionInviteReceived").contains("fromName"));
		const auto inviteId = RequireFrame(invite, 20, "factionInviteReceived").at("inviteId").get<std::string>();
		host.HandleMessage(20, EncodeJson(Json{ { "type", "factionInviteResponse" }, { "inviteId", inviteId }, { "accept", true } }), 2.2, 101.2);
		const auto roster = host.HandleMessage(10, EncodeJson(Json{ { "type", "playerRosterRequest" } }), 2.3, 101.3);
		bool sawBob = false;
		for (const auto frame = RequireFrame(roster, 10, "playerRoster"); const auto& player : frame.at("players")) {
			if (player.at("playerId") == bob.playerId) {
				sawBob = true;
				assert(player.at("displayName") == "Bob");
				assert(player.at("name") == "Bob");
			}
		}
		assert(sawBob);
		assert(alice.playerId != bob.playerId);
	}
}

int main()
{
	TestGraphSemantics();
	TestGraphAdminBypass();
	TestLoadKnownNonObjectLogsUnreadable();
	TestGraphPersistenceRoundTrip();
	TestLiveRelayMasked();
	TestCatchUpMasked();
	TestChatLiveAndHistoryMasked();
	TestLegacyClientReceivesDisplayText();
	TestAdminSeesRealNames();
	TestRosterMaskedPerViewer();
	TestUnboundViewerRosterShape();
	TestSameFactionSeesRealNames();
	TestFactionJoinWritesKnownPair();
	TestLoadedFactionMatesSeeRealNames();
	TestForgetProfileUnit();
	TestPurgeForgetsKnownProfile();
	TestPurgeKeepsKnownWhileSiblingSlotLives();
	TestPurgeKeepsKnownWhileSoftDeletedSiblingHoldsState();
	TestEraseProfileByKeyParentScrub();
	return 0;
}
