#include "PersistenceGate.h"
#include "ServerProtocol.h"
#include "ServerQuestAdminAction.h"
#include "ServerQuestTestSupport.h"

#include "F4TQuestProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using CommonwealthOnline::Server::RunQuestGateAdminAction;
	using CommonwealthOnline::Server::RunQuestModeAdminAction;
	using CommonwealthOnline::Server::RunQuestSoloAdminAction;
	using CommonwealthOnline::Server::RunQuestSoloSelectorAdminAction;
	using CommonwealthOnline::Server::RuntimeCountersJson;
	using CommonwealthOnline::Server::WithQuestAdminHooks;
	using F4T::QuestProtocol::FormKey;
	using F4T::QuestProtocol::Mode;

	constexpr const char* kQuest = "0A1B2C:fallout4.esm";
	constexpr const char* kOtherQuest = "123456:dlc.esp";

	FormKey Key(const char* a_value)
	{
		const auto parsed = F4T::QuestProtocol::ParseFormKey(a_value);
		assert(parsed.has_value());
		return *parsed;
	}

	Json Decode(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() });
	}

	std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	std::vector<OutboundFrame> Connect(ProtocolHost& a_host, std::uint32_t a_connectionId, double a_unixTimeSeconds)
	{
		return a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
	}

	bool AdvertisesQuestSync(const std::vector<OutboundFrame>& a_welcome)
	{
		assert(!a_welcome.empty());
		const auto packet = Decode(a_welcome.front());
		const auto capabilities = packet.find("capabilities");
		if (capabilities == packet.end() || !capabilities->is_array()) {
			return false;
		}
		for (const auto& entry : *capabilities) {
			if (entry.is_string() && entry.get<std::string>() == std::string{ F4T::QuestProtocol::kSyncCapability }) {
				return true;
			}
		}
		return false;
	}

	void Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		(void)Connect(a_host, a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		const Json hello{
			{ "type", "hello" },
			{ "protocolVersion", 2 },
			{ "capabilities",
				Json::array({ std::string{ F4T::QuestProtocol::kSyncCapability }, std::string{ F4T::QuestProtocol::kReadyCapability } }) }
		};
		(void)a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		const Json tutorial{
			{ "type", "questReport" },
			{ "quest", "01CC2A:fallout4.esm" },
			{ "stage", F4T::QuestProtocol::kTutorialExitStage }
		};
		(void)a_host.HandleMessage(a_connectionId, EncodeJson(tutorial), a_monotonicSeconds + 0.05, a_unixTimeSeconds + 0.15);
		const Json ready{ { "type", "questReady" } };
		(void)a_host.HandleMessage(a_connectionId, EncodeJson(ready), a_monotonicSeconds + 0.1, a_unixTimeSeconds + 0.2);
	}

	Json Report(const char* a_quest, std::uint16_t a_stage)
	{
		return Json{ { "type", "questReport" }, { "quest", std::string{ a_quest } }, { "stage", a_stage } };
	}

	std::size_t CountType(const std::vector<OutboundFrame>& a_frames, std::string_view a_type)
	{
		std::size_t count = 0;
		for (const auto& frame : a_frames) {
			if (Decode(frame).value("type", std::string{}) == std::string{ a_type }) {
				++count;
			}
		}
		return count;
	}

	std::filesystem::path TempDirectory()
	{
		auto directory = std::filesystem::temp_directory_path() /
			("co-server-quest-admin-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		return directory;
	}

	void TestCapabilityAdvertisement()
	{
		ProtocolHost disabled{ "Quests Off" };
		assert(!AdvertisesQuestSync(Connect(disabled, 10, 1000.0)));

		ProtocolHost enabled{ "Quests On" };
		assert(QuestTestSupport::EnableStoryFixture(enabled, nullptr, Mode::CoopStory, {}));
		assert(AdvertisesQuestSync(Connect(enabled, 10, 1000.0)));
	}

	void TestModeMessages()
	{
		ProtocolHost host{ "Quests Admin Mode" };
		std::vector<OutboundFrame> sent;
		const auto send = [&](std::vector<OutboundFrame>& a_frames) {
			for (const auto& frame : a_frames) sent.push_back(frame);
		};

		auto result = RunQuestModeAdminAction(host, "", 1000.0, send);
		assert(!result.ok);
		assert(result.message == "Quest sync is not enabled on this server.");

		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);

		result = RunQuestModeAdminAction(host, "", 1001.0, send);
		assert(result.ok);
		assert(result.message == "Quest mode is coop-story.");
		assert(result.data.at("mode") == "coop-story");

		result = RunQuestModeAdminAction(host, "warp", 1002.0, send);
		assert(!result.ok);
		assert(result.message.find("Invalid quest mode 'warp'") == 0);
		assert(host.QuestMode() == Mode::CoopStory);

		result = RunQuestModeAdminAction(host, "coop-story", 1003.0, send);
		assert(result.ok);
		assert(result.message == "Quest mode is already coop-story.");
		assert(sent.empty());

		result = RunQuestModeAdminAction(host, "faction-war", 1004.0, send);
		assert(result.ok);
		assert(result.message == "Quest mode set to faction-war.");
		assert(host.QuestMode() == Mode::FactionWar);
		assert(CountType(sent, "questPolicy") == 1);
		assert(Decode(sent.back()).at("mode") == "faction-war");

		result = RunQuestModeAdminAction(host, "faction-war", 1005.0, send);
		assert(result.ok);
		assert(result.message == "Quest mode is already faction-war.");
		assert(CountType(sent, "questPolicy") == 1);
	}

	void TestGateMessages()
	{
		ProtocolHost host{ "Quests Admin Gate" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		std::vector<OutboundFrame> sent;
		const auto send = [&](std::vector<OutboundFrame>& a_frames) {
			for (const auto& frame : a_frames) sent.push_back(frame);
		};

		auto result = RunQuestGateAdminAction(host, Key(kOtherQuest), 31, true, 1001.0, send);
		assert(!result.ok);
		assert(result.message == "No quest gate is set for " + std::string{ kOtherQuest } + " stage 31.");
		assert(sent.empty());

		result = RunQuestGateAdminAction(host, Key(kQuest), 30, false, 1002.0, send);
		assert(result.ok);
		assert(result.message == "Gated quest stage " + std::string{ kQuest } + " stage 30.");
		assert(host.IsQuestGated(Key(kQuest), 30));
		assert(CountType(sent, "questPolicy") == 1);
		assert(result.data.at("quest") == kQuest);
		assert(result.data.at("stage") == 30);

		result = RunQuestGateAdminAction(host, Key(kQuest), 30, false, 1003.0, send);
		assert(result.ok);
		assert(result.message == "Quest gate for " + std::string{ kQuest } + " stage 30 was already set.");
		assert(CountType(sent, "questPolicy") == 1);

		const auto gatedReport = host.HandleMessage(10, EncodeJson(Report(kQuest, 30)), 103.0, 1003.0);
		assert(CountType(gatedReport, "questStage") == 0);
		assert(host.QuestStatus().at("held").size() == 1);

		result = RunQuestGateAdminAction(host, Key(kQuest), 30, true, 1004.0, send);
		assert(result.ok);
		assert(result.message == "Released quest gate " + std::string{ kQuest } + " stage 30.");
		assert(!host.IsQuestGated(Key(kQuest), 30));
		assert(CountType(sent, "questStage") == 1);
		assert(host.QuestStatus().at("held").empty());
	}

	void TestSoloMessages()
	{
		{
			ProtocolHost disabled{ "Quests Admin Solo Off" };
			std::vector<OutboundFrame> none;
			const auto sink = [&](std::vector<OutboundFrame>& a_frames) {
				for (const auto& frame : a_frames) none.push_back(frame);
			};
			const auto refused = RunQuestSoloAdminAction(disabled, "p10", Key(kQuest), true, 1000.0, sink);
			assert(!refused.ok);
			assert(refused.message == std::string{ CommonwealthOnline::Server::kQuestSyncDisabledMessage });
			assert(none.empty());
			const auto gateRefused = RunQuestGateAdminAction(disabled, Key(kQuest), 30, false, 1000.0, sink);
			assert(!gateRefused.ok);
			assert(gateRefused.message == std::string{ CommonwealthOnline::Server::kQuestSyncDisabledMessage });
			assert(none.empty());
		}

		ProtocolHost host{ "Quests Admin Solo" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		std::vector<OutboundFrame> sent;
		const auto send = [&](std::vector<OutboundFrame>& a_frames) {
			for (const auto& frame : a_frames) sent.push_back(frame);
		};

		auto result = RunQuestSoloAdminAction(host, "p10", Key(kQuest), false, 1001.0, send);
		assert(result.ok);
		assert(result.message == "Quest " + std::string{ kQuest } + " is not solo for p10.");
		assert(sent.empty());

		result = RunQuestSoloAdminAction(host, "p10", Key(kQuest), true, 1002.0, send);
		assert(result.ok);
		assert(result.message == "Quest " + std::string{ kQuest } + " is now solo for p10.");
		assert(host.IsQuestSolo("p10", Key(kQuest)));
		assert(CountType(sent, "questPolicy") == 1);
		assert(result.data.at("profileId") == "p10");

		result = RunQuestSoloAdminAction(host, "p10", Key(kQuest), true, 1003.0, send);
		assert(result.ok);
		assert(result.message == "Quest " + std::string{ kQuest } + " is already solo for p10.");
		assert(CountType(sent, "questPolicy") == 1);

		result = RunQuestSoloAdminAction(host, "p10", Key(kQuest), false, 1004.0, send);
		assert(result.ok);
		assert(result.message == "Cleared solo quest " + std::string{ kQuest } + " for p10.");
		assert(!host.IsQuestSolo("p10", Key(kQuest)));
		assert(CountType(sent, "questPolicy") == 2);
	}

	void TestSoloSelectorMessages()
	{
		const auto resolver = [](std::string_view a_selector) -> std::optional<std::string> {
			if (a_selector == "42") return std::string{ "p42" };
			return std::nullopt;
		};
		std::vector<OutboundFrame> sent;
		const auto send = [&](std::vector<OutboundFrame>& a_frames) {
			for (const auto& frame : a_frames) sent.push_back(frame);
		};

		ProtocolHost disabled{ "Quests Admin Selector Off" };
		auto result = RunQuestSoloSelectorAdminAction(disabled, "42", Key(kQuest), true, 1000.0, send, resolver);
		assert(!result.ok);
		assert(result.message == std::string{ CommonwealthOnline::Server::kQuestSyncDisabledMessage });
		assert(sent.empty());

		ProtocolHost host{ "Quests Admin Selector" };
		assert(QuestTestSupport::EnableStoryFixture(host, nullptr, Mode::CoopStory, {}));
		Join(host, 10, "p42", 100.0, 1000.0);

		result = RunQuestSoloSelectorAdminAction(host, "99", Key(kQuest), true, 1001.0, send, resolver);
		assert(!result.ok);
		assert(result.message == "No connected player matching 99.");
		assert(sent.empty());

		result = RunQuestSoloSelectorAdminAction(host, "42", Key(kQuest), true, 1002.0, send, resolver);
		assert(result.ok);
		assert(result.message == "Quest " + std::string{ kQuest } + " is now solo for p42.");
		assert(host.IsQuestSolo("p42", Key(kQuest)));
		assert(CountType(sent, "questPolicy") == 1);
	}

	struct FakeConnection
	{
		bool active{};
		std::uint32_t playerId{};
		std::string profileId;
	};

	void TestProfileSelectorRefusesUnknownProfiles()
	{
		const auto directory = TempDirectory();
		DurableStateFileStore store(directory / "durable-state.json", [] { return 1000.0; }, [] { return std::string{ "0123456789abcdef0123456789abcdef" }; });
		PersistenceGate gate(store);
		assert(store.EnsureProfileSecret("offline", "0123456789abcdef0123456789abcdef"));
		ProtocolHost host{ "Quests Admin Known Profile" };
		assert(host.EnableQuestSync(&gate, Mode::CoopStory, {}));
		Join(host, 10, "p42", 100.0, 1000.0);
		const std::unordered_map<std::uint32_t, FakeConnection> connections{
			{ 10, FakeConnection{ true, 42, "p42" } },
			{ 11, FakeConnection{ false, 43, "gone" } }
		};
		assert(CommonwealthOnline::Server::IsKnownQuestProfile("p42", nullptr, connections));
		assert(!CommonwealthOnline::Server::IsKnownQuestProfile("gone", nullptr, connections));
		assert(!CommonwealthOnline::Server::IsKnownQuestProfile("offline", nullptr, connections));
		assert(CommonwealthOnline::Server::IsKnownQuestProfile("offline", &gate, connections));
		assert(!CommonwealthOnline::Server::IsKnownQuestProfile("ghost", &gate, connections));

		std::vector<OutboundFrame> sent;
		const auto hooks = WithQuestAdminHooks({}, host, &gate, connections, [&](std::vector<OutboundFrame>& a_frames) {
			for (const auto& frame : a_frames) sent.push_back(frame);
		}, [] { return 1001.0; });
		auto result = hooks.questSolo("profile:ghost", Key(kQuest), true);
		assert(!result.ok);
		assert(result.message == "No known profile matching profile:ghost.");
		assert(!host.IsQuestSolo("ghost", Key(kQuest)));
		result = hooks.questSolo("99", Key(kQuest), true);
		assert(!result.ok && result.message == "No connected player matching 99.");
		result = hooks.questSolo("profile:offline", Key(kQuest), true);
		assert(result.ok);
		assert(host.IsQuestSolo("offline", Key(kQuest)));
		result = hooks.questSolo("42", Key(kOtherQuest), true);
		assert(result.ok);
		assert(host.IsQuestSolo("p42", Key(kOtherQuest)));
		store.WaitForCommits();
		std::filesystem::remove_all(directory);
	}

	void TestRuntimeCountersIncludeQuestScopes()
	{
		CommonwealthOnline::Server::ProtocolStats stats;
		stats.packetsReceived = 7;
		stats.respawnRequestsRejected = 2;
		stats.questScopesReceived = 5;
		stats.questScopesRejected = 1;
		stats.questScopesApplied = 3;
		stats.voiceReceived = 9;
		const auto counters = RuntimeCountersJson(stats);
		assert(counters.at("packets_received") == 7);
		assert(counters.at("respawn_requests_rejected") == 2);
		assert(counters.at("quest_scopes_received") == 5);
		assert(counters.at("quest_scopes_rejected") == 1);
		assert(counters.at("quest_scopes_applied") == 3);
		assert(counters.at("voice").at("received") == 9);
		assert(counters.size() == 14);
	}

	void TestPersistenceFailureMessages()
	{
		const auto directory = TempDirectory();
		const auto path = directory / "durable-state.json";
		const auto clock = [] { return 1000.0; };
		const auto instanceId = [] { return std::string{ "0123456789abcdef0123456789abcdef" }; };
		DurableStateFileStore store(path, clock, instanceId);
		PersistenceGate gate(store);
		ProtocolHost host{ "Quests Admin Persist" };
		assert(QuestTestSupport::EnableStoryFixture(host, &gate, Mode::CoopStory, {}));
		Join(host, 10, "p10", 100.0, 1000.0);
		assert(host.SetQuestGate(Key(kQuest), 30, false, 1000.5).size() == 1);
		store.WaitForCommits();

		const auto backup = std::filesystem::path{ path.string() + ".bak" };
		std::filesystem::remove_all(backup);
		std::filesystem::create_directories(backup / "occupied");

		std::vector<OutboundFrame> sent;
		const auto send = [&](std::vector<OutboundFrame>& a_frames) {
			for (const auto& frame : a_frames) sent.push_back(frame);
		};

		auto result = RunQuestModeAdminAction(host, "free-roam", 1001.0, send);
		assert(!result.ok);
		assert(result.message == "Quest mode could not be saved; the durable quest write failed.");
		assert(host.QuestMode() == Mode::CoopStory);

		result = RunQuestGateAdminAction(host, Key(kOtherQuest), 12, false, 1002.0, send);
		assert(!result.ok);
		assert(result.message == "Quest gate for " + std::string{ kOtherQuest } + " stage 12 could not be applied; the gate table is full or the durable quest write failed.");
		assert(!host.IsQuestGated(Key(kOtherQuest), 12));

		result = RunQuestGateAdminAction(host, Key(kQuest), 30, true, 1003.0, send);
		assert(!result.ok);
		assert(result.message == "Quest gate for " + std::string{ kQuest } + " stage 30 could not be released; the durable quest write failed.");
		assert(host.IsQuestGated(Key(kQuest), 30));

		result = RunQuestSoloAdminAction(host, "p10", Key(kQuest), true, 1004.0, send);
		assert(!result.ok);
		assert(result.message == "Solo quest " + std::string{ kQuest } + " could not be set for p10; the solo table is full or the durable quest write failed.");
		assert(!host.IsQuestSolo("p10", Key(kQuest)));
		assert(sent.empty());

		std::filesystem::remove_all(backup);
		std::filesystem::remove_all(directory);
	}
}

int main()
{
	TestCapabilityAdvertisement();
	TestModeMessages();
	TestGateMessages();
	TestSoloMessages();
	TestSoloSelectorMessages();
	TestProfileSelectorRefusesUnknownProfiles();
	TestRuntimeCountersIncludeQuestScopes();
	TestPersistenceFailureMessages();
	return 0;
}
