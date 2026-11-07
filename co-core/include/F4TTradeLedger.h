#pragma once

#include <nlohmann/json.hpp>
#include "F4TDropLedger.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::Trade
{
	inline constexpr std::size_t kMaximumTradeIdChars = 64;
	inline constexpr double kIdleExpirySeconds = 60.0;
	inline constexpr double kRequestExpirySeconds = 15.0;
	inline constexpr double kPairCooldownSeconds = 30.0;

	struct Line
	{
		std::string itemKey;
		std::uint32_t count{};

		bool operator==(const Line&) const = default;
	};

	enum class IntentKind : std::uint8_t
	{
		Request,
		Respond,
		Offer,
		Confirm,
		Cancel
	};

	struct Intent
	{
		IntentKind kind{ IntentKind::Request };
		std::string tradeId;
		std::uint32_t targetPlayerId{};
		bool accept{};
		std::uint32_t offerVersion{};
		std::vector<Line> lines;
	};

	struct Side
	{
		std::uint32_t playerId{};
		std::vector<Line> offer;
		bool confirmed{};
		std::uint32_t confirmedRevision{};
	};

	struct Trade
	{
		std::string tradeId;
		Side initiator;
		Side target;
		bool accepted{};
		std::uint32_t offerVersion{};
		double lastActivityMonotonic{};
	};

	struct Ended
	{
		std::string tradeId;
		std::uint32_t initiatorPlayerId{};
		std::uint32_t targetPlayerId{};
		std::string reason;
	};

	struct Verdict
	{
		bool accepted{};
		std::string reason;
	};

	struct CommitParty
	{
		std::string profileId;
		std::uint32_t expectedRevision{};
		std::vector<Line> give;
	};

	struct CommitRequest
	{
		std::string operationId;
		CommitParty first;
		CommitParty second;
	};

	struct CommitResult
	{
		F4T::Drops::Outcome outcome{ F4T::Drops::Outcome::Rejected };
		std::string reason;
		std::optional<std::uint32_t> firstRevision;
		std::optional<std::uint32_t> secondRevision;
	};

	[[nodiscard]] std::optional<Intent> ParseClientIntent(const nlohmann::json& a_packet);
	[[nodiscard]] std::optional<nlohmann::json> NormalizeCommitRequest(const nlohmann::json& a_record);
	[[nodiscard]] nlohmann::json CommitResultRecord(const nlohmann::json& a_record, std::uint32_t a_revision);
	[[nodiscard]] CommitResult Commit(nlohmann::json& a_root, const CommitRequest& a_request);
	[[nodiscard]] nlohmann::json StatePacket(const Trade& a_trade, std::uint32_t a_viewerPlayerId);
	[[nodiscard]] nlohmann::json EndedPacket(const Ended& a_ended, std::uint32_t a_viewerPlayerId);
	[[nodiscard]] nlohmann::json CommitResultPacket(const std::string& a_tradeId, const CommitRequest& a_request,
		const CommitResult& a_result, bool a_first);
	[[nodiscard]] nlohmann::json RefusalPacket(std::string_view a_tradeId, std::string_view a_reason);

	class Book
	{
	public:
		explicit Book(std::uint64_t a_epoch) : _epoch{ a_epoch } {}

		[[nodiscard]] Verdict Request(std::uint32_t a_initiatorPlayerId, std::uint32_t a_targetPlayerId, double a_nowMonotonic);
		[[nodiscard]] Verdict Respond(const std::string& a_tradeId, std::uint32_t a_playerId, bool a_accept, double a_nowMonotonic);
		[[nodiscard]] Verdict SetOffer(const std::string& a_tradeId, std::uint32_t a_playerId, std::vector<Line> a_lines, double a_nowMonotonic);
		[[nodiscard]] Verdict Confirm(const std::string& a_tradeId, std::uint32_t a_playerId, std::uint32_t a_revision, std::uint32_t a_offerVersion, double a_nowMonotonic);
		[[nodiscard]] Verdict Cancel(const std::string& a_tradeId, std::uint32_t a_playerId, double a_nowMonotonic);
		[[nodiscard]] bool Ready(const std::string& a_tradeId) const;
		[[nodiscard]] std::optional<CommitRequest> BuildCommit(const std::string& a_tradeId,
			const std::string& a_initiatorProfileId, const std::string& a_targetProfileId) const;
		void Unconfirm(const std::string& a_tradeId);
		void Finish(const std::string& a_tradeId, const std::string& a_reason);
		void EndFor(std::uint32_t a_playerId, const std::string& a_reason);
		void Expire(double a_nowMonotonic);
		[[nodiscard]] const Trade* Find(const std::string& a_tradeId) const;
		[[nodiscard]] const Trade* ActiveFor(std::uint32_t a_playerId) const;
		[[nodiscard]] const std::vector<Trade>& Trades() const noexcept;
		[[nodiscard]] std::vector<Ended> TakeEnded();

	private:
		void EndWithCooldown(std::size_t a_index, const std::string& a_reason, double a_nowMonotonic);

		std::uint64_t _epoch{};
		std::uint64_t _sequence{};
		std::vector<Trade> _trades;
		std::vector<Ended> _ended;
		std::map<std::pair<std::uint32_t, std::uint32_t>, double> _cooldowns;
	};
}
