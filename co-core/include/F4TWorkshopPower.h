#pragma once

#include "F4TWorkshopTransactions.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

namespace F4T::WorkshopPower
{
	using ObjectId = Workshop::ObjectId;

	inline constexpr std::size_t MaximumNodes = 512;
	inline constexpr std::size_t MaximumEdges = 1024;

	enum class DeviceClass : std::uint8_t
	{
		Other,
		Light,
		Water,
		Door,
		Turret
	};

	struct Node
	{
		ObjectId objectId{};
		std::uint32_t outputWatts{};
		std::uint32_t drawWatts{};
		bool switchClosed{ true };
		bool needsPower{};
		DeviceClass deviceClass{ DeviceClass::Other };
	};

	struct Edge
	{
		ObjectId fromId{};
		ObjectId toId{};
		ObjectId wireId{};
	};

	class PowerGraph
	{
	public:
		[[nodiscard]] bool AddNode(Node a_node);
		[[nodiscard]] bool RemoveNode(ObjectId a_objectId);
		[[nodiscard]] bool Connect(ObjectId a_fromId, ObjectId a_toId, ObjectId a_wireId = 0);
		[[nodiscard]] bool RemoveWire(ObjectId a_wireId);
		[[nodiscard]] bool Disconnect(ObjectId a_fromId, ObjectId a_toId);
		[[nodiscard]] bool SetSwitch(ObjectId a_objectId, bool a_closed);
		void Solve();
		[[nodiscard]] const std::unordered_set<ObjectId>& Powered() const noexcept { return m_powered; }
		[[nodiscard]] const std::vector<Node>& Nodes() const noexcept { return m_nodes; }
		[[nodiscard]] const std::vector<Edge>& Edges() const noexcept { return m_edges; }
		[[nodiscard]] nlohmann::json ToJson() const;
		[[nodiscard]] bool FromJson(const nlohmann::json& a_state);

	private:
		void MarkTouched(ObjectId a_objectId);
		void SolveComponent(const std::vector<ObjectId>& a_component);
		[[nodiscard]] std::vector<std::vector<ObjectId>> ComponentsForTouched() const;

		std::vector<Node> m_nodes;
		std::vector<Edge> m_edges;
		std::unordered_set<ObjectId> m_powered;
		std::unordered_set<ObjectId> m_touched;
	};
}
