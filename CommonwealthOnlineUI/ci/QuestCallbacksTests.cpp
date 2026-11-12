#include "QuestCallbacks.h"
#include "F4TViewJson.h"

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
		assert(std::string(F4T::Quests::ScopeName(Scope::Fireteam)) == "fireteam");
		assert(F4T::Quests::ParseScope("shared") == Scope::Shared);
		assert(F4T::Quests::ParseScope("solo") == Scope::Solo);
		assert(F4T::Quests::ParseScope("party") == Scope::Party);
		assert(F4T::Quests::ParseScope("held") == Scope::Held);
		assert(F4T::Quests::ParseScope("fireteam") == Scope::Fireteam);
		assert(!F4T::Quests::ParseScope("Fireteam"));
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

	bool SameAction(const Action& a_left, const Action& a_right)
	{
		return a_left.kind == a_right.kind && a_left.scope == a_right.scope && a_left.quest == a_right.quest &&
			a_left.target == a_right.target && a_left.inviteId == a_right.inviteId && a_left.accept == a_right.accept;
	}

	void ExpectRoundTrip(const Action& a_action, const std::string& a_json)
	{
		const auto json = F4T::Quests::FormatActionJson(a_action);
		assert(json == a_json);
		const auto parsed = F4T::Quests::ParseAction(json);
		assert(parsed && SameAction(*parsed, a_action));
	}

	void TestFireteamParse()
	{
		const auto invite = F4T::Quests::ParseAction(R"({"action":"fireteamInvite","target":7})");
		assert(invite && invite->kind == ActionKind::FireteamInvite && invite->target == 7);
		const auto kick = F4T::Quests::ParseAction(R"({"action":"fireteamKick","target":4294967295})");
		assert(kick && kick->kind == ActionKind::FireteamKick && kick->target == 4294967295U);
		assert(F4T::Quests::ParseAction(R"({"action":"fireteamInvite","target":1})"));

		const auto leave = F4T::Quests::ParseAction(R"({"action":"fireteamLeave"})");
		assert(leave && leave->kind == ActionKind::FireteamLeave);

		const auto accept = F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"i-7","accept":true})");
		assert(accept && accept->kind == ActionKind::FireteamRespond && accept->inviteId == "i-7" && accept->accept);
		const auto decline = F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"inv-9_A","accept":false})");
		assert(decline && decline->kind == ActionKind::FireteamRespond && decline->inviteId == "inv-9_A" && !decline->accept);
		assert(F4T::Quests::ParseAction(nlohmann::json{ { "action", "fireteamRespond" }, { "inviteId", std::string(64, 'i') }, { "accept", true } }.dump()));

		const auto scope = F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"fireteam"})");
		assert(scope && scope->kind == ActionKind::SetScope && scope->scope == Scope::Fireteam);
	}

	void TestFireteamRejects()
	{
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamInvite"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamInvite","target":0})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamInvite","target":4294967296})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamKick","target":18446744073709551615})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamKick","target":-1})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamKick","target":7.5})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamKick","target":"7"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamKick","target":true})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","accept":true})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"i-7"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"i-7","accept":"true"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":7,"accept":true})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"","accept":true})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"../i-7","accept":true})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"fireteamRespond","inviteId":"i 7","accept":true})"));
		assert(!F4T::Quests::ParseAction(nlohmann::json{ { "action", "fireteamRespond" }, { "inviteId", std::string(65, 'i') }, { "accept", true } }.dump()));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","scope":"fireteam"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"Fireteam"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":5})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","quest":5,"scope":"solo"})"));
		assert(!F4T::Quests::ParseAction(R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":5})"));
	}

	void TestFireteamRoundTrip()
	{
		ExpectRoundTrip(Action{ ActionKind::FireteamInvite, Scope::Shared, {}, 7, {}, false }, R"({"action":"fireteamInvite","target":7})");
		ExpectRoundTrip(Action{ ActionKind::FireteamKick, Scope::Shared, {}, 4294967295U, {}, false }, R"({"action":"fireteamKick","target":4294967295})");
		ExpectRoundTrip(Action{ ActionKind::FireteamRespond, Scope::Shared, {}, 0, "i-7", true }, R"({"accept":true,"action":"fireteamRespond","inviteId":"i-7"})");
		ExpectRoundTrip(Action{ ActionKind::FireteamRespond, Scope::Shared, {}, 0, "i-8", false }, R"({"accept":false,"action":"fireteamRespond","inviteId":"i-8"})");
		ExpectRoundTrip(Action{ ActionKind::FireteamLeave, Scope::Shared, {} }, R"({"action":"fireteamLeave"})");
		ExpectRoundTrip(Action{ ActionKind::SetScope, Scope::Fireteam, "1A2B3C:Fallout4.esm" }, R"({"action":"setScope","quest":"1A2B3C:Fallout4.esm","scope":"fireteam"})");

		FakeHost host;
		F4T::Quests::SetHost(&host);
		F4T::Quests::ForwardAction(Action{ ActionKind::FireteamInvite, Scope::Shared, {}, 9, {}, false });
		assert(host.lastAction == R"({"action":"fireteamInvite","target":9})");
		F4T::Quests::ForwardAction(Action{ ActionKind::FireteamLeave, Scope::Shared, {} });
		assert(host.lastAction == R"({"action":"fireteamLeave"})");
		F4T::Quests::SetHost(nullptr);
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

void TestViewJsonReplacesInvalidUtf8()
{
	const auto dumped = F4T::ViewJson::Dump(nlohmann::json{ { "name", std::string("bad\xff") } });
	assert(dumped == "{\"name\":\"bad\xEF\xBF\xBD\"}");
	assert(F4T::ViewJson::Dump(nlohmann::json{ { "count", 1 } }, -1, true) == "{\"count\":1}");
}

int main()
{
	TestViewJsonReplacesInvalidUtf8();
	TestQuestKey();
	TestScopeNames();
	TestParseAction();
	TestActionRoundTrip();
	TestFireteamParse();
	TestFireteamRejects();
	TestFireteamRoundTrip();
	TestFormatStateScript();
	TestStateBufferBoundary();
	TestHostStatePull();
	return 0;
}
