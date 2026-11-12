#include "pch.h"
#include "F4TDeathBagLocal.h"
#include "F4TDeathBagView.h"
#include "F4TDeathBagViewLogic.h"
#include "F4TDeathBagWire.h"
#include "F4TDropMirrorRefs.h"
#include "F4TInventoryDropGate.h"
#include "F4TItemHealthReadback.h"
#include "RE/A/Actor.h"
#include "RE/A/ActorEquipManager.h"
#include "RE/B/BGSInventoryItem.h"
#include "RE/B/BGSKeyword.h"
#include "RE/B/BGSKeywordForm.h"
#include "RE/B/BGSObjectInstance.h"
#include "RE/E/ExtraDataList.h"
#include "RE/E/ExtraInstanceData.h"
#include "RE/N/NEW_REFR_DATA.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESDataHandler.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectCONT.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESWorldSpace.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace F4T::DeathBagLocal
{
	namespace
	{
		using Instance = RE::BGSObjectInstance;

		struct Worn
		{
			std::uint32_t base{};
			std::uint32_t stackId{};
		};

		struct Pending
		{
			std::string deathId;
			std::vector<Stack> stacks;
			std::vector<Worn> worn;
			std::vector<Instance> instances;
			nlohmann::json manifest = nlohmann::json::array();
		};

		struct Pipboy
		{
			std::uint64_t count{};
			std::uint64_t equipped{};
			bool operator==(const Pipboy&) const = default;
		};

		constexpr std::uint32_t kDeathBagLocalFormId = 39300;
		constexpr std::size_t kDiscoveryEntryOverheadBytes = 512;
		constexpr std::uint32_t kCcPipboyKeywordFormId = 2400440;
		constexpr std::uint32_t kHasPipboyKeywordFormId = 1865578;

		std::optional<Pending> g_pending;

		bool SameStacks(const std::vector<Stack>& a_left, const std::vector<Stack>& a_right)
		{
			if (a_left.size() != a_right.size()) return false;
			for (std::size_t i = 0; i < a_left.size(); ++i) {
				if (a_left[i].base != a_right[i].base || a_left[i].index != a_right[i].index || a_left[i].count != a_right[i].count || a_left[i].unrepresentable != a_right[i].unrepresentable) return false;
			}
			return true;
		}

		bool CarriesState(const RE::BGSInventoryItem::Stack& a_stack, bool a_allowWorn)
		{
			using Flag = RE::BGSInventoryItem::Stack::Flag;
			return a_stack.flags.any(Flag::kEquipStateLocked) || a_stack.flags.any(Flag::kInvShouldEquip) || a_stack.flags.any(Flag::kTemporary) || (!a_allowWorn && a_stack.IsEquipped());
		}

		bool HasPipboyKeyword(const RE::TESForm& a_form)
		{
			const auto* keywords = a_form.As<RE::BGSKeywordForm>();
			if (!keywords) return false;
			static const RE::BGSKeyword* ccPipboy = nullptr;
			static const RE::BGSKeyword* pipboy = nullptr;
			if (!ccPipboy) ccPipboy = RE::TESForm::GetFormByID<RE::BGSKeyword>(kCcPipboyKeywordFormId);
			if (!pipboy) pipboy = RE::TESForm::GetFormByID<RE::BGSKeyword>(kHasPipboyKeywordFormId);
			return (ccPipboy && keywords->HasKeyword(ccPipboy)) || (pipboy && keywords->HasKeyword(pipboy));
		}

		bool IsProtectedPipboyForm(const RE::TESForm* a_form)
		{
			return a_form && (IsPipboyArmor(a_form->GetFormID()) || HasPipboyKeyword(*a_form));
		}

		bool IsProtectedPipboyBase(std::uint32_t a_base)
		{
			return IsPipboyArmor(a_base) || IsProtectedPipboyForm(RE::TESForm::GetFormByID(a_base));
		}

		std::optional<std::vector<Stack>> DepositPlan(const std::vector<Stack>& a_stacks)
		{
			auto plan = Plan(a_stacks);
			if (plan) std::erase_if(*plan, [](const Stack& a_stack) { return IsProtectedPipboyBase(a_stack.base); });
			return plan;
		}

		std::optional<Pipboy> ReadPipboy(RE::Actor& a_player)
		{
			auto* list = a_player.inventoryList;
			if (!list) return std::nullopt;
			Pipboy pipboy;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (!item.object) return std::nullopt;
				if (!IsProtectedPipboyForm(item.object)) continue;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					pipboy.count += stack->GetCount();
					if (stack->IsEquipped()) pipboy.equipped += stack->GetCount();
				}
			}
			return pipboy;
		}

		bool Representable(const RE::BGSInventoryItem::Stack& a_stack)
		{
			return !a_stack.extra || F4T::ItemHealthReadback::Capture(a_stack).has_value();
		}

		std::optional<std::vector<Stack>> ReadStacks(RE::TESObjectREFR& a_reference, nlohmann::json& a_manifest, nlohmann::json& a_retained, bool a_allowWorn, bool a_retainUnrepresentable = false)
		{
			auto* list = a_reference.inventoryList;
			if (!list) return std::nullopt;
			std::vector<Stack> stacks;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (auto& item : list->data) {
				if (!item.object) return std::nullopt;
				std::uint32_t index = 0;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++index) {
					if (stacks.size() >= 4096) return std::nullopt;
					const auto form = item.object->GetFormID();
					const auto quest = item.IsQuestObject(index);
					const auto unrepresentable = !IsPipboyArmor(form) && !quest && !IsProtectedPipboyForm(item.object) && !Representable(*stack);
					if (unrepresentable && !a_retainUnrepresentable) {
						static bool logged = false;
						if (!logged) {
							logged = true;
							REX::WARN("Death bag snapshot refused: a stack carries data the bag cannot represent.");
						}
						return std::nullopt;
					}
					stacks.push_back({ form, index, stack->GetCount(), quest, unrepresentable });
					if (IsPipboyArmor(form)) continue;
					const auto key = F4T::InventoryDropGate::TranslateItemKey(item.object);
					if (!key) return std::nullopt;
					if (quest || unrepresentable || IsProtectedPipboyForm(item.object)) {
						a_retained.push_back({ { "itemKey", *key }, { "count", stack->GetCount() } });
						continue;
					}
					if (CarriesState(*stack, a_allowWorn)) return std::nullopt;
					nlohmann::json metadata = nullptr;
					if (stack->extra) {
						const auto health = F4T::ItemHealthReadback::Capture(*stack);
						if (!health) return std::nullopt;
						metadata = *health;
					}
					a_manifest.push_back({ { "itemKey", *key }, { "count", stack->GetCount() }, { "metadata", std::move(metadata) } });
				}
			}
			return stacks;
		}

		bool RecordWorn(RE::Actor& a_player, std::vector<Worn>& a_worn, std::vector<Instance>& a_instances)
		{
			auto* list = a_player.inventoryList;
			if (!list) return false;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (auto& item : list->data) {
				if (!item.object || IsProtectedPipboyForm(item.object)) continue;
				std::uint32_t index = 0;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++index) {
					if (!stack->IsEquipped() || item.IsQuestObject(index) || !Representable(*stack)) continue;
					RE::TBO_InstanceData* instance{ nullptr };
					if (stack->extra) {
						if (auto* extra = stack->extra->GetByType<RE::ExtraInstanceData>()) instance = extra->data.get();
					}
					a_worn.push_back({ item.object->GetFormID(), index });
					a_instances.push_back(Instance{ item.object, instance });
				}
			}
			return true;
		}

		bool UnequipCarried(RE::Actor& a_player, std::vector<Worn>& a_worn, std::vector<Instance>& a_instances)
		{
			if (!RecordWorn(a_player, a_worn, a_instances)) return false;
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!equipManager) return a_worn.empty();
			bool unequipped = true;
			for (std::size_t i = 0; i < a_worn.size() && i < a_instances.size(); ++i) {
				if (!equipManager->UnequipObject(&a_player, &a_instances[i], 1, nullptr, a_worn[i].stackId, false, false, true, true, nullptr)) {
					unequipped = false;
				}
			}
			return unequipped;
		}

		bool Reequip(RE::Actor& a_player, const std::vector<Worn>& a_worn, const std::vector<Instance>& a_instances)
		{
			auto* equipManager = RE::ActorEquipManager::GetSingleton();
			if (!equipManager) return false;
			bool restored = true;
			for (std::size_t i = 0; i < a_worn.size() && i < a_instances.size(); ++i) {
				if (IsProtectedPipboyBase(a_worn[i].base)) continue;
				if (!equipManager->EquipObject(&a_player, a_instances[i], a_worn[i].stackId, 1, nullptr, false, false, true, true, false)) {
					restored = false;
				}
			}
			if (!restored) REX::WARN("Death bag re-equip failed for at least one carried instance; it stays carried but worn state was lost.");
			return restored;
		}

		void ReequipPending(Pending& a_pending)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				REX::WARN("Death bag abandonment could not run: the local player is unavailable.");
				return;
			}
			(void)Reequip(*player, a_pending.worn, a_pending.instances);
		}

		RE::TESObjectCONT* DeathBagBase()
		{
			auto* data = RE::TESDataHandler::GetSingleton();
			return data ? data->LookupForm<RE::TESObjectCONT>(kDeathBagLocalFormId, "CommonwealthOnline.esp") : nullptr; // TODO-verify: runtime local-id lookup unverified in game
		}

		std::optional<Counts> ReadCounts(RE::TESObjectREFR& a_reference)
		{
			auto* list = a_reference.inventoryList;
			if (!list) return std::nullopt;
			Counts counts;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (!item.object) return std::nullopt;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					auto& count = counts[item.object->GetFormID()];
					if (count > (std::numeric_limits<std::uint64_t>::max)() - stack->GetCount()) return std::nullopt;
					count += stack->GetCount();
				}
			}
			return counts;
		}

		std::optional<Counts> ReadBagCounts(RE::TESObjectREFR& a_bag)
		{
			if (!a_bag.inventoryList) return Counts{};
			return ReadCounts(a_bag);
		}

		bool MatchesMetadata(const RE::BGSInventoryItem::Stack& a_stack, const nlohmann::json& a_metadata)
		{
			if (!a_stack.extra) return a_metadata.is_null();
			const auto health = F4T::ItemHealthReadback::Capture(a_stack);
			return health && *health == a_metadata;
		}

		std::uint64_t CountMatches(RE::TESObjectREFR& a_reference, RE::TESBoundObject& a_object, const nlohmann::json& a_metadata)
		{
			auto* list = a_reference.inventoryList;
			if (!list) return 0;
			std::uint64_t total = 0;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (item.object != &a_object) continue;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					if (MatchesMetadata(*stack, a_metadata)) total += stack->GetCount();
				}
			}
			return total;
		}

		std::optional<std::pair<std::uint32_t, std::uint32_t>> FindMatch(RE::TESObjectREFR& a_reference, RE::TESBoundObject& a_object, const nlohmann::json& a_metadata)
		{
			auto* list = a_reference.inventoryList;
			if (!list) return std::nullopt;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (item.object != &a_object) continue;
				std::uint32_t index = 0;
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++index) {
					if (stack->GetCount() > 0 && MatchesMetadata(*stack, a_metadata)) return std::pair<std::uint32_t, std::uint32_t>{ index, stack->GetCount() };
				}
			}
			return std::nullopt;
		}

		std::uint64_t RevertTake(RE::Actor& a_player, RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object, std::uint32_t a_count, const nlohmann::json& a_metadata)
		{
			std::uint64_t moved = 0;
			for (std::size_t guard = 0; moved < a_count && guard < 4096; ++guard) {
				const auto match = FindMatch(a_player, a_object, a_metadata);
				if (!match) break;
				const auto before = CountMatches(a_player, a_object, a_metadata);
				const auto take = (std::min)(static_cast<std::uint64_t>(match->second), static_cast<std::uint64_t>(a_count) - moved);
				RE::TESObjectREFR::RemoveItemData remove{ &a_object, static_cast<std::int32_t>(take) };
				remove.stackData.push_back(match->first);
				remove.reason = RE::ITEM_REMOVE_REASON::kStoreContainer;
				remove.otherContainer = &a_bag;
				a_player.RemoveItem(remove);
				const auto after = CountMatches(a_player, a_object, a_metadata);
				const auto taken = before > after ? before - after : 0;
				if (taken == 0) break;
				moved += (std::min)(taken, take);
			}
			return moved;
		}
	}

	std::optional<Manifest> Snapshot(RE::Actor& a_player, const std::string& a_deathId)
	{
		AbandonPending();
		if (!F4T::DeathBagWire::ValidDeathId(a_deathId)) return std::nullopt;
		auto* cell = a_player.GetParentCell();
		if (!cell || !cell->IsAttached() || (cell->IsExterior() && !cell->worldSpace)) return std::nullopt;
		const auto pipboy = ReadPipboy(a_player);
		if (!pipboy || !pipboy->equipped) {
			REX::WARN("Death bag snapshot refused: the Pip-Boy armor is not equipped.");
			return std::nullopt;
		}
		Manifest ignored;
		const auto worn = ReadStacks(a_player, ignored.stacks, ignored.retained, true, true);
		if (!worn || ignored.stacks.empty()) return std::nullopt;
		if (ignored.stacks.size() > F4T::DeathBagWire::MaximumOwnerDiscoveryStacks || ignored.stacks.dump().size() + kDiscoveryEntryOverheadBytes > F4T::DeathBagWire::MaximumDiscoveryBytes) {
			static bool logged = false;
			if (!logged) {
				logged = true;
				REX::WARN("Death bag not taken: the carried inventory exceeds the server discovery limits; all items stay with the player.");
			}
			return std::nullopt;
		}
		const auto plan = DepositPlan(*worn);
		const auto before = ReadCounts(a_player);
		if (!plan || !before || !MatchesSource(*worn, *before)) return std::nullopt;
		std::vector<Worn> recordedWorn;
		std::vector<Instance> recordedInstances;
		if (!UnequipCarried(a_player, recordedWorn, recordedInstances) || recordedWorn.size() != recordedInstances.size()) {
			(void)Reequip(a_player, recordedWorn, recordedInstances);
			return std::nullopt;
		}
		const auto pipboyAfter = ReadPipboy(a_player);
		if (!pipboyAfter || !(*pipboyAfter == *pipboy)) {
			REX::WARN("Death bag snapshot refused: the Pip-Boy armor changed while carried items were unequipped.");
			(void)Reequip(a_player, recordedWorn, recordedInstances);
			return std::nullopt;
		}
		Manifest manifest;
		const auto stacks = ReadStacks(a_player, manifest.stacks, manifest.retained, false, true);
		if (!stacks || !SameStacks(*stacks, *worn)) {
			(void)Reequip(a_player, recordedWorn, recordedInstances);
			return std::nullopt;
		}
		if (std::any_of(stacks->begin(), stacks->end(), [](const Stack& a_stack) { return a_stack.unrepresentable; })) {
			REX::WARN("Death bag keeps stacks the bag cannot represent (item mods or other data) with the player.");
		}
		g_pending = Pending{ a_deathId, *plan, std::move(recordedWorn), std::move(recordedInstances), manifest.stacks };
		return manifest;
	}

	PlaceResult Place(RE::Actor& a_player, const std::string& a_deathId)
	{
		if (!g_pending || g_pending->deathId != a_deathId) return {};
		auto pending = std::move(*g_pending);
		g_pending.reset();
		const auto restore = [&a_player, &pending](bool a_restored = true, nlohmann::json a_remaining = nlohmann::json::array()) {
			(void)Reequip(a_player, pending.worn, pending.instances);
			return PlaceResult{ {}, a_restored, std::move(a_remaining) };
		};
		auto* handler = RE::TESDataHandler::GetSingleton();
		auto* base = DeathBagBase();
		auto* cell = a_player.GetParentCell();
		if (!handler || !base || !cell || !cell->IsAttached() || (cell->IsExterior() && !cell->worldSpace)) return restore();
		Manifest ignored;
		const auto stacks = ReadStacks(a_player, ignored.stacks, ignored.retained, false, true);
		if (!stacks) return restore();
		const auto plan = DepositPlan(*stacks);
		if (!plan || plan->empty() || !SameStacks(*plan, pending.stacks) || !F4T::DeathBagView::CountStacks(pending.manifest)) return restore();
		const auto before = ReadCounts(a_player);
		if (!before || !MatchesSource(*stacks, *before)) return restore();
		const auto pipboy = ReadPipboy(a_player);
		if (!pipboy || !pipboy->equipped) {
			REX::WARN("Death bag deposit refused: the Pip-Boy armor is not equipped.");
			return restore();
		}
		RE::NEW_REFR_DATA data;
		data.object = base;
		data.location = a_player.GetPosition();
		data.direction = { 0.0F, 0.0F, 0.0F };
		data.reference = nullptr;
		data.interior = cell->IsExterior() ? nullptr : cell;
		data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
		data.extra = nullptr;
		data.maxLevel = 0;
		data.forcePersist = false;
		data.clearStillLoadingFlag = true;
		data.initializeScripts = false;
		data.initiallyDisabled = false;
		auto handle = handler->CreateReferenceAtLocation(data);
		auto bag = handle.get();
		if (!bag) return restore();
		const auto fresh = ReadBagCounts(*bag);
		if (!fresh || !fresh->empty()) {
			bag->MarkAsDeleted();
			return restore();
		}
		std::vector<Stack> moved;
		for (auto it = plan->rbegin(); it != plan->rend(); ++it) {
			if (IsProtectedPipboyBase(it->base)) continue;
			auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(it->base);
			if (!object) break;
			moved.push_back(*it);
			RE::TESObjectREFR::RemoveItemData remove{ object, static_cast<std::int32_t>(it->count) };
			remove.stackData.push_back(it->index);
			remove.reason = RE::ITEM_REMOVE_REASON::kStoreContainer;
			remove.otherContainer = bag.get();
			a_player.RemoveItem(remove);
		}
		const auto after = ReadCounts(a_player);
		const auto contents = ReadBagCounts(*bag);
		const auto pipboyAfter = ReadPipboy(a_player);
		const auto pipboyKept = pipboyAfter && *pipboyAfter == *pipboy;
		if (!pipboyKept) REX::WARN("Death bag deposit rolled back: the Pip-Boy armor changed during the deposit.");
		if (pipboyKept && after && contents && CountsMatch(*before, *after, *contents, *plan)) {
			F4T::DeathBagView::Adopt(a_deathId, handle, pending.manifest);
			return PlaceResult{ handle, true };
		}
		for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
			auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(it->base);
			if (!object || IsProtectedPipboyForm(object)) continue;
			RE::TESObjectREFR::RemoveItemData back{ object, static_cast<std::int32_t>(it->count) };
			back.reason = RE::ITEM_REMOVE_REASON::kNone;
			back.otherContainer = &a_player;
			bag->RemoveItem(back);
		}
		const auto returned = ReadCounts(a_player);
		const auto leftover = ReadBagCounts(*bag);
		const bool restored = returned && *returned == *before && leftover && leftover->empty();
		if (restored) {
			bag->MarkAsDeleted();
			return restore(true);
		}
		REX::WARN("Death bag deposit rollback is incomplete: the container is kept with the items it still holds.");
		Manifest stuck;
		if (!ReadStacks(*bag, stuck.stacks, stuck.retained, false)) stuck.stacks = nlohmann::json::array();
		return restore(false, std::move(stuck.stacks));
	}

	RE::ObjectRefHandle SpawnView(RE::Actor& a_player, const RE::NiPoint3& a_location)
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		auto* base = DeathBagBase();
		auto* cell = a_player.GetParentCell();
		if (!handler || !base || !cell || !cell->IsAttached() || (cell->IsExterior() && !cell->worldSpace)) return {};
		RE::NEW_REFR_DATA data;
		data.object = base;
		data.location = a_location;
		data.direction = { 0.0F, 0.0F, 0.0F };
		data.reference = nullptr;
		data.interior = cell->IsExterior() ? nullptr : cell;
		data.world = cell->IsExterior() ? cell->worldSpace : nullptr;
		data.extra = nullptr;
		data.maxLevel = 0;
		data.forcePersist = false;
		data.clearStillLoadingFlag = true;
		data.initializeScripts = false;
		data.initiallyDisabled = false;
		return handler->CreateReferenceAtLocation(data);
	}

	bool FillView(RE::TESObjectREFR& a_bag, const nlohmann::json& a_stacks)
	{
		for (const auto& stack : a_stacks) {
			const auto& itemKey = stack.at("itemKey").get_ref<const std::string&>();
			const auto& metadata = stack.at("metadata");
			const auto baseId = F4T::DropMirrorRefs::ResolveItemFormId(itemKey);
			auto* object = baseId ? RE::TESForm::GetFormByID<RE::TESBoundObject>(*baseId) : nullptr;
			auto extra = metadata.is_null() ? RE::BSTSmartPointer<RE::ExtraDataList>{} : F4T::ItemHealthReadback::CreateExtra(metadata);
			if (!object || (!metadata.is_null() && !extra)) {
				return false;
			}
			const auto count = static_cast<std::int32_t>(stack.at("count").get<std::uint64_t>());
			a_bag.AddObjectToContainer(object, std::move(extra), count, nullptr, RE::ITEM_REMOVE_REASON::kNone);
		}
		return true;
	}

	std::uint64_t ReturnToMirror(RE::Actor& a_player, RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object, std::uint32_t a_count, const std::vector<nlohmann::json>& a_candidates)
	{
		std::uint64_t returned = 0;
		for (const auto& metadata : a_candidates) {
			if (returned >= a_count) break;
			const auto available = CountMatches(a_player, a_object, metadata);
			const auto want = (std::min)(available, static_cast<std::uint64_t>(a_count) - returned);
			if (want == 0) continue;
			returned += RevertTake(a_player, a_bag, a_object, static_cast<std::uint32_t>(want), metadata);
		}
		return returned;
	}

	std::uint64_t ReturnFromMirror(RE::Actor& a_player, RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object, std::uint32_t a_count, const nlohmann::json& a_metadata)
	{
		std::uint64_t moved = 0;
		for (std::size_t guard = 0; moved < a_count && guard < 4096; ++guard) {
			const auto match = FindMatch(a_bag, a_object, a_metadata);
			if (!match) break;
			const auto before = CountMatches(a_bag, a_object, a_metadata);
			const auto take = (std::min)(static_cast<std::uint64_t>(match->second), static_cast<std::uint64_t>(a_count) - moved);
			RE::TESObjectREFR::RemoveItemData remove{ &a_object, static_cast<std::int32_t>(take) };
			remove.stackData.push_back(match->first);
			remove.reason = RE::ITEM_REMOVE_REASON::kNone;
			remove.otherContainer = &a_player;
			a_bag.RemoveItem(remove);
			const auto after = CountMatches(a_bag, a_object, a_metadata);
			const auto taken = before > after ? before - after : 0;
			if (taken == 0) break;
			moved += (std::min)(taken, take);
		}
		return moved;
	}

	bool ReturnAll(RE::Actor& a_player, RE::TESObjectREFR& a_bag, nlohmann::json& a_remaining)
	{
		nlohmann::json held = nlohmann::json::array();
		nlohmann::json retained = nlohmann::json::array();
		if (!ReadStacks(a_bag, held, retained, false)) {
			a_remaining = nlohmann::json::array();
			return false;
		}
		for (const auto& stack : held) {
			const auto baseId = F4T::DropMirrorRefs::ResolveItemFormId(stack.at("itemKey").get<std::string>());
			auto* object = baseId ? RE::TESForm::GetFormByID<RE::TESBoundObject>(*baseId) : nullptr;
			if (object) {
				(void)ReturnFromMirror(a_player, a_bag, *object, stack.at("count").get<std::uint32_t>(), stack.at("metadata"));
			}
		}
		a_remaining = nlohmann::json::array();
		retained = nlohmann::json::array();
		return ReadStacks(a_bag, a_remaining, retained, false) && a_remaining.empty();
	}

	bool IsBagReference(RE::TESObjectREFR& a_reference)
	{
		auto* base = DeathBagBase();
		return base != nullptr && a_reference.GetObjectReference() == base;
	}

	void Abandon(const std::string& a_deathId)
	{
		if (!g_pending || g_pending->deathId != a_deathId) return;
		auto abandoned = std::move(*g_pending);
		g_pending.reset();
		ReequipPending(abandoned);
	}

	void AbandonPending()
	{
		if (!g_pending) return;
		auto abandoned = std::move(*g_pending);
		g_pending.reset();
		ReequipPending(abandoned);
	}

	nlohmann::json PendingStacks(const std::string& a_deathId)
	{
		return g_pending && g_pending->deathId == a_deathId ? g_pending->manifest : nlohmann::json::array();
	}

	std::optional<nlohmann::json> CarriedStacks(RE::Actor& a_player)
	{
		nlohmann::json stacks = nlohmann::json::array();
		nlohmann::json retained = nlohmann::json::array();
		if (!ReadStacks(a_player, stacks, retained, false, true)) return std::nullopt;
		return stacks;
	}
}
