#pragma once

#include "F4TQuestProtocol.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::QuestCredit
{
	inline constexpr std::size_t kMaximumPendingCredits = 256;
	inline constexpr std::size_t kMaximumPendingCreditsPerProfile = 16;
	inline constexpr double kCreditTtlSeconds = 60.0;

	struct PendingCredit
	{
		std::string creditId;
		std::string profileId;
		QuestProtocol::FormKey quest;
		std::uint32_t objectiveId{};
		QuestProtocol::AssistKind kind{ QuestProtocol::AssistKind::KillActor };
		double expiresAtSeconds{};
	};

	class CreditLedger
	{
	public:
		explicit CreditLedger(double a_ttlSeconds = kCreditTtlSeconds)
			: _ttlSeconds(a_ttlSeconds)
		{}

		std::string Mint(
			std::string_view a_profileId,
			const QuestProtocol::FormKey& a_quest,
			std::uint32_t a_objectiveId,
			QuestProtocol::AssistKind a_kind,
			double a_nowSeconds)
		{
			Expire(a_nowSeconds);
			if (_credits.size() >= kMaximumPendingCredits) {
				return {};
			}
			std::size_t pendingForProfile = 0;
			for (const auto& entry : _credits) {
				const auto& pending = entry.second;
				if (pending.profileId != a_profileId) continue;
				if (pending.quest == a_quest && pending.objectiveId == a_objectiveId && pending.kind == a_kind) {
					return {};
				}
				++pendingForProfile;
			}
			if (pendingForProfile >= kMaximumPendingCreditsPerProfile) {
				return {};
			}
			const std::string creditId = "c-" + std::to_string(++_counter);
			PendingCredit credit;
			credit.creditId = creditId;
			credit.profileId = std::string{ a_profileId };
			credit.quest = a_quest;
			credit.objectiveId = a_objectiveId;
			credit.kind = a_kind;
			credit.expiresAtSeconds = a_nowSeconds + _ttlSeconds;
			_credits.emplace(creditId, std::move(credit));
			return creditId;
		}

		[[nodiscard]] std::optional<PendingCredit> Peek(std::string_view a_creditId, std::string_view a_profileId, double a_nowSeconds) const
		{
			const auto found = _credits.find(std::string{ a_creditId });
			if (found == _credits.end()) {
				return std::nullopt;
			}
			if (a_nowSeconds >= found->second.expiresAtSeconds) {
				return std::nullopt;
			}
			if (found->second.profileId != a_profileId) {
				return std::nullopt;
			}
			return found->second;
		}

		std::optional<PendingCredit> Take(std::string_view a_creditId, std::string_view a_profileId, double a_nowSeconds)
		{
			const auto found = _credits.find(std::string{ a_creditId });
			if (found == _credits.end()) {
				return std::nullopt;
			}
			if (a_nowSeconds >= found->second.expiresAtSeconds) {
				_credits.erase(found);
				return std::nullopt;
			}
			if (found->second.profileId != a_profileId) {
				return std::nullopt;
			}
			PendingCredit taken = std::move(found->second);
			_credits.erase(found);
			return taken;
		}

		void Expire(double a_nowSeconds)
		{
			for (auto credit = _credits.begin(); credit != _credits.end();) {
				if (a_nowSeconds >= credit->second.expiresAtSeconds) {
					credit = _credits.erase(credit);
				} else {
					++credit;
				}
			}
		}

		[[nodiscard]] std::size_t Size() const noexcept { return _credits.size(); }
		[[nodiscard]] std::uint64_t NextId() const noexcept { return _counter + 1; }

	private:
		double _ttlSeconds{ kCreditTtlSeconds };
		std::uint64_t _counter{};
		std::map<std::string, PendingCredit> _credits;
	};
}
