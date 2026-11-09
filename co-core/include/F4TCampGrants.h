#pragma once

#include "F4TTradeLedger.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::CampGrants
{
	inline constexpr std::size_t kMaximumRecords = 32;
	inline constexpr std::size_t kMaximumAckedRecords = 512;
	inline constexpr std::size_t kMaximumChunksPerGrant = 4;

	struct Chunk
	{
		std::uint32_t revision{};
		std::vector<Trade::Line> lines;

		bool operator==(const Chunk&) const = default;
	};

	enum class Outcome : std::uint8_t
	{
		Committed,
		Replayed,
		Rejected
	};

	enum class ApplyOutcome : std::uint8_t
	{
		Acknowledge,
		Hold,
		Retry
	};

	struct Commit
	{
		Outcome outcome{ Outcome::Rejected };
		std::string reason;
		std::vector<Chunk> chunks;
		bool pending{};
	};

	struct Grant
	{
		std::uint64_t operationId{};
		std::vector<Chunk> chunks;
	};

	using Counts = std::map<std::string, std::int64_t>;

	enum class ReceiptState : std::uint8_t
	{
		Pending,
		Applied,
		Acked
	};

	struct Receipt
	{
		std::uint64_t operationId{};
		ReceiptState state{ ReceiptState::Pending };
		Counts baseline;

		bool operator==(const Receipt&) const = default;
	};

	struct Snapshot
	{
		std::string worldId;
		std::vector<Receipt> receipts;

		bool operator==(const Snapshot&) const = default;
	};

	using Worlds = std::map<std::string, std::vector<Receipt>>;

	[[nodiscard]] Commit CommitMove(nlohmann::json& a_root, std::string_view a_profileId, std::uint64_t a_operationId,
		const std::vector<Trade::Line>& a_lines);
	[[nodiscard]] std::optional<Commit> Replay(const nlohmann::json& a_root, std::string_view a_profileId, std::uint64_t a_operationId);
	[[nodiscard]] bool Ack(nlohmann::json& a_root, std::string_view a_profileId, std::uint64_t a_operationId);
	[[nodiscard]] std::vector<Grant> Pending(const nlohmann::json& a_root, std::string_view a_profileId);
	[[nodiscard]] nlohmann::json MakeGrantPacket(std::uint64_t a_operationId, const std::vector<Chunk>& a_chunks);
	[[nodiscard]] std::optional<Grant> ParseGrantPacket(const nlohmann::json& a_packet);
	[[nodiscard]] nlohmann::json MakeGrantAck(std::uint64_t a_operationId);
	[[nodiscard]] std::optional<std::uint64_t> ParseGrantAck(const nlohmann::json& a_packet);
	[[nodiscard]] std::vector<Trade::Line> FlattenLines(const std::vector<Chunk>& a_chunks);
	[[nodiscard]] ApplyOutcome ApplyOnce(const Grant& a_grant, const Counts& a_current, std::vector<Receipt>& a_receipts,
		const std::function<bool(const std::vector<Trade::Line>&)>& a_apply, const std::function<std::optional<Counts>()>& a_recount);
	[[nodiscard]] std::vector<std::uint64_t> AppliedIds(const std::vector<Receipt>& a_receipts);
	[[nodiscard]] std::vector<std::uint64_t> MarkLanded(std::vector<Receipt>& a_receipts, const std::vector<std::uint64_t>& a_operationIds);
	void AddAcked(std::vector<Receipt>& a_receipts, const std::vector<std::uint64_t>& a_operationIds);
	[[nodiscard]] std::optional<std::vector<std::uint64_t>> ParseAckedIds(std::string_view a_text);
	[[nodiscard]] std::string FormatWorlds(const Worlds& a_worlds);
	[[nodiscard]] std::optional<Worlds> ParseWorlds(std::string_view a_text);
	[[nodiscard]] std::optional<Snapshot> ParseSnapshot(std::string_view a_text);
	void BoundAcked(Worlds& a_worlds);
	[[nodiscard]] bool FitWorlds(Worlds& a_worlds, const std::string& a_currentWorld, std::size_t a_maximumBytes);
}
