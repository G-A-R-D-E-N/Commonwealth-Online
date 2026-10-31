#pragma once

#include "F4TEchoLedger.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace F4T::NpcLootSync
{
	inline constexpr std::size_t MaximumPublishTypes = 64;
	inline constexpr std::size_t MaximumCoalescedTakes = 64;
	inline constexpr std::size_t MaximumEchoEntries = F4T::MaximumEchoEntries;
	inline constexpr std::size_t MaximumOperationResults = 64;
	inline constexpr std::uint64_t EchoLifetimeMilliseconds = F4T::EchoLifetimeMilliseconds;
	inline constexpr std::uint32_t MaximumLootCount = 65535;
	// source:CO-BETA/docs/protocol/packets.md
	inline constexpr std::uint32_t RuntimeFormIdBase = 0xFF000000;

	struct LootItem
	{
		std::uint32_t formId{};
		std::uint32_t count{};

		bool operator==(const LootItem&) const = default;
	};

	struct LootIdentity
	{
		std::uint32_t npcId{};
		std::uint32_t sourceFormId{};
		std::uint32_t cellId{};
		std::uint32_t worldspaceId{};
		std::uint32_t authorityEpoch{};

		bool operator==(const LootIdentity&) const = default;
	};

	struct RemoteLoot
	{
		LootIdentity identity;
		std::uint32_t revision{};
		std::uint32_t authorityPlayerId{};
		std::vector<LootItem> items;

		bool operator==(const RemoteLoot&) const = default;
	};

	struct TakeRequest
	{
		LootIdentity identity;
		std::uint32_t takerPlayerId{};
		std::string operationId;
		std::uint32_t formId{};
		std::uint32_t count{};

		bool operator==(const TakeRequest&) const = default;
	};

	struct TakeResult
	{
		std::string operationId;
		bool accepted{};
		std::string reason;
		std::uint32_t formId{};
		std::uint32_t count{};
		std::uint32_t revision{};
		std::uint32_t takerPlayerId{};

		bool operator==(const TakeResult&) const = default;
	};

	struct PublishList
	{
		std::vector<LootItem> items;
		bool truncated{};
	};

	struct MirrorDiff
	{
		std::vector<LootItem> removals;
		std::vector<LootItem> additions;
	};

	struct CoalescedTake
	{
		std::uint32_t npcId{};
		std::uint32_t formId{};
		std::uint32_t count{};

		bool operator==(const CoalescedTake&) const = default;
	};

	[[nodiscard]] std::optional<RemoteLoot> ParseRemoteLoot(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<TakeRequest> ParseTakeRequest(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<TakeResult> ParseTakeResult(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json RemoteLootToJson(const RemoteLoot& a_loot);
	[[nodiscard]] nlohmann::json TakeRequestToJson(const TakeRequest& a_take);
	[[nodiscard]] nlohmann::json TakeResultToJson(const TakeResult& a_result);
	[[nodiscard]] PublishList BuildPublishList(std::span<const LootItem> a_raw);
	[[nodiscard]] MirrorDiff ComputeMirrorDiff(std::span<const LootItem> a_current, std::span<const LootItem> a_target);
	[[nodiscard]] std::uint32_t ClampToAvailable(std::uint32_t a_requested, std::uint32_t a_available) noexcept;
	[[nodiscard]] std::uint32_t NextRevision(std::uint32_t a_previous) noexcept;
	[[nodiscard]] std::string MakeOperationId(std::uint32_t a_takerPlayerId, std::uint32_t a_counter);

	class TakeCoalescer
	{
	public:
		[[nodiscard]] bool Add(std::uint32_t a_npcId, std::uint32_t a_formId, std::uint32_t a_count);
		[[nodiscard]] std::vector<CoalescedTake> Drain(std::size_t a_maxPerCall);

	private:
		std::vector<CoalescedTake> _takes;
	};

	using EchoLedger = F4T::EchoLedger;

	class RevisionCounter
	{
	public:
		[[nodiscard]] std::uint32_t Next(std::uint32_t a_npcId);
		void Clear() noexcept;

	private:
		std::unordered_map<std::uint32_t, std::uint32_t> _last;
	};

	class OperationLru
	{
	public:
		void Remember(std::uint32_t a_takerPlayerId, std::string_view a_operationId, const TakeResult& a_result);
		[[nodiscard]] std::optional<TakeResult> Find(std::uint32_t a_takerPlayerId, std::string_view a_operationId);

	private:
		struct Entry
		{
			std::uint32_t takerPlayerId{};
			std::string operationId;
			TakeResult result;
		};

		std::vector<Entry> _entries;
	};
}
