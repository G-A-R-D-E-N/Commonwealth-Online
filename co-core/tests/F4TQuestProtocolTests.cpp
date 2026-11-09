#include "F4TQuestProtocol.h"
#include "F4TReceiveFrame.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>
#include <vector>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::QuestProtocol;

	const auto key = ParseFormKey("0A1B2C:Fallout4.esm");
	assert(key.has_value());
	assert(key->localId == 0x0A1B2Cu);
	assert(key->plugin == "fallout4.esm");
	assert(FormatFormKey(*key) == "0A1B2C:fallout4.esm");

	const auto lower = ParseFormKey("0a1b2c:fallout4.esm");
	assert(lower.has_value());
	assert(*lower == *key);
	assert(FormatFormKey(*lower) == "0A1B2C:fallout4.esm");
	assert(ParseFormKey("0a1b2c:FALLOUT4.ESM") == key);
	assert(ParseFormKey("0A1B2C:FaLlOuT4.eSm") == key);
	assert(FormatFormKey(ParseFormKey("000001:My Mod.esp").value()) == "000001:my mod.esp");
	assert(FormatFormKey(ParseFormKey("FFFFFF:X.ESP").value()) == "FFFFFF:x.esp");
	assert(ParseFormKey("FFFFFF:X.ESP")->localId == 0xFFFFFFu);
	assert(ParseFormKey("0A1B2C:Fallout4.esm") != ParseFormKey("0A1B2D:Fallout4.esm"));
	assert(ParseFormKey("0A1B2C:Fallout4.esm") != ParseFormKey("0A1B2C:DLC.esp"));

	assert(!ParseFormKey(""));
	assert(!ParseFormKey("0A1B2C"));
	assert(!ParseFormKey("0A1B2:Fallout4.esm"));
	assert(!ParseFormKey("0A1B2CD:Fallout4.esm"));
	assert(!ParseFormKey("0A1B2C:"));
	assert(!ParseFormKey("000000:Fallout4.esm"));
	assert(!ParseFormKey("0A1B2C:Plugin:Extra"));
	assert(!ParseFormKey("0A1B2C:Plug\tin"));
	const auto utf8 = ParseFormKey("123456:Caf\xC3\xA9.esp");
	assert(utf8.has_value());
	assert(utf8->localId == 0x123456u);
	assert(utf8->plugin == "caf\xC3\xA9.esp");
	assert(FormatFormKey(*utf8) == "123456:caf\xC3\xA9.esp");
	assert(ParseFormKey("0A1B2C:Pl\xC3\xBCgin").has_value());
	assert(!ParseFormKey("0A1B2C:Pl\x01gin"));
	assert(!ParseFormKey("0A1B2C:Pl\x7Fgin"));
	assert(!ParseFormKey("0A1B2C:dir/Fallout4.esm"));
	assert(!ParseFormKey("0A1B2C:dir\\Fallout4.esm"));
	assert(!ParseFormKey("0A1B2C:" + std::string(129, 'x')));
	assert(ParseFormKey("0A1B2C:" + std::string(128, 'x')).has_value());

	assert(ParseStage(Json(0)) == 0);
	assert(ParseStage(Json(30)) == 30);
	assert(ParseStage(Json(65535)) == 65535);
	assert(!ParseStage(Json(-1)));
	assert(!ParseStage(Json(65536)));
	assert(!ParseStage(Json(4294967296ULL)));
	assert(!ParseStage(Json(1.5)));
	assert(!ParseStage(Json(true)));
	assert(!ParseStage(Json("30")));
	assert(!ParseStage(Json(nullptr)));

	assert(ModeName(Mode::CoopStory) == "coop-story");
	assert(ModeName(Mode::FactionWar) == "faction-war");
	assert(ModeName(Mode::InstancedStory) == "instanced-story");
	assert(ModeName(Mode::FreeRoam) == "free-roam");
	assert(ParseMode("coop-story") == Mode::CoopStory);
	assert(ParseMode("faction-war") == Mode::FactionWar);
	assert(ParseMode("instanced-story") == Mode::InstancedStory);
	assert(ParseMode("free-roam") == Mode::FreeRoam);
	assert(!ParseMode("coop"));
	assert(!ParseMode(""));
	assert(!ParseMode("Coop-Story"));

	assert(OverrideName(Override::Shared) == "shared");
	assert(OverrideName(Override::Solo) == "solo");
	assert(OverrideName(Override::Blocked) == "blocked");
	assert(ParseOverride("shared") == Override::Shared);
	assert(ParseOverride("solo") == Override::Solo);
	assert(ParseOverride("blocked") == Override::Blocked);
	assert(!ParseOverride("off"));

	const auto report = NormalizeReport(Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 } });
	assert(report.has_value());
	assert(report->quest == *key);
	assert(report->stage == 30);
	assert(!report->completed);
	const auto completedReport = NormalizeReport(
		Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 40 }, { "completed", true } });
	assert(completedReport.has_value());
	assert(completedReport->completed);
	assert(NormalizeReport(Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 40 }, { "extra", 1 } }).has_value());
	assert(!NormalizeReport(Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" } }));
	assert(!NormalizeReport(Json{ { "type", "questReport" }, { "stage", 30 } }));
	assert(!NormalizeReport(Json{ { "type", "questReport" }, { "quest", "bogus" }, { "stage", 30 } }));
	assert(!NormalizeReport(Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 70000 } }));
	assert(!NormalizeReport(Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 }, { "completed", "yes" } }));
	assert(!NormalizeReport(Json{ { "type", "questStage" }, { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 } }));
	assert(!NormalizeReport(Json::array()));

	const auto scope = NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "solo" } });
	assert(scope.has_value());
	assert(scope->quest == *key);
	assert(scope->scope == F4T::QuestProtocol::Scope::Solo);
	const auto sharedScope = NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0a1b2c:FALLOUT4.ESM" }, { "scope", "shared" } });
	assert(sharedScope.has_value());
	assert(sharedScope->quest == *key);
	assert(sharedScope->scope == F4T::QuestProtocol::Scope::Shared);
	assert(!NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" } }));
	assert(!NormalizeScope(Json{ { "type", "questScope" }, { "scope", "solo" } }));
	assert(!NormalizeScope(Json{ { "type", "questScope" }, { "quest", "bogus" }, { "scope", "solo" } }));
	assert(!NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "held" } }));
	assert(!NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", 1 } }));
	assert(NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "solo" }, { "stage", 4 } }).has_value());
	assert(!NormalizeScope(Json{ { "type", "questReport" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "solo" } }));
	assert(!NormalizeScope(Json::array()));

	const auto sync = NormalizeSync(Json{
		{ "type", "questSync" },
		{ "stages", Json::array({ Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 } } }) },
		{ "completed", Json::array({ "0A1B2C:Fallout4.esm" }) },
		{ "serverTime", 12.5 }
	});
	assert(sync.has_value());
	assert(sync->stages.size() == 1);
	assert(sync->stages.front().quest == *key);
	assert(sync->stages.front().stage == 30);
	assert(sync->completed.size() == 1);
	assert(NormalizeSync(Json{ { "type", "questSync" }, { "stages", Json::array() } }).has_value());
	assert(!NormalizeSync(Json{ { "type", "questSync" } }));
	assert(!NormalizeSync(Json{ { "type", "questSync" }, { "stages", Json::object() } }));
	assert(!NormalizeSync(Json{ { "type", "questSync" }, { "stages", Json::array({ Json{ { "quest", "0A1B2C:Fallout4.esm" } } }) } }));
	assert(!NormalizeSync(Json{ { "type", "questSync" }, { "stages", Json::array({ Json{ { "quest", "nope" }, { "stage", 1 } } }) } }));
	assert(!NormalizeSync(Json{ { "type", "questSync" }, { "stages", Json::array() }, { "completed", Json::array({ 7 }) } }));
	assert(!NormalizeSync(Json{ { "type", "questPolicy" }, { "stages", Json::array() } }));

	Json manyStages = Json::array();
	for (std::size_t index = 0; index < kMaximumSyncStages; ++index) {
		manyStages.push_back(Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 1 } });
	}
	assert(NormalizeSync(Json{ { "type", "questSync" }, { "stages", manyStages } }).has_value());
	manyStages.push_back(Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 1 } });
	assert(!NormalizeSync(Json{ { "type", "questSync" }, { "stages", manyStages } }));

	const auto policy = NormalizePolicy(Json{
		{ "type", "questPolicy" },
		{ "mode", "faction-war" },
		{ "version", 4 },
		{ "soloQuests", Json::array({ "0A1B2C:Fallout4.esm" }) },
		{ "gates", Json::array({ Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 } } }) }
	});
	assert(policy.has_value());
	assert(policy->mode == Mode::FactionWar);
	assert(policy->version == 4);
	assert(policy->soloQuests.size() == 1);
	assert(policy->gates.size() == 1);
	assert(policy->gates.front().stage == 30);
	const auto minimal = NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "free-roam" } });
	assert(minimal.has_value());
	assert(minimal->mode == Mode::FreeRoam);
	assert(minimal->version == 0);
	assert(minimal->soloQuests.empty());
	assert(minimal->gates.empty());
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" } }));
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "story" } }));
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", 3 } }));
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "version", -1 } }));
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "soloQuests", Json::array({ "bad" }) } }));
	assert(!NormalizePolicy(Json{ { "type", "questSync" }, { "mode", "coop-story" } }));

	const auto stage = MakeStage(Stage{ *key, 30, 7, false }, 100.0);
	assert(stage.at("type") == "questStage");
	assert(stage.at("quest") == "0A1B2C:fallout4.esm");
	assert(stage.at("stage") == 30);
	assert(stage.at("fromPlayerId") == 7);
	assert(stage.at("serverTime") == 100.0);
	assert(!stage.contains("completed"));
	assert(MakeStage(Stage{ *key, 30, 7, true }, 100.0).at("completed") == true);

	const auto syncPacket = MakeSync(Sync{ { StageRef{ *key, 30 } }, { *key } }, 101.0);
	const auto syncRoundTrip = NormalizeSync(syncPacket);
	assert(syncRoundTrip.has_value());
	assert(syncRoundTrip->stages.size() == 1);
	assert(syncRoundTrip->stages.front().quest == *key);
	assert(syncRoundTrip->stages.front().stage == 30);
	assert(syncRoundTrip->completed.size() == 1);
	assert(syncRoundTrip->completed.front() == *key);

	const auto longestPlugin = std::string(kMaximumPluginChars - 4, 'n') + ".esm";
	assert(ParseFormKey("0A1B2C:" + longestPlugin).has_value());
	assert(!ParseFormKey("0A1B2C:" + longestPlugin + "x").has_value());
	assert(kMaximumQuestKeyChars == 6U + 1U + kMaximumPluginChars);

	const auto longPlugin = ParseFormKey("0A1B2C:" + std::string(kMaximumPluginChars, 'x')).value();
	Sync largest;
	for (std::size_t index = 0; index < kMaximumSyncStages; ++index) {
		largest.stages.push_back(StageRef{ longPlugin, static_cast<std::uint16_t>(index) });
	}
	for (std::size_t index = 0; index < kMaximumCompletedQuests; ++index) {
		largest.completed.push_back(longPlugin);
	}
	const auto largestPacket = MakeSync(largest, 103.0);
	assert(largestPacket.dump().size() > kMaximumSyncFrameBytes);
	assert(largestPacket.dump().size() > F4T::ReceiveFrame::DefaultMaximumFrameBytes);
	assert(NormalizeSync(largestPacket).has_value());

	const auto policyPacket = MakePolicy(Policy{ Mode::InstancedStory, 9, { *key }, { StageRef{ *key, 12 } } }, 102.0);
	const auto policyRoundTrip = NormalizePolicy(policyPacket);
	assert(policyRoundTrip.has_value());
	assert(policyRoundTrip->mode == Mode::InstancedStory);
	assert(policyRoundTrip->version == 9);
	assert(policyRoundTrip->soloQuests.size() == 1);
	assert(policyRoundTrip->soloQuests.front() == *key);
	assert(policyRoundTrip->gates.size() == 1);
	assert(policyRoundTrip->gates.front().stage == 12);

	const auto notReady = MakeNotReady(kNotReadyReason, "0A1B2C:fallout4.esm", 104.0);
	assert(notReady.at("type") == std::string{ kNotReadyType });
	assert(notReady.at("reason") == std::string{ kNotReadyReason });
	assert(notReady.at("quest") == "0A1B2C:fallout4.esm");
	assert(notReady.at("serverTime") == 104.0);
	assert(notReady.contains("quest"));
	const auto unnamedNotReady = MakeNotReady(kUpdateRequiredReason, {}, 105.0);
	assert(unnamedNotReady.at("reason") == std::string{ kUpdateRequiredReason });
	assert(!unnamedNotReady.contains("quest"));

	const auto notReachable =
		MakeNotReachable(kNotReachablePrerequisite, "0A1B2D:fallout4.esm", "0A1B2C:fallout4.esm", "Out of Time", 106.0);
	assert(notReachable.at("type") == std::string{ kNotReachableType });
	assert(notReachable.at("reason") == std::string{ kNotReachablePrerequisite });
	assert(notReachable.at("quest") == "0A1B2D:fallout4.esm");
	assert(notReachable.at("missing") == "0A1B2C:fallout4.esm");
	assert(notReachable.at("previousLabel") == "Out of Time");
	assert(notReachable.at("serverTime") == 106.0);
	const auto bareNotReachable = MakeNotReachable(kNotReachableUnknown, "0A1B2D:fallout4.esm", {}, {}, 107.0);
	assert(bareNotReachable.at("reason") == std::string{ kNotReachableUnknown });
	assert(!bareNotReachable.contains("missing"));
	assert(!bareNotReachable.contains("previousLabel"));

	assert(ScopeName(Scope::Shared) == "shared");
	assert(ScopeName(Scope::Solo) == "solo");
	assert(ScopeName(Scope::Party) == "party");
	assert(ScopeName(Scope::Fireteam) == "fireteam");
	assert(ParseScope("shared") == Scope::Shared);
	assert(ParseScope("solo") == Scope::Solo);
	assert(ParseScope("party") == Scope::Party);
	assert(ParseScope("fireteam") == Scope::Fireteam);
	assert(ParseScope(ScopeName(Scope::Shared)) == Scope::Shared);
	assert(ParseScope(ScopeName(Scope::Solo)) == Scope::Solo);
	assert(ParseScope(ScopeName(Scope::Party)) == Scope::Party);
	assert(ParseScope(ScopeName(Scope::Fireteam)) == Scope::Fireteam);
	assert(!ParseScope("Fireteam"));
	assert(!ParseScope("held"));
	assert(!ParseScope(""));
	const auto fireteamScope =
		NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "fireteam" } });
	assert(fireteamScope.has_value());
	assert(fireteamScope->quest == *key);
	assert(fireteamScope->scope == Scope::Fireteam);
	assert(!NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "raid" } }));

	assert(AssistKindName(AssistKind::KillActor) == "killActor");
	assert(AssistKindName(AssistKind::ReachLocation) == "reachLocation");
	assert(AssistKindName(AssistKind::Activate) == "activate");
	assert(AssistKindName(AssistKind::Dialogue) == "dialogue");
	assert(AssistKindName(AssistKind::Escort) == "escort");
	assert(ParseAssistKind("killActor") == AssistKind::KillActor);
	assert(ParseAssistKind("reachLocation") == AssistKind::ReachLocation);
	assert(ParseAssistKind("activate") == AssistKind::Activate);
	assert(ParseAssistKind("dialogue") == AssistKind::Dialogue);
	assert(ParseAssistKind("escort") == AssistKind::Escort);
	assert(ParseAssistKind(AssistKindName(AssistKind::KillActor)) == AssistKind::KillActor);
	assert(ParseAssistKind(AssistKindName(AssistKind::ReachLocation)) == AssistKind::ReachLocation);
	assert(ParseAssistKind(AssistKindName(AssistKind::Activate)) == AssistKind::Activate);
	assert(ParseAssistKind(AssistKindName(AssistKind::Dialogue)) == AssistKind::Dialogue);
	assert(ParseAssistKind(AssistKindName(AssistKind::Escort)) == AssistKind::Escort);
	assert(!ParseAssistKind("kill"));
	assert(!ParseAssistKind("KillActor"));
	assert(!ParseAssistKind(""));

	assert(ValidFireteamId("p-1"));
	assert(ValidFireteamId("p-1234567890"));
	assert(!ValidFireteamId("p-"));
	assert(!ValidFireteamId("p-x"));
	assert(!ValidFireteamId("x-1"));
	assert(!ValidFireteamId("f-1"));
	assert(!ValidFireteamId(""));
	assert(!ValidFireteamId("p-12345678901"));
	assert(!ValidFireteamId("p- 1"));
	assert(!ValidFireteamId("p-+1"));
	assert(!ValidFireteamId("p-" + std::string(kMaximumFireteamIdChars, '1')));
	assert(ValidCreditId("c-7"));
	assert(ValidCreditId("c-1234567890"));
	assert(!ValidCreditId("c-"));
	assert(!ValidCreditId("c-x"));
	assert(!ValidCreditId("x-1"));
	assert(!ValidCreditId("p-1"));
	assert(!ValidCreditId("f-1"));
	assert(!ValidCreditId(""));
	assert(!ValidCreditId("c-12345678901"));
	assert(!ValidCreditId("c- 7"));
	assert(!ValidCreditId("c-+7"));
	assert(!ValidCreditId("c-" + std::string(kMaximumCreditIdChars, '7')));

	const Json killAssist{
		{ "type", "questAssist" },
		{ "sequence", 12 },
		{ "quest", "0A1B2C:Fallout4.esm" },
		{ "objectiveId", 5 },
		{ "kind", "killActor" },
		{ "targetFormKey", "0A1B2D:Fallout4.esm" },
		{ "attackSequence", 99 }
	};
	const auto assist = NormalizeAssist(killAssist);
	assert(assist.has_value());
	assert(assist->sequence == 12);
	assert(assist->quest == *key);
	assert(assist->objectiveId == 5);
	assert(assist->kind == AssistKind::KillActor);
	assert(assist->target == *ParseFormKey("0A1B2D:Fallout4.esm"));
	assert(assist->attackSequence == 99);

	Json forbiddenAssist = killAssist;
	forbiddenAssist["stage"] = 30;
	assert(!NormalizeAssist(forbiddenAssist));
	forbiddenAssist = killAssist;
	forbiddenAssist["profileId"] = "profile";
	assert(!NormalizeAssist(forbiddenAssist));
	forbiddenAssist = killAssist;
	forbiddenAssist["targetProfileId"] = "profile";
	assert(!NormalizeAssist(forbiddenAssist));
	forbiddenAssist = killAssist;
	forbiddenAssist["playerId"] = 7;
	assert(!NormalizeAssist(forbiddenAssist));
	forbiddenAssist = killAssist;
	forbiddenAssist["targetPlayerId"] = 7;
	assert(!NormalizeAssist(forbiddenAssist));
	forbiddenAssist = killAssist;
	forbiddenAssist["creditId"] = "c-1";
	assert(!NormalizeAssist(forbiddenAssist));

	const auto escortAssist = NormalizeAssist(Json{
		{ "type", "questAssist" },
		{ "sequence", 3 },
		{ "quest", "0A1B2C:Fallout4.esm" },
		{ "objectiveId", 0 },
		{ "kind", "escort" },
		{ "targetFormKey", "0A1B2D:Fallout4.esm" }
	});
	assert(escortAssist.has_value());
	assert(escortAssist->kind == AssistKind::Escort);
	assert(escortAssist->attackSequence == 0);
	assert(escortAssist->target.localId == 0x0A1B2Du);
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "killActor" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "dialogue" }, { "attackSequence", 9 } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "kill" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "bogus" }, { "objectiveId", 5 }, { "kind", "escort" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "escort" }, { "targetFormKey", "bogus" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "escort" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "objectiveId", 5 }, { "kind", "escort" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "kind", "escort" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 4294967296ULL }, { "kind", "escort" } }));
	assert(!NormalizeAssist(Json{ { "type", "questAssist" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "killActor" }, { "attackSequence", -1 } }));
	assert(!NormalizeAssist(Json{ { "type", "questCredit" }, { "sequence", 1 }, { "quest", "0A1B2C:Fallout4.esm" }, { "objectiveId", 5 }, { "kind", "escort" } }));
	assert(!NormalizeAssist(Json::array()));

	const auto creditAck = NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "c-12" }, { "stage", 40 } });
	assert(creditAck.has_value());
	assert(creditAck->creditId == "c-12");
	assert(creditAck->stage == 40);
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "f-1" }, { "stage", 40 } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "" }, { "stage", 40 } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "stage", 40 } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "c-1" } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "c-1" }, { "stage", 70000 } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", 12 }, { "stage", 40 } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCredit" }, { "creditId", "c-1" }, { "stage", 4 } }));
	assert(NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "c-1234567890" }, { "stage", 40 } }).has_value());
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "c-12345678901" }, { "stage", 40 } }));
	assert(!NormalizeCreditAck(Json{ { "type", "questCreditAck" }, { "creditId", "c-" + std::string(kMaximumCreditIdChars, '7') }, { "stage", 40 } }));

	const auto invitePacket = MakeFireteamInvite(FireteamInvite{ "i-7", 7, "Nate", 9 }, 202.0);
	assert(invitePacket.at("type") == std::string{ kFireteamInviteType });
	assert(invitePacket.at("inviteId") == "i-7");
	assert(invitePacket.at("fromPlayerId") == 7);
	assert(invitePacket.at("fromName") == "Nate");
	assert(invitePacket.at("targetPlayerId") == 9);
	assert(invitePacket.at("serverTime") == 202.0);
	const auto inviteEcho = MakeFireteamInvite(FireteamInvite{ "i-7", 0, "", 0 }, 203.0);
	assert(inviteEcho.at("inviteId") == "i-7");
	assert(inviteEcho.at("fromPlayerId") == 0);
	assert(!inviteEcho.contains("targetPlayerId"));
	assert(!inviteEcho.contains("fromName"));
	const auto invited = NormalizeFireteamInvite(Json{ { "type", "fireteamInvite" }, { "targetPlayerId", 9 } });
	assert(invited.has_value());
	assert(invited->targetPlayerId == 9);
	assert(!NormalizeFireteamInvite(Json{ { "type", "fireteamInvite" } }));
	assert(!NormalizeFireteamInvite(Json{ { "type", "fireteamInvite" }, { "targetPlayerId", -1 } }));
	assert(!NormalizeFireteamInvite(Json{ { "type", "fireteamInvite" }, { "targetPlayerId", "9" } }));
	assert(!NormalizeFireteamInvite(Json{ { "type", "fireteamKick" }, { "targetPlayerId", 9 } }));

	const auto respondPacket = MakeFireteamRespond(FireteamRespond{ "i-7", false }, 9, 204.0);
	assert(respondPacket.at("type") == std::string{ kFireteamRespondType });
	assert(respondPacket.at("inviteId") == "i-7");
	assert(respondPacket.at("accept") == false);
	assert(respondPacket.at("fromPlayerId") == 9);
	assert(respondPacket.at("serverTime") == 204.0);
	const auto clientRespond = MakeFireteamRespond(FireteamRespond{ "i-7", true }, 0, 205.0);
	assert(clientRespond.at("accept") == true);
	assert(!clientRespond.contains("fromPlayerId"));
	const auto respond = NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "inviteId", "i-7" }, { "accept", true } });
	assert(respond.has_value());
	assert(respond->inviteId == "i-7");
	assert(respond->accept);
	assert(!NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "inviteId", "" }, { "accept", true } }));
	assert(!NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "accept", true } }));
	assert(!NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "inviteId", "i-7" } }));
	assert(!NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "inviteId", "i-7" }, { "accept", "yes" } }));
	assert(!NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "inviteId", std::string(65, 'i') }, { "accept", true } }));
	assert(NormalizeFireteamRespond(Json{ { "type", "fireteamRespond" }, { "inviteId", std::string(64, 'i') }, { "accept", false } }).has_value());

	const auto kick = NormalizeFireteamKick(Json{ { "type", "fireteamKick" }, { "targetPlayerId", 9 } });
	assert(kick.has_value());
	assert(kick->targetPlayerId == 9);
	assert(!NormalizeFireteamKick(Json{ { "type", "fireteamKick" } }));
	assert(!NormalizeFireteamKick(Json{ { "type", "fireteamKick" }, { "targetPlayerId", 1.5 } }));
	assert(!NormalizeFireteamKick(Json{ { "type", "fireteamLeave" }, { "targetPlayerId", 9 } }));

	const auto event = MakeFireteamEvent(kFireteamInviteType, kFireteamReasonFull, 9, 206.0);
	assert(event.at("type") == std::string{ kFireteamInviteType });
	assert(event.at("reason") == std::string{ kFireteamReasonFull });
	assert(event.at("playerId") == 9);
	assert(event.at("serverTime") == 206.0);
	const auto anonymousEvent = MakeFireteamEvent(kFireteamLeaveType, kFireteamLeaveLeft, 0, 207.0);
	assert(anonymousEvent.at("type") == std::string{ kFireteamLeaveType });
	assert(anonymousEvent.at("reason") == std::string{ kFireteamLeaveLeft });
	assert(!anonymousEvent.contains("playerId"));

	FireteamSnapshot team;
	team.fireteamId = "p-3";
	team.leaderPlayerId = 7;
	team.members.push_back(FireteamMember{ 7, "Nate" });
	team.members.push_back(FireteamMember{ 9, "Codsworth" });
	const auto snapshotPacket = MakeFireteamSnapshot(team, 200.0);
	assert(snapshotPacket.at("type") == std::string{ kFireteamSnapshotType });
	assert(snapshotPacket.at("fireteamId") == "p-3");
	assert(snapshotPacket.at("leaderPlayerId") == 7);
	assert(snapshotPacket.at("members").size() == 2);
	assert(snapshotPacket.at("members").front().at("playerId") == 7);
	assert(snapshotPacket.at("members").front().at("characterName") == "Nate");
	assert(snapshotPacket.at("members").back().at("playerId") == 9);
	assert(snapshotPacket.at("members").back().at("characterName") == "Codsworth");
	assert(snapshotPacket.at("serverTime") == 200.0);
	const auto snapshotRoundTrip = NormalizeFireteamSnapshot(snapshotPacket);
	assert(snapshotRoundTrip.has_value());
	assert(snapshotRoundTrip->fireteamId == "p-3");
	assert(snapshotRoundTrip->leaderPlayerId == 7);
	assert(snapshotRoundTrip->members.size() == 2);
	assert(snapshotRoundTrip->members.front().playerId == 7);
	assert(snapshotRoundTrip->members.front().characterName == "Nate");
	assert(snapshotRoundTrip->members.back().playerId == 9);
	assert(snapshotRoundTrip->members.back().characterName == "Codsworth");

	const auto emptyPacket = MakeFireteamSnapshot(FireteamSnapshot{}, 201.0);
	assert(emptyPacket.at("fireteamId") == "");
	assert(emptyPacket.at("leaderPlayerId") == 0);
	assert(emptyPacket.at("members").empty());
	const auto emptyTeam = NormalizeFireteamSnapshot(emptyPacket);
	assert(emptyTeam.has_value());
	assert(emptyTeam->fireteamId.empty());
	assert(emptyTeam->leaderPlayerId == 0);
	assert(emptyTeam->members.empty());

	Json duplicateMembers = Json::array({
		Json{ { "playerId", 7 }, { "characterName", "Nate" } },
		Json{ { "playerId", 7 }, { "characterName", "Other" } }
	});
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", duplicateMembers } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", Json::array() } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "" }, { "leaderPlayerId", 7 }, { "members", Json::array() } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "" }, { "leaderPlayerId", 0 }, { "members", duplicateMembers } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "f-3" }, { "leaderPlayerId", 7 }, { "members", Json::array({ Json{ { "playerId", 7 }, { "characterName", "Nate" } } }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", Json::array() } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "members", Json::array() } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", -1 }, { "members", Json::array({ Json{ { "playerId", 7 }, { "characterName", "Nate" } } }) } }));

	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 9 }, { "members", Json::array({ Json{ { "playerId", 7 }, { "characterName", "Nate" } } }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 0 }, { "members", Json::array({ Json{ { "playerId", 7 }, { "characterName", "Nate" } } }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", Json::array({ Json{ { "playerId", 0 }, { "characterName", "Nate" } }, Json{ { "playerId", 7 }, { "characterName", "Other" } } }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 0 }, { "members", Json::array({ Json{ { "playerId", 0 }, { "characterName", "Nate" } } }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", Json::array({ "junk" }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", Json::array({ Json{ { "playerId", 7 }, { "characterName", 5 } } }) } }));
	const auto unorderedLeader = NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 7 }, { "members", Json::array({ Json{ { "playerId", 9 }, { "characterName", "Codsworth" } }, Json{ { "playerId", 7 }, { "characterName", "Nate" } } }) } });
	assert(unorderedLeader.has_value());
	assert(unorderedLeader->leaderPlayerId == 7);

	Json maximumMembers = Json::array();
	for (std::size_t index = 0; index < kMaximumFireteamMembers; ++index) {
		maximumMembers.push_back(Json{ { "playerId", index + 1 }, { "characterName", "P" } });
	}
	assert(NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 1 }, { "members", maximumMembers } }).has_value());
	maximumMembers.push_back(Json{ { "playerId", 99 }, { "characterName", "Q" } });
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 1 }, { "members", maximumMembers } }));

	const auto longestName = std::string(kMaximumCharacterNameChars, 'n');
	const auto shortestName = std::string(kMaximumCharacterNameChars + 1, 'n');
	assert(NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 1 }, { "members", Json::array({ Json{ { "playerId", 1 }, { "characterName", longestName } } }) } }).has_value());
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 1 }, { "members", Json::array({ Json{ { "playerId", 1 }, { "characterName", shortestName } } }) } }));
	assert(!NormalizeFireteamSnapshot(Json{ { "type", "fireteamSnapshot" }, { "fireteamId", "p-3" }, { "leaderPlayerId", 1 }, { "members", Json::array({ Json{ { "playerId", 1 } } }) } }));

	const auto fireteamPolicy = NormalizePolicy(Json{
		{ "type", "questPolicy" },
		{ "mode", "coop-story" },
		{ "version", 2 },
		{ "soloQuests", Json::array() },
		{ "partyQuests", Json::array({ "0A1B2C:Fallout4.esm" }) },
		{ "fireteamQuests", Json::array({ "0A1B2D:Fallout4.esm" }) },
		{ "gates", Json::array() },
		{ "fireteam", true }
	});
	assert(fireteamPolicy.has_value());
	assert(fireteamPolicy->mode == Mode::CoopStory);
	assert(fireteamPolicy->version == 2);
	assert(fireteamPolicy->partyQuests.size() == 1);
	assert(fireteamPolicy->partyQuests.front() == *key);
	assert(fireteamPolicy->fireteamQuests.size() == 1);
	assert(fireteamPolicy->fireteamQuests.front() == *ParseFormKey("0A1B2D:Fallout4.esm"));
	assert(fireteamPolicy->fireteamSupported);
	const auto legacyPolicy = NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" } });
	assert(legacyPolicy.has_value());
	assert(legacyPolicy->partyQuests.empty());
	assert(legacyPolicy->fireteamQuests.empty());
	assert(!legacyPolicy->fireteamSupported);
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "fireteam", "yes" } }));
	const auto malformedParty = NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "soloQuests", Json::array({ "0A1B2C:Fallout4.esm" }) }, { "partyQuests", Json::object() } });
	assert(malformedParty.has_value());
	assert(malformedParty->partyQuests.empty());
	assert(malformedParty->soloQuests.size() == 1);
	const auto mixedParty = NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "partyQuests", Json::array({ "bad", 7 }) } });
	assert(mixedParty.has_value());
	assert(mixedParty->partyQuests.empty());
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "fireteamQuests", Json::array({ "bad" }) } }));

	Json maximumPartyQuests = Json::array();
	for (std::size_t index = 0; index < kMaximumPartyQuests; ++index) {
		maximumPartyQuests.push_back("0A1B2C:Fallout4.esm");
	}
	const auto maximumParty = NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "partyQuests", maximumPartyQuests } });
	assert(maximumParty.has_value());
	assert(maximumParty->partyQuests.size() == kMaximumPartyQuests);
	maximumPartyQuests.push_back("0A1B2C:Fallout4.esm");
	const auto oversizedParty = NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "partyQuests", maximumPartyQuests } });
	assert(oversizedParty.has_value());
	assert(oversizedParty->partyQuests.empty());

	Json maximumFireteamQuests = Json::array();
	for (std::size_t index = 0; index < kMaximumFireteamQuests; ++index) {
		maximumFireteamQuests.push_back("0A1B2C:Fallout4.esm");
	}
	assert(NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "fireteamQuests", maximumFireteamQuests } }).has_value());
	maximumFireteamQuests.push_back("0A1B2C:Fallout4.esm");
	assert(!NormalizePolicy(Json{ { "type", "questPolicy" }, { "mode", "coop-story" }, { "fireteamQuests", maximumFireteamQuests } }));

	Policy fireteamFields;
	fireteamFields.mode = Mode::FreeRoam;
	fireteamFields.partyQuests.push_back(*key);
	fireteamFields.fireteamQuests.push_back(*ParseFormKey("0A1B2D:Fallout4.esm"));
	fireteamFields.fireteamSupported = true;
	const auto fireteamPolicyPacket = MakePolicy(fireteamFields, 211.0);
	assert(fireteamPolicyPacket.at("fireteam") == true);
	assert(fireteamPolicyPacket.at("partyQuests").size() == 1);
	assert(fireteamPolicyPacket.at("fireteamQuests").size() == 1);
	assert(fireteamPolicyPacket.at("fireteamQuests").front() == "0A1B2D:fallout4.esm");
	const auto fireteamPolicyRoundTrip = NormalizePolicy(fireteamPolicyPacket);
	assert(fireteamPolicyRoundTrip.has_value());
	assert(fireteamPolicyRoundTrip->fireteamSupported);
	assert(fireteamPolicyRoundTrip->partyQuests.size() == 1);
	assert(fireteamPolicyRoundTrip->partyQuests.front() == *key);
	assert(fireteamPolicyRoundTrip->fireteamQuests.size() == 1);
	assert(fireteamPolicyRoundTrip->fireteamQuests.front() == *ParseFormKey("0A1B2D:Fallout4.esm"));
	assert(policyPacket.at("fireteam") == false);
	assert(policyPacket.at("fireteamQuests").empty());

	const auto credit = MakeCredit(Credit{ "c-12", 7, *key, 5, AssistKind::ReachLocation }, 208.0);
	assert(credit.at("type") == std::string{ kQuestCreditType });
	assert(credit.at("creditId") == "c-12");
	assert(credit.at("actorPlayerId") == 7);
	assert(credit.at("quest") == "0A1B2C:fallout4.esm");
	assert(credit.at("objectiveId") == 5);
	assert(credit.at("kind") == "reachLocation");
	assert(credit.at("serverTime") == 208.0);
	assert(!credit.contains("targetFormKey"));
	const auto killCredit = MakeCredit(Credit{ "c-13", 7, *key, 5, AssistKind::KillActor, *ParseFormKey("000123:Fallout4.esm") }, 208.0);
	assert(killCredit.at("kind") == "killActor");
	assert(killCredit.at("targetFormKey") == "000123:fallout4.esm");

	PresenceRef presenceRef;
	presenceRef.form = *ParseFormKey("0A1B2D:Fallout4.esm");
	presenceRef.cell = "Sanctuary";
	presenceRef.worldspace = "Commonwealth";
	presenceRef.enabled = true;
	presenceRef.locked = false;
	presenceRef.hasPosition = true;
	presenceRef.x = 1.0f;
	presenceRef.y = 2.0f;
	presenceRef.z = 3.0f;
	const auto presencePacket = MakePresence(Presence{ 7, *key, 30, { presenceRef } }, 209.0);
	assert(presencePacket.at("type") == std::string{ kQuestPresenceType });
	assert(presencePacket.at("ownerPlayerId") == 7);
	assert(presencePacket.at("quest") == "0A1B2C:fallout4.esm");
	assert(presencePacket.at("stage") == 30);
	assert(presencePacket.at("serverTime") == 209.0);
	assert(presencePacket.at("refs").size() == 1);
	const auto& presenceRefPacket = presencePacket.at("refs").front();
	assert(presenceRefPacket.at("formKey") == "0A1B2D:fallout4.esm");
	assert(presenceRefPacket.at("cell") == "Sanctuary");
	assert(presenceRefPacket.at("worldspace") == "Commonwealth");
	assert(presenceRefPacket.at("enabled") == true);
	assert(presenceRefPacket.at("locked") == false);
	assert(presenceRefPacket.at("position").size() == 3);
	assert(presenceRefPacket.at("position").at(0) == 1.0);
	assert(presenceRefPacket.at("position").at(1) == 2.0);
	assert(presenceRefPacket.at("position").at(2) == 3.0);

	Presence oversizedPresence{ 7, *key, 30, {} };
	for (std::size_t index = 0; index < kMaximumPresenceRefs + 4; ++index) {
		oversizedPresence.refs.push_back(PresenceRef{ *key, {}, {}, false, false, false, 0.0f, 0.0f, 0.0f });
	}
	const auto truncatedPresence = MakePresence(oversizedPresence, 210.0);
	assert(truncatedPresence.at("refs").size() == kMaximumPresenceRefs);
	assert(truncatedPresence.at("refs").front().at("enabled") == false);
	assert(truncatedPresence.at("refs").front().at("locked") == false);
	assert(!truncatedPresence.at("refs").front().contains("cell"));
	assert(!truncatedPresence.at("refs").front().contains("worldspace"));
	assert(!truncatedPresence.at("refs").front().contains("position"));

	return 0;
}
