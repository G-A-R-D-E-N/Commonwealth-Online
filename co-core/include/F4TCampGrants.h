#pragma once

#include "F4TTradeLedger.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
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
	[[nodiscard]] ApplyOutcome ApplyOnce(std::uint64_t a_operationId, const std::function<bool(std::uint64_t)>& a_wasApplied,
		const std::function<bool()>& a_apply, const std::function<void(std::uint64_t)>& a_remember);
	[[nodiscard]] std::vector<std::uint64_t> ParseAppliedIds(std::string_view a_text);
	[[nodiscard]] std::string FormatAppliedIds(const std::vector<std::uint64_t>& a_ids);
	void RememberAppliedId(std::vector<std::uint64_t>& a_ids, std::uint64_t a_operationId);
}
