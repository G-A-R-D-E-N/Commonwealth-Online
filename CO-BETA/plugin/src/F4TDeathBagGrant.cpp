#include "pch.h"
#include "F4TDeathBagGrant.h"
#include "F4TDeathBagGrantLedger.h"
#include "F4TDropMirrorRefs.h"
#include "F4TFailClosedNotice.h"
#include "F4TItemHealthReadback.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "RE/A/Actor.h"
#include "RE/B/BGSInventoryItem.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectREFR.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <set>
#include <utility>
#include <vector>

namespace F4T::DeathBagGrant
{
	namespace
	{
		constexpr std::size_t kMaximumReconciliationGrants = 64;
		constexpr std::size_t kMaximumQueuedGrants = 256;
		std::mutex g_queueMutex;
		std::vector<F4T::DeathBagWire::HealthGrant> g_queued;
		std::vector<F4T::DeathBagWire::HealthGrant> g_awaitingSave;
		std::set<std::string> g_freshWithdrawals;
		std::uint64_t g_generation{};
		std::string g_cursor;
		std::chrono::steady_clock::time_point g_nextPage{};

		void ResetGeneration(std::uint64_t a_generation)
		{
			if (g_generation == a_generation) return;
			g_generation = a_generation;
			g_cursor.clear();
			g_queued.clear();
			g_awaitingSave.clear();
			g_freshWithdrawals.clear();
			g_nextPage = {};
		}

		std::string OperationKey(const F4T::DeathBagWire::HealthGrant& a_grant)
		{
			return a_grant.deathId + ":" + a_grant.operationId;
		}
	}

	bool TrackWithdrawal(const std::string& a_deathId, const std::string& a_operationId)
	{
		const std::scoped_lock applicationLock(F4T::DeathBagGrantLedger::ApplicationMutex());
		if (F4T::DeathBagGrantLedger::SaveInProgress()) return false;
		const std::scoped_lock lock(g_queueMutex);
		ResetGeneration(F4T::Networking::GetConnectionGeneration());
		const auto operation = a_deathId + ":" + a_operationId;
		if (g_freshWithdrawals.contains(operation)) return true;
		if (g_freshWithdrawals.size() >= kMaximumQueuedGrants) return false;
		if (!F4T::DeathBagGrantLedger::PrepareWithdrawal(operation)) return false;
		return g_freshWithdrawals.insert(operation).second;
	}

	void ForgetWithdrawal(const std::string& a_deathId, const std::string& a_operationId)
	{
		const std::scoped_lock lock(g_queueMutex);
		g_freshWithdrawals.erase(a_deathId + ":" + a_operationId);
	}

	void OnLoad()
	{
		const std::scoped_lock lock(g_queueMutex);
		g_queued.clear();
		g_awaitingSave.clear();
		g_freshWithdrawals.clear();
		g_cursor.clear();
		g_nextPage = {};
	}

	void AwaitSave(const F4T::DeathBagWire::HealthGrant& a_grant)
	{
		const std::scoped_lock lock(g_queueMutex);
		ResetGeneration(F4T::Networking::GetConnectionGeneration());
		const auto key = OperationKey(a_grant);
		if (std::any_of(g_awaitingSave.begin(), g_awaitingSave.end(), [&](const auto& a_waiting) { return OperationKey(a_waiting) == key; })) return;
		if (g_awaitingSave.size() < kMaximumQueuedGrants) g_awaitingSave.push_back(a_grant);
	}

	void RetryFresh(const F4T::DeathBagWire::HealthGrant& a_grant)
	{
		const std::scoped_lock lock(g_queueMutex);
		const auto key = OperationKey(a_grant);
		if (!a_grant.IsFresh() || (!g_freshWithdrawals.contains(key) && !F4T::DeathBagGrantLedger::HasWithdrawalProof(key)) || g_queued.size() >= kMaximumQueuedGrants) return;
		if (std::none_of(g_queued.begin(), g_queued.end(), [&](const auto& a_queued) { return OperationKey(a_queued) == key; })) g_queued.push_back(a_grant);
	}

	Outcome ApplyHealth(RE::Actor& a_actor, const F4T::DeathBagWire::HealthGrant& a_grant)
	{
		const std::scoped_lock applicationLock(F4T::DeathBagGrantLedger::ApplicationMutex());
		if (F4T::DeathBagGrantLedger::SaveInProgress()) return Outcome::Rejected;
		const auto operation = OperationKey(a_grant);
		if (!a_actor.GetParentCell()) return Outcome::Rejected;
		const auto baseId = F4T::DropMirrorRefs::ResolveItemFormId(a_grant.itemKey);
		auto* item = baseId ? RE::TESForm::GetFormByID<RE::TESBoundObject>(*baseId) : nullptr;
		const auto plain = a_grant.metadata.is_null();
		auto extra = plain ? RE::BSTSmartPointer<RE::ExtraDataList>{} : F4T::ItemHealthReadback::CreateExtra(a_grant.metadata);
		if (!item || (!plain && !extra) || a_grant.count == 0 || a_grant.count > static_cast<std::uint32_t>((std::numeric_limits<std::int32_t>::max)())) return Outcome::Rejected;
		const auto receipt = F4T::DeathBagGrantLedger::ReceiptFor(operation);
		bool proof = false;
		if (receipt == F4T::DeathBagGrantLedger::ReceiptStatus::Unknown && a_grant.IsFresh()) {
			const std::scoped_lock lock(g_queueMutex);
			proof = g_freshWithdrawals.contains(operation) || F4T::DeathBagGrantLedger::HasWithdrawalProof(operation);
		}
		const auto decision = F4T::DeathBagReceipts::DecideGrant(receipt, a_grant.IsFresh(), proof);
		if (decision == F4T::DeathBagReceipts::GrantDisposition::Hold ||
			(receipt == F4T::DeathBagGrantLedger::ReceiptStatus::Unknown && !F4T::DeathBagGrantLedger::BeginFreshReceipt())) {
			F4T::Networking::Detail::LogThrottledWarning("death_bag_receipt_unknown", "A death bag grant has unknown save history and requires manual reconciliation.");
			return Outcome::Rejected;
		}
		if (decision == F4T::DeathBagReceipts::GrantDisposition::Duplicate) return Outcome::Duplicate;
		if (!F4T::DeathBagGrantLedger::CanRecordReceipt(operation)) return Outcome::Rejected;
		if (!F4T::DeathBagGrantLedger::Find(operation) && !F4T::DeathBagGrantLedger::Record(operation, 0)) return Outcome::Rejected;
		a_actor.AddObjectToContainer(item, std::move(extra), static_cast<std::int32_t>(a_grant.count), nullptr, RE::ITEM_REMOVE_REASON::kNone);
		(void)F4T::DeathBagGrantLedger::MarkAdded(operation);
		ForgetWithdrawal(a_grant.deathId, a_grant.operationId);
		return Outcome::Attempted;
	}

	void NoteReconciliation(const nlohmann::json& a_packet)
	{
		const auto grants = a_packet.find("grants");
		const auto cursor = a_packet.find("nextCursor");
		if ((a_packet.size() != 2 && a_packet.size() != 3) || grants == a_packet.end() || !grants->is_array() || grants->size() > kMaximumReconciliationGrants ||
			(a_packet.size() == 3 && (cursor == a_packet.end() || (!cursor->is_null() && !cursor->is_string())))) return;
		const std::scoped_lock lock(g_queueMutex);
		ResetGeneration(F4T::Networking::GetConnectionGeneration());
		if (g_queued.size() + grants->size() > kMaximumQueuedGrants) {
			return;
		}
		for (const auto& record : *grants) {
			const auto grant = F4T::DeathBagWire::DecodeGrantRecord(record);
			if (grant && g_queued.size() < kMaximumQueuedGrants) g_queued.push_back(*grant);
		}
		if (cursor != a_packet.end()) {
			if (cursor->is_string()) {
				g_cursor = cursor->get<std::string>();
				g_nextPage = {};
			} else {
				g_cursor.clear();
				g_nextPage = std::chrono::steady_clock::now() + std::chrono::seconds(30);
			}
		}
	}

	void NotePlaceFailedAck(const nlohmann::json& a_packet)
	{
		if (const auto deathId = F4T::DeathBagWire::DecodePlaceFailedAck(a_packet)) F4T::DeathBagGrantLedger::ForgetPlaceFailure(*deathId);
	}

	void NoteUnplacedResult(const nlohmann::json& a_packet)
	{
		const auto result = F4T::DeathBagWire::DecodeUnplacedResult(a_packet);
		if (!result) return;
		F4T::DeathBagGrantLedger::ForgetPendingPlace(result->deathId);
		if (result->status == "refunded") {
			F4T::FailClosedNotice::Show("Commonwealth Online could not place your death bag, so its items were refunded to you.");
		} else if (result->status == "refused") {
			REX::WARN("Death bag unplaced report was refused by the server; the bag is held there.");
		}
	}

	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet)
	{
		if (a_type == "deathBagGrantReconciliation") {
			NoteReconciliation(a_packet);
		} else if (a_type == "deathBagGrantReceipt") {
			if (!F4T::DeathBagWire::DecodeGrantReceipt(a_packet)) return false;
		} else if (a_type == "deathBagPlaceFailedAck") {
			NotePlaceFailedAck(a_packet);
		} else if (a_type == "deathBagUnplacedResult") {
			if (!F4T::DeathBagWire::DecodeUnplacedResult(a_packet)) return false;
			NoteUnplacedResult(a_packet);
		} else {
			return false;
		}
		return true;
	}

	void ApplyReconciliation(const std::function<bool(const nlohmann::json&, std::uint64_t)>& a_sendAttempt)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !player->GetParentCell()) return;
		std::vector<F4T::DeathBagWire::HealthGrant> grants;
		std::uint64_t grantsGeneration{};
		{
			const std::scoped_lock lock(g_queueMutex);
			ResetGeneration(F4T::Networking::GetConnectionGeneration());
			grantsGeneration = g_generation;
			grants.swap(g_queued);
		}
		for (const auto& grant : grants) {
			if (grant.claimRequired) {
				const auto claim = F4T::DeathBagWire::EncodeGrantClaim(grant.deathId, grant.operationId);
				bool prepared = false;
				if (claim) (void)F4T::Networking::TryRunForConnectionGeneration(grantsGeneration, [&] { prepared = TrackWithdrawal(grant.deathId, grant.operationId); });
				if (prepared) {
					(void)F4T::Networking::Detail::SendPacketBytesForGeneration(claim->dump(), grantsGeneration);
				}
				continue;
			}
			const bool applied = F4T::Networking::TryRunForConnectionGeneration(grantsGeneration, [&] {
				const auto outcome = ApplyHealth(*player, grant);
				if (!grant.delivered && (outcome == Outcome::Attempted || outcome == Outcome::Duplicate)) AwaitSave(grant);
				else if (outcome == Outcome::Rejected) RetryFresh(grant);
			});
			if (!applied) {
				const std::scoped_lock lock(g_queueMutex);
				if (g_generation == grantsGeneration && F4T::Networking::GetConnectionGeneration() == grantsGeneration && g_queued.size() < kMaximumQueuedGrants) g_queued.push_back(grant);
			}
		}
		std::vector<F4T::DeathBagWire::HealthGrant> awaiting;
		std::uint64_t awaitingGeneration{};
		{
			const std::scoped_lock lock(g_queueMutex);
			ResetGeneration(F4T::Networking::GetConnectionGeneration());
			awaitingGeneration = g_generation;
			awaiting.swap(g_awaitingSave);
		}
		std::vector<F4T::DeathBagWire::HealthGrant> waiting;
		for (const auto& grant : awaiting) {
			bool send = false;
			bool keep = false;
			const bool checked = F4T::Networking::TryRunForConnectionGeneration(awaitingGeneration, [&] {
				const auto entry = F4T::DeathBagGrantLedger::Find(OperationKey(grant));
				if (!entry || !entry->added) return;
				if (entry->confirmed) send = true;
				else if (entry->unsaved || ApplyHealth(*player, grant) != Outcome::Rejected) keep = true;
				else F4T::Networking::Detail::LogThrottledWarning("death_bag_grant_recheck_rejected", "A death bag grant could not be rechecked after a save load; it waits until the server offers it again.");
			});
			if (!checked || keep || (send && !a_sendAttempt(F4T::DeathBagWire::EncodeHealthGrantAttempt(grant), awaitingGeneration))) waiting.push_back(grant);
		}
		std::string nextCursor;
		bool requestPage = false;
		{
			const std::scoped_lock lock(g_queueMutex);
			ResetGeneration(F4T::Networking::GetConnectionGeneration());
			for (auto& grant : waiting) {
				if (g_generation == awaitingGeneration && g_awaitingSave.size() < kMaximumQueuedGrants) g_awaitingSave.push_back(std::move(grant));
			}
			const auto now = std::chrono::steady_clock::now();
			if (g_queued.empty() && now >= g_nextPage) {
				nextCursor = g_cursor;
				g_nextPage = now + std::chrono::seconds(5);
				requestPage = true;
			}
		}
		if (requestPage) (void)F4T::Networking::SendDeathBagGrantReconcile(nextCursor);
	}
}
