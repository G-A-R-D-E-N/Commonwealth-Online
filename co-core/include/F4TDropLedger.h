#pragma once

#include <nlohmann/json.hpp>
#include "F4TSnapshotDeltaModel.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace F4T::Drops
{
	using EntityId = std::uint32_t;

	inline constexpr EntityId kFirstDroppedEntityId = 0x80000000U;
	inline constexpr std::size_t kMaximumOperationIdChars = 64;
	inline constexpr std::uint32_t kMaximumDropCount = 65535;
	inline constexpr std::size_t kMaximumOperationHistory = 64;
	inline constexpr std::size_t kMaximumLiveDropsPerPlayer = 256;
	inline constexpr std::size_t kMaximumLiveDrops = 4096;
	inline constexpr std::size_t kMaximumDropBaselineBytes = 65536;

	struct DropRequest
	{
		std::string operationId;
		std::uint32_t expectedRevision{};
		std::string itemForm;
		std::uint32_t count{};
		std::string cellId;
		std::string worldspaceId;
		float x{};
		float y{};
		float z{};

		bool operator==(const DropRequest&) const = default;
	};

	struct DroppedObject
	{
		EntityId entityId{};
		std::uint32_t ownerPlayerId{};
		std::string itemForm;
		std::uint32_t count{};
		std::string cellId;
		std::string worldspaceId;
		float x{};
		float y{};
		float z{};
	};

	enum class Outcome : std::uint8_t
	{
		Committed,
		Replayed,
		Rejected
	};

	struct TransactionResult
	{
		Outcome outcome{ Outcome::Rejected };
		nlohmann::json packet;
	};

	struct InventoryPosition
	{
		std::string cellId;
		std::string worldspaceId;
		float x{};
		float y{};
		float z{};
	};

	struct StableDroppedObject
	{
		nlohmann::json item;
		InventoryPosition position;
	};

	[[nodiscard]] std::optional<std::vector<std::uint8_t>> EncodeStableDroppedObject(const StableDroppedObject& a_object);
	[[nodiscard]] std::optional<StableDroppedObject> DecodeStableDroppedObject(std::span<const std::uint8_t> a_bytes);
	[[nodiscard]] std::optional<F4T::Replication::SnapshotState> BuildStableDropSnapshot(const nlohmann::json& a_live);

	[[nodiscard]] std::optional<DropRequest> NormalizeDropRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::vector<std::uint8_t> EncodeDroppedObject(const DroppedObject& a_object);
	[[nodiscard]] std::optional<DroppedObject> DecodeDroppedObject(std::span<const std::uint8_t> a_bytes);

	class DropLedger
	{
	public:
		[[nodiscard]] static TransactionResult Drop(nlohmann::json& a_root, const std::string& a_profileId,
			const nlohmann::json& a_request, const InventoryPosition& a_position);
		[[nodiscard]] static TransactionResult Pickup(nlohmann::json& a_root, const std::string& a_profileId,
			const nlohmann::json& a_request, const InventoryPosition& a_position);
		[[nodiscard]] static std::optional<TransactionResult> Precheck(const nlohmann::json& a_root, const std::string& a_profileId,
			const nlohmann::json& a_request, bool a_drop);
		static void Validate(const nlohmann::json& a_root);
	};
}
