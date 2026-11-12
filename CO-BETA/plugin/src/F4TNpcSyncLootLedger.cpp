#include "pch.h"

#include "F4TNpcSyncLootLedger.h"

#include "F4TDebugLogging.h"
#include "F4TInventoryGrant.h"
#include "F4TNpcSyncLootInternal.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	namespace Loot = F4T::NpcSync::Loot::Detail;

	constexpr std::uint32_t kMaximumRecordBytes = 8192;
	constexpr const char* kDeferredRollbackKind = "deferredRollback";
	constexpr const char* kTimedOutTakeKind = "timedOutTake";
	constexpr const char* kHeldGrantUndoKind = "heldGrantUndo";

	struct StagedRecords
	{
		std::vector<Loot::DeferredRollback> deferred;
		std::vector<Loot::TimedOutTake> timedOut;
		std::vector<Loot::GrantAwaitingAck> undos;
	};

	StagedRecords g_staged;

	bool SessionIsSaveable(const F4T::NpcLootSync::TakeContext& a_context, std::uint64_t a_loadEpoch)
	{
		return a_context.loadEpoch == a_loadEpoch && !a_context.serverKey.empty() && !a_context.characterId.empty();
	}

	nlohmann::json SessionToJson(const F4T::NpcLootSync::TakeContext& a_context)
	{
		return nlohmann::json{ { "serverKey", a_context.serverKey }, { "worldId", a_context.worldId }, { "characterId", a_context.characterId } };
	}

	F4T::NpcLootSync::TakeContext SessionFromJson(const nlohmann::json& a_json)
	{
		return F4T::NpcLootSync::TakeContext{ a_json.at("serverKey").get<std::string>(), a_json.at("worldId").get<std::string>(), a_json.at("characterId").get<std::string>(), 0 };
	}

	nlohmann::json IdentityToJson(const F4T::NpcLootSync::LootIdentity& a_identity)
	{
		return nlohmann::json{ { "npcId", a_identity.npcId }, { "sourceFormId", a_identity.sourceFormId }, { "cellId", a_identity.cellId }, { "worldspaceId", a_identity.worldspaceId }, { "authorityEpoch", a_identity.authorityEpoch } };
	}

	std::uint32_t U32(const nlohmann::json& a_json)
	{
		const auto value = a_json.is_number_integer() ? a_json.get<std::int64_t>() : -1;
		if (value < 0 || value > (std::numeric_limits<std::uint32_t>::max)()) {
			throw std::out_of_range("loot record number is not a 32-bit count");
		}
		return static_cast<std::uint32_t>(value);
	}

	std::optional<std::uint32_t> ResolveSaved(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_savedFormId)
	{
		if (a_savedFormId == 0) {
			return 0U;
		}
		const auto resolved = a_intfc->ResolveFormID(a_savedFormId);
		if (!resolved) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot record dropped, saved form id {:08X} does not resolve", a_savedFormId);
		}
		return resolved;
	}

	std::optional<F4T::NpcLootSync::LootIdentity> IdentityFromJson(const F4SE::SerializationInterface* a_intfc, const nlohmann::json& a_json)
	{
		const auto sourceFormId = ResolveSaved(a_intfc, U32(a_json.at("sourceFormId")));
		const auto cellId = ResolveSaved(a_intfc, U32(a_json.at("cellId")));
		const auto worldspaceId = ResolveSaved(a_intfc, U32(a_json.at("worldspaceId")));
		if (!sourceFormId || !cellId || !worldspaceId) {
			return std::nullopt;
		}
		return F4T::NpcLootSync::LootIdentity{ U32(a_json.at("npcId")), *sourceFormId, *cellId, *worldspaceId, U32(a_json.at("authorityEpoch")) };
	}

	void WriteRecord(const F4SE::SerializationInterface* a_intfc, const nlohmann::json& a_record)
	{
		const auto bytes = a_record.dump();
		if (bytes.size() > kMaximumRecordBytes) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot record not saved, {} bytes is over the {} byte bound", bytes.size(), kMaximumRecordBytes);
			return;
		}
		if (!a_intfc->WriteRecord(F4T::NpcLootLedger::RecordType, F4T::NpcLootLedger::RecordVersion, bytes.data(), static_cast<std::uint32_t>(bytes.size()))) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot record not written, {} bytes", bytes.size());
		}
	}
}

namespace F4T::NpcLootLedger
{
	void SaveRecords(const F4SE::SerializationInterface* a_intfc)
	{
		if (!a_intfc) {
			return;
		}
		const auto loadEpoch = F4T::InventoryGrant::LoadEpoch();
		for (const auto& entry : Loot::DeferredRollbacks()) {
			if (SessionIsSaveable(entry.context, loadEpoch)) {
				WriteRecord(a_intfc, nlohmann::json{ { "kind", kDeferredRollbackKind }, { "session", SessionToJson(entry.context) }, { "containerFormId", entry.containerFormId }, { "itemId", entry.itemId }, { "count", entry.count }, { "restoreToContainer", entry.restoreToContainer } });
			}
		}
		for (const auto& entry : Loot::TimedOutTakes()) {
			if (SessionIsSaveable(entry.context, loadEpoch)) {
				WriteRecord(a_intfc, nlohmann::json{ { "kind", kTimedOutTakeKind }, { "session", SessionToJson(entry.context) }, { "operationId", entry.operationId }, { "containerFormId", entry.containerFormId }, { "itemId", entry.itemId }, { "removed", entry.removed }, { "lateResult", entry.lateResult ? F4T::NpcLootSync::TakeResultToJson(*entry.lateResult) : nlohmann::json(nullptr) } });
			}
		}
		for (const auto& entry : Loot::HeldGrantUndos()) {
			if (SessionIsSaveable(entry.context, loadEpoch)) {
				WriteRecord(a_intfc, nlohmann::json{ { "kind", kHeldGrantUndoKind }, { "session", SessionToJson(entry.context) }, { "identity", IdentityToJson(entry.identity) }, { "itemId", entry.itemId }, { "count", entry.count }, { "authorityPlayerId", entry.authorityPlayerId } });
			}
		}
	}

	void LoadRecord(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length)
	{
		if (!a_intfc || a_version != RecordVersion || a_length > kMaximumRecordBytes) {
			return;
		}
		std::string bytes(a_length, '\0');
		if (a_intfc->ReadRecordData(bytes.data(), a_length) != a_length) {
			return;
		}
		try {
			const auto record = nlohmann::json::parse(bytes);
			const auto kind = record.at("kind").get<std::string>();
			const auto session = SessionFromJson(record.at("session"));
			if (kind == kDeferredRollbackKind) {
				const auto containerFormId = ResolveSaved(a_intfc, U32(record.at("containerFormId")));
				const auto itemId = ResolveSaved(a_intfc, U32(record.at("itemId")));
				if (containerFormId && itemId) {
					g_staged.deferred.push_back(Loot::DeferredRollback{ *containerFormId, *itemId, U32(record.at("count")), record.at("restoreToContainer").get<bool>(), session });
				}
			} else if (kind == kTimedOutTakeKind) {
				std::optional<F4T::NpcLootSync::TakeResult> lateResult;
				if (!record.at("lateResult").is_null()) {
					lateResult = F4T::NpcLootSync::ParseTakeResult(record.at("lateResult"));
					if (!lateResult) {
						F4T::DebugLogging::Log("NpcCoop", "npc loot timed-out take record skipped, its late result is unreadable item={}", record.at("itemId").get<std::uint32_t>());
						return;
					}
					auto& [lateOperationId, lateAccepted, lateReason, lateItemId, lateCount, lateRevision, lateTakerPlayerId] = *lateResult;
					const auto resolvedLateItemId = ResolveSaved(a_intfc, lateItemId);
					if (!resolvedLateItemId) {
						return;
					}
					lateItemId = *resolvedLateItemId;
				}
				const auto containerFormId = ResolveSaved(a_intfc, U32(record.at("containerFormId")));
				const auto itemId = ResolveSaved(a_intfc, U32(record.at("itemId")));
				if (containerFormId && itemId) {
					g_staged.timedOut.push_back(Loot::TimedOutTake{ record.at("operationId").get<std::string>(), *containerFormId, *itemId, U32(record.at("removed")), Loot::NowMilliseconds(), session, lateResult });
				}
			} else if (kind == kHeldGrantUndoKind) {
				const auto identity = IdentityFromJson(a_intfc, record.at("identity"));
				const auto itemId = ResolveSaved(a_intfc, U32(record.at("itemId")));
				if (identity && itemId) {
					g_staged.undos.push_back(Loot::GrantAwaitingAck{ *identity, *itemId, U32(record.at("count")), U32(record.at("authorityPlayerId")), Loot::NowMilliseconds(), session });
				}
			}
		} catch (...) {
			F4T::DebugLogging::Log("NpcCoop", "npc loot record skipped, it could not be read");
		}
	}

	void PublishLoadedRecords()
	{
		auto staged = std::exchange(g_staged, StagedRecords{});
		if (staged.deferred.empty() && staged.timedOut.empty() && staged.undos.empty()) {
			return;
		}
		const auto loadEpoch = F4T::InventoryGrant::LoadEpoch();
		F4T::DebugLogging::Log("NpcCoop", "npc loot records published deferred={} timedOut={} undos={}", staged.deferred.size(), staged.timedOut.size(), staged.undos.size());
		for (auto& entry : staged.deferred) {
			entry.context.loadEpoch = loadEpoch;
			Loot::DeferRollback(entry.context, entry.containerFormId, entry.itemId, entry.count, entry.restoreToContainer);
		}
		for (auto& entry : staged.timedOut) {
			entry.context.loadEpoch = loadEpoch;
			Loot::AddTimedOutTake(std::move(entry));
		}
		for (auto& entry : staged.undos) {
			entry.context.loadEpoch = loadEpoch;
			Loot::HoldGrantUndo(entry);
		}
	}
}
