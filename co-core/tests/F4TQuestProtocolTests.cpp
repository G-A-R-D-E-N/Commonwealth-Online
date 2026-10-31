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

	return 0;
}
