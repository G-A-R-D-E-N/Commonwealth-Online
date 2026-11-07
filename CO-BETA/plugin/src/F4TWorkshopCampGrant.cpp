#include "pch.h"

#include "F4TWorkshopReplicationInternal.h"

#include "F4TAppliedOperations.h"
#include "F4TCampGrants.h"
#include "F4TDebugLogging.h"
#include "F4TDropMirrorRefs.h"
#include "F4TInventorySync.h"
#include "F4TLocalPlayerState.h"
#include "F4TNpcSyncLootInternal.h"
#include "F4TServerProfileStore.h"
#include "RE/S/SendHUDMessage.h"
#include "RE/T/TESBoundObject.h"
#include "RE/T/TESForm.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace F4T::WorkshopReplicationClient
{
	using Json = nlohmann::json;

	namespace
	{
		constexpr std::size_t kMaximumQueuedGrants = 16;
		constexpr std::size_t kMaximumListedNames = 6;

		struct TradeApplyScope
		{
			TradeApplyScope() { F4T::InventorySync::g_tradeApplying = true; }
			~TradeApplyScope() { F4T::InventorySync::g_tradeApplying = false; }
		};

		std::mutex g_grantMutex;
		std::deque<F4T::CampGrants::Grant> g_queuedGrants;
		std::string g_appliedScope;
		std::vector<std::uint64_t> g_appliedGrants;

		// Constraint: the item name is the engine full name; source:CO-BETA/plugin/src/F4TTradeWindow.cpp:33
		std::string ItemName(const std::string& a_itemKey)
		{
			if (const auto resolved = F4T::DropMirrorRefs::ResolveItemFormId(a_itemKey)) {
				if (const auto* item = RE::TESForm::GetFormByID<RE::TESBoundObject>(*resolved)) {
					const auto fullName = RE::TESFullName::GetFullName(*item);
					if (!fullName.empty()) {
						return std::string{ fullName };
					}
				}
			}
			return a_itemKey;
		}

		std::string DescribeLines(const std::vector<F4T::Trade::Line>& a_lines)
		{
			std::string text;
			std::size_t listed = 0;
			for (const auto& line : a_lines) {
				if (listed == kMaximumListedNames) {
					text += ", and more";
					break;
				}
				text += std::format("{}{} x {}", listed == 0 ? "" : ", ", line.count, ItemName(line.itemKey));
				++listed;
			}
			return text;
		}

		void Hud(const std::string& a_text)
		{
			RE::SendHUDMessage::ShowHUDMessage(a_text.c_str(), nullptr, true, true);
		}

		bool ApplyGrant(const F4T::CampGrants::Grant& a_grant)
		{
			auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				return false;
			}
			for (const auto& chunk : a_grant.chunks) {
				for (const auto& line : chunk.lines) {
					if (!F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey)) {
						return false;
					}
				}
			}
			std::vector<F4T::Trade::Line> received;
			bool complete = true;
			for (const auto& chunk : a_grant.chunks) {
				if (!F4T::InventorySync::NoteTradeCommitted(chunk.revision, {}, chunk.lines)) {
					complete = false;
				}
			}
			{
				const TradeApplyScope scope;
				for (const auto& chunk : a_grant.chunks) {
					for (const auto& line : chunk.lines) {
						const auto resolved = F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey);
						// Constraint: same container add the trade apply uses; source:CO-BETA/plugin/src/F4TTradeClient.cpp:244
						F4T::NpcSync::Loot::Detail::AddLootToContainer(*player, *resolved, line.count);
						received.push_back(line);
					}
				}
			}
			if (!complete) {
				F4T::InventorySync::NoteResyncNeeded();
			}
			if (!received.empty()) {
				Hud("Camp refund received: " + DescribeLines(received) + ".");
			}
			return true;
		}

		std::string AppliedScope()
		{
			const auto worldId = F4T::ServerProfileStore::ConnectedWorldId();
			return worldId.empty() ? std::string{} : "camp-grants-" + worldId;
		}

		void LoadAppliedGrants(const std::string& a_scope)
		{
			std::scoped_lock lock(g_grantMutex);
			if (a_scope == g_appliedScope) {
				return;
			}
			g_appliedScope = a_scope;
			g_appliedGrants = F4T::CampGrants::ParseAppliedIds(F4T::AppliedOperations::ReadScope(a_scope));
		}

		bool WasApplied(std::uint64_t a_operationId)
		{
			std::scoped_lock lock(g_grantMutex);
			return std::find(g_appliedGrants.begin(), g_appliedGrants.end(), a_operationId) != g_appliedGrants.end();
		}

		void RememberApplied(const std::string& a_scope, std::uint64_t a_operationId)
		{
			std::scoped_lock lock(g_grantMutex);
			F4T::CampGrants::RememberAppliedId(g_appliedGrants, a_operationId);
			if (!F4T::AppliedOperations::WriteScope(a_scope, F4T::CampGrants::FormatAppliedIds(g_appliedGrants))) {
				Warn("camp_grant_remember", "Camp refund applied but could not be remembered.");
			}
		}
	}

	bool HandleCampGrant(const Json& a_packet)
	{
		auto grant = F4T::CampGrants::ParseGrantPacket(a_packet);
		if (!grant) {
			F4T::DebugLogging::Log("CampKit", "campGrant dropped: malformed grant");
			return false;
		}
		std::scoped_lock lock(g_grantMutex);
		const auto queued = std::any_of(g_queuedGrants.begin(), g_queuedGrants.end(), [&](const auto& a_entry) { return a_entry.operationId == grant->operationId; });
		if (queued) {
			return true;
		}
		if (g_queuedGrants.size() >= kMaximumQueuedGrants) {
			return false;
		}
		g_queuedGrants.push_back(std::move(*grant));
		return true;
	}

	bool HandleCampRefund(const Json& a_packet)
	{
		const auto removed = a_packet.value("removedPieces", 0U);
		const auto unpriced = a_packet.value("unpricedPieces", 0U);
		Hud(std::format("Camp moved: {} piece(s) were picked up.", removed));
		if (unpriced > 0) {
			Hud(std::format("{} piece(s) have no known cost and gave no refund.", unpriced));
		}
		return true;
	}

	void TickCampGrants()
	{
		std::vector<F4T::CampGrants::Grant> work;
		{
			std::scoped_lock lock(g_grantMutex);
			work.assign(g_queuedGrants.begin(), g_queuedGrants.end());
		}
		const auto scope = AppliedScope();
		if (work.empty() || scope.empty()) {
			return;
		}
		LoadAppliedGrants(scope);
		for (const auto& grant : work) {
			const auto outcome = F4T::CampGrants::ApplyOnce(
				grant.operationId,
				[](std::uint64_t a_operationId) { return WasApplied(a_operationId); },
				[&grant] { return ApplyGrant(grant); },
				[&scope](std::uint64_t a_operationId) { RememberApplied(scope, a_operationId); });
			if (outcome == F4T::CampGrants::ApplyOutcome::Retry) {
				continue;
			}
			{
				std::scoped_lock lock(g_grantMutex);
				std::erase_if(g_queuedGrants, [&](const auto& a_entry) { return a_entry.operationId == grant.operationId; });
			}
			QueuePacket(F4T::CampGrants::MakeGrantAck(grant.operationId));
		}
	}

	void ForgetCampGrants()
	{
		std::scoped_lock lock(g_grantMutex);
		g_queuedGrants.clear();
	}
}
