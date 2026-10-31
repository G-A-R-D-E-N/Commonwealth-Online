#pragma once

#include "PersistenceGate.h"
#include "ServerProtocol.h"

#include "F4TQuestProtocol.h"
#include "F4TQuestStoryOrder.h"
#include "F4TReceiveFrame.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace QuestTestSupport
{
	using Json = nlohmann::json;
	using CommonwealthOnline::Server::DurableStateFileStore;
	using CommonwealthOnline::Server::OutboundFrame;
	using CommonwealthOnline::Server::PersistenceGate;
	using CommonwealthOnline::Server::ProtocolHost;
	using F4T::QuestProtocol::FormKey;
	using F4T::QuestProtocol::Mode;
	using F4T::QuestProtocol::Override;
	using F4T::QuestProtocol::OverrideEntry;

	constexpr const char* kQuest = "0A1B2C:fallout4.esm";
	constexpr const char* kOtherQuest = "0000AA:fallout4.esm";
	constexpr const char* kThirdQuest = "123456:dlc.esp";
	constexpr const char* kMixedCaseQuest = "0A1B2C:Fallout4.esm";
	constexpr const char* kMixedCaseOther = "123456:DLC.esp";
	constexpr const char* kTutorialQuest = "01CC2A:fallout4.esm";
	constexpr std::uint16_t kTutorialExitStage = 2;

	inline FormKey Key(const char* a_value)
	{
		const auto parsed = F4T::QuestProtocol::ParseFormKey(a_value);
		assert(parsed.has_value());
		return *parsed;
	}

	inline std::vector<std::uint8_t> EncodeJson(const Json& a_packet)
	{
		const auto text = a_packet.dump();
		return { text.begin(), text.end() };
	}

	inline Json DecodePacket(const OutboundFrame& a_frame)
	{
		return Json::parse(std::string{ reinterpret_cast<const char*>(a_frame.payload.data()), a_frame.payload.size() });
	}

	inline const OutboundFrame* FindFrame(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		for (const auto& frame : a_frames) {
			if (frame.connectionId == a_connectionId && DecodePacket(frame).value("type", std::string{}) == std::string{ a_type }) {
				return &frame;
			}
		}
		return nullptr;
	}

	inline bool HasPacket(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		return FindFrame(a_frames, a_connectionId, a_type) != nullptr;
	}

	inline Json RequirePacket(const std::vector<OutboundFrame>& a_frames, std::uint32_t a_connectionId, std::string_view a_type)
	{
		const auto* frame = FindFrame(a_frames, a_connectionId, a_type);
		assert(frame != nullptr);
		return DecodePacket(*frame);
	}

	inline std::vector<Json> FramesOfType(const std::vector<OutboundFrame>& a_frames, std::string_view a_type)
	{
		std::vector<Json> packets;
		for (const auto& frame : a_frames) {
			const auto packet = DecodePacket(frame);
			if (packet.value("type", std::string{}) == std::string{ a_type }) packets.push_back(packet);
		}
		return packets;
	}

	inline std::vector<OutboundFrame> Send(ProtocolHost& a_host, std::uint32_t a_connectionId, const Json& a_packet, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return a_host.HandleMessage(a_connectionId, EncodeJson(a_packet), a_monotonicSeconds, a_unixTimeSeconds);
	}

	inline Json Report(const char* a_quest, std::uint16_t a_stage, bool a_completed = false)
	{
		Json packet{ { "type", "questReport" }, { "quest", std::string{ a_quest } }, { "stage", a_stage } };
		if (a_completed) packet["completed"] = true;
		return packet;
	}

	inline Json ScopeRequestPacket(const char* a_quest, const char* a_scope)
	{
		return Json{ { "type", "questScope" }, { "quest", std::string{ a_quest } }, { "scope", std::string{ a_scope } } };
	}

	struct Joined
	{
		std::uint32_t playerId{};
		std::vector<OutboundFrame> hello;
	};

	inline Joined JoinWith(
		ProtocolHost& a_host,
		std::uint32_t a_connectionId,
		const std::string& a_profileId,
		bool a_questSync,
		double a_monotonicSeconds,
		double a_unixTimeSeconds)
	{
		const auto welcome = a_host.HandleConnected(a_connectionId, a_unixTimeSeconds);
		a_host.BindSessionProfile(a_connectionId, a_profileId);
		Json hello{ { "type", "hello" }, { "protocolVersion", 2 } };
		if (a_questSync) {
			hello["capabilities"] = Json::array(
				{ std::string{ F4T::QuestProtocol::kSyncCapability }, std::string{ F4T::QuestProtocol::kReadyCapability } });
		}
		Joined joined;
		joined.playerId = DecodePacket(welcome.front()).at("playerId").get<std::uint32_t>();
		joined.hello = a_host.HandleMessage(a_connectionId, EncodeJson(hello), a_monotonicSeconds, a_unixTimeSeconds + 0.1);
		Send(a_host, a_connectionId, Report(kTutorialQuest, kTutorialExitStage), a_monotonicSeconds - 1.0, a_unixTimeSeconds + 0.15);
		Send(a_host, a_connectionId, Json{ { "type", "questReady" } }, a_monotonicSeconds - 0.9, a_unixTimeSeconds + 0.2);
		assert(HasPacket(joined.hello, a_connectionId, "sessionReady"));
		return joined;
	}

	inline Joined Join(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return JoinWith(a_host, a_connectionId, a_profileId, true, a_monotonicSeconds, a_unixTimeSeconds);
	}

	inline Joined JoinWithoutQuestSync(ProtocolHost& a_host, std::uint32_t a_connectionId, const std::string& a_profileId, double a_monotonicSeconds, double a_unixTimeSeconds)
	{
		return JoinWith(a_host, a_connectionId, a_profileId, false, a_monotonicSeconds, a_unixTimeSeconds);
	}

	inline std::filesystem::path TempDirectory(std::string_view a_suffix)
	{
		auto directory = std::filesystem::temp_directory_path() /
			("co-server-quests-" + std::string{ a_suffix } + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		return directory;
	}

	inline F4T::QuestStory::Order FixtureStoryOrder()
	{
		Json rows = Json::array();
		for (const char* quest : { kQuest, kOtherQuest, kThirdQuest }) {
			rows.push_back(Json{
				{ "quest", std::string{ quest } },
				{ "label", "Fixture" },
				{ "requires", Json::array() },
				{ "startable", true },
				{ "source", "test fixture" },
				{ "verified", false }
			});
		}
		const auto order = F4T::QuestStory::Order::Load(Json{ { "quests", std::move(rows) } });
		assert(order.has_value());
		return *order;
	}

	inline bool EnableStoryFixture(
		ProtocolHost& a_host,
		PersistenceGate* a_persistence,
		Mode a_mode,
		std::vector<OverrideEntry> a_overrides)
	{
		if (!a_host.EnableQuestSync(a_persistence, a_mode, std::move(a_overrides))) return false;
		a_host.SetQuestStoryOrder(FixtureStoryOrder());
		return true;
	}

	inline bool EnableDefaultStory(
		ProtocolHost& a_host,
		PersistenceGate* a_persistence,
		Mode a_mode,
		std::vector<OverrideEntry> a_overrides)
	{
		if (!a_host.EnableQuestSync(a_persistence, a_mode, std::move(a_overrides))) return false;
		a_host.SetQuestStoryOrder(F4T::QuestStory::Order::Default());
		return true;
	}

	inline std::string ReadText(const std::filesystem::path& a_path)
	{
		std::ifstream input(a_path, std::ios::binary);
		assert(input.good());
		std::ostringstream buffer;
		buffer << input.rdbuf();
		return buffer.str();
	}
}
