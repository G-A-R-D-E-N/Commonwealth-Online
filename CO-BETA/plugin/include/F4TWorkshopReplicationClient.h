#pragma once

#include "F4TWorkshopTransactions.h"
#include "RE/B/BSPointerHandle.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace F4SE
{
	class SerializationInterface;
}

namespace F4T::WorkshopReplicationClient
{
	inline constexpr std::uint32_t CampReceiptRecordType = 'CAGR';

	struct PlacedObject
	{
		Workshop::BuildZoneId buildZoneId{};
		Workshop::ObjectId objectId{};
	};

	bool TryHandlePacket(std::string_view a_type, const nlohmann::json& a_packet);
	void PushStructureDamageHud(const nlohmann::json& a_packet);
	void PushStructureDestroyedHud(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_objectId);
	void NoteLocalStructureHit(const PlacedObject& a_object);
	void Reset();
	void Tick();
	void NoteActiveBuildZone(Workshop::BuildZoneId a_buildZoneId);
	[[nodiscard]] std::optional<Workshop::BuildZoneId> ActiveBuildZoneId();
	[[nodiscard]] std::optional<Workshop::Revision> ZoneRevision(Workshop::BuildZoneId a_buildZoneId);
	[[nodiscard]] std::vector<Workshop::ObjectId> DamagedObjectIds(Workshop::BuildZoneId a_buildZoneId);
	bool SubmitPlace(const RE::TESObjectREFR& a_ref);
	bool SubmitCampFound(const RE::TESObjectREFR& a_ref, std::uint32_t a_dropEntityId);
	bool SubmitCampStore(const RE::TESObjectREFR& a_workbench, bool a_sneaking);
	bool SubmitCampMove(Workshop::BuildZoneId a_buildZoneId);
	bool SubmitCampRemove(Workshop::BuildZoneId a_buildZoneId);
	[[nodiscard]] bool IsCampKitForm(const Workshop::FormKey& a_form) noexcept;
	[[nodiscard]] bool IsCampKitRef(const RE::TESObjectREFR& a_ref);
	inline const Workshop::FormKey kCampWorkbenchForm{ "Fallout4.esm", 0x000C1AEB }; // TODO-verify: camp anchor spawn unverified in game
	bool HandleCampPlacement(const nlohmann::json& a_packet);
	bool HandleCampRejected(const nlohmann::json& a_packet);
	bool HandleCampList(const nlohmann::json& a_packet);
	void ApplyCampWorkbench(const Workshop::ObjectState& a_workbench, bool a_own);
	bool ResolvePendingCamp(std::uint64_t a_operationId, bool a_placed, std::optional<Workshop::BuildZoneId> a_zone = std::nullopt);
	bool ResolvePendingCampStore(std::uint64_t a_operationId, std::optional<Workshop::BuildZoneId> a_zone = std::nullopt);
	void RememberPendingCampRemove(std::uint64_t a_operationId);
	bool ResolvePendingCampRemove(std::uint64_t a_operationId);
	bool HandleCampGrant(const nlohmann::json& a_packet);
	bool HandleCampRefund(const nlohmann::json& a_packet);
	void SaveCampReceipts(const F4SE::SerializationInterface* a_intfc);
	void LoadCampReceipts(const F4SE::SerializationInterface* a_intfc, std::uint32_t a_version, std::uint32_t a_length);
	void ResetCampReceipts();
	void FinishCampGrantSave();
	bool SubmitMove(const RE::TESObjectREFR& a_ref);
	bool SubmitScrap(std::uint32_t a_refHandle);
	void HandleDoorActivation(RE::TESObjectREFR& a_ref, bool a_sneaking);
	[[nodiscard]] std::optional<PlacedObject> FindPlacedObject(const RE::TESObjectREFR& a_ref);
	[[nodiscard]] RE::ObjectRefHandle FindPlacedObjectHandle(Workshop::BuildZoneId a_buildZoneId, Workshop::ObjectId a_objectId);
	[[nodiscard]] std::optional<Workshop::FormKey> SpaceKeyOf(const RE::TESObjectREFR& a_ref);
	[[nodiscard]] std::optional<std::uint32_t> FormIdOfKey(const Workshop::FormKey& a_key);
}
