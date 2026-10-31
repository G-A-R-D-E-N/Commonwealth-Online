#include "F4TQuestState.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
	using Json = nlohmann::json;
	using namespace F4T::QuestState;
	using F4T::QuestProtocol::FormKey;
	using F4T::QuestProtocol::Mode;
	using F4T::QuestProtocol::Override;
	using F4T::QuestProtocol::OverrideEntry;
	using F4T::QuestProtocol::ParseFormKey;

	FormKey Key(const char* a_value)
	{
		const auto parsed = ParseFormKey(a_value);
		assert(parsed.has_value());
		return *parsed;
	}

	ReportResult Report(Table& a_table, const FormKey& a_quest, std::uint16_t a_stage, bool a_completed = false)
	{
		return a_table.Report(ReportRequest{ a_quest, a_stage, a_completed, Sharing::Shared, 7 });
	}
}

int main()
{
	const auto quest = Key("0A1B2C:Fallout4.esm");
	const auto other = Key("0000AA:Fallout4.esm");
	const auto third = Key("123456:DLC.esp");

	{
		Table table;
		assert(table.IsEmpty());
		const auto first = Report(table, quest, 10);
		assert(first.outcome == Outcome::Applied);
		assert(first.relayed.has_value());
		assert(first.relayed->stage == 10);
		assert(!first.completed);
		assert(table.TrackedCount() == 1);

		assert(Report(table, quest, 10).outcome == Outcome::IgnoredDuplicate);
		assert(!Report(table, quest, 10).relayed.has_value());
		assert(Report(table, quest, 5).outcome == Outcome::IgnoredStale);
		assert(table.Stages().front().stage == 10);
		assert(Report(table, quest, 20).outcome == Outcome::Applied);
		assert(table.TrackedCount() == 1);
		assert(table.Stages().front().stage == 20);

		assert(Report(table, other, 3).outcome == Outcome::Applied);
		const auto stages = table.Stages();
		assert(stages.size() == 2);
		assert(stages.at(0).quest == quest);
		assert(stages.at(1).quest == other);
	}

	{
		Table table;
		const auto completed = Report(table, quest, 40, true);
		assert(completed.outcome == Outcome::Applied);
		assert(completed.completed);
		assert(completed.relayed.has_value());
		assert(table.IsCompleted(quest));
		assert(table.Completed().size() == 1);
		const auto repeated = Report(table, quest, 40, true);
		assert(repeated.outcome == Outcome::IgnoredDuplicate);
		assert(!repeated.completed);
		assert(Report(table, other, 5, true).outcome == Outcome::Applied);
		assert(table.Completed().size() == 2);
	}

	{
		Table table;
		assert(Report(table, quest, 40).outcome == Outcome::Applied);
		const auto equal = Report(table, quest, 40, true);
		assert(equal.outcome == Outcome::CompletedOnly);
		assert(equal.completed);
		assert(equal.relayed.has_value());
		assert(equal.relayed->stage == 40);
		assert(table.IsCompleted(quest));

		Table lower;
		assert(Report(lower, quest, 40).outcome == Outcome::Applied);
		const auto late = Report(lower, quest, 30, true);
		assert(late.outcome == Outcome::CompletedOnly);
		assert(late.completed);
		assert(late.relayed.has_value());
		assert(late.relayed->stage == 40);
		assert(lower.Stages().front().stage == 40);
		assert(lower.IsCompleted(quest));
		assert(Report(lower, quest, 30, true).outcome == Outcome::IgnoredStale);
	}

	{
		Table table;
		assert(table.Report(ReportRequest{ quest, 40, true, Sharing::Shared, 7, {}, "p7" }).outcome == Outcome::Applied);
		const auto otherPlayer = table.Report(ReportRequest{ quest, 40, true, Sharing::Shared, 8, {}, "p8" });
		assert(otherPlayer.outcome == Outcome::CompletedOnly);
		assert(!otherPlayer.completed);
		assert(otherPlayer.relayed.has_value());
		assert(otherPlayer.relayed->stage == 40);
		assert(otherPlayer.profileRecorded);
		assert(table.ProfileCompleted("p8", quest));
		assert(table.Completed().size() == 1);

		const auto again = table.Report(ReportRequest{ quest, 40, true, Sharing::Shared, 8, {}, "p8" });
		assert(again.outcome == Outcome::CompletedOnly);
		assert(!again.completed);
		assert(!again.profileRecorded);
		assert(table.Completed().size() == 1);
	}

	{
		Table table;
		for (std::size_t index = 0; index < kMaximumProfileRecords; ++index) {
			ReportRequest request{ quest, 10, false, Sharing::Shared, 1 };
			request.profileId = "p" + std::to_string(index);
			assert(table.Report(request).profileRecorded);
		}
		ReportRequest touched{ quest, 20, false, Sharing::Shared, 1 };
		touched.profileId = "p0";
		assert(table.Report(touched).profileRecorded);
		ReportRequest extra{ quest, 10, false, Sharing::Shared, 1 };
		extra.profileId = "p" + std::to_string(kMaximumProfileRecords);
		assert(table.Report(extra).profileRecorded);
		assert(table.ProfileQuests("p0").size() == 1);
		assert(table.ProfileQuests("p0").at(0).stage == 20);
		assert(table.ProfileQuests("p1").empty());
		assert(table.ProfileQuests(extra.profileId).size() == 1);
	}

	{
		Table table;
		assert(table.Report(ReportRequest{ quest, 10, false, Sharing::Blocked, 1 }).outcome == Outcome::DroppedBlocked);
		assert(table.Report(ReportRequest{ quest, 10, false, Sharing::Solo, 1 }).outcome == Outcome::DroppedSolo);
		assert(table.IsEmpty());
	}

	{
		Table table;
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, {}) == Sharing::Shared);
		assert(table.ResolveSharing("p1", quest, Mode::FactionWar, {}) == Sharing::Shared);
		assert(table.ResolveSharing("p1", quest, Mode::InstancedStory, {}) == Sharing::Solo);
		assert(table.ResolveSharing("p1", quest, Mode::FreeRoam, {}) == Sharing::Solo);
		assert(table.SetSolo("p1", quest, true));
		assert(table.IsSolo("p1", quest));
		assert(!table.IsSolo("p2", quest));
		assert(table.SoloQuests("p1").size() == 1);
		assert(table.SoloQuests("p2").empty());
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, {}) == Sharing::Solo);
		assert(table.ResolveSharing("p2", quest, Mode::CoopStory, {}) == Sharing::Shared);
		assert(!table.SetSolo("p1", quest, true));
		assert(table.SetSolo("p1", other, true));
		assert(table.SoloQuests("p1").size() == 2);

		const std::vector<OverrideEntry> overrides{ OverrideEntry{ quest, Override::Blocked } };
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, overrides) == Sharing::Blocked);
		assert(table.ResolveSharing("p1", other, Mode::CoopStory, overrides) == Sharing::Solo);
		const std::vector<OverrideEntry> forced{ OverrideEntry{ quest, Override::Shared } };
		assert(table.ResolveSharing("p1", quest, Mode::FreeRoam, forced) == Sharing::Shared);
		const std::vector<OverrideEntry> soloOverride{ OverrideEntry{ other, Override::Solo } };
		assert(table.ResolveSharing("p2", other, Mode::CoopStory, soloOverride) == Sharing::Solo);

		const auto upperQuest = Key("0A1B2C:FALLOUT4.ESM");
		const std::vector<OverrideEntry> upperOverride{ OverrideEntry{ upperQuest, Override::Blocked } };
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, upperOverride) == Sharing::Blocked);
		assert(table.ResolveSharing("p1", quest, Mode::FreeRoam, upperOverride) == Sharing::Blocked);
		assert(table.IsSolo("p1", upperQuest) == table.IsSolo("p1", quest));

		assert(table.SetSolo("p1", quest, false));
		assert(!table.IsSolo("p1", quest));
		assert(table.SoloQuests("p1").size() == 1);
		assert(table.SetSolo("p1", other, false));
		assert(table.SoloQuests("p1").empty());
		assert(!table.SetSolo("p1", quest, false));
		assert(!table.SetSolo("", quest, true));
		assert(!table.SetSolo(std::string(65, 'p'), quest, true));
	}

	{
		Table table;
		assert(!table.IsGated(quest, 30));
		assert(table.AddGate(quest, 30));
		assert(!table.AddGate(quest, 30));
		assert(table.IsGated(quest, 30));
		assert(table.Gates().size() == 1);
		const auto held = Report(table, quest, 30);
		assert(held.outcome == Outcome::Held);
		assert(!held.relayed.has_value());
		assert(table.TrackedCount() == 0);
		assert(table.Held().size() == 1);
		assert(table.Held().front().fromPlayerId == 7);
		assert(Report(table, quest, 30).outcome == Outcome::Held);
		assert(table.Held().size() == 1);
		assert(Report(table, quest, 31).outcome == Outcome::Applied);

		const auto promoted = table.ReleaseGate(quest, 30);
		assert(promoted.empty());
		assert(table.Held().empty());
		assert(!table.IsGated(quest, 30));
		assert(table.Stages().front().stage == 31);
	}

	{
		Table table;
		assert(table.AddGate(quest, 30));
		assert(Report(table, quest, 30, true).outcome == Outcome::Held);
		const auto promoted = table.ReleaseGate(quest, 30);
		assert(promoted.size() == 1);
		assert(promoted.front().stage == 30);
		assert(promoted.front().completed);
		assert(table.IsCompleted(quest));
		assert(table.Stages().front().stage == 30);
		assert(table.Held().empty());
	}

	{
		Table table{ 1 };
		assert(Report(table, quest, 5).outcome == Outcome::Applied);
		assert(Report(table, other, 5).outcome == Outcome::RejectedCapacity);
		assert(table.TrackedCount() == 1);
		assert(table.AddGate(other, 5));
		assert(Report(table, other, 5).outcome == Outcome::Held);
		assert(table.ReleaseGate(other, 5).empty());
		assert(table.Held().size() == 1);
	}

	{
		Table table;
		assert(table.Version() == 1);
		table.BumpVersion();
		assert(table.Version() == 2);
		table.BumpVersion();
		assert(table.Version() == 3);
	}

	{
		Table table;
		const auto upperQuest = Key("0A1B2C:FALLOUT4.ESM");
		const auto upperThird = Key("123456:dlc.ESP");
		assert(upperQuest == quest);
		assert(upperThird == third);

		assert(Report(table, quest, 10).outcome == Outcome::Applied);
		assert(Report(table, upperQuest, 20).outcome == Outcome::Applied);
		assert(table.TrackedCount() == 1);
		assert(table.Stages().front().stage == 20);
		assert(Report(table, upperQuest, 30, true).outcome == Outcome::Applied);
		assert(table.Completed().size() == 1);
		assert(table.IsCompleted(upperQuest));
		assert(Report(table, quest, 30, true).outcome == Outcome::IgnoredDuplicate);
		assert(table.Completed().size() == 1);

		assert(table.AddGate(third, 12));
		assert(!table.AddGate(upperThird, 12));
		assert(table.Gates().size() == 1);
		assert(table.IsGated(upperThird, 12));
		assert(Report(table, upperThird, 12).outcome == Outcome::Held);
		assert(table.Held().size() == 1);
		assert(table.ReleaseGate(upperThird, 12).size() == 1);
		assert(table.Gates().empty());

		const auto stored = table.ToJson();
		const auto loaded = Table::FromJson(stored);
		assert(loaded.has_value());
		assert(loaded->Stages().size() == 2);
		assert(loaded->Stages().front().quest == quest);
		assert(loaded->Stages().front().stage == 30);
		assert(loaded->Stages().at(1).quest == upperThird);
		assert(loaded->Stages().at(1).stage == 12);
		assert(loaded->Completed().size() == 1);
		assert(loaded->IsCompleted(upperQuest));
		assert(loaded->Gates().empty());
		assert(loaded->Held().empty());
		assert(loaded->ToJson() == stored);
	}

	{
		Table table;
		assert(Report(table, quest, 30, true).outcome == Outcome::Applied);
		assert(Report(table, other, 5).outcome == Outcome::Applied);
		assert(table.AddGate(third, 12));
		assert(Report(table, third, 12).outcome == Outcome::Held);
		assert(table.SetSolo("p1", quest, true));
		table.BumpVersion();
		table.BumpVersion();

		const auto stored = table.ToJson();
		auto loaded = Table::FromJson(stored);
		assert(loaded.has_value());
		assert(loaded->Version() == table.Version());
		assert(loaded->Stages().size() == 2);
		assert(loaded->Stages().at(0).quest == quest);
		assert(loaded->Stages().at(0).stage == 30);
		assert(loaded->Stages().at(1).quest == other);
		assert(loaded->Completed().size() == 1);
		assert(loaded->IsCompleted(quest));
		assert(loaded->Held().size() == 1);
		assert(loaded->Held().front().quest == third);
		assert(loaded->Held().front().stage == 12);
		assert(loaded->IsGated(third, 12));
		assert(loaded->SoloQuests("p1").size() == 1);
		assert(loaded->IsSolo("p1", quest));
		assert(loaded->ToJson() == stored);

		assert(Report(*loaded, quest, 40).outcome == Outcome::Applied);
		assert(loaded->Stages().front().stage == 40);
		assert(loaded->Held().size() == 1);
	}

	{
		assert(!Table::FromJson(Json::array()));
		assert(!Table::FromJson(Json{ { "stages", Json::object() } }));
		assert(!Table::FromJson(Json{ { "solo", Json::array() } }));
		assert(!Table::FromJson(Json{ { "held", 3 } }));
		const auto loaded = Table::FromJson(Json{
			{ "version", 12 },
			{ "stages", Json::array({
				Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 } },
				Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 40 } },
				Json{ { "quest", "not-a-form-key" }, { "stage", 10 } },
				Json{ { "quest", "0000AA:Fallout4.esm" }, { "stage", 70000 } },
				Json{ "junk" }
			}) },
			{ "completed", Json::array({ "0A1B2C:Fallout4.esm", "0A1B2C:Fallout4.esm", 9 }) },
			{ "gates", Json::array({ Json{ { "quest", "123456:DLC.esp" }, { "stage", 12 } }, Json{ { "quest", "123456:DLC.esp" }, { "stage", 12 } } }) },
			{ "solo", Json{ { "p1", Json::array({ "0A1B2C:Fallout4.esm", "bogus" }) }, { "", Json::array({ "0A1B2C:Fallout4.esm" }) } } }
		});
		assert(loaded.has_value());
		assert(loaded->Version() == 12);
		assert(loaded->Stages().size() == 1);
		assert(loaded->Stages().front().stage == 30);
		assert(loaded->Completed().size() == 1);
		assert(loaded->Gates().size() == 1);
		assert(loaded->SoloQuests("p1").size() == 1);
		assert(loaded->SoloQuests("").empty());
	}

	{
		const auto loaded = Table::FromJson(Json{
			{ "held", Json::array({
				Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 }, { "fromPlayerId", std::uint32_t{ 7 } } },
				Json{ { "quest", "0000AA:Fallout4.esm" }, { "stage", 5 }, { "fromPlayerId", std::uint64_t{ 0x100000007ULL } } },
				Json{ { "quest", "123456:DLC.esp" }, { "stage", 9 } }
			}) }
		});
		assert(loaded.has_value());
		assert(loaded->Held().size() == 3);
		assert(loaded->Held().at(0).fromPlayerId == 7);
		assert(loaded->Held().at(1).fromPlayerId == 0);
		assert(loaded->Held().at(2).fromPlayerId == 0);
		const auto stored = loaded->ToJson();
		assert(stored.at("held").at(0).at("fromPlayerId") == 7);
		assert(stored.at("held").at(1).at("fromPlayerId") == 0);
		const auto reloaded = Table::FromJson(stored);
		assert(reloaded.has_value());
		assert(reloaded->Held().at(0).fromPlayerId == 7);
	}

	{
		Table table;
		const auto untracked = table.Report(ReportRequest{ quest, 65535, false, Sharing::Shared, 7 });
		assert(untracked.outcome == Outcome::RejectedImplausible);
		assert(!untracked.relayed.has_value());
		assert(table.IsEmpty());

		assert(Report(table, quest, 20).outcome == Outcome::Applied);
		const auto jump = table.Report(ReportRequest{ quest, 65535, true, Sharing::Shared, 7 });
		assert(jump.outcome == Outcome::RejectedImplausible);
		assert(!jump.relayed.has_value());
		assert(!jump.completed);
		assert(!table.IsCompleted(quest));
		assert(table.Stages().front().stage == 20);
		assert(Report(table, quest, static_cast<std::uint16_t>(20 + kMaximumStageAdvance)).outcome == Outcome::Applied);
		assert(table.Stages().front().stage == 20 + kMaximumStageAdvance);
		const auto over = table.Report(ReportRequest{
			quest,
			static_cast<std::uint16_t>(20 + 2 * kMaximumStageAdvance + 1),
			false,
			Sharing::Shared,
			7
		});
		assert(over.outcome == Outcome::RejectedImplausible);
		assert(table.Stages().front().stage == 20 + kMaximumStageAdvance);
	}

	{
		Table table;
		assert(Report(table, quest, 900).outcome == Outcome::Applied);
		assert(table.Report(ReportRequest{ quest, 30, false, Sharing::Shared, 7 }).outcome == Outcome::IgnoredStale);
		const auto small = table.Report(ReportRequest{ quest, 890, false, Sharing::Shared, 9 });
		assert(small.outcome == Outcome::IgnoredStale);
		assert(table.Stages().front().stage == 900);

		const auto corrected = table.Report(ReportRequest{ quest, 30, false, Sharing::Shared, 9 });
		assert(corrected.outcome == Outcome::Corrected);
		assert(corrected.relayed.has_value());
		assert(corrected.relayed->stage == 30);
		assert(corrected.relayed->fromPlayerId == 9);
		assert(table.Stages().front().stage == 30);
		assert(table.Stages().front().fromPlayerId == 9);
		assert(!table.Stages().front().confirmed);

		assert(table.Report(ReportRequest{ quest, 40, false, Sharing::Shared, 7 }).outcome == Outcome::Applied);
		assert(table.Stages().front().confirmed);
		assert(table.Report(ReportRequest{ quest, 65535, false, Sharing::Shared, 9 }).outcome == Outcome::RejectedImplausible);
		assert(table.Stages().front().stage == 40);
		assert(table.Report(ReportRequest{ quest, 1040, false, Sharing::Shared, 9 }).outcome == Outcome::Applied);
		const auto confirmedLow = table.Report(ReportRequest{ quest, 5, false, Sharing::Shared, 11 });
		assert(confirmedLow.outcome == Outcome::IgnoredStale);
		assert(table.Stages().front().stage == 1040);
	}

	{
		Table table;
		for (std::size_t index = 1; index <= kMaximumQuestsPerProfile + 1; ++index) {
			char digits[7] = {};
			std::snprintf(digits, sizeof(digits), "%06zX", index);
			const auto key = Key((std::string{ digits } + ":Fallout4.esm").c_str());
			const auto result = table.Report(ReportRequest{ key, 10, false, Sharing::Shared, 7 });
			if (index <= kMaximumQuestsPerProfile) {
				assert(result.outcome == Outcome::Applied);
			} else {
				assert(result.outcome == Outcome::RejectedCapacity);
			}
		}
		assert(table.TrackedCount() == kMaximumQuestsPerProfile);
		assert(table.Report(ReportRequest{ other, 10, false, Sharing::Shared, 9 }).outcome == Outcome::Applied);
		assert(table.TrackedCount() == kMaximumQuestsPerProfile + 1);
	}

	{
		Json stages = Json::array();
		for (std::size_t index = 1; index <= kMaximumCompletionsPerProfile + 1; ++index) {
			char digits[7] = {};
			std::snprintf(digits, sizeof(digits), "%06zX", index);
			stages.push_back(Json{
				{ "quest", std::string{ digits } + ":Fallout4.esm" },
				{ "stage", 5 }
			});
		}
		const auto seeded = Table::FromJson(Json{ { "stages", std::move(stages) } });
		assert(seeded.has_value());
		Table table = *seeded;
		std::size_t recorded = 0;
		for (const auto& entry : table.Stages()) {
			const auto result = table.Report(ReportRequest{ entry.quest, entry.stage, true, Sharing::Shared, 7 });
			assert(result.outcome == Outcome::CompletedOnly || result.outcome == Outcome::IgnoredDuplicate);
			if (result.completed) ++recorded;
		}
		assert(recorded == kMaximumCompletionsPerProfile);
		assert(table.Completed().size() == kMaximumCompletionsPerProfile);
		assert(table.Report(ReportRequest{ other, 5, true, Sharing::Shared, 9 }).completed);
		assert(table.Completed().size() == kMaximumCompletionsPerProfile + 1);
	}

	{
		const auto legacy = Json{
			{ "version", 2 },
			{ "stages", Json::array({ Json{ { "quest", "0A1B2C:Fallout4.esm" }, { "stage", 30 } } }) },
			{ "completed", Json::array({ "0A1B2C:Fallout4.esm" }) }
		};
		const auto loaded = Table::FromJson(legacy);
		assert(loaded.has_value());
		assert(loaded->Stages().size() == 1);
		assert(loaded->Stages().front().stage == 30);
		assert(loaded->Stages().front().fromPlayerId == 0);
		assert(!loaded->Stages().front().confirmed);
		assert(loaded->Completed().size() == 1);
		assert(loaded->IsCompleted(quest));
		const auto stored = loaded->ToJson();
		assert(stored.at("completed").size() == 1);
		assert(stored.at("completed").at(0) == F4T::QuestProtocol::FormatFormKey(quest));
		assert(stored.at("stages").at(0).at("stage") == 30);
	}

	{
		Table table;
		const auto shared = table.Report(ReportRequest{ quest, 10, false, Sharing::Shared, 7, "", "p7" });
		assert(shared.outcome == Outcome::Applied);
		assert(table.ProfileStage("p7", quest).value() == 10);
		assert(!table.ProfileStage("p8", quest).has_value());
		assert(!table.ProfileCompleted("p7", quest));
		assert(table.ProfileQuests("p7").size() == 1);

		const auto completed = table.Report(ReportRequest{ quest, 10, true, Sharing::Shared, 7, "", "p7" });
		assert(completed.completed);
		assert(table.ProfileCompleted("p7", quest));
		assert(table.ProfileStage("p7", quest).value() == 10);

		const auto solo = table.Report(ReportRequest{ other, 30, true, Sharing::Solo, 8, "", "p8" });
		assert(solo.outcome == Outcome::DroppedSolo);
		assert(!table.FindStage(other, {}).has_value());
		assert(table.ProfileStage("p8", other).value() == 30);
		assert(table.ProfileCompleted("p8", other));

		const auto blocked = table.Report(ReportRequest{ third, 5, false, Sharing::Blocked, 9, "", "p9" });
		assert(blocked.outcome == Outcome::DroppedBlocked);
		assert(!table.ProfileStage("p9", third).has_value());

		const auto reloaded = Table::FromJson(table.ToJson());
		assert(reloaded.has_value());
		assert(reloaded->ProfileStage("p7", quest).value() == 10);
		assert(reloaded->ProfileCompleted("p7", quest));
		assert(reloaded->ProfileStage("p8", other).value() == 30);
		assert(reloaded->ProfileQuests("p8").size() == 1);
		assert(!reloaded->ProfileQuests("p9").size());
		assert(!reloaded->IsEmpty());
	}

	{
		Table table;
		for (std::size_t index = 1; index < kMaximumProfileRecords; ++index) {
			ReportRequest request{ quest, 10, false, Sharing::Shared, 1 };
			request.profileId = "p" + std::to_string(index);
			assert(table.Report(request).profileRecorded);
		}
		ReportRequest mostRecent{ quest, 10, false, Sharing::Shared, 1 };
		mostRecent.profileId = "p0";
		assert(table.Report(mostRecent).profileRecorded);

		const auto reloaded = Table::FromJson(table.ToJson());
		assert(reloaded.has_value());
		Table restored = *reloaded;
		ReportRequest extra{ quest, 10, false, Sharing::Shared, 1 };
		extra.profileId = "pextra";
		assert(restored.Report(extra).profileRecorded);
		assert(restored.ProfileQuests("p0").size() == 1);
		assert(restored.ProfileQuests("p1").empty());
		assert(restored.ProfileQuests("pextra").size() == 1);
	}

	return 0;
}
