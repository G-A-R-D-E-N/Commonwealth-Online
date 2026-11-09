#include "pch.h"
#include "F4TInventoryDropGate.h"
#include "F4TDropMirrorRefs.h"
#include "F4TInventoryPickupGate.h"
#include "F4TInventorySync.h"

#include "F4TFormIdentity.h"
#include "F4TInventoryContract.h"
#include "F4TInventoryOperations.h"
#include "F4TWorkshopCamps.h"
#include "RE/S/SendHUDMessage.h"

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace F4T::InventoryDropGate
{
	namespace
	{
		thread_local bool g_replayingDrop = false;
		std::mutex g_mutex;

		struct Request
		{
			RE::BGSObjectInstance object;
			std::vector<std::uint32_t> stackData;
			bool hasStackData{};
			std::uint32_t count{};
			std::optional<RE::NiPoint3> point;
			std::optional<RE::NiPoint3> rotate;
			Original original{};
		};
		std::map<std::string, Request> g_requests;
	}

	std::optional<std::string> TranslateItemKey(const RE::TESForm* a_form)
	{
		const RE::TESForm* form = a_form;
		if (!form) {
			return std::nullopt;
		}
		const auto split = F4T::FormIdentity::SplitFormId(form->GetFormID());
		if (split.kind == F4T::FormIdentity::Kind::Runtime) {
			return std::nullopt;
		}
		auto* data = RE::TESDataHandler::GetSingleton();
		if (!data) {
			return std::nullopt;
		}
		const auto* file = split.kind == F4T::FormIdentity::Kind::Light ?
			data->LookupLoadedLightModByIndex(static_cast<std::uint16_t>(split.fileIndex)) :
			data->LookupLoadedModByIndex(static_cast<std::uint8_t>(split.fileIndex));
		if (!file || !F4T::FormIdentity::IsValidRecordIdentity({ std::string(file->GetFilename()), split.localId }, split.kind)) {
			return std::nullopt;
		}
		return F4T::InventoryContract::MakeItemKey(std::string(file->GetFilename()), split.localId, split.kind);
	}

	bool Hold(
		const RE::BGSObjectInstance& a_object,
		RE::BSTSmallArray<std::uint32_t, 4>* a_stackData,
		std::int32_t a_count,
		const RE::NiPoint3* a_point,
		const RE::NiPoint3* a_rotate,
		Original a_original)
	{
		if (F4T::InventorySync::g_tradeApplying || g_replayingDrop || !a_original || a_count <= 0) {
			return false;
		}
		if (!F4T::InventorySync::GetStatus().sessionActive) {
			return false;
		}
		const auto itemKey = TranslateItemKey(a_object.object);
		if (!itemKey) {
			return false;
		}
		if (*itemKey == F4T::WorkshopCamps::CampKitItemKey) {
			RE::SendHUDMessage::ShowHUDMessage("Press E on the camp kit to set up your camp.", nullptr, true, true);
		}
		F4T::InventoryOps::DropContext context;
		context.itemKey = itemKey;
		context.hasInstanceData = a_object.instanceData.get() != nullptr;
		if (const auto* form = a_object.object) {
			const auto type = form->GetFormType();
			if (type == RE::ENUM_FORM_ID::kBOOK) {
				context.formKind = F4T::InventoryOps::FormKind::Book;
			} else if (type == RE::ENUM_FORM_ID::kNOTE) {
				context.formKind = F4T::InventoryOps::FormKind::Note;
			}
		}
		context.origin = F4T::InventoryOps::DropOrigin::Unknown;
		context.count = static_cast<std::uint32_t>(a_count);
		if (const auto view = F4T::InventorySync::ServerInventory()) {
			if (const auto found = view->items.find(*itemKey); found != view->items.end()) {
				context.viewCount = found->second.count;
			}
		}
		const auto decision = F4T::InventoryOps::ClassifyDrop(context);
		if (decision.verdict == F4T::InventoryOps::DropVerdict::PassThrough) {
			return false;
		}
		const std::scoped_lock lock(g_mutex);
		if (decision.verdict == F4T::InventoryOps::DropVerdict::Block) {
			REX::WARN("Bound drop blocked: the server cannot represent it; the item stays in the inventory.");
			return true;
		}
		if (g_requests.size() >= F4T::InventoryOps::kMaximumPendingOperations) {
			REX::WARN("Bound drop held without request: pending request queue is full.");
			return true;
		}
		const auto operationId = F4T::InventorySync::BeginOwnedDrop(*itemKey, context.count);
		if (!operationId) {
			REX::WARN("Bound drop held without request: owned inventory unavailable.");
			return true;
		}
		Request request{ a_object, {}, a_stackData != nullptr, context.count, std::nullopt, std::nullopt, a_original };
		if (a_stackData) {
			for (const auto value : *a_stackData) {
				request.stackData.push_back(value);
			}
		}
		if (a_point) {
			request.point = *a_point;
		}
		if (a_rotate) {
			request.rotate = *a_rotate;
		}
		g_requests.emplace(*operationId, std::move(request));
		return true;
	}

	void ReplayRequest(const Request& a_request, F4T::Drops::EntityId a_entityId)
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player || !a_request.original) {
			REX::WARN("Committed bound drop discarded: the player or the saved drop call no longer exists.");
			return;
		}
		if (!F4T::InventorySync::ReserveOwnedEntity(a_entityId)) {
			REX::WARN("Committed bound drop discarded: the entity reservation failed, so replay would duplicate the mirror.");
			return;
		}
		RE::BSTSmallArray<std::uint32_t, 4> stackData;
		for (const auto value : a_request.stackData) {
			stackData.push_back(value);
		}
		auto* stackArg = a_request.hasStackData ? &stackData : nullptr;
		const RE::NiPoint3* point = a_request.point ? &*a_request.point : nullptr;
		const RE::NiPoint3* rotate = a_request.rotate ? &*a_request.rotate : nullptr;
		struct ReplayScope
		{
			ReplayScope() { g_replayingDrop = true; }
			~ReplayScope() { g_replayingDrop = false; }
		} replayScope;
		RE::ObjectRefHandle created;
		a_request.original(player, &created, a_request.object, stackArg, static_cast<std::int32_t>(a_request.count), point, rotate);
		const auto createdRef = created.get();
		if (F4T::DropMirrorRefs::IsMirrored(a_entityId)) {
			if (createdRef) {
				createdRef->Disable();
				createdRef->MarkAsDeleted();
			}
			REX::WARN("Committed bound drop already has a drop mirror ref; the replayed duplicate was removed.");
			F4T::InventorySync::UnbindOwnedEntity(a_entityId);
			return;
		}
		if (createdRef) {
			if (!F4T::InventoryPickupGate::BindReference(a_entityId, *createdRef)) {
				REX::WARN("Committed bound drop replayed without a pickup binding; its pickup stays untracked.");
			}
		} else {
			REX::WARN("Committed bound drop replayed without a reference; its pickup stays untracked.");
			F4T::InventorySync::UnbindOwnedEntity(a_entityId);
		}
	}

	void Complete(const std::string& a_operationId, bool a_committed, std::optional<F4T::Drops::EntityId> a_entityId)
	{
		std::optional<Request> request;
		{
			const std::scoped_lock lock(g_mutex);
			const auto entry = g_requests.find(a_operationId);
			if (entry == g_requests.end()) {
				return;
			}
			request = entry->second;
			g_requests.erase(entry);
		}
		if (!a_committed) {
			REX::WARN("Bound drop rejected or timed out; the item stays in the inventory.");
			return;
		}
		if (!a_entityId) {
			REX::WARN("Bound drop committed without an entity id; the item stays in the inventory.");
			return;
		}
		ReplayRequest(*request, *a_entityId);
	}
}
