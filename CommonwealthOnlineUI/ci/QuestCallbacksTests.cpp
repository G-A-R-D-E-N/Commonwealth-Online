#include "QuestCallbacks.h"

#include <cassert>
#include <cstddef>
#include <cstring>
#include <nlohmann/json.hpp>
#include <string>

namespace
{
	using F4T::Quests::Action;
	using F4T::Quests::ActionKind;
	using F4T::Quests::IQuestHost;
	using F4T::Quests::Scope;

	class FakeHost final : public IQuestHost
	{
	public:
		std::string state{ R"({"enabled":true})" };
		std::string errorText{ "no data" };
		std::string lastAction;
		bool        ok{ true };
		bool        fillsBuffer{ false };

		bool GetStateJson(
			char*       a_jsonOut,
			std::size_t a_jsonOutSize,
			char*       a_errorBuf,
			std::size_t a_errorBufSize) override
		{
			if (a_errorBuf && a_errorBufSize > 0) {
				a_errorBuf[0] = '\0';
			}
			if (!ok) {
				if (a_errorBuf && a_errorBufSize > errorText.size()) {
					std::memcpy(a_errorBuf, errorText.data(), errorText.size());
					a_errorBuf[errorText.size()] = '\0';
				}
				return false;
			}
			if (fillsBuffer) {
				std::memset(a_jsonOut, 'x', a_jsonOutSize);
				return true;
			}
			std::memcpy(a_jsonOut, state.data(), state.size());
			a_jsonOut[state.size()] = '\0';
			return true;
		}

		void SendAction(const char* a_actionJson) override
		{
			lastAction = a_actionJson ? a_actionJson : "";
		}
	};

	std::size_t CountOccurrences(const std::string& a_text, const std::string& a_needle)
	{
		std::size_t count = 0;
		std::size_t at = a_text.find(a_needle);
		while (at != std::string::npos) {
			++count;
			at = a_text.find(a_needle, at + a_needle.size());
		}
		return count;
	}

	constexpr std::size_t kMaxQuests = 32;
	constexpr std::size_t kMaxFriendsPerQuest = 16;
	constexpr std::size_t kMaxNotices = 128;
	constexpr std::size_t kMaxQuestNameChars = 128;
	constexpr std::size_t kMaxNoticeTextChars = 256;
	// 124 + ".esp" must equal the server's kMaximumPluginChars (128) so the key stays inside kMaximumQuestKeyChars.
	constexpr std::size_t kMaxPluginNameChars = 124;

	std::string BuildMaximumState()
	{
		nlohmann::json state{
			{ "enabled", true },
			{ "mode", "coop-story" },
			{ "quests", nlohmann::json::array() },
			{ "notices", nlohmann::json::array() }
		};

		for (std::size_t questIndex = 0; questIndex < kMaxQuests; ++questIndex) {
			nlohmann::json friends = nlohmann::json::array();
			for (std::size_t friendIndex = 0; friendIndex < kMaxFriendsPerQuest; ++friendIndex) {
				friends.push_back({
					{ "id", std::string(64, '7') },
					{ "name", std::string(kMaxQuestNameChars, 'F') },
					{ "stage", 65535 }
				});
			}
			state["quests"].push_back({
				{ "quest", "1A2B3C:" + std::string(kMaxPluginNameChars, 'M') + ".esp" },
				{ "name", std::string(kMaxQuestNameChars, 'Q') },
				{ "stage", 65535 },
				{ "scope", "shared" },
				{ "friends", friends }
			});
		}

		for (std::size_t noticeIndex = 0; noticeIndex < kMaxNotices; ++noticeIndex) {
			state["notices"].push_back({
				{ "text", std::string(kMaxNoticeTextChars, 'N') },
				{ "time", "12:34" }
			});
		}

		return state.dump();
	}

	void TestQuestKey()
	{
		assert(F4T::Quests::IsValidQuestKey("1A2B3C:Fallout4.esm"));
		assert(F4T::Quests::IsValidQuestKey("000800:CommonwealthOnline.esp"));
		assert(F4T::Quests::IsValidQuestKey("abc123:MyMod.esl"));
		assert(F4T::Quests::IsValidQuestKey("1A2B3C:My Mod.esp"));
		assert(F4T::Quests::IsValidQuestKey("1A2B3C:Fallout4.ESM"));
		assert(F4T::Quests::IsValidQuestKey("abc123:My Mod.EsL"));
		assert(!F4T::Quests::IsValidQuestKey("FF000800:Fallout4.esm"));
		assert(!F4T::Quests::IsValidQuestKey("12345:Fallout4.esm"));
		assert(!F4T::Quests::IsValidQuestKey("1A2B3C:Fallout4"));
		assert(!F4T::Quests::IsValidQuestKey("1A2B3C:Fallout4.esp.bak"));
		assert(!F4T::Quests::IsValidQuestKey("1A2B3C:"));
		assert(!F4T::Quests::IsValidQuestKey(":Fallout4.esm"));
		assert(!F4T::Quests::IsValidQuestKey("1A2B3C:../Fallout4.esm"));
		assert(!F4T::Quests::IsValidQuestKey("1A2B3C:Fallout4.esm:2"));
		assert(!F4T::Quests::IsValidQuestKey("1G2B3C:Fallout4.esm"));
		assert(!F4T::Quests::IsValidQuestKey(""));
		assert(!F4T::Quests::IsValidQuestKey(std::string(200, 'a') + ":Fallout4.esm"));

		const std::string aMaximumPlugin = std::string(124, 'P') + ".esp";
		assert(F4T::Quests::IsValidQuestKey("1A2B3C:" + aMaximumPlugin));
		const std::string anOverlongPlugin = std::string(125, 'P') + ".esp";
		assert(!F4T::Quests::IsValidQuestKey("1A2B3C:" + anOverlongPlugin));
	}

	void TestScopeNames()
	{
		assert(std::string(F4T::Quests::ScopeName(Scope::Shared)) == "shared");
		assert(std::string(F4T::Quests::ScopeName(Scope::Solo)) == "solo");
		assert(std::string(F4T::Quests::ScopeName(Scope::Party)) == "party");
		assert(std::string(F4T::Quests::ScopeName(Scope::Held)) == "held");
		assert(F4T::Quests::ParseScope("shared") == Scope::Shared);
		assert(F4T::Quests::ParseScope("solo") == Scope::Solo);
		assert(F4T::Quests::ParseScope("party") == Scope::Party);
		assert(F4T::Quests::ParseScope("held") == Scope::Held);
		assert(!F4T::Quests::ParseScope("Shared"));
		assert(!F4T::Quests::ParseScope(""));
	}

	void TestParseAction()
	{
		const auto solo = F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"solo"})");
		assert(solo && solo->kind == ActionKind::SetScope);
		assert(solo->scope == Scope::Solo);
		assert(solo->quest == "1A2B3C:Fallout4.esm");

		const auto shared = F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"shared"})");
		assert(shared && shared->kind == ActionKind::SetScope && shared->scope == Scope::Shared);

		const auto party = F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"party"})");
		assert(party && party->kind == ActionKind::SetScope && party->scope == Scope::Party);

		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"held"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","quest":"nope","scope":"solo"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","scope":"solo"})"));

		assert(!F4T::Quests::ParseAction(R"({"action":"joinFriend","quest":"1A2B3C:Fallout4.esm","playerId":"7"})"));

		const auto close = F4T::Quests::ParseAction(R"({"action":"close"})");
		assert(close && close->kind == ActionKind::Close);
		const auto refresh = F4T::Quests::ParseAction(R"({"action":"refresh"})");
		assert(refresh && refresh->kind == ActionKind::Refresh);

		assert(!F4T::Quests::ParseAction(R"({"action":"kick","quest":"1A2B3C:Fallout4.esm"})"));
		assert(!F4T::Quests::ParseAction(R"({"quest":"1A2B3C:Fallout4.esm","scope":"solo"})"));
		assert(!F4T::Quests::ParseAction("{"));
		assert(!F4T::Quests::ParseAction("[1,2]"));
		assert(!F4T::Quests::ParseAction(""));
	}

	void TestActionRoundTrip()
	{
		const Action scopeAction{ ActionKind::SetScope, Scope::Solo, "1A2B3C:Fallout4.esm" };
		const auto scopeJson = F4T::Quests::FormatActionJson(scopeAction);
		assert(scopeJson == R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"solo"})");
		const auto reparsed = F4T::Quests::ParseAction(scopeJson);
		assert(reparsed && reparsed->kind == scopeAction.kind && reparsed->scope == Scope::Solo);
		assert(reparsed->quest == scopeAction.quest);

		assert(F4T::Quests::FormatActionJson(Action{ ActionKind::Close, Scope::Shared, {} }) == R"({"action":"close"})");
		assert(F4T::Quests::FormatActionJson(Action{ ActionKind::Refresh, Scope::Shared, {} }) == R"({"action":"refresh"})");
	}

	void TestFormatStateScript()
	{
		const auto script = F4T::Quests::FormatStateScript(R"({"enabled":true,"mode":"coop-story"})");
		assert(script == R"(if(window.coQuestSetState){window.coQuestSetState({"enabled":true,"mode":"coop-story"});})");
		assert(CountOccurrences(script, "window.coQuestSetState(") == 1);

		const std::string payload = R"({"name":"x\");window.coQuestSetState({\"enabled\":false});alert(1)//"})";
		const auto injected = F4T::Quests::FormatStateScript(payload);
		const std::string wrapper = "if(window.coQuestSetState){window.coQuestSetState(";
		assert(injected.rfind(wrapper, 0) == 0);
		assert(injected.size() > wrapper.size() + 3);
		assert(injected.compare(injected.size() - 3, 3, ");}") == 0);
		const auto payloadJson = injected.substr(wrapper.size(), injected.size() - wrapper.size() - 3);
		assert(payloadJson == nlohmann::json::parse(payload).dump());
		assert(payloadJson.find("\\\"") != std::string::npos);
		assert(injected.find('\n') == std::string::npos);

		const auto broken = F4T::Quests::FormatStateScript("{not json");
		assert(broken == "if(window.coQuestSetState){window.coQuestSetState(" +
			F4T::Quests::UnavailableStateJson() + ");}");
		assert(CountOccurrences(broken, "window.coQuestSetState(") == 1);

		const auto empty = F4T::Quests::FormatStateScript("");
		assert(empty.find(F4T::Quests::UnavailableStateJson()) != std::string::npos);

		const auto unavailable = F4T::Quests::UnavailableStateJson();
		assert(unavailable == R"({"enabled":false,"mode":"","notices":[],"quests":[]})");
	}

	void TestStateBufferBoundary()
	{
		const auto maximum = BuildMaximumState();
		assert(maximum.size() + 1 <= F4T::Quests::kStateJsonMaxBytes);
		assert(F4T::Quests::IsValidQuestKey(
			nlohmann::json::parse(maximum)["quests"][0]["quest"].get<std::string>()));

		FakeHost host;
		host.state = maximum;
		F4T::Quests::SetHost(&host);

		std::string error;
		assert(F4T::Quests::PullStateJson(error) == maximum);
		assert(error.empty());

		host.fillsBuffer = true;
		error.clear();
		assert(F4T::Quests::PullStateJson(error) == F4T::Quests::UnavailableStateJson());
		assert(error.find(std::to_string(F4T::Quests::kStateJsonMaxBytes)) != std::string::npos);

		F4T::Quests::SetHost(nullptr);
	}

	void TestHostStatePull()
	{
		F4T::Quests::SetHost(nullptr);
		assert(F4T::Quests::GetHost() == nullptr);
		std::string error;
		assert(F4T::Quests::PullStateJson(error) == F4T::Quests::UnavailableStateJson());
		assert(!error.empty());

		FakeHost host;
		F4T::Quests::SetHost(&host);
		assert(F4T::Quests::GetHost() == &host);

		host.state = R"({"enabled":true,"mode":"free-roam","quests":[],"notices":[]})";
		error.clear();
		assert(F4T::Quests::PullStateJson(error) == host.state);
		assert(error.empty());

		host.ok = false;
		error.clear();
		assert(F4T::Quests::PullStateJson(error) == F4T::Quests::UnavailableStateJson());
		assert(error == "no data");

		host.ok = true;
		host.fillsBuffer = true;
		error.clear();
		assert(F4T::Quests::PullStateJson(error) == F4T::Quests::UnavailableStateJson());
		assert(error.find("buffer") != std::string::npos);

		host.fillsBuffer = false;
		F4T::Quests::ForwardAction(Action{ ActionKind::SetScope, Scope::Shared, "1A2B3C:Fallout4.esm" });
		assert(host.lastAction == R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"shared"})");

		F4T::Quests::SetHost(nullptr);
		F4T::Quests::ForwardAction(Action{ ActionKind::SetScope, Scope::Shared, "1A2B3C:Fallout4.esm" });
		assert(host.lastAction == R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"shared"})");
	}
}

int main()
{
	TestQuestKey();
	TestScopeNames();
	TestParseAction();
	TestActionRoundTrip();
	TestFormatStateScript();
	TestStateBufferBoundary();
	TestHostStatePull();
	return 0;
}
