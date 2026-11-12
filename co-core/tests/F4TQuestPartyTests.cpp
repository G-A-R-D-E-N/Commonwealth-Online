#include "F4TQuestProtocol.h"
#include "F4TQuestState.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
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
	using F4T::QuestProtocol::Scope;

	FormKey Key(const char* a_value)
	{
		const auto parsed = ParseFormKey(a_value);
		assert(parsed.has_value());
		return *parsed;
	}

	ReportResult SharedReport(Table& a_table, const FormKey& a_quest, std::uint16_t a_stage, bool a_completed = false)
	{
		return a_table.Report(ReportRequest{ a_quest, a_stage, a_completed, Sharing::Shared, 7, {} });
	}

	ReportResult PartyReport(Table& a_table, const FormKey& a_quest, std::uint16_t a_stage, std::string_view a_party, bool a_completed = false)
	{
		return a_table.Report(ReportRequest{ a_quest, a_stage, a_completed, Sharing::Party, 7, std::string{ a_party } });
	}
}

int main()
{
	const auto quest = Key("0A1B2C:Fallout4.esm");
	const auto other = Key("0000AA:Fallout4.esm");

	{
		Table table;
		const auto applied = PartyReport(table, quest, 10, "alpha");
		assert(applied.outcome == Outcome::Applied);
		assert(applied.relayed.has_value());
		assert(applied.relayed->party == "alpha");
		assert(table.Stages().size() == 1);
		assert(table.Stages().front().party == "alpha");
		assert(PartyReport(table, quest, 10, "alpha").outcome == Outcome::IgnoredDuplicate);
		assert(PartyReport(table, quest, 12, "beta").outcome == Outcome::Applied);
		assert(table.Stages().size() == 2);
		assert(PartyReport(table, quest, 14, "alpha").outcome == Outcome::Applied);
		assert(table.Stages().size() == 2);
	}

	{
		Table table;
		assert(PartyReport(table, quest, 10, "").outcome == Outcome::DroppedSolo);
		assert(table.IsEmpty());
		const auto shared = table.Report(ReportRequest{ quest, 10, false, Sharing::Shared, 7, "alpha" });
		assert(shared.outcome == Outcome::Applied);
		assert(shared.relayed.has_value());
		assert(shared.relayed->party.empty());
		assert(table.Stages().front().party.empty());
		assert(table.Report(ReportRequest{ other, 10, false, Sharing::Solo, 7, "alpha" }).outcome == Outcome::DroppedSolo);
		assert(table.Report(ReportRequest{ other, 10, false, Sharing::Blocked, 7, "alpha" }).outcome == Outcome::DroppedBlocked);
		assert(table.TrackedCount() == 1);
	}

	{
		Table table;
		assert(SharedReport(table, quest, 10).outcome == Outcome::Applied);
		assert(PartyReport(table, quest, 20, "alpha").outcome == Outcome::Applied);
		assert(table.Stages().size() == 2);
		assert(table.AddGate(quest, 30));
		assert(PartyReport(table, quest, 30, "alpha").outcome == Outcome::Held);
		assert(table.Held().size() == 1);
		assert(table.Held().front().party == "alpha");
		const auto promoted = table.ReleaseGate(quest, 30);
		assert(promoted.size() == 1);
		assert(table.Held().empty());
		assert(table.Stages().size() == 2);
		assert(table.Stages().back().stage == 30);
		assert(table.Stages().back().party == "alpha");
		assert(table.Stages().front().stage == 10);
		assert(table.Stages().front().party.empty());
	}

	{
		Table table;
		const auto completed = PartyReport(table, quest, 40, "alpha", true);
		assert(completed.outcome == Outcome::Applied);
		assert(completed.completed);
		assert(!table.IsCompleted(quest));
		assert(table.Completed().empty());
		const auto tracks = table.CompletedTracks();
		assert(tracks.size() == 1);
		assert(tracks.front().quest == quest);
		assert(tracks.front().party == "alpha");
		assert(PartyReport(table, quest, 40, "alpha", true).outcome == Outcome::IgnoredDuplicate);
		assert(PartyReport(table, quest, 40, "beta", true).completed);
		assert(table.CompletedTracks().size() == 2);
		assert(SharedReport(table, quest, 40, true).completed);
		assert(table.IsCompleted(quest));
		assert(table.Completed().size() == 1);
		assert(table.CompletedTracks().size() == 3);
	}

	{
		assert(Table::Receives(Sharing::Shared, "", "", ""));
		assert(Table::Receives(Sharing::Shared, "alpha", "", "beta"));
		assert(Table::Receives(Sharing::Solo, "", "", ""));
		assert(Table::Receives(Sharing::Party, "alpha", "", "alpha"));
		assert(!Table::Receives(Sharing::Party, "alpha", "", "beta"));
		assert(!Table::Receives(Sharing::Party, "", "", ""));
		assert(!Table::Receives(Sharing::Party, "", "", "alpha"));
		assert(Table::Receives(Sharing::Party, "alpha", "alpha", ""));
		assert(!Table::Receives(Sharing::Shared, "alpha", "alpha", ""));
		assert(!Table::Receives(Sharing::Party, "beta", "alpha", "alpha"));
		assert(!Table::Receives(Sharing::Shared, "beta", "alpha", "alpha"));
		assert(!Table::Receives(Sharing::Solo, "", "alpha", ""));
		assert(!Table::Receives(Sharing::Solo, "alpha", "alpha", ""));
	}

	{
		Table table;
		assert(table.SetScope("p1", quest, Scope::Party));
		assert(table.IsParty("p1", quest));
		assert(!table.IsSolo("p1", quest));
		assert(table.PartyQuests("p1").size() == 1);
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, {}) == Sharing::Party);
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, { OverrideEntry{ quest, Override::Shared } }) == Sharing::Shared);
		assert(table.SetScope("p1", quest, Scope::Solo));
		assert(!table.IsParty("p1", quest));
		assert(table.IsSolo("p1", quest));
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, {}) == Sharing::Solo);
		assert(!table.SetScope("p1", quest, Scope::Solo));
		assert(table.SetScope("p1", quest, Scope::Shared));
		assert(table.IsEmpty());
		assert(table.ResolveSharing("p1", quest, Mode::CoopStory, {}) == Sharing::Shared);
	}

	{
		Table table;
		for (std::size_t index = 0; index < kMaximumPartyQuestsPerPlayer; ++index) {
			char text[32]{};
			std::snprintf(text, sizeof(text), "%06zX:Fallout4.esm", index + 1);
			assert(table.SetParty("p1", Key(text), true));
		}
		assert(!table.SetParty("p1", quest, true));
		assert(table.PartyQuests("p1").size() == kMaximumPartyQuestsPerPlayer);
	}

	{
		Table table(1);
		assert(PartyReport(table, quest, 10, "alpha").outcome == Outcome::Applied);
		assert(PartyReport(table, other, 10, "alpha").outcome == Outcome::RejectedCapacity);
		assert(SharedReport(table, other, 10).outcome == Outcome::RejectedCapacity);
	}

	{
		Table table;
		assert(PartyReport(table, quest, 10, "alpha", true).outcome == Outcome::Applied);
		assert(table.SetScope("p1", other, Scope::Party));
		const auto stored = table.ToJson();
		const auto loaded = Table::FromJson(stored);
		assert(loaded.has_value());
		assert(loaded->Stages().size() == 1);
		assert(loaded->Stages().front().party == "alpha");
		assert(loaded->CompletedTracks().size() == 1);
		assert(loaded->CompletedTracks().front().party == "alpha");
		assert(loaded->IsParty("p1", other));
		assert(loaded->ToJson() == stored);
	}

	{
		const Json legacy{
			{ "version", 3 },
			{ "stages", Json::array({ Json{ { "quest", "0A1B2C:fallout4.esm" }, { "stage", 30 }, { "fromPlayerId", 7 }, { "confirmed", true } } }) },
			{ "completed", Json::array({ "0A1B2C:fallout4.esm" }) },
			{ "solo", Json{ { "p1", Json::array({ "0000AA:fallout4.esm" }) } } }
		};
		const auto loaded = Table::FromJson(legacy);
		assert(loaded.has_value());
		assert(loaded->Stages().size() == 1);
		assert(loaded->Stages().front().party.empty());
		assert(loaded->CompletedTracks().size() == 1);
		assert(loaded->CompletedTracks().front().party.empty());
		assert(loaded->PartyQuests("p1").empty());
		assert(loaded->IsSolo("p1", other));
	}

	{
		const Json broken{
			{ "stages", Json::array({ Json{ { "quest", "0A1B2C:fallout4.esm" }, { "stage", 30 }, { "party", "" } } }) },
			{ "completed", Json::array({ Json{ { "quest", "0A1B2C:fallout4.esm" }, { "party", "" } } }) },
			{ "party", Json{ { "p1", Json::array({ "0A1B2C:fallout4.esm" }) } } }
		};
		const auto loaded = Table::FromJson(broken);
		assert(loaded.has_value());
		assert(loaded->Stages().empty());
		assert(loaded->CompletedTracks().empty());
		assert(loaded->PartyQuests("p1").size() == 1);
		assert(!Table::FromJson(Json{ { "party", 5 } }).has_value());
	}

	{
		const auto scope = F4T::QuestProtocol::NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "party" } });
		assert(scope.has_value());
		assert(scope->quest == quest);
		assert(scope->scope == Scope::Party);
		assert(!F4T::QuestProtocol::NormalizeScope(Json{ { "type", "questScope" }, { "quest", "0A1B2C:Fallout4.esm" }, { "scope", "faction" } }));
		const auto policy = F4T::QuestProtocol::MakePolicy(F4T::QuestProtocol::Policy{ Mode::CoopStory, 4, {}, {}, { quest } }, 1.0);
		assert(policy.at("partyQuests").size() == 1);
		assert(policy.at("partyQuests").at(0) == "0A1B2C:fallout4.esm");
	}

	return 0;
}
