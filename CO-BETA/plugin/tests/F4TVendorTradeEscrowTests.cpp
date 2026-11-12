#include "F4TVendorTradeEscrow.h"

#include <cassert>
#include <chrono>
#include <cstddef>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace VE = F4T::VendorTradeEscrow;
namespace VT = F4T::VendorTrade;

namespace
{
	const VE::Context kAlpha{ "server-a", "world-1", "profile-1", 1U };
	const VE::Context kOtherServer{ "server-b", "world-2", "profile-1", 1U };
	const VE::Context kOtherCharacter{ "server-a", "world-1", "profile-1", 2U };
	const VE::Context kOtherWorld{ "server-a", "world-2", "profile-1", 1U };
	const VE::Context kOtherProfile{ "server-a", "world-1", "profile-2", 1U };
	const VE::Context kNoServer{ "", "world-1", "profile-1", 1U };
	const VE::Context kNoWorld{ "server-a", "", "profile-1", 1U };
	const VE::Context kNoSlot{ "server-b", "world-2", "profile-1", std::nullopt };
	const VE::Clock::time_point kStart{ std::chrono::seconds{ 100 } };
	const std::string kValidParked = R"json({"unresolved":[{"operationId":"op-1","taken":[],"capsTaken":0,"grant":[],"capsGrant":0,"packet":{"operationId":"op-1"},"context":{"serverKey":"server-a","worldId":"world-1","profileId":"profile-1","characterSlot":null}}],"payouts":[]})json";

	VE::Escrow MakeEscrow(const std::string& a_operationId, const VE::Context& a_context)
	{
		VE::Escrow escrow;
		escrow.operationId = a_operationId;
		escrow.context = a_context;
		escrow.packet = nlohmann::json{ { "type", "vendorTrade" }, { "operationId", a_operationId } };
		escrow.nextResend = kStart + VE::kResendDelay;
		return escrow;
	}

	std::map<std::string, VE::Escrow> OneEscrow(const VE::Context& a_context)
	{
		std::map<std::string, VE::Escrow> unresolved;
		unresolved.emplace("op-1", MakeEscrow("op-1", a_context));
		return unresolved;
	}

	void TestSameWorldReconnectResends()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto beforeReconnect = VE::DueResends(unresolved, kAlpha, false, kStart);
		assert(beforeReconnect.empty());
		const auto due = VE::DueResends(unresolved, kAlpha, true, kStart);
		assert(due.size() == 1);
		assert(due.front().first == "op-1");
		assert(due.front().second.value("resend", false));
	}

	void TestOtherServerDoesNotRetry()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto due = VE::DueResends(unresolved, kOtherServer, true, kStart);
		assert(due.empty());
		assert(!unresolved.at("op-1").nextResend.has_value());
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kOtherServer);
		assert(!resolved.has_value());
		assert(unresolved.size() == 1);
	}

	void TestCharacterSwitchDoesNotRetry()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto due = VE::DueResends(unresolved, kOtherCharacter, true, kStart);
		assert(due.empty());
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kOtherCharacter);
		assert(!resolved.has_value());
		assert(unresolved.size() == 1);
	}

	void TestReturnToOriginalResolves()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto parked = VE::DueResends(unresolved, kOtherServer, true, kStart);
		assert(parked.empty());
		const auto due = VE::DueResends(unresolved, kAlpha, false, kStart);
		assert(due.size() == 1);
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kAlpha);
		assert(resolved.has_value());
		assert(resolved->operationId == "op-1");
		assert(unresolved.empty());
	}

	void TestEmptyServerKeyIsNeverReconcilable()
	{
		assert(!VE::MayReconcile(kNoServer, kNoServer));
		assert(VE::MayReconcile(kAlpha, kAlpha));
		auto unresolved = OneEscrow(kNoServer);
		const auto due = VE::DueResends(unresolved, kNoServer, true, kStart);
		assert(due.empty());
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kNoServer);
		assert(!resolved.has_value());
	}

	void TestParkedPayoutsWait()
	{
		std::vector<VE::Payout> pending{ VE::Payout{ MakeEscrow("op-1", kAlpha), true }, VE::Payout{ MakeEscrow("op-2", kOtherServer), false } };
		const auto due = VE::TakeDuePayouts(pending, kAlpha);
		assert(due.size() == 1);
		assert(due.front().escrow.operationId == "op-1");
		assert(due.front().grant);
		assert(pending.size() == 1);
		assert(pending.front().escrow.operationId == "op-2");
		const auto later = VE::TakeDuePayouts(pending, kOtherServer);
		assert(later.size() == 1);
		assert(pending.empty());
	}

	void TestFailedSendWaitsForTheDelay()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto first = VE::DueResends(unresolved, kAlpha, true, kStart);
		assert(first.size() == 1);
		const auto early = VE::DueResends(unresolved, kAlpha, false, kStart + std::chrono::seconds{ 1 });
		assert(early.empty());
		const auto reconnected = VE::DueResends(unresolved, kAlpha, true, kStart + std::chrono::seconds{ 2 });
		assert(reconnected.size() == 1);
		const auto later = VE::DueResends(unresolved, kAlpha, false, kStart + std::chrono::seconds{ 7 });
		assert(later.size() == 1);
	}

	void TestOnlyWorldIdChangeParks()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto due = VE::DueResends(unresolved, kOtherWorld, true, kStart);
		assert(due.empty());
		assert(!unresolved.at("op-1").nextResend.has_value());
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kOtherWorld);
		assert(!resolved.has_value());
		assert(unresolved.size() == 1);
	}

	void TestOnlyProfileIdChangeParks()
	{
		auto unresolved = OneEscrow(kAlpha);
		const auto due = VE::DueResends(unresolved, kOtherProfile, true, kStart);
		assert(due.empty());
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kOtherProfile);
		assert(!resolved.has_value());
		assert(unresolved.size() == 1);
	}

	void TestEmptyWorldIdCompareAsAValue()
	{
		auto unresolved = OneEscrow(kNoWorld);
		const auto parked = VE::DueResends(unresolved, kAlpha, true, kStart);
		assert(parked.empty());
		const auto due = VE::DueResends(unresolved, kNoWorld, true, kStart);
		assert(due.size() == 1);
		const auto resolved = VE::TakeResolved(unresolved, "op-1", kNoWorld);
		assert(resolved.has_value());
		assert(unresolved.empty());
	}

	void TestParkedRoundTripKeepsEveryField()
	{
		VE::Escrow escrow = MakeEscrow("op-1", kAlpha);
		escrow.taken = { VT::StockLine{ 0x1234U, 2U } };
		escrow.capsTaken = 50U;
		escrow.grant = { VT::StockLine{ 0x5678U, 1U } };
		escrow.capsGrant = 7U;
		VE::Parked parked;
		parked.unresolved.emplace("op-1", escrow);
		parked.payouts.push_back(VE::Payout{ MakeEscrow("op-2", kNoSlot), true });
		const auto text = VE::EncodeParked(parked);
		assert(text.has_value());
		const auto decoded = VE::DecodeParked(*text);
		assert(decoded.has_value());
		const auto& restored = decoded->unresolved.at("op-1");
		assert(restored.operationId == "op-1");
		assert(restored.taken == escrow.taken);
		assert(restored.capsTaken == 50U);
		assert(restored.grant == escrow.grant);
		assert(restored.capsGrant == 7U);
		assert(restored.packet == escrow.packet);
		assert(restored.context == kAlpha);
		assert(!restored.nextResend.has_value());
		assert(decoded->payouts.size() == 1);
		assert(decoded->payouts.front().grant);
		assert(decoded->payouts.front().escrow.operationId == "op-2");
		assert(decoded->payouts.front().escrow.context == kNoSlot);
	}

	void TestTruncatedOrCorruptParkedIsRejected()
	{
		assert(VE::DecodeParked(kValidParked).has_value());
		VE::Parked parked;
		parked.unresolved.emplace("op-1", MakeEscrow("op-1", kAlpha));
		const auto text = VE::EncodeParked(parked);
		assert(text.has_value());
		assert(!VE::DecodeParked(text->substr(0, text->size() / 2)).has_value());
		assert(!VE::DecodeParked("").has_value());
		assert(!VE::DecodeParked("[]").has_value());
		assert(!VE::DecodeParked(R"json({"unresolved":[],"payouts":"none"})json").has_value());
		assert(!VE::DecodeParked(R"json({"unresolved":[{"operationId":"op-1"}],"payouts":[]})json").has_value());
		assert(!VE::DecodeParked(R"json({"unresolved":[],"payouts":[{"escrow":{},"grant":true}]})json").has_value());
		assert(!VE::DecodeParked(R"json({"unresolved":[{"operationId":"op-1","taken":[],"capsTaken":-1,"grant":[],"capsGrant":0,"packet":{},"context":{"serverKey":"server-a","worldId":"world-1","profileId":"profile-1","characterSlot":null}}],"payouts":[]})json").has_value());
	}

	void TestParkedCapsAreEnforced()
	{
		VE::Parked parked;
		for (std::size_t index = 0; index <= VE::kMaximumParkedEntries; ++index) {
			const auto operationId = "op-" + std::to_string(index);
			parked.unresolved.emplace(operationId, MakeEscrow(operationId, kAlpha));
		}
		assert(!VE::EncodeParked(parked).has_value());
		parked.unresolved.erase("op-0");
		assert(VE::EncodeParked(parked).has_value());
		parked.unresolved.at("op-1").packet["blob"] = std::string(VE::kMaximumParkedBytes, 'x');
		assert(!VE::EncodeParked(parked).has_value());
		assert(!VE::DecodeParked(std::string(VE::kMaximumParkedBytes + 1U, ' ')).has_value());
	}

	void TestParkedRejectsForeignPacketAndOversizedCaps()
	{
		VE::Parked parked;
		parked.unresolved.emplace("op-1", MakeEscrow("op-1", kAlpha));
		assert(VE::DecodeParked(*VE::EncodeParked(parked)).has_value());
		parked.unresolved.at("op-1").packet["operationId"] = "op-2";
		assert(!VE::DecodeParked(*VE::EncodeParked(parked)).has_value());
		parked.unresolved.at("op-1").packet["operationId"] = "op-1";
		parked.unresolved.at("op-1").capsTaken = VT::MaximumCaps;
		assert(VE::DecodeParked(*VE::EncodeParked(parked)).has_value());
		parked.unresolved.at("op-1").capsTaken = VT::MaximumCaps + 1U;
		assert(!VE::DecodeParked(*VE::EncodeParked(parked)).has_value());
		parked.unresolved.at("op-1").capsTaken = 0U;
		parked.unresolved.at("op-1").capsGrant = VT::MaximumCaps + 1U;
		assert(!VE::DecodeParked(*VE::EncodeParked(parked)).has_value());
	}

	void TestFitsParkedRefusesWhatWouldOverflowTheSave()
	{
		VE::Parked parked;
		assert(VE::FitsParked(parked, MakeEscrow("op-2", kAlpha)));
		parked.unresolved.emplace("op-1", MakeEscrow("op-1", kAlpha));
		parked.unresolved.at("op-1").packet["blob"] = "";
		const auto base = VE::EncodeParked(parked);
		assert(base.has_value());
		parked.unresolved.at("op-1").packet["blob"] = std::string(VE::kMaximumParkedBytes - base->size(), 'x');
		assert(VE::EncodeParked(parked).has_value());
		assert(!VE::FitsParked(parked, MakeEscrow("op-2", kAlpha)));
		parked.unresolved.at("op-1").packet["blob"] = std::string(1024U, 'x');
		assert(VE::FitsParked(parked, MakeEscrow("op-2", kAlpha)));
	}

	void TestParkedCountIsPerContext()
	{
		std::map<std::string, VE::Escrow> unresolved;
		std::vector<VE::Payout> payouts;
		for (std::size_t index = 0; index < 63; ++index) {
			const auto operationId = "u-" + std::to_string(index);
			unresolved.emplace(operationId, MakeEscrow(operationId, kAlpha));
		}
		payouts.push_back(VE::Payout{ MakeEscrow("p-0", kAlpha), true });
		assert(VE::CountParkedFor(unresolved, payouts, kAlpha) == VE::kMaximumContextEntries);
		assert(VE::CountParkedFor(unresolved, payouts, kOtherServer) == 0U);
		const VE::Context others[] = { kOtherServer, kOtherCharacter, kOtherWorld };
		for (std::size_t index = 0; index < 192; ++index) {
			const auto operationId = "o-" + std::to_string(index);
			unresolved.emplace(operationId, MakeEscrow(operationId, others[index % 3]));
		}
		assert(unresolved.size() + payouts.size() == VE::kMaximumParkedEntries);
		assert(VE::CountParkedFor(unresolved, payouts, kAlpha) == VE::kMaximumContextEntries);
		assert(VE::CountParkedFor(unresolved, payouts, kOtherServer) == VE::kMaximumContextEntries);
		assert(VE::CountParkedFor(unresolved, payouts, kOtherProfile) == 0U);
	}
}

int main()
{
	TestSameWorldReconnectResends();
	TestOtherServerDoesNotRetry();
	TestCharacterSwitchDoesNotRetry();
	TestReturnToOriginalResolves();
	TestEmptyServerKeyIsNeverReconcilable();
	TestParkedPayoutsWait();
	TestFailedSendWaitsForTheDelay();
	TestOnlyWorldIdChangeParks();
	TestOnlyProfileIdChangeParks();
	TestEmptyWorldIdCompareAsAValue();
	TestParkedRoundTripKeepsEveryField();
	TestTruncatedOrCorruptParkedIsRejected();
	TestParkedCapsAreEnforced();
	TestParkedRejectsForeignPacketAndOversizedCaps();
	TestFitsParkedRefusesWhatWouldOverflowTheSave();
	TestParkedCountIsPerContext();
	return 0;
}
