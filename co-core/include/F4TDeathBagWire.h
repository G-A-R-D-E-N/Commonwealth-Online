#pragma once
#include <nlohmann/json.hpp>
#include "F4TInventoryContract.h"
#include "F4TDropLedger.h"
#include "F4TItemHealthSnapshot.h"
#include <limits>
#include <cstdint>
#include <algorithm>
#include <optional>
#include <string>
namespace F4T::DeathBagWire
{
	inline constexpr std::size_t MaximumDiscoveryBags = 128;
	inline constexpr std::size_t MaximumOwnerDiscoveryStacks = 1024;
	inline constexpr std::size_t MaximumForeignDiscoveryStacks = 128;
	inline constexpr std::size_t MaximumDiscoveryBytes = 60 * 1024;
	[[nodiscard]] inline std::size_t DiscoveryFrameBytes()
	{
		return nlohmann::json{ { "type", "deathBagDiscovery" }, { "bags", nlohmann::json::array() } }.dump().size();
	}
	[[nodiscard]] inline std::size_t DiscoveryEntryBytes(const nlohmann::json& a_entry)
	{
		return a_entry.dump().size() + 1;
	}
	[[nodiscard]] inline bool ValidDeathId(const std::string& a_id)
	{
		return a_id.size() == 32 && std::all_of(a_id.begin(), a_id.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
	}
	[[nodiscard]] inline nlohmann::json EncodeDiscovery()
	{
		return nlohmann::json{ { "type", "deathBagDiscover" } };
	}
	[[nodiscard]] inline std::optional<nlohmann::json> EncodeGrantClaim(const std::string& a_id, const std::string& a_operation)
	{
		if (!ValidDeathId(a_id) || a_operation.empty() || a_operation.size() + 5 + a_id.size() > F4T::Drops::kMaximumOperationIdChars) return std::nullopt;
		return nlohmann::json{ { "type", "deathBagGrantClaim" }, { "deathId", a_id }, { "operationId", a_operation } };
	}
	[[nodiscard]] inline std::optional<nlohmann::json> EncodeWithdraw(const std::string& a_id, const std::string& a_itemKey,
		std::uint32_t a_count, std::uint32_t a_revision, const std::string& a_operation, bool a_receiptProof = false)
	{
		if (!ValidDeathId(a_id) || !F4T::InventoryContract::ParseItemKey(a_itemKey) || a_count == 0 || a_operation.empty() ||
			a_operation.size() + 5 + a_id.size() > F4T::Drops::kMaximumOperationIdChars) return std::nullopt;
		auto packet = nlohmann::json{ { "type", "deathBagWithdraw" }, { "deathId", a_id }, { "itemKey", a_itemKey },
			{ "count", a_count }, { "expectedRevision", a_revision }, { "operationId", a_operation } };
		if (a_receiptProof) packet["receiptProof"] = true;
		return packet;
	}
	[[nodiscard]] inline bool DecodeGrantReceipt(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 4 || !a_packet.contains("type") || a_packet.at("type") != "deathBagGrantReceipt" ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !ValidDeathId(a_packet.at("deathId").get<std::string>()) || !a_packet.contains("operationId") || !a_packet.at("operationId").is_string() || a_packet.at("operationId").get<std::string>().empty() || !a_packet.contains("status") || !a_packet.at("status").is_string()) return false;
		return a_packet.at("status") == "attempted" || a_packet.at("status") == "claimed";
	}
	struct HealthGrant
	{
		std::string deathId;
		std::string operationId;
		std::string itemKey;
		std::uint32_t count{};
		nlohmann::json metadata;
		bool delivered{};
		std::optional<bool> fresh;
		bool claimRequired{};
		[[nodiscard]] bool IsFresh() const { return fresh.value_or(false); }
	};
	[[nodiscard]] inline std::optional<HealthGrant> DecodeHealthGrant(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || (a_packet.size() != 6 && a_packet.size() != 7) || !a_packet.contains("type") || a_packet.at("type") != "deathBagHealthGrant" ||
			(a_packet.contains("fresh") && !a_packet.at("fresh").is_boolean()) || (a_packet.size() == 7 && !a_packet.contains("fresh")) ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("operationId") || !a_packet.at("operationId").is_string() ||
			!a_packet.contains("itemKey") || !a_packet.at("itemKey").is_string() || !a_packet.contains("count") || !a_packet.at("count").is_number_unsigned() ||
			!a_packet.contains("metadata") || (!a_packet.at("metadata").is_null() && !F4T::ItemHealthSnapshot::Decode(a_packet.at("metadata")))) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		const auto operation = a_packet.at("operationId").get<std::string>();
		const auto key = a_packet.at("itemKey").get<std::string>();
		const auto count = a_packet.at("count").get<std::uint64_t>();
		if (!ValidDeathId(id) || operation.empty() || operation.size() + 5 + id.size() > F4T::Drops::kMaximumOperationIdChars ||
			!F4T::InventoryContract::ParseItemKey(key) || count == 0 || count > static_cast<std::uint64_t>((std::numeric_limits<std::int32_t>::max)())) return std::nullopt;
		return HealthGrant{ id, operation, key, static_cast<std::uint32_t>(count), a_packet.at("metadata"), false,
			a_packet.contains("fresh") ? std::optional<bool>{ a_packet.at("fresh").get<bool>() } : std::nullopt };
	}
	[[nodiscard]] inline std::optional<HealthGrant> DecodeGrantRecord(const nlohmann::json& a_record)
	{
		if (!a_record.is_object() || a_record.size() != 7 + a_record.count("fresh") + a_record.count("claimRequired") ||
			(a_record.contains("fresh") && !a_record.at("fresh").is_boolean()) ||
			(a_record.contains("claimRequired") && (a_record.at("claimRequired") != true || a_record.contains("fresh"))) ||
			!a_record.contains("deathId") || !a_record.at("deathId").is_string() ||
			!a_record.contains("operationId") || !a_record.at("operationId").is_string() || !a_record.contains("characterId") || !a_record.at("characterId").is_string() ||
			!a_record.contains("itemKey") || !a_record.at("itemKey").is_string() || !a_record.contains("count") || !a_record.at("count").is_number_unsigned() ||
			!a_record.contains("metadata") || !a_record.contains("state") || (a_record.at("state") != "pending" && a_record.at("state") != "attempted")) return std::nullopt;
		nlohmann::json packet{ { "type", "deathBagHealthGrant" }, { "deathId", a_record.at("deathId") }, { "operationId", a_record.at("operationId") },
			{ "itemKey", a_record.at("itemKey") }, { "count", a_record.at("count") }, { "metadata", a_record.at("metadata") } };
		if (a_record.contains("fresh")) packet["fresh"] = a_record.at("fresh");
		auto grant = DecodeHealthGrant(packet);
		if (grant) {
			grant->delivered = a_record.at("state") == "attempted";
			grant->claimRequired = a_record.contains("claimRequired");
			if (grant->delivered && grant->claimRequired) return std::nullopt;
		}
		return grant;
	}
	[[nodiscard]] inline nlohmann::json EncodeHealthGrantAttempt(const HealthGrant& a_grant)
	{
		return nlohmann::json{ { "type", "deathBagGrantAttempted" }, { "deathId", a_grant.deathId }, { "operationId", a_grant.operationId } };
	}
	[[nodiscard]] inline std::optional<nlohmann::json> EncodeSnapshot(const std::string& a_id, const nlohmann::json& a_stacks, const nlohmann::json& a_retained)
	{
		if (!ValidDeathId(a_id) || !a_stacks.is_array() || !a_retained.is_array()) return std::nullopt;
		return nlohmann::json{ { "type", "deathBagSnapshot" }, { "deathId", a_id }, { "status", "countsVerified" }, { "stacks", a_stacks }, { "retained", a_retained } };
	}
	[[nodiscard]] inline std::optional<nlohmann::json> EncodePlaced(const std::string& a_id)
	{
		if (!ValidDeathId(a_id)) return std::nullopt;
		return nlohmann::json{ { "type", "deathBagPlaced" }, { "deathId", a_id } };
	}
	struct DepositResult
	{
		std::string deathId;
		bool committed{};
	};
	[[nodiscard]] inline std::optional<DepositResult> DecodeDepositResult(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 3 || !a_packet.contains("type") || a_packet.at("type") != "deathBagDepositResult" ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("status") || !a_packet.at("status").is_string()) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		const auto status = a_packet.at("status").get<std::string>();
		if (!ValidDeathId(id) || (status != "committed" && status != "rejected")) return std::nullopt;
		return DepositResult{ id, status == "committed" };
	}
	[[nodiscard]] inline bool ValidStacks(const nlohmann::json& a_stacks)
	{
		if (!a_stacks.is_array() || a_stacks.size() > MaximumOwnerDiscoveryStacks) return false;
		for (const auto& stack : a_stacks) {
			if (!stack.is_object() || stack.size() != 3 || !stack.contains("itemKey") || !stack.at("itemKey").is_string() ||
				!F4T::InventoryContract::ParseItemKey(stack.at("itemKey").get_ref<const std::string&>()) || !stack.contains("count") ||
				!stack.at("count").is_number_unsigned() || stack.at("count").get<std::uint64_t>() == 0 ||
				stack.at("count").get<std::uint64_t>() > (std::numeric_limits<std::uint32_t>::max)() || !stack.contains("metadata") ||
				(!stack.at("metadata").is_null() && !F4T::ItemHealthSnapshot::Decode(stack.at("metadata")))) return false;
		}
		return true;
	}
	[[nodiscard]] inline bool ValidRemaining(bool a_restored, const nlohmann::json& a_remaining)
	{
		return ValidStacks(a_remaining) && (!a_restored || a_remaining.empty());
	}
	struct PlaceFailed
	{
		std::string deathId;
		bool restored{};
		nlohmann::json remaining;
	};
	[[nodiscard]] inline std::optional<nlohmann::json> EncodePlaceFailed(const std::string& a_id, bool a_restored, const nlohmann::json& a_remaining)
	{
		if (!ValidDeathId(a_id) || !ValidRemaining(a_restored, a_remaining)) return std::nullopt;
		return nlohmann::json{ { "type", "deathBagPlaceFailed" }, { "deathId", a_id }, { "restored", a_restored }, { "remaining", a_remaining } };
	}
	[[nodiscard]] inline std::optional<PlaceFailed> DecodePlaceFailed(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 4 || !a_packet.contains("type") || !a_packet.at("type").is_string() ||
			a_packet.at("type") != "deathBagPlaceFailed" || !a_packet.contains("deathId") || !a_packet.at("deathId").is_string() ||
			!a_packet.contains("restored") || !a_packet.at("restored").is_boolean() || !a_packet.contains("remaining")) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		const auto restored = a_packet.at("restored").get<bool>();
		if (!ValidDeathId(id) || !ValidRemaining(restored, a_packet.at("remaining"))) return std::nullopt;
		return PlaceFailed{ id, restored, a_packet.at("remaining") };
	}
	[[nodiscard]] inline std::optional<nlohmann::json> EncodePlaceFailedAck(const std::string& a_id)
	{
		if (!ValidDeathId(a_id)) return std::nullopt;
		return nlohmann::json{ { "type", "deathBagPlaceFailedAck" }, { "deathId", a_id } };
	}
	[[nodiscard]] inline std::optional<std::string> DecodePlaceFailedAck(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 2 || !a_packet.contains("type") || !a_packet.at("type").is_string() ||
			a_packet.at("type") != "deathBagPlaceFailedAck" || !a_packet.contains("deathId") || !a_packet.at("deathId").is_string()) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		if (!ValidDeathId(id)) return std::nullopt;
		return id;
	}
	[[nodiscard]] inline std::optional<nlohmann::json> DecodeDiscovery(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 2 || !a_packet.contains("type") || a_packet.at("type") != "deathBagDiscovery" ||
			!a_packet.contains("bags") || !a_packet.at("bags").is_array() || a_packet.at("bags").size() > MaximumDiscoveryBags) return std::nullopt;
		std::size_t bytes = DiscoveryFrameBytes();
		for (const auto& bag : a_packet.at("bags")) {
			bytes += DiscoveryEntryBytes(bag);
			if (bytes > MaximumDiscoveryBytes) return std::nullopt;
			if (!bag.is_object() || bag.size() != 4 || !bag.contains("deathId") || !bag.at("deathId").is_string() ||
				!ValidDeathId(bag.at("deathId").get<std::string>()) || !bag.contains("canTake") || !bag.at("canTake").is_boolean() ||
				!bag.contains("location") || !bag.at("location").is_object() || !bag.contains("stacks") || !ValidStacks(bag.at("stacks"))) return std::nullopt;
			const auto& location = bag.at("location");
			if (location.size() != 3 || !location.contains("cellKey") || !location.at("cellKey").is_string() || !location.contains("worldspaceKey") ||
				(!location.at("worldspaceKey").is_null() && !location.at("worldspaceKey").is_string()) || !location.contains("position") ||
				!location.at("position").is_array() || location.at("position").size() != 3) return std::nullopt;
		}
		return a_packet.at("bags");
	}
	struct Voided
	{
		std::string deathId;
		nlohmann::json stacks;
	};
	[[nodiscard]] inline std::optional<Voided> DecodeVoided(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 3 || !a_packet.contains("type") || a_packet.at("type") != "deathBagVoided" ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("stacks") || !a_packet.at("stacks").is_array()) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		if (!ValidDeathId(id)) return std::nullopt;
		return Voided{ id, a_packet.at("stacks") };
	}
	struct PlacedResult
	{
		std::string deathId;
		bool placed{ false };
	};
	[[nodiscard]] inline nlohmann::json EncodePlacedResult(const std::string& a_id, bool a_placed)
	{
		return nlohmann::json{ { "type", "deathBagPlacedResult" }, { "deathId", a_id }, { "status", a_placed ? "placed" : "rejected" } };
	}
	[[nodiscard]] inline std::optional<PlacedResult> DecodePlacedResult(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 3 || !a_packet.contains("type") || a_packet.at("type") != "deathBagPlacedResult" ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("status") || !a_packet.at("status").is_string()) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		const auto& status = a_packet.at("status").get_ref<const std::string&>();
		if (!ValidDeathId(id) || (status != "placed" && status != "rejected")) return std::nullopt;
		return PlacedResult{ id, status == "placed" };
	}
	[[nodiscard]] inline std::optional<nlohmann::json> EncodeUnplaced(const std::string& a_id, const std::string& a_operation)
	{
		if (!ValidDeathId(a_id) || a_operation.empty() || a_operation.size() > 16) return std::nullopt;
		return nlohmann::json{ { "type", "deathBagUnplaced" }, { "deathId", a_id }, { "operationId", a_operation } };
	}
	struct UnplacedResult
	{
		std::string deathId;
		std::string status;
	};
	[[nodiscard]] inline std::optional<UnplacedResult> DecodeUnplacedResult(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 3 || !a_packet.contains("type") || a_packet.at("type") != "deathBagUnplacedResult" ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string() || !a_packet.contains("status") || !a_packet.at("status").is_string()) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		const auto status = a_packet.at("status").get<std::string>();
		if (!ValidDeathId(id) || (status != "refunded" && status != "absent" && status != "refused")) return std::nullopt;
		return UnplacedResult{ id, status };
	}
	[[nodiscard]] inline std::optional<std::string> DecodeCreate(const nlohmann::json& a_packet)
	{
		if (!a_packet.is_object() || a_packet.size() != 2 || !a_packet.contains("type") || !a_packet.at("type").is_string() ||
			a_packet.at("type") != "deathBagCreate" ||
			!a_packet.contains("deathId") || !a_packet.at("deathId").is_string()) return std::nullopt;
		const auto id = a_packet.at("deathId").get<std::string>();
		if (!ValidDeathId(id)) return std::nullopt;
		return id;
	}
}
