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
#include <optional>
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
		constexpr std::uint32_t kCampReceiptVersion = 2;
		constexpr std::uint32_t kLegacyCampReceiptVersion = 1;
		constexpr std::size_t kMaximumCampReceiptBytes = 256 * 1024;

		struct TradeApplyScope
		{
			TradeApplyScope() { F4T::InventorySync::g_tradeApplying = true; }
			~TradeApplyScope() { F4T::InventorySync::g_tradeApplying = false; }
		};

		std::mutex g_grantMutex;
		std::deque<F4T::CampGrants::Grant> g_queuedGrants;
		std::string g_receiptWorld;
		F4T::CampGrants::Worlds g_receipts;
		std::string g_landingWorld;
		std::vector<std::uint64_t> g_landingIds;

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

		std::optional<F4T::CampGrants::Counts> CurrentCounts(const F4T::CampGrants::Grant& a_grant)
		{
			auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				return std::nullopt;
			}
			F4T::CampGrants::Counts counts;
			for (const auto& chunk : a_grant.chunks) {
				for (const auto& line : chunk.lines) {
					const auto resolved = F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey);
					const auto* item = resolved ? RE::TESForm::GetFormByID<RE::TESBoundObject>(*resolved) : nullptr;
					if (!item) {
						return std::nullopt;
					}
					counts[line.itemKey] = static_cast<std::int64_t>(player->GetInventoryObjectCount(item));
				}
			}
			return counts;
		}

		bool ApplyGrant(const F4T::CampGrants::Grant& a_grant, const std::vector<F4T::Trade::Line>& a_missing)
		{
			auto* player = F4T::LocalPlayerState::TryGetLocalPlayer();
			if (!player) {
				return false;
			}
			for (const auto& line : a_missing) {
				if (!F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey)) {
					return false;
				}
			}
			bool complete = true;
			for (const auto& chunk : a_grant.chunks) {
				if (!F4T::InventorySync::NoteTradeCommitted(chunk.revision, {}, chunk.lines)) {
					complete = false;
				}
			}
			{
				const TradeApplyScope scope;
				for (const auto& line : a_missing) {
					const auto resolved = F4T::DropMirrorRefs::ResolveItemFormId(line.itemKey);
					// Constraint: same container add the trade apply uses; source:CO-BETA/plugin/src/F4TTradeClient.cpp:244
					F4T::NpcSync::Loot::Detail::AddLootToContainer(*player, *resolved, line.count);
				}
			}
			if (!complete) {
				F4T::InventorySync::NoteResyncNeeded();
			}
			if (!a_missing.empty()) {
				Hud("Camp refund received: " + DescribeLines(a_missing) + ".");
			}
			return true;
		}

		std::vector<std::uint64_t> LegacyAckedIds(const std::string& a_scope)
		{
			return F4T::CampGrants::ParseAckedIds(F4T::AppliedOperations::ReadScope(a_scope)).value_or(std::vector<std::uint64_t>{});
		}

		void AdoptReceipts(const std::string& a_worldId)
		{
			if (g_receiptWorld == a_worldId) {
				return;
			}
			g_receiptWorld = a_worldId;
			g_landingWorld.clear();
			g_landingIds.clear();
			F4T::CampGrants::AddAcked(g_receipts[a_worldId], LegacyAckedIds("camp-grants-" + a_worldId));
			F4T::CampGrants::BoundAcked(g_receipts);
		}

		void PublishReceipts(const std::string& a_worldId, const std::vector<F4T::CampGrants::Receipt>& a_receipts)
		{
			std::scoped_lock lock(g_grantMutex);
			g_receipts[a_worldId] = a_receipts;
			F4T::CampGrants::BoundAcked(g_receipts);
		}

		std::optional<F4T::CampGrants::Worlds> ParseCampReceipts(const std::string& a_bytes, std::uint32_t a_version)
		{
			if (a_version == kCampReceiptVersion) {
				return F4T::CampGrants::ParseWorlds(a_bytes);
			}
			const auto snapshot = F4T::CampGrants::ParseSnapshot(a_bytes);
			if (!snapshot) {
				return std::nullopt;
			}
			return F4T::CampGrants::Worlds{ { snapshot->worldId, snapshot->receipts } };
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

	void SaveCampReceipts(const F4SE::SerializationInterface* a_intfc)
	{
		std::string world;
		std::string text;
		std::vector<std::uint64_t> applied;
		bool truncated = false;
		{
			std::scoped_lock lock(g_grantMutex);
			world = g_receiptWorld;
			auto fitted = g_receipts;
			truncated = F4T::CampGrants::FitWorlds(fitted, world, kMaximumCampReceiptBytes);
			text = F4T::CampGrants::FormatWorlds(fitted);
			const auto current = fitted.find(world);
			if (current != fitted.end()) {
				applied = F4T::CampGrants::AppliedIds(current->second);
			}
		}
		if (truncated) {
			F4T::DebugLogging::Log("CampKit", "camp refund records exceed {} bytes; unacked refunds that do not fit are left out of this save", kMaximumCampReceiptBytes);
		}
		const auto written = text.size() <= kMaximumCampReceiptBytes &&
			a_intfc->WriteRecord(CampReceiptRecordType, kCampReceiptVersion, text.data(), static_cast<std::uint32_t>(text.size()));
		{
			std::scoped_lock lock(g_grantMutex);
			g_landingWorld = world;
			g_landingIds = written && !world.empty() ? applied : std::vector<std::uint64_t>{};
		}
		if (!written) {
			Warn("camp_grant_remember", "Camp refund could not be recorded; it will be retried.");
		}
	}

	void LoadCampReceipts(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (!a_intfc || (a_version != kCampReceiptVersion && a_version != kLegacyCampReceiptVersion)) {
			return;
		}
		if (a_length > kMaximumCampReceiptBytes) {
			F4T::DebugLogging::Log("CampKit", "camp refund records dropped: {} bytes is over the {} byte limit", a_length, kMaximumCampReceiptBytes);
			return;
		}
		std::string bytes(a_length, '\0');
		if (a_intfc->ReadRecordData(bytes.data(), a_length) != a_length) {
			return;
		}
		auto worlds = ParseCampReceipts(bytes, a_version);
		if (!worlds) {
			F4T::DebugLogging::Log("CampKit", "camp refund records dropped: malformed record");
			return;
		}
		std::scoped_lock lock(g_grantMutex);
		g_receipts = std::move(*worlds);
	}

	void ResetCampReceipts()
	{
		std::scoped_lock lock(g_grantMutex);
		g_receiptWorld.clear();
		g_receipts.clear();
		g_landingWorld.clear();
		g_landingIds.clear();
	}

	void FinishCampGrantSave()
	{
		const auto world = F4T::ServerProfileStore::ConnectedWorldId();
		std::vector<std::uint64_t> landed;
		{
			std::scoped_lock lock(g_grantMutex);
			if (world == g_landingWorld && g_receiptWorld == g_landingWorld) {
				landed = F4T::CampGrants::MarkLanded(g_receipts[world], g_landingIds);
				F4T::CampGrants::BoundAcked(g_receipts);
				std::erase_if(g_queuedGrants, [&landed](const auto& a_entry) {
					return std::find(landed.begin(), landed.end(), a_entry.operationId) != landed.end();
				});
			}
			g_landingWorld.clear();
			g_landingIds.clear();
		}
		for (const auto operationId : landed) {
			QueuePacket(F4T::CampGrants::MakeGrantAck(operationId));
		}
	}

	void TickCampGrants()
	{
		std::vector<F4T::CampGrants::Grant> work;
		{
			std::scoped_lock lock(g_grantMutex);
			work.assign(g_queuedGrants.begin(), g_queuedGrants.end());
		}
		const auto world = F4T::ServerProfileStore::ConnectedWorldId();
		if (work.empty() || world.empty()) {
			return;
		}
		std::vector<F4T::CampGrants::Receipt> receipts;
		{
			std::scoped_lock lock(g_grantMutex);
			AdoptReceipts(world);
			receipts = g_receipts[world];
		}
		for (const auto& grant : work) {
			const auto counts = CurrentCounts(grant);
			if (!counts) {
				continue;
			}
			const auto outcome = F4T::CampGrants::ApplyOnce(
				grant,
				*counts,
				receipts,
				[&](const std::vector<F4T::Trade::Line>& a_missing) {
					PublishReceipts(world, receipts);
					return ApplyGrant(grant, a_missing);
				},
				[&grant]() { return CurrentCounts(grant); });
			PublishReceipts(world, receipts);
			if (outcome != F4T::CampGrants::ApplyOutcome::Acknowledge) {
				continue;
			}
			{
				std::scoped_lock lock(g_grantMutex);
				std::erase_if(g_queuedGrants, [&grant](const auto& a_entry) { return a_entry.operationId == grant.operationId; });
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
