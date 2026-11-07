#include "F4TCreditLedger.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace
{
	using F4T::QuestCredit::CreditLedger;
	using F4T::QuestCredit::kCreditTtlSeconds;
	using F4T::QuestCredit::kMaximumPendingCredits;
	using F4T::QuestCredit::kMaximumPendingCreditsPerProfile;
	using F4T::QuestProtocol::AssistKind;
	using F4T::QuestProtocol::FormKey;

	FormKey Quest()
	{
		return FormKey{ "fallout4.esm", 0x01CC2A };
	}

	std::uint32_t Objective(std::size_t a_index)
	{
		return static_cast<std::uint32_t>(a_index);
	}
}

int main()
{
	{
		CreditLedger ledger;
		assert(ledger.Size() == 0);
		assert(ledger.NextId() == 1);
		assert(ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 0.0) == "c-1");
		assert(ledger.Mint("bob", Quest(), 6, AssistKind::ReachLocation, 0.0) == "c-2");
		assert(ledger.Mint("alice", Quest(), 7, AssistKind::Activate, 0.0) == "c-3");
		assert(ledger.Size() == 3);
		assert(ledger.NextId() == 4);
	}

	{
		CreditLedger ledger;
		const std::string creditId = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 100.0);
		assert(creditId == "c-1");
		const auto taken = ledger.Take(creditId, "alice", 110.0);
		assert(taken.has_value());
		assert(taken->creditId == creditId);
		assert(taken->profileId == "alice");
		assert(taken->quest == Quest());
		assert(taken->objectiveId == 5);
		assert(taken->kind == AssistKind::KillActor);
		assert(taken->expiresAtSeconds == 160.0);
		assert(ledger.Size() == 0);
		assert(!ledger.Take(creditId, "alice", 110.0).has_value());
		assert(!ledger.Take("c-99", "alice", 110.0).has_value());
	}

	{
		CreditLedger ledger;
		const std::string creditId = ledger.Mint("alice", Quest(), 5, AssistKind::Dialogue, 0.0);
		assert(!ledger.Take(creditId, "bob", 1.0).has_value());
		assert(ledger.Size() == 1);
		const auto taken = ledger.Take(creditId, "alice", 1.0);
		assert(taken.has_value());
		assert(taken->kind == AssistKind::Dialogue);
		assert(ledger.Size() == 0);
	}

	{
		CreditLedger ledger;
		const std::string creditId = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 0.0);
		assert(!ledger.Take(creditId, "alice", kCreditTtlSeconds).has_value());
		assert(ledger.Size() == 0);
		const std::string second = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 0.0);
		assert(second == "c-2");
		assert(ledger.Take(second, "alice", kCreditTtlSeconds - 0.001).has_value());
		const std::string third = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 0.0);
		assert(third == "c-3");
		const std::string fourth = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, kCreditTtlSeconds);
		assert(fourth == "c-4");
		assert(!ledger.Take(third, "alice", kCreditTtlSeconds).has_value());
		assert(ledger.Take(fourth, "alice", kCreditTtlSeconds).has_value());
	}

	{
		CreditLedger ledger;
		const std::string first = ledger.Mint("alice", Quest(), 1, AssistKind::KillActor, 0.0);
		const std::string second = ledger.Mint("bob", Quest(), 2, AssistKind::KillActor, 30.0);
		ledger.Expire(59.0);
		assert(ledger.Size() == 2);
		ledger.Expire(60.0);
		assert(ledger.Size() == 1);
		assert(!ledger.Take(first, "alice", 61.0).has_value());
		assert(!ledger.Take(first, "bob", 61.0).has_value());
		const auto taken = ledger.Take(second, "bob", 61.0);
		assert(taken.has_value());
		assert(taken->objectiveId == 2);
		assert(ledger.Size() == 0);
	}

	{
		CreditLedger ledger;
		for (std::size_t index = 0; index < kMaximumPendingCredits; ++index) {
			const std::string profile = "p" + std::to_string(index / kMaximumPendingCreditsPerProfile);
			assert(!ledger.Mint(profile, Quest(), Objective(index), AssistKind::KillActor, 0.0).empty());
		}
		assert(ledger.Size() == kMaximumPendingCredits);
		assert(ledger.NextId() == kMaximumPendingCredits + 1);
		assert(ledger.Mint("extra", Quest(), 999, AssistKind::KillActor, 0.0).empty());
		assert(ledger.Size() == kMaximumPendingCredits);
		assert(ledger.NextId() == kMaximumPendingCredits + 1);
		const std::string evicted = ledger.Mint("alice", Quest(), 999, AssistKind::KillActor, kCreditTtlSeconds);
		assert(evicted == "c-257");
		assert(ledger.Size() == 1);
		assert(ledger.NextId() == 258);
	}

	{
		CreditLedger ledger;
		for (std::size_t index = 0; index < kMaximumPendingCredits; ++index) {
			const std::string profile = "p" + std::to_string(index / kMaximumPendingCreditsPerProfile);
			assert(!ledger.Mint(profile, Quest(), Objective(index), AssistKind::KillActor, 0.0).empty());
		}
		ledger.Expire(kCreditTtlSeconds);
		assert(ledger.Size() == 0);
		const std::string minted = ledger.Mint("alice", Quest(), 42, AssistKind::KillActor, kCreditTtlSeconds);
		assert(minted == "c-257");
		assert(ledger.Size() == 1);
	}

	{
		CreditLedger ledger(5.0);
		const std::string creditId = ledger.Mint("alice", Quest(), 5, AssistKind::Escort, 0.0);
		assert(!ledger.Take(creditId, "alice", 5.0).has_value());
		assert(ledger.Size() == 0);
		const std::string second = ledger.Mint("alice", Quest(), 5, AssistKind::Escort, 0.0);
		assert(ledger.Take(second, "alice", 4.999).has_value());
	}

	{
		CreditLedger ledger;
		const std::string creditId = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 10.0);
		assert(ledger.Take(creditId, "", 10.0) == std::nullopt);
		assert(ledger.Size() == 1);
		assert(ledger.Take(creditId, std::string_view{}, 10.0) == std::nullopt);
		assert(ledger.Take(creditId, "alice", 10.0).has_value());
	}

	{
		CreditLedger ledger;
		const std::string creditId = ledger.Mint("alice", Quest(), 7, AssistKind::Activate, 10.0);
		const auto peeked = ledger.Peek(creditId, "alice", 10.0);
		assert(peeked.has_value());
		assert(peeked->creditId == creditId);
		assert(peeked->profileId == "alice");
		assert(peeked->objectiveId == 7);
		assert(peeked->kind == AssistKind::Activate);
		assert(ledger.Size() == 1);
		assert(ledger.Peek(creditId, "alice", 10.0).has_value());
		assert(ledger.Size() == 1);
		assert(!ledger.Peek(creditId, "bob", 10.0).has_value());
		assert(!ledger.Peek("c-999", "alice", 10.0).has_value());
		assert(ledger.Size() == 1);
		assert(ledger.Peek(creditId, "alice", kCreditTtlSeconds + 10.0).has_value() == false);
		assert(ledger.Take(creditId, "alice", 10.0).has_value());
		assert(!ledger.Peek(creditId, "alice", 10.0).has_value());
	}

	{
		CreditLedger ledger;
		for (std::size_t index = 0; index < kMaximumPendingCreditsPerProfile; ++index) {
			assert(!ledger.Mint("alice", Quest(), Objective(index), AssistKind::KillActor, 0.0).empty());
		}
		assert(ledger.Mint("alice", Quest(), 999, AssistKind::KillActor, 0.0).empty());
		assert(ledger.Size() == kMaximumPendingCreditsPerProfile);
		assert(!ledger.Mint("bob", Quest(), 999, AssistKind::KillActor, 0.0).empty());
		assert(ledger.Size() == kMaximumPendingCreditsPerProfile + 1);
		assert(ledger.Take("c-1", "alice", 1.0).has_value());
		assert(!ledger.Mint("alice", Quest(), 999, AssistKind::KillActor, 1.0).empty());
		assert(ledger.Mint("alice", Quest(), 998, AssistKind::KillActor, 1.0).empty());
		assert(!ledger.Mint("alice", Quest(), 997, AssistKind::KillActor, kCreditTtlSeconds).empty());
	}

	{
		CreditLedger ledger;
		const std::string first = ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 0.0);
		assert(first == "c-1");
		assert(ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 1.0).empty());
		assert(ledger.Size() == 1);
		assert(!ledger.Mint("alice", Quest(), 5, AssistKind::Activate, 1.0).empty());
		assert(!ledger.Mint("alice", Quest(), 6, AssistKind::KillActor, 1.0).empty());
		assert(!ledger.Mint("bob", Quest(), 5, AssistKind::KillActor, 1.0).empty());
		assert(!ledger.Mint("alice", FormKey{ "other.esm", 0x01CC2A }, 5, AssistKind::KillActor, 1.0).empty());
		assert(ledger.Size() == 5);
		assert(ledger.Take(first, "alice", 2.0).has_value());
		assert(!ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 2.0).empty());
		assert(ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 2.0).empty());
	}

	{
		CreditLedger ledger;
		assert(!ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, 0.0).empty());
		assert(ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, kCreditTtlSeconds - 0.001).empty());
		assert(!ledger.Mint("alice", Quest(), 5, AssistKind::KillActor, kCreditTtlSeconds).empty());
	}

	return 0;
}
