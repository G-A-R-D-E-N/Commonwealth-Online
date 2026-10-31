#pragma once

#include "F4TQuestProtocol.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace F4T::QuestStory
{
struct Requirement
{
	QuestProtocol::FormKey quest;
	std::optional<std::uint16_t> stage;
	bool completed{};
};

struct Row
{
	QuestProtocol::FormKey quest;
	std::string label;
	std::vector<Requirement> prerequisites;
	bool startable{};
	std::string source;
	bool verified{};
};

class Order
{
public:
	[[nodiscard]] bool Empty() const noexcept;
	[[nodiscard]] std::size_t Size() const noexcept;
	[[nodiscard]] const Row* Find(const QuestProtocol::FormKey& a_quest) const noexcept;
	[[nodiscard]] const std::vector<Row>& Rows() const noexcept;

	// constraint: Strict Order::Load rejects duplicates, cycles, dangling requires.
	[[nodiscard]] static std::optional<Order> Load(const nlohmann::json& a_document);

	// must stay byte-identical to co-core/data/quest-story-order.json.
	[[nodiscard]] static const Order& Default();

private:
	std::vector<Row> _rows;
};
}
