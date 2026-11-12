#include "pch.h"
// TODO-verify: mirror frame placement, piece apply and sync on an unentered frame are unverified at runtime; source:CodeNotes-GARDEN power-armor-sync.

#include "F4TPowerArmorMirror.h"

#include "F4TPowerArmorScript.h"
#include "F4TProxyActorControllerInternal.h"

#include "RE/B/BGSKeyword.h"
#include "RE/B/BGSMod.h"
#include "RE/B/BGSObjectInstanceExtra.h"
#include "RE/C/CHANGE_TYPES.h"
#include "RE/E/ExtraDataList.h"
#include "RE/P/PowerArmor.h"
#include "RE/T/TESFurniture.h"
#include "RE/T/TESObjectARMO.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESWorldSpace.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <vector>

namespace
{
	using F4T::ProxyActorController::detail::LogThrottledWarning;

	constexpr std::uint32_t kFrameWaitTickLimit = 600;
	constexpr std::uint32_t kFurnitureTypePowerArmorKeywordFormId = 0x0003430B;

	struct FrameCreationData : RE::NEW_REFR_DATA
	{
		RE::NiPointer<RE::TESObjectREFR> created;

		void HandlePre3D(RE::TESObjectREFR* a_ref) override
		{
			created.reset(a_ref);
		}
	};

	// TODO-verify: BGSObjectInstanceExtra construction and AddMod on a frame container add; source:CodeNotes-GARDEN power-armor-sync.
	RE::BSTSmartPointer<RE::ExtraDataList> BuildPieceExtra(const F4T::PowerArmorSync::Piece& a_piece, RE::TESBoundObject& a_object)
	{
		if (!a_piece.mods || a_piece.mods->empty()) {
			return {};
		}
		std::vector<RE::BGSMod::Attachment::Mod*> forms;
		forms.reserve(a_piece.mods->size());
		for (const auto& mod : *a_piece.mods) {
			auto* form = RE::TESForm::GetFormByID<RE::BGSMod::Attachment::Mod>(mod.formId);
			if (!form) {
				LogThrottledWarning(
					std::format("mirror_piece_mod_missing_{:08X}", a_object.GetFormID()),
					std::format("Mirror power armor piece {:08X} has an unresolved mod; adding the base form.", a_object.GetFormID()));
				return {};
			}
			forms.push_back(form);
		}
		auto* instanceExtra = new RE::BGSObjectInstanceExtra(nullptr, std::addressof(a_object), nullptr);
		RE::BSTSmartPointer<RE::ExtraDataList> extraList{ new RE::ExtraDataList() };
		extraList->AddExtra(instanceExtra);
		for (std::size_t i = 0; i < forms.size(); ++i) {
			instanceExtra->AddMod(*forms[i], a_piece.mods->at(i).index, a_piece.mods->at(i).rank, false);
		}
		return extraList;
	}
}

namespace F4T::PowerArmorSync
{
	// TODO-verify: EnsureMirrorFrame placement, AddChange(kFormCreated) and Get3D on a mirror frame; source:CodeNotes-GARDEN power-armor-sync.
	MirrorStatus EnsureMirrorFrame(
		MirrorFrame& a_mirror,
		std::uint32_t a_baseFormId,
		std::uint32_t a_cellId,
		std::uint32_t a_worldspaceId,
		float a_x,
		float a_y,
		float a_z,
		float a_angleZ)
	{
		if (!a_mirror.reference) {
			auto* base = RE::TESForm::GetFormByID(a_baseFormId);
			auto* furniture = base && base->GetFormType() == RE::ENUM_FORM_ID::kFURN ? base->As<RE::TESFurniture>() : nullptr;
			if (!furniture) {
				LogThrottledWarning(
					std::format("mirror_frame_not_furniture_{:08X}", a_baseFormId),
					std::format("Mirror power armor frame {:08X} is not a FURN.", a_baseFormId));
				return MirrorStatus::kFailed;
			}
			auto* keyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kFurnitureTypePowerArmorKeywordFormId);
			if (!keyword || !furniture->HasKeyword(keyword)) {
				LogThrottledWarning(
					std::format("mirror_frame_not_power_armor_{:08X}", a_baseFormId),
					std::format("Mirror power armor frame {:08X} is not a FURN carrying FurnitureTypePowerArmor.", a_baseFormId));
				return MirrorStatus::kFailed;
			}
			auto* interior = a_worldspaceId == 0 ? RE::TESForm::GetFormByID<RE::TESObjectCELL>(a_cellId) : nullptr;
			auto* world = a_worldspaceId != 0 ? RE::TESForm::GetFormByID<RE::TESWorldSpace>(a_worldspaceId) : nullptr;
			auto* handler = RE::TESDataHandler::GetSingleton();
			if (!handler || (!interior && !world)) {
				LogThrottledWarning(
					std::format("mirror_frame_place_missing_{:08X}", a_baseFormId),
					std::format("Mirror power armor frame {:08X} has no resolvable cell or worldspace to place into.", a_baseFormId));
				return MirrorStatus::kFailed;
			}

			FrameCreationData data;
			data.object = furniture;
			data.location = RE::NiPoint3{ a_x, a_y, a_z };
			data.direction = RE::NiPoint3{ 0.0F, 0.0F, a_angleZ };
			data.interior = interior;
			data.world = world;
			data.clearStillLoadingFlag = true;
			a_mirror.handle = handler->CreateReferenceAtLocation(data);
			a_mirror.reference = std::move(data.created);
			if (!a_mirror.reference) {
				a_mirror.reference = a_mirror.handle.get();
			}
			if (!a_mirror.reference) {
				a_mirror = {};
				LogThrottledWarning(
					std::format("mirror_frame_create_failed_{:08X}", a_baseFormId),
					std::format("Could not create mirror power armor frame {:08X}.", a_baseFormId));
				return MirrorStatus::kFailed;
			}
			a_mirror.baseFormId = a_baseFormId;
			a_mirror.refFormId = a_mirror.reference->GetFormID();
			a_mirror.x = a_x;
			a_mirror.y = a_y;
			a_mirror.z = a_z;
			a_mirror.cellId = a_cellId;
			a_mirror.worldspaceId = a_worldspaceId;
			a_mirror.angleZ = a_angleZ;
		}

		auto* frame = a_mirror.reference.get();
		if (!frame->AddChange(RE::CHANGE_TYPES::kFormCreated)) {
			if (++a_mirror.waitTicks < kFrameWaitTickLimit) {
				return MirrorStatus::kPending;
			}
			LogThrottledWarning(
				std::format("mirror_frame_registration_{:08X}", a_baseFormId),
				std::format("Mirror power armor frame {:08X} never registered; deleting the frame.", a_baseFormId));
			DestroyMirrorFrame(a_mirror);
			return MirrorStatus::kFailed;
		}
		if (frame->Get3D() == nullptr) {
			if (++a_mirror.waitTicks < kFrameWaitTickLimit) {
				return MirrorStatus::kPending;
			}
			LogThrottledWarning(
				std::format("mirror_frame_not_loaded_{:08X}", a_baseFormId),
				std::format("Mirror power armor frame {:08X} never loaded 3D; deleting the frame.", a_baseFormId));
			DestroyMirrorFrame(a_mirror);
			return MirrorStatus::kFailed;
		}
		return MirrorStatus::kReady;
	}

	// TODO-verify: DestroyPlacedFrame retire path applied to an unowned mirror frame; source:CodeNotes-GARDEN power-armor-sync.
	void DestroyMirrorFrame(MirrorFrame& a_mirror)
	{
		// TODO-verify: ObjectRefHandle get() returns null once the frame is deleted; source:CodeNotes-GARDEN power-armor-sync.
		if (!a_mirror.handle || a_mirror.handle.get()) {
			PlacedPowerArmorFrame placed;
			placed.baseFormId = a_mirror.baseFormId;
			placed.refFormId = a_mirror.refFormId;
			placed.handle = a_mirror.handle;
			placed.reference = a_mirror.reference;
			DestroyPlacedFrame(placed, nullptr);
		}
		F4T::PowerArmorScript::ForgetFrameLock(a_mirror.refFormId);
		a_mirror = {};
	}

	// Constraint: a mirror re-applies its unresolved pieces on at most kMaximumPieceApplyPasses later ticks; a piece already added is never added twice.
	constexpr std::uint32_t kMaximumPieceApplyPasses = 30;

	// TODO-verify: TESObjectREFR AddObjectToContainer on an unentered frame and PowerArmor SyncFurnitureVisualsToInventory; source:CodeNotes-GARDEN power-armor-sync.
	bool ApplyMirrorPieces(MirrorFrame& a_mirror, const PowerArmorState& a_state)
	{
		auto* frame = a_mirror.reference.get();
		if (!frame) {
			return false;
		}
		if (a_mirror.piecesApplied) {
			return true;
		}
		bool added{};
		bool unresolved{};
		for (const auto& piece : a_state.pieces) {
			if (std::find(a_mirror.addedPieceFormIds.begin(), a_mirror.addedPieceFormIds.end(), piece.formId) != a_mirror.addedPieceFormIds.end()) {
				continue;
			}
			if (!IsPowerArmorPieceForm(piece.formId)) {
				LogThrottledWarning(
					"mirror_piece_not_power_armor",
					"Skipped a mirror piece that is not a power armor piece.");
				continue;
			}
			auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(piece.formId);
			if (!object) {
				unresolved = true;
				LogThrottledWarning(
					"mirror_piece_missing",
					"Skipped a mirror power armor piece that does not resolve to an object.");
				continue;
			}
			frame->AddObjectToContainer(object, BuildPieceExtra(piece, *object), 1, nullptr, RE::ITEM_REMOVE_REASON::kNone);
			a_mirror.addedPieceFormIds.push_back(piece.formId);
			added = true;
		}
		if (added) {
			RE::PowerArmor::SyncFurnitureVisualsToInventory(frame, true, nullptr, false);
		}
		a_mirror.appliedPieces = a_state;
		if (!unresolved || a_mirror.pieceApplyPasses >= kMaximumPieceApplyPasses) {
			a_mirror.piecesApplied = true;
		} else {
			++a_mirror.pieceApplyPasses;
		}
		return true;
	}
}
