#include "F4TQuestStoryOrder.h"

#include <string>
#include <string_view>
#include <unordered_map>

namespace F4T::QuestStory
{
namespace
{
	using Json = nlohmann::json;

	// Must stay byte-identical to co-core/data/quest-story-order.json
	constexpr std::string_view kEmbeddedStoryOrder = R"STORYJSON({
  "$schema": "quest-story-order",
  "comment": "Single-player Main Quest order for the story guard (ServerQuest.cpp:HandleQuestReport). Every row was read from Fallout4.esm through FO4IDE: EDID + FormKey + quest Filter (Main Quest) from search_records, MQ102 flags (1280, Run Once, no Start Game Enabled) from the record grid, MQ102 full stage list (INDX max 1000) and Concord/museum stage log texts from get_record_fields. Quest-level start conditions do not exist in the data (get_conditions returned [] for MQ103; get_conditions_at has no quest-level path): the chain is started by scripts, so every requires edge below is the single-player EDID sequence, NOT a decoded fragment. All rows are verified:false until the lead rules on them.",
  "quests": [
    {
      "quest": "01ED86:fallout4.esm",
      "label": "War Never Changes",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 01ED86 (EDID MQ101, Filter Main Quest, SwfFile MQ101_War Never Changes.swf). Prologue slideshow, quest-type 6, never synced by the client, so startable:false documents it instead of gating on it.",
      "verified": false
    },
    {
      "quest": "01CC2A:fallout4.esm",
      "label": "Out of Time",
      "requires": [],
      "startable": true,
      "source": "Fallout4.esm QUST 01CC2A (EDID MQ102, Filter Main Quest, Flags 1280, SwfFile MQ102_Out of Time.swf, INDX max stage 1000, Concord/museum stage logs). Entry point of the synced story with no requires: MQ101 is quest-type 6 and never syncs, so nothing in the sync record could satisfy it; the 1e readiness gate already proves the tutorial exit before any story write is evaluated.",
      "verified": false
    },
    {
      "quest": "0229E5:fallout4.esm",
      "label": "Jewel of the Commonwealth",
      "requires": [{ "quest": "01CC2A:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229E5 (EDID MQ103, Filter Main Quest). Next link of the single-player Main Quest EDID sequence after MQ102; exact triggering stage/fragment not decoded from VMAD (opaque), edge unverified.",
      "verified": false
    },
    {
      "quest": "01F25E:fallout4.esm",
      "label": "Unlikely Valentine",
      "requires": [{ "quest": "0229E5:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 01F25E (EDID MQ104, Filter Main Quest, SwfFile MQ104_Unlikely Valentine.swf). Next link after MQ103; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229E6:fallout4.esm",
      "label": "Getting a Clue",
      "requires": [{ "quest": "01F25E:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229E6 (EDID MQ105, Filter Main Quest). Next link after MQ104; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229E7:fallout4.esm",
      "label": "Reunions",
      "requires": [{ "quest": "0229E6:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229E7 (EDID MQ106, Filter Main Quest). Next link after MQ105; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229E8:fallout4.esm",
      "label": "Dangerous Minds",
      "requires": [{ "quest": "0229E7:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229E8 (EDID MQ201, Filter Main Quest). Next link after MQ106; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229E9:fallout4.esm",
      "label": "The Glow",
      "requires": [{ "quest": "0229E8:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229E9 (EDID MQ202, Filter Main Quest). Next link after MQ201; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229EA:fallout4.esm",
      "label": "Hunter/Hunted",
      "requires": [{ "quest": "0229E9:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229EA (EDID MQ203, Filter Main Quest). Next link after MQ202; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "06B500:fallout4.esm",
      "label": "The Molecular Level",
      "requires": [{ "quest": "0229EA:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 06B500 (EDID MQ204, Filter Main Quest). Faction-neutral trunk link after MQ203; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229EB:fallout4.esm",
      "label": "Institutionalized",
      "requires": [{ "quest": "06B500:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229EB (EDID MQ205, Filter Main Quest). Next link after MQ204; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0B1752:fallout4.esm",
      "label": "The Nuclear Option",
      "requires": [{ "quest": "0229EB:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0B1752 (EDID MQ206, Filter Main Quest). Next link after MQ205; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "0229EC:fallout4.esm",
      "label": "Endgame",
      "requires": [{ "quest": "0B1752:fallout4.esm", "completed": true }],
      "startable": true,
      "source": "Fallout4.esm QUST 0229EC (EDID MQ207, Filter Main Quest). End of the trunk after MQ206; exact triggering stage/fragment not decoded, edge unverified.",
      "verified": false
    },
    {
      "quest": "06B4FD:fallout4.esm",
      "label": "MQ204Min",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 06B4FD (EDID MQ204Min). Faction branch of MQ204; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "06B4FE:fallout4.esm",
      "label": "MQ204RR",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 06B4FE (EDID MQ204RR). Faction branch of MQ204; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "06B4FF:fallout4.esm",
      "label": "MQ204BoS",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 06B4FF (EDID MQ204BoS). Faction branch of MQ204; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "0B26BC:fallout4.esm",
      "label": "MQ206RR",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 0B26BC (EDID MQ206RR). Faction branch of MQ206; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "0B26BD:fallout4.esm",
      "label": "MQ206BoS",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 0B26BD (EDID MQ206BoS). Faction branch of MQ206; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "0B26BE:fallout4.esm",
      "label": "MQ206Min",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 0B26BE (EDID MQ206Min). Faction branch of MQ206; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "06D41F:fallout4.esm",
      "label": "MQ201Piper",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 06D41F (EDID MQ201Piper). Companion side thread; no verified order, refused until confirmed.",
      "verified": false
    },
    {
      "quest": "154AD1:fallout4.esm",
      "label": "MQ206OptionalObjectives",
      "requires": [],
      "startable": false,
      "source": "Fallout4.esm QUST 154AD1 (EDID MQ206OptionalObjectives). Optional objectives record; no verified order, refused until confirmed.",
      "verified": false
    }
  ]
}
)STORYJSON";

	std::optional<Requirement> ReadRequirement(const Json& a_item)
	{
		if (!a_item.is_object()) return std::nullopt;
		const auto quest = a_item.find("quest");
		if (quest == a_item.end() || !quest->is_string()) return std::nullopt;
		const auto key = QuestProtocol::ParseFormKey(quest->get_ref<const std::string&>());
		if (!key) return std::nullopt;
		Requirement requirement{ *key, std::nullopt, false };
		if (const auto stage = a_item.find("stage"); stage != a_item.end()) {
			const auto parsed = QuestProtocol::ParseStage(*stage);
			if (!parsed) return std::nullopt;
			requirement.stage = *parsed;
		} else {
			const auto completed = a_item.find("completed");
			if (completed == a_item.end() || !completed->is_boolean() || !completed->get<bool>()) return std::nullopt;
			requirement.completed = true;
		}
		return requirement;
	}

	bool HasCycle(const Order& a_order)
	{
		std::unordered_map<std::string, int> state;
		for (const auto& row : a_order.Rows()) {
			state.emplace(QuestProtocol::FormatFormKey(row.quest), 0);
		}
		const auto visit = [&](const auto& a_self, const QuestProtocol::FormKey& a_quest) -> bool {
			const auto key = QuestProtocol::FormatFormKey(a_quest);
			const auto slot = state.find(key);
			if (slot == state.end()) return false;
			if (slot->second == 1) return true;
			if (slot->second == 2) return false;
			slot->second = 1;
			const auto* row = a_order.Find(a_quest);
			if (row != nullptr) {
				for (const auto& requirement : row->prerequisites) {
					if (a_self(a_self, requirement.quest)) return true;
				}
			}
			slot->second = 2;
			return false;
		};
		for (const auto& row : a_order.Rows()) {
			if (visit(visit, row.quest)) return true;
		}
		return false;
	}
}

bool Order::Empty() const noexcept
{
	return _rows.empty();
}

std::size_t Order::Size() const noexcept
{
	return _rows.size();
}

const Row* Order::Find(const QuestProtocol::FormKey& a_quest) const noexcept
{
	for (const auto& row : _rows) {
		if (row.quest == a_quest) return &row;
	}
	return nullptr;
}

const std::vector<Row>& Order::Rows() const noexcept
{
	return _rows;
}

std::optional<Order> Order::Load(const nlohmann::json& a_document)
{
	if (!a_document.is_object()) return std::nullopt;
	const auto quests = a_document.find("quests");
	if (quests == a_document.end() || !quests->is_array()) return std::nullopt;
	Order order;
	for (const auto& item : *quests) {
		if (!item.is_object()) return std::nullopt;
		const auto quest = item.find("quest");
		const auto label = item.find("label");
		const auto requiresJson = item.find("requires");
		const auto startable = item.find("startable");
		const auto source = item.find("source");
		const auto verified = item.find("verified");
		if (quest == item.end() || !quest->is_string()) return std::nullopt;
		if (label == item.end() || !label->is_string() || label->get_ref<const std::string&>().empty()) return std::nullopt;
		if (requiresJson == item.end() || !requiresJson->is_array()) return std::nullopt;
		if (startable == item.end() || !startable->is_boolean()) return std::nullopt;
		if (source == item.end() || !source->is_string() || source->get_ref<const std::string&>().empty()) return std::nullopt;
		if (verified == item.end() || !verified->is_boolean()) return std::nullopt;
		const auto key = QuestProtocol::ParseFormKey(quest->get_ref<const std::string&>());
		if (!key || order.Find(*key) != nullptr) return std::nullopt;
		Row row{
			*key,
			label->get<std::string>(),
			{},
			startable->get<bool>(),
			source->get<std::string>(),
			verified->get<bool>()
		};
		for (const auto& entry : *requiresJson) {
			const auto requirement = ReadRequirement(entry);
			if (!requirement) return std::nullopt;
			row.prerequisites.push_back(*requirement);
		}
		order._rows.push_back(std::move(row));
	}
	for (const auto& row : order._rows) {
		for (const auto& requirement : row.prerequisites) {
			if (order.Find(requirement.quest) == nullptr) return std::nullopt;
		}
	}
	if (HasCycle(order)) return std::nullopt;
	return order;
}

const Order& Order::Default()
{
	static const Order order = [] {
		const auto parsed = Json::parse(std::string{ kEmbeddedStoryOrder }, nullptr, false);
		const auto loaded = Load(parsed);
		return loaded ? *loaded : Order{};
	}();
	return order;
}
}
