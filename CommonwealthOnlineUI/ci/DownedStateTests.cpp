#include "F4TDownedUiState.h"

#include <cassert>
#include <chrono>
#include <string>
#include <string_view>

using F4T::DownedUi::Choice;
using F4T::DownedUi::FormatSetStateScript;
using F4T::DownedUi::IsEchoableId;
using F4T::DownedUi::IsKnownPoint;
using F4T::DownedUi::IsOverlayVisible;
using F4T::DownedUi::IsRespawnMenuVisible;
using F4T::DownedUi::kMaxFriends;
using F4T::DownedUi::kMaxPoints;
using F4T::DownedUi::kMaxRemainingMs;
using F4T::DownedUi::ParseChoice;
using F4T::DownedUi::ParsePhase;
using F4T::DownedUi::ParseState;
using F4T::DownedUi::Phase;
using F4T::DownedUi::PhaseName;
using F4T::DownedUi::SanitizeText;
using F4T::DownedUi::ShouldPushStateScript;
using F4T::DownedUi::State;

namespace
{
	constexpr std::string_view kOverlayEntry = "coDownedSetState";
	constexpr std::string_view kRespawnEntry = "coRespawnSetState";

	struct ScriptParts
	{
		std::string entry;
		std::string payload;
	};

	ScriptParts SplitScript(const std::string& a_script)
	{
		constexpr std::string_view kHead = "if(window.";
		constexpr std::string_view kCall = "){window.";
		constexpr std::string_view kSuffix = ");}";

		assert(a_script.starts_with(kHead));
		assert(a_script.ends_with(kSuffix));
		const auto open = a_script.find(kCall, kHead.size());
		assert(open != std::string::npos);
		const auto callEnd = open + kCall.size();
		const auto paren = a_script.find('(', callEnd);
		assert(paren != std::string::npos);

		ScriptParts parts{};
		parts.entry = a_script.substr(kHead.size(), open - kHead.size());
		assert(a_script.substr(callEnd, paren - callEnd) == parts.entry);
		parts.payload = a_script.substr(paren + 1, a_script.size() - paren - 1 - kSuffix.size());
		return parts;
	}

	State RequireState(std::string_view a_json)
	{
		auto state = ParseState(a_json);
		assert(state.has_value());
		return *state;
	}

	void TestPhase()
	{
		assert(ParsePhase("downed") == Phase::Downed);
		assert(ParsePhase("dead") == Phase::Dead);
		assert(ParsePhase("hidden") == Phase::Hidden);
		assert(!ParsePhase("").has_value());
		assert(!ParsePhase("DOWNED").has_value());
		assert(!ParsePhase("reviving").has_value());

		assert(std::string_view(PhaseName(Phase::Hidden)) == "hidden");
		assert(std::string_view(PhaseName(Phase::Downed)) == "downed");
		assert(std::string_view(PhaseName(Phase::Dead)) == "dead");

		assert(IsKnownPoint("sanctuary"));
		assert(IsKnownPoint("lastbed"));
		assert(IsKnownPoint("checkpoint"));
		assert(!IsKnownPoint("friend"));
		assert(!IsKnownPoint("Sanctuary"));
		assert(!IsKnownPoint(""));
	}

	void TestText()
	{
		assert(SanitizeText("Bo", 32) == "Bo");
		assert(SanitizeText("Bo\nne", 32) == "Bone");
		assert(SanitizeText("Bo\nnne", 32) == "Bonne");
		assert(SanitizeText(std::string("a\tb\r\nc"), 32) == "abc");
		assert(SanitizeText("\xc3\xa9t\xc3\xa9", 32) == "t");
		assert(SanitizeText("abcdef", 3) == "abc");
		assert(SanitizeText("", 8).empty());

		assert(IsEchoableId("profile-1"));
		assert(IsEchoableId(std::string(64, 'x')));
		assert(!IsEchoableId(std::string(65, 'x')));
		assert(!IsEchoableId(""));
		assert(!IsEchoableId("a\nb"));
	}

	void TestMalformedPushKeepsPreviousState()
	{
		for (const auto* payload : { "", "not json", "[]", "null", "42", "\"downed\"", "{}", R"({"foo":1})",
									 R"({"phase":7})", R"({"phase":"respawn"})", R"({"phase":"Downed"})" }) {
			assert(!ParseState(payload).has_value());
		}

		const auto oversized = std::string(70 * 1024, 'x');
		assert(!ParseState(oversized).has_value());

		assert(RequireState(R"({"phase":"hidden"})").phase == Phase::Hidden);
	}

	void TestRemainingMs()
	{
		assert(RequireState(R"({"phase":"downed","remainingMs":12345})").remainingMs == 12345);
		assert(RequireState(R"({"phase":"downed","remainingMs":120000})").remainingMs == 120000);
		assert(RequireState(R"({"phase":"downed","remainingMs":600000})").remainingMs == kMaxRemainingMs);
		assert(RequireState(R"({"phase":"downed","remainingMs":600001})").remainingMs == kMaxRemainingMs);
		assert(RequireState(R"({"phase":"downed","remainingMs":1000000000})").remainingMs == kMaxRemainingMs);
		assert(RequireState(R"({"phase":"downed","remainingMs":-5})").remainingMs == 0);
		assert(RequireState(R"({"phase":"downed","remainingMs":0})").remainingMs == 0);
		assert(RequireState(R"({"phase":"downed","remainingMs":1200.9})").remainingMs == 1200);
		assert(RequireState(R"({"phase":"downed","remainingMs":"1200"})").remainingMs == 0);
		assert(RequireState(R"({"phase":"downed","remainingMs":true})").remainingMs == 0);
		assert(RequireState(R"({"phase":"downed","remainingMs":null})").remainingMs == 0);
	}

	void TestRescuers()
	{
		const auto none = RequireState(R"({"phase":"downed","rescuer":null})");
		assert(!none.rescuer.active);
		assert(!none.reviving.active);

		const auto wrongType = RequireState(R"({"phase":"downed","rescuer":"Bo","reviving":7})");
		assert(!wrongType.rescuer.active);
		assert(!wrongType.reviving.active);

		const auto named = RequireState(R"({"phase":"downed","rescuer":{"name":"Bo","progress":0.5}})");
		assert(named.rescuer.active);
		assert(named.rescuer.name == "Bo");
		assert(named.rescuer.progress == 0.5);

		const auto empty = RequireState(R"({"phase":"downed","rescuer":{}})");
		assert(empty.rescuer.active);
		assert(empty.rescuer.name.empty());
		assert(empty.rescuer.progress == 0.0);

		const auto clamped = RequireState(R"({"phase":"downed","rescuer":{"progress":-4}})");
		assert(clamped.rescuer.progress == 0.0);
		const auto high = RequireState(R"({"phase":"downed","rescuer":{"progress":4}})");
		assert(high.rescuer.progress == 1.0);
		const auto bogus = RequireState(R"({"phase":"downed","rescuer":{"progress":"half"}})");
		assert(bogus.rescuer.progress == 0.0);

		const auto truncated = RequireState(R"({"phase":"downed","rescuer":{"name":"AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDD"}})");
		assert(truncated.rescuer.name.size() == 32);
		assert(truncated.rescuer.name == "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDD");

		const auto reviving = RequireState(R"({"phase":"hidden","reviving":{"name":"Cait","progress":0.25}})");
		assert(reviving.reviving.active);
		assert(reviving.reviving.name == "Cait");
		assert(reviving.reviving.progress == 0.25);
		assert(reviving.phase == Phase::Hidden);
	}

	void TestPoints()
	{
		const auto state = RequireState(
			R"({"phase":"dead","points":[)"
			R"({"id":"sanctuary","label":"Sanctuary Hills","available":true},)"
			R"({"id":"lastbed","label":"Last bed","available":false},)"
			R"({"id":"lastbed","label":"Dupe"},)"
			R"({"id":"nowhere","label":"Nope","available":true},)"
			R"({"id":7,"label":"Bad id"},)"
			R"("string",)"
			R"({"id":"sanctuary","label":"No availability flag"}]})");

		assert(state.points.size() == 4);
		assert(state.points[0].id == "sanctuary");
		assert(state.points[0].label == "Sanctuary Hills");
		assert(state.points[0].available);
		assert(state.points[1].id == "lastbed");
		assert(state.points[1].label == "Last bed");
		assert(!state.points[1].available);
		assert(state.points[2].id == "lastbed");
		assert(state.points[2].label == "Dupe");
		assert(!state.points[2].available);
		assert(state.points[3].id == "sanctuary");
		assert(state.points[3].label == "No availability flag");
		assert(!state.points[3].available);

		std::string many = R"({"phase":"dead","points":[)";
		for (std::size_t index = 0; index < kMaxPoints + 4; ++index) {
			if (index != 0) {
				many += ",";
			}
			many += R"({"id":"sanctuary","label":"L","available":true})";
		}
		many += "]}";
		assert(RequireState(many).points.size() == kMaxPoints);

		const auto longLabel = RequireState(R"({"phase":"dead","points":[{"id":"sanctuary","label":"AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDEEEEEEEEEEFFFFGGGG"}]})");
		assert(longLabel.points.size() == 1);
		assert(longLabel.points[0].label.size() == 48);
		assert(longLabel.points[0].label == "AAAAAAAAAABBBBBBBBBBCCCCCCCCCCDDDDEEEEEEEEEEFFFF");

		const auto notArray = RequireState(R"({"phase":"dead","points":"sanctuary"})");
		assert(notArray.points.empty());
	}

	void TestFriends()
	{
		const auto state = RequireState(
			R"({"phase":"dead","friends":[)"
			R"({"id":"profile-1","name":"Bo"},)"
			R"({"id":"","name":"No id"},)"
			R"({"name":"Missing id"},)"
			R"({"id":42,"name":"Bad id"},)"
			R"({"id":"has\nnewline","name":"Bad id"},)"
			R"({"id":"profile-2"}]})");

		assert(state.friends.size() == 2);
		assert(state.friends[0].id == "profile-1");
		assert(state.friends[0].name == "Bo");
		assert(state.friends[1].id == "profile-2");
		assert(state.friends[1].name.empty());

		std::string many = R"({"phase":"dead","friends":[)";
		for (std::size_t index = 0; index < kMaxFriends + 4; ++index) {
			if (index != 0) {
				many += ",";
			}
			many += R"({"id":"p","name":"N"})";
		}
		many += "]}";
		assert(RequireState(many).friends.size() == kMaxFriends);

		const auto longId = ParseState(std::string(R"({"phase":"dead","friends":[{"id":")") + std::string(80, 'x') + R"(","name":"N"}]})");
		assert(longId.has_value());
		assert(longId->friends.empty());
	}

	void TestVisibility()
	{
		const auto hiddenState = RequireState(R"({"phase":"hidden"})");
		assert(!IsOverlayVisible(hiddenState));
		assert(!IsRespawnMenuVisible(hiddenState));

		const auto downed = RequireState(R"({"phase":"downed","remainingMs":1000})");
		assert(IsOverlayVisible(downed));
		assert(!IsRespawnMenuVisible(downed));

		const auto dead = RequireState(R"({"phase":"dead"})");
		assert(!IsOverlayVisible(dead));
		assert(IsRespawnMenuVisible(dead));

		const auto rescuing = RequireState(R"({"phase":"hidden","reviving":{"name":"Cait","progress":0.1}})");
		assert(IsOverlayVisible(rescuing));
		assert(!IsRespawnMenuVisible(rescuing));

		const auto deadAndReviving = RequireState(R"({"phase":"dead","reviving":{"name":"Cait","progress":0.4}})");
		assert(IsOverlayVisible(deadAndReviving));
		assert(IsRespawnMenuVisible(deadAndReviving));
	}

	void TestScriptCache()
	{
		constexpr PrismaView kHandleA = 7;
		constexpr PrismaView kHandleB = 8;
		const std::string script = "if(window.coDownedSetState){window.coDownedSetState({});}";
		const std::string other = "if(window.coDownedSetState){window.coDownedSetState({\"phase\":\"downed\"});}";

		assert(ShouldPushStateScript("", 0, kHandleA, script));
		assert(!ShouldPushStateScript(script, kHandleA, kHandleA, script));
		assert(ShouldPushStateScript(script, kHandleA, kHandleA, other));
		assert(ShouldPushStateScript(script, kHandleA, kHandleB, script));
		assert(!ShouldPushStateScript(script, 0, 0, script));
	}

	void TestControllerBindRetryTiming()
	{
		using Timing = F4T::DownedUi::ControllerBindRetryTiming;
		const auto t0 = Timing::Clock::time_point{};

		Timing timing{};
		assert(timing.CanAttempt(t0));
		timing.RecordAttempt(t0);
		assert(timing.attempts == 1);
		assert(!timing.CanAttempt(t0));
		assert(!timing.CanAttempt(t0 + std::chrono::milliseconds(249)));
		assert(timing.CanAttempt(t0 + std::chrono::milliseconds(250)));

		Timing deferred{};
		deferred.ScheduleRetry(t0);
		assert(!deferred.CanAttempt(t0));
		assert(deferred.CanAttempt(t0 + std::chrono::milliseconds(250)));

		Timing capped{};
		auto now = t0;
		for (std::size_t i = 0; i < capped.maxAttempts; ++i) {
			assert(capped.CanAttempt(now));
			capped.RecordAttempt(now);
			now += std::chrono::milliseconds(250);
		}
		assert(capped.attempts == capped.maxAttempts);
		assert(!capped.CanAttempt(now + std::chrono::seconds(30)));

		capped.Reset();
		assert(capped.CanAttempt(now + std::chrono::seconds(30)));
	}

	void TestScript()
	{
		const auto state = RequireState(
			R"({"phase":"dead","remainingMs":42000,)"
			R"("rescuer":{"name":"Bo \"B\"","progress":0.5},)"
			R"("points":[{"id":"sanctuary","label":"Sanctuary Hills","available":true}],)"
			R"("friends":[{"id":"profile-1","name":"Cait"}]})");

		const auto overlayScript = FormatSetStateScript(state, kOverlayEntry);
		const auto respawnScript = FormatSetStateScript(state, kRespawnEntry);

		const auto overlayParts = SplitScript(overlayScript);
		const auto respawnParts = SplitScript(respawnScript);
		assert(overlayParts.entry == kOverlayEntry);
		assert(respawnParts.entry == kRespawnEntry);
		assert(overlayParts.payload == respawnParts.payload);
		assert(overlayParts.payload.find("\\\"B\\\"") != std::string::npos);

		const auto reparsed = RequireState(overlayParts.payload);
		assert(reparsed.phase == Phase::Dead);
		assert(reparsed.remainingMs == 42000);
		assert(reparsed.rescuer.active);
		assert(reparsed.rescuer.progress == 0.5);
		assert(reparsed.points.size() == 1);
		assert(reparsed.points[0].label == "Sanctuary Hills");
		assert(reparsed.friends.size() == 1);
		assert(reparsed.friends[0].id == "profile-1");
		assert(FormatSetStateScript(reparsed, kOverlayEntry) == overlayScript);

		const auto hiddenScript = FormatSetStateScript(RequireState(R"({"phase":"hidden"})"), kRespawnEntry);
		assert(SplitScript(hiddenScript).entry == kRespawnEntry);
		assert(hiddenScript.find(R"("rescuer":null)") != std::string::npos);
		assert(hiddenScript.find(R"("reviving":null)") != std::string::npos);
		assert(hiddenScript.find(R"("phase":"hidden")") != std::string::npos);
		assert(hiddenScript.find(R"("points":[])") != std::string::npos);
		assert(hiddenScript.find(R"("friends":[])") != std::string::npos);
	}

	void TestChoice()
	{
		const auto sanctuary = ParseChoice(R"({"action":"respawn","point":"sanctuary"})");
		assert(sanctuary.has_value());
		assert(sanctuary->point == "sanctuary");
		assert(sanctuary->friendId.empty());

		const auto lastBed = ParseChoice(R"({"action":"respawn","point":"lastbed","friendId":"ignored"})");
		assert(lastBed.has_value());
		assert(lastBed->point == "lastbed");
		assert(lastBed->friendId.empty());

		const auto checkpoint = ParseChoice(R"({"action":"respawn","point":"checkpoint","friendId":"ignored"})");
		assert(checkpoint.has_value());
		assert(checkpoint->point == "checkpoint");
		assert(checkpoint->friendId.empty());

		const auto friendChoice = ParseChoice(R"({"action":"respawn","point":"friend","friendId":"profile-1"})");
		assert(friendChoice.has_value());
		assert(friendChoice->point == "friend");
		assert(friendChoice->friendId == "profile-1");

		assert(!ParseChoice(R"({"action":"respawn","point":"friend"})").has_value());
		assert(!ParseChoice(R"({"action":"respawn","point":"friend","friendId":""})").has_value());
		assert(!ParseChoice(R"({"action":"respawn","point":"friend","friendId":7})").has_value());
		assert(!ParseChoice(R"({"action":"respawn","point":"friend","friendId":"a\nb"})").has_value());
		assert(!ParseChoice(R"({"action":"respawn","point":"nowhere"})").has_value());
		assert(!ParseChoice(R"({"action":"respawn"})").has_value());
		assert(!ParseChoice(R"({"action":"respawn","point":7})").has_value());
		assert(!ParseChoice(R"({"action":"close","point":"sanctuary"})").has_value());
		assert(!ParseChoice(R"({"point":"sanctuary"})").has_value());
		assert(!ParseChoice("").has_value());
		assert(!ParseChoice("not json").has_value());
		assert(!ParseChoice("[]").has_value());
		assert(!ParseChoice(std::string(2 * 1024, 'x')).has_value());

		const auto oversizedId = std::string(R"({"action":"respawn","point":"friend","friendId":")") + std::string(80, 'x') + R"("})";
		assert(!ParseChoice(oversizedId).has_value());

		const auto extra = ParseChoice(R"({"action":"respawn","point":"friend","friendId":"p","reason":"ignored"})");
		assert(extra.has_value());
		assert(extra->friendId == "p");
	}
}

int main()
{
	TestPhase();
	TestText();
	TestMalformedPushKeepsPreviousState();
	TestRemainingMs();
	TestRescuers();
	TestPoints();
	TestFriends();
	TestVisibility();
	TestScript();
	TestChoice();
	TestScriptCache();
	TestControllerBindRetryTiming();
	return 0;
}
