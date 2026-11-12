#include "F4TWorkshopPower.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <limits>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>

namespace F4T::WorkshopPower
{
	namespace
	{
		using Json = nlohmann::json;

		std::optional<std::uint64_t> ReadU64(const Json& a_value)
		{
			if (a_value.is_boolean()) {
				return std::nullopt;
			}
			if (a_value.is_number_unsigned()) {
				return a_value.get<std::uint64_t>();
			}
			if (a_value.is_number_integer()) {
				const auto value = a_value.get<std::int64_t>();
				if (value >= 0) {
					return static_cast<std::uint64_t>(value);
				}
			}
			return std::nullopt;
		}

		std::optional<std::uint32_t> ReadU32(const Json& a_value)
		{
			const auto value = ReadU64(a_value);
			if (!value || *value > (std::numeric_limits<std::uint32_t>::max)()) {
				return std::nullopt;
			}
			return static_cast<std::uint32_t>(*value);
		}

		std::optional<DeviceClass> ReadDeviceClass(const Json& a_value)
		{
			if (!a_value.is_string()) {
				return std::nullopt;
			}
			const auto name = a_value.get<std::string>();
			if (name == "other") {
				return DeviceClass::Other;
			}
			if (name == "light") {
				return DeviceClass::Light;
			}
			if (name == "water") {
				return DeviceClass::Water;
			}
			if (name == "door") {
				return DeviceClass::Door;
			}
			if (name == "turret") {
				return DeviceClass::Turret;
			}
			return std::nullopt;
		}

		[[nodiscard]] std::string_view DeviceClassName(DeviceClass a_class) noexcept
		{
			switch (a_class) {
			case DeviceClass::Light:
				return "light";
			case DeviceClass::Water:
				return "water";
			case DeviceClass::Door:
				return "door";
			case DeviceClass::Turret:
				return "turret";
			case DeviceClass::Other:
			default:
				return "other";
			}
		}

		[[nodiscard]] int ShedRank(DeviceClass a_class) noexcept
		{
			switch (a_class) {
			case DeviceClass::Turret:
				return 0;
			case DeviceClass::Door:
				return 1;
			case DeviceClass::Water:
				return 2;
			case DeviceClass::Light:
				return 3;
			case DeviceClass::Other:
			default:
				return 4;
			}
		}

		[[nodiscard]] const Node* FindNode(const std::vector<Node>& a_nodes, ObjectId a_objectId) noexcept
		{
			const auto it = std::find_if(a_nodes.begin(), a_nodes.end(), [&](const Node& a_node) {
				return a_node.objectId == a_objectId;
			});
			return it == a_nodes.end() ? nullptr : &*it;
		}
	}

	bool PowerGraph::AddNode(Node a_node)
	{
		if (a_node.objectId == 0 || m_nodes.size() >= MaximumNodes || FindNode(m_nodes, a_node.objectId) != nullptr) {
			return false;
		}
		m_nodes.push_back(std::move(a_node));
		MarkTouched(m_nodes.back().objectId);
		return true;
	}

	bool PowerGraph::RemoveNode(ObjectId a_objectId)
	{
		const auto before = m_nodes.size();
		std::erase_if(m_nodes, [&](const Node& a_node) { return a_node.objectId == a_objectId; });
		if (m_nodes.size() == before) {
			return false;
		}
		for (const auto& edge : m_edges) {
			if (edge.fromId == a_objectId || edge.toId == a_objectId) {
				MarkTouched(edge.fromId);
				MarkTouched(edge.toId);
			}
		}
		std::erase_if(m_edges, [&](const Edge& a_edge) {
			return a_edge.fromId == a_objectId || a_edge.toId == a_objectId;
		});
		m_powered.erase(a_objectId);
		MarkTouched(a_objectId);
		return true;
	}

	bool PowerGraph::Connect(ObjectId a_fromId, ObjectId a_toId, ObjectId a_wireId)
	{
		if (a_fromId == 0 || a_toId == 0 || a_fromId == a_toId || m_edges.size() >= MaximumEdges ||
			FindNode(m_nodes, a_fromId) == nullptr || FindNode(m_nodes, a_toId) == nullptr ||
			a_wireId == a_fromId || a_wireId == a_toId) {
			return false;
		}
		for (const auto& edge : m_edges) {
			if ((edge.fromId == a_fromId && edge.toId == a_toId) || (edge.fromId == a_toId && edge.toId == a_fromId) || (a_wireId != 0 && edge.wireId == a_wireId)) {
				return false;
			}
		}
		m_edges.push_back({ a_fromId, a_toId, a_wireId });
		MarkTouched(a_fromId);
		MarkTouched(a_toId);
		return true;
	}

	bool PowerGraph::Disconnect(ObjectId a_fromId, ObjectId a_toId)
	{
		const auto before = m_edges.size();
		std::erase_if(m_edges, [&](const Edge& a_edge) {
			return (a_edge.fromId == a_fromId && a_edge.toId == a_toId) ||
			       (a_edge.fromId == a_toId && a_edge.toId == a_fromId);
		});
		if (m_edges.size() == before) {
			return false;
		}
		MarkTouched(a_fromId);
		MarkTouched(a_toId);
		return true;
	}

	bool PowerGraph::RemoveWire(ObjectId a_wireId)
	{
		if (a_wireId == 0) {
			return false;
		}
		const auto before = m_edges.size();
		for (const auto& edge : m_edges) {
			if (edge.wireId == a_wireId) {
				MarkTouched(edge.fromId);
				MarkTouched(edge.toId);
			}
		}
		std::erase_if(m_edges, [&](const Edge& a_edge) { return a_edge.wireId == a_wireId; });
		return m_edges.size() != before;
	}

	bool PowerGraph::SetSwitch(ObjectId a_objectId, bool a_closed)
	{
		const auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&](const Node& a_node) {
			return a_node.objectId == a_objectId;
		});
		if (it == m_nodes.end()) {
			return false;
		}
		if (it->switchClosed == a_closed) {
			return true;
		}
		it->switchClosed = a_closed;
		MarkTouched(a_objectId);
		return true;
	}

	void PowerGraph::MarkTouched(ObjectId a_objectId)
	{
		if (a_objectId != 0) {
			m_touched.insert(a_objectId);
		}
	}

	std::vector<std::vector<ObjectId>> PowerGraph::ComponentsForTouched() const
	{
		std::unordered_map<ObjectId, std::vector<ObjectId>> adjacency;
		for (const auto& node : m_nodes) {
			adjacency[node.objectId];
		}
		for (const auto& edge : m_edges) {
			adjacency[edge.fromId].push_back(edge.toId);
			adjacency[edge.toId].push_back(edge.fromId);
		}

		std::unordered_set<ObjectId> visited;
		std::vector<std::vector<ObjectId>> components;
		const auto seed = [&](ObjectId a_start) {
			if (visited.contains(a_start)) {
				return;
			}
			std::vector<ObjectId> component;
			std::queue<ObjectId> pending;
			pending.push(a_start);
			visited.insert(a_start);
			while (!pending.empty()) {
				const auto current = pending.front();
				pending.pop();
				component.push_back(current);
				const auto neighbors = adjacency.find(current);
				if (neighbors == adjacency.end()) {
					continue;
				}
				for (const auto neighbor : neighbors->second) {
					if (visited.insert(neighbor).second) {
						pending.push(neighbor);
					}
				}
			}
			components.push_back(std::move(component));
		};

		if (m_touched.empty()) {
			for (const auto& node : m_nodes) {
				seed(node.objectId);
			}
			return components;
		}

		std::unordered_set<ObjectId> touchedComponents;
		for (const auto objectId : m_touched) {
			if (!adjacency.contains(objectId)) {
				continue;
			}
			std::queue<ObjectId> pending;
			std::unordered_set<ObjectId> seen;
			pending.push(objectId);
			seen.insert(objectId);
			while (!pending.empty()) {
				const auto current = pending.front();
				pending.pop();
				for (const auto neighbor : adjacency[current]) {
					if (seen.insert(neighbor).second) {
						pending.push(neighbor);
					}
				}
			}
			for (const auto member : seen) {
				touchedComponents.insert(member);
			}
		}

		for (const auto objectId : touchedComponents) {
			seed(objectId);
		}
		return components;
	}

	void PowerGraph::SolveComponent(const std::vector<ObjectId>& a_component)
	{
		std::unordered_set<ObjectId> componentSet(a_component.begin(), a_component.end());
		for (const auto objectId : a_component) {
			m_powered.erase(objectId);
		}

		std::uint64_t supply = 0;
		std::vector<const Node*> consumers;
		std::uint64_t demand = 0;
		for (const auto objectId : a_component) {
			const auto* node = FindNode(m_nodes, objectId);
			if (node == nullptr) {
				continue;
			}
			if (node->switchClosed && node->outputWatts > 0) {
				supply += node->outputWatts;
				m_powered.insert(objectId);
			}
			if (node->needsPower && node->switchClosed && node->drawWatts > 0) {
				consumers.push_back(node);
				demand += node->drawWatts;
			}
		}

		if (supply == 0 && demand == 0) {
			for (const auto objectId : a_component) {
				const auto* node = FindNode(m_nodes, objectId);
				if (node != nullptr && node->switchClosed && !node->needsPower) {
					m_powered.insert(objectId);
				}
			}
			return;
		}

		if (demand <= supply) {
			for (const auto objectId : a_component) {
				const auto* node = FindNode(m_nodes, objectId);
				if (node != nullptr && node->switchClosed) {
					m_powered.insert(objectId);
				}
			}
			return;
		}

		std::sort(consumers.begin(), consumers.end(), [](const Node* a_left, const Node* a_right) {
			const int leftRank = ShedRank(a_left->deviceClass);
			const int rightRank = ShedRank(a_right->deviceClass);
			if (leftRank != rightRank) {
				return leftRank < rightRank;
			}
			return a_left->objectId < a_right->objectId;
		});

		std::uint64_t budget = supply;
		for (const auto* consumer : consumers) {
			if (consumer->drawWatts <= budget) {
				budget -= consumer->drawWatts;
				m_powered.insert(consumer->objectId);
			}
		}

		std::unordered_map<ObjectId, std::vector<ObjectId>> adjacency;
		for (const auto& edge : m_edges) {
			if (!componentSet.contains(edge.fromId) || !componentSet.contains(edge.toId)) {
				continue;
			}
			adjacency[edge.fromId].push_back(edge.toId);
			adjacency[edge.toId].push_back(edge.fromId);
		}

		std::queue<ObjectId> pending;
		for (const auto objectId : m_powered) {
			if (componentSet.contains(objectId)) {
				pending.push(objectId);
			}
		}
		while (!pending.empty()) {
			const auto current = pending.front();
			pending.pop();
			const auto neighbors = adjacency.find(current);
			if (neighbors == adjacency.end()) {
				continue;
			}
			for (const auto neighbor : neighbors->second) {
				if (m_powered.contains(neighbor)) {
					continue;
				}
				const auto* node = FindNode(m_nodes, neighbor);
				if (node == nullptr || !node->switchClosed) {
					continue;
				}
				if (node->needsPower && node->drawWatts > 0) {
					continue;
				}
				m_powered.insert(neighbor);
				pending.push(neighbor);
			}
		}
	}

	void PowerGraph::Solve()
	{
		const auto components = ComponentsForTouched();
		for (const auto& component : components) {
			SolveComponent(component);
		}
		m_touched.clear();
	}

	nlohmann::json PowerGraph::ToJson() const
	{
		Json state{
			{ "nodes", Json::array() },
			{ "edges", Json::array() }
		};
		for (const auto& node : m_nodes) {
			state["nodes"].push_back(Json{
				{ "objectId", node.objectId },
				{ "outputWatts", node.outputWatts },
				{ "drawWatts", node.drawWatts },
				{ "switchClosed", node.switchClosed },
				{ "needsPower", node.needsPower },
				{ "deviceClass", std::string{ DeviceClassName(node.deviceClass) } }
			});
		}
		for (const auto& edge : m_edges) {
			Json entry{
				{ "fromId", edge.fromId },
				{ "toId", edge.toId }
			};
			if (edge.wireId != 0) {
				entry["wireId"] = edge.wireId;
			}
			state["edges"].push_back(std::move(entry));
		}
		return state;
	}

	bool PowerGraph::FromJson(const nlohmann::json& a_state)
	{
		if (!a_state.is_object()) {
			return false;
		}
		const auto nodes = a_state.find("nodes");
		const auto edges = a_state.find("edges");
		if (nodes == a_state.end() || edges == a_state.end() || !nodes->is_array() || !edges->is_array() ||
			nodes->size() > MaximumNodes || edges->size() > MaximumEdges) {
			return false;
		}

		std::vector<Node> restoredNodes;
		std::unordered_set<ObjectId> seenNodes;
		for (const auto& entry : *nodes) {
			if (!entry.is_object()) {
				return false;
			}
			const auto objectId = entry.find("objectId");
			const auto outputWatts = entry.find("outputWatts");
			const auto drawWatts = entry.find("drawWatts");
			const auto switchClosed = entry.find("switchClosed");
			const auto needsPower = entry.find("needsPower");
			const auto deviceClass = entry.find("deviceClass");
			if (objectId == entry.end() || outputWatts == entry.end() || drawWatts == entry.end() ||
				switchClosed == entry.end() || needsPower == entry.end() || deviceClass == entry.end()) {
				return false;
			}
			const auto id = ReadU64(*objectId);
			const auto output = ReadU32(*outputWatts);
			const auto draw = ReadU32(*drawWatts);
			const auto device = ReadDeviceClass(*deviceClass);
			if (!id || *id == 0 || !output || !draw || !device ||
				(!switchClosed->is_boolean()) || (!needsPower->is_boolean()) ||
				seenNodes.contains(*id)) {
				return false;
			}
			seenNodes.insert(*id);
			restoredNodes.push_back(Node{
				*id,
				*output,
				*draw,
				switchClosed->get<bool>(),
				needsPower->get<bool>(),
				*device
			});
		}

		std::vector<Edge> restoredEdges;
		for (const auto& entry : *edges) {
			if (!entry.is_object()) {
				return false;
			}
			const auto fromId = entry.find("fromId");
			const auto toId = entry.find("toId");
			if (fromId == entry.end() || toId == entry.end()) {
				return false;
			}
			const auto from = ReadU64(*fromId);
			const auto to = ReadU64(*toId);
			if (!from || !to || *from == 0 || *to == 0 || *from == *to ||
				!seenNodes.contains(*from) || !seenNodes.contains(*to)) {
				return false;
			}
			ObjectId wire{};
			if (const auto wireId = entry.find("wireId"); wireId != entry.end()) {
				const auto wireValue = ReadU64(*wireId);
				if (!wireValue || *wireValue == 0 || *wireValue == *from || *wireValue == *to) {
					return false;
				}
				wire = *wireValue;
			}
			for (const auto& edge : restoredEdges) {
				if ((edge.fromId == *from && edge.toId == *to) || (edge.fromId == *to && edge.toId == *from) ||
					(wire != 0 && edge.wireId == wire)) {
					return false;
				}
			}
			restoredEdges.push_back({ *from, *to, wire });
		}

		m_nodes = std::move(restoredNodes);
		m_edges = std::move(restoredEdges);
		m_powered.clear();
		m_touched.clear();
		for (const auto& node : m_nodes) {
			m_touched.insert(node.objectId);
		}
		Solve();
		return true;
	}
}
