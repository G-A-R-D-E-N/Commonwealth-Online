#include "pch.h"
#include "F4TInventoryPickupGate.h"
#include "F4TInventorySync.h"

#include <map>
#include <mutex>
#include <optional>

namespace F4T::InventoryPickupGate
{
	namespace
	{
		thread_local bool g_replayingPickup = false;
		std::mutex g_mutex;

		struct Bound
		{
			F4T::Drops::EntityId entityId{};
			RE::ObjectRefHandle handle;
		};
		struct Request
		{
			Bound bound;
			std::int32_t count{};
			bool sounds{};
			Original original{};
		};
		std::map<std::uint32_t, Bound> g_bound;
		std::map<std::string, Request> g_requests;

		std::optional<Bound> FindBound(std::uint32_t a_formId)
		{
			const auto found = g_bound.find(a_formId);
			if (found == g_bound.end()) {
				return std::nullopt;
			}
			const auto entity = F4T::InventorySync::FindOwnedEntity(std::to_string(a_formId));
			if (!entity || *entity != found->second.entityId) {
				g_bound.erase(found);
				return std::nullopt;
			}
			return found->second;
		}
	}

	bool BindReference(F4T::Drops::EntityId a_entityId, RE::TESObjectREFR& a_reference)
	{
		const auto* base = a_reference.GetObjectReference();
		if (!base || base->GetFormType() == RE::ENUM_FORM_ID::kBOOK || base->GetFormType() == RE::ENUM_FORM_ID::kNOTE) {
			return false;
		}
		const auto handle = a_reference.GetHandle();
		if (!handle) {
			return false;
		}
		const std::scoped_lock lock(g_mutex);
		for (auto it = g_bound.begin(); it != g_bound.end();) {
			if (it->second.entityId == a_entityId || !F4T::InventorySync::FindOwnedEntity(std::to_string(it->first))) {
				it = g_bound.erase(it);
			} else {
				++it;
			}
		}
		if (g_bound.size() >= F4T::InventoryOps::kMaximumBoundEntities ||
			!F4T::InventorySync::BindOwnedEntity(a_entityId, std::to_string(a_reference.GetFormID()))) {
			return false;
		}
		g_bound[a_reference.GetFormID()] = { a_entityId, handle };
		return true;
	}

	bool Hold(RE::TESObjectREFR* a_reference, std::int32_t a_count, bool a_sounds, Original a_original)
	{
		if (g_replayingPickup || !a_reference) {
			return false;
		}
		const std::scoped_lock lock(g_mutex);
		const auto bound = FindBound(a_reference->GetFormID());
		if (!bound) {
			return false;
		}
		if (a_count <= 0) {
			a_count = 1;
		}
		if (g_requests.size() >= F4T::InventoryOps::kMaximumPendingOperations) {
			REX::WARN("Bound pickup held without request: pending request queue is full.");
			return true;
		}
		for (const auto& [id, request] : g_requests) {
			if (request.bound.entityId == bound->entityId) {
				return true;
			}
		}
		const auto operationId = F4T::InventorySync::BeginOwnedPickup(bound->entityId);
		if (operationId) {
			g_requests.emplace(*operationId, Request{ *bound, a_count, a_sounds, a_original });
		} else {
			REX::WARN("Bound pickup held without request: owned inventory unavailable.");
		}
		return true;
	}

	void Complete(const std::string& a_operationId, bool a_committed)
	{
		Request request;
		{
			const std::scoped_lock lock(g_mutex);
			const auto found = g_requests.find(a_operationId);
			if (found == g_requests.end()) {
				return;
			}
			request = found->second;
			g_requests.erase(found);
		}
		if (!a_committed) {
			REX::WARN("Bound pickup rejected or timed out; waiting for the removal snapshot.");
			return;
		}
		auto reference = request.bound.handle.get();
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!reference || reference->IsDeleted() || reference->IsDisabled() || !player) {
			REX::WARN("Committed bound pickup cannot replay: reference or player no longer exists.");
			return;
		}
		struct ReplayScope
		{
			ReplayScope() { g_replayingPickup = true; }
			~ReplayScope() { g_replayingPickup = false; }
		} replayScope;
		request.original(player, reference.get(), request.count, request.sounds);
	}
}
