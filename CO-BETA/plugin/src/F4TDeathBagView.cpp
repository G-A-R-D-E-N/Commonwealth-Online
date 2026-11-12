#include "pch.h"
#include "F4TDeathBagGrant.h"
#include "F4TDeathBagView.h"
#include "F4TDeathBagLocal.h"
#include "F4TDeathBagViewLogic.h"
#include "F4TDeathBagWire.h"
#include "F4TDropMirrorRefs.h"
#include "F4TInventoryDropGate.h"
#include "F4TInventorySync.h"
#include "F4TItemHealthReadback.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "RE/A/Actor.h"
#include "RE/B/BGSInventoryItem.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/T/TESObjectCELL.h"
#include "RE/T/TESObjectREFR.h"
#include "RE/T/TESWorldSpace.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::DeathBagView
{
	namespace
	{
		constexpr double kAdoptGraceSeconds = 5.0;
		constexpr double kRetryMinimumSeconds = 0.25;
		constexpr double kRetryMaximumSeconds = 4.0;
		constexpr double kHoldCapSeconds = 30.0;

		struct ViewBag
		{
			std::string deathId;
			nlohmann::json stacks;
			KeyedCounts built;
			RE::ObjectRefHandle ref;
			double graceUntil{ 0.0 };
			bool listed{ false };
		};

		using Slot = std::pair<std::string, nlohmann::json>;
		using SlotCounts = std::map<Slot, std::uint64_t>;
		using FormCounts = std::map<std::uint32_t, std::uint64_t>;

		struct Retry
		{
			double nextAt{ 0.0 };
			double delay{ 0.0 };
		};

		struct StackRef
		{
			std::uint32_t index{};
			std::uint64_t count{};
			bool plain{};
		};

		nlohmann::json g_latest = nlohmann::json::array();
		std::map<std::string, ViewBag> g_views;
		std::map<std::string, Retry> g_retries;
		InFlight g_inFlight;
		bool g_discoveryPending{ false };
		bool g_blocked{ false };
		bool g_holdWarned{ false };
		double g_now{ 0.0 };
		double g_nextDiscoverySeconds{ 0.0 };
		double g_nextCheckSeconds{ 0.0 };
		std::optional<std::string> g_lastCellKey;
		std::optional<std::chrono::steady_clock::time_point> g_holdStart;
		std::uint64_t g_operationSequence{ 0 };

		std::optional<KeyedCounts> ReadKeyedCounts(RE::TESObjectREFR& a_reference)
		{
			auto* list = a_reference.inventoryList;
			if (!list) {
				return KeyedCounts{};
			}
			KeyedCounts counts;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (!item.object) {
					return std::nullopt;
				}
				const auto key = F4T::InventoryDropGate::TranslateItemKey(item.object);
				if (!key) {
					return std::nullopt;
				}
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					auto& total = counts[*key];
					const auto count = static_cast<std::uint64_t>(stack->GetCount());
					if (total > (std::numeric_limits<std::uint64_t>::max)() - count) {
						return std::nullopt;
					}
					total += count;
				}
			}
			return counts;
		}

		std::optional<SlotCounts> ReadSlotCounts(RE::TESObjectREFR& a_reference)
		{
			auto* list = a_reference.inventoryList;
			if (!list) {
				return SlotCounts{};
			}
			SlotCounts counts;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (!item.object) {
					return std::nullopt;
				}
				const auto key = F4T::InventoryDropGate::TranslateItemKey(item.object);
				if (!key) {
					return std::nullopt;
				}
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					nlohmann::json metadata = nullptr;
					if (stack->extra) {
						const auto health = F4T::ItemHealthReadback::Capture(*stack);
						if (!health) {
							return std::nullopt;
						}
						metadata = *health;
					}
					auto& total = counts[Slot{ *key, metadata }];
					const auto count = static_cast<std::uint64_t>(stack->GetCount());
					if (total > (std::numeric_limits<std::uint64_t>::max)() - count) {
						return std::nullopt;
					}
					total += count;
				}
			}
			return counts;
		}

		std::optional<FormCounts> ReadFormCounts(RE::TESObjectREFR& a_reference)
		{
			auto* list = a_reference.inventoryList;
			if (!list) {
				return FormCounts{};
			}
			FormCounts counts;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (!item.object) {
					return std::nullopt;
				}
				const auto form = item.object->GetFormID();
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					const auto count = static_cast<std::uint64_t>(stack->GetCount());
					if (count == 0) {
						continue;
					}
					auto& total = counts[form];
					if (total > (std::numeric_limits<std::uint64_t>::max)() - count) {
						return std::nullopt;
					}
					total += count;
				}
			}
			return counts;
		}

		SlotCounts BuiltSlots(const nlohmann::json& a_stacks)
		{
			SlotCounts counts;
			for (const auto& stack : a_stacks) {
				counts[Slot{ stack.at("itemKey").get<std::string>(), stack.at("metadata") }] += stack.at("count").get<std::uint64_t>();
			}
			return counts;
		}

		std::optional<FormCounts> BuiltForms(const nlohmann::json& a_stacks)
		{
			FormCounts counts;
			for (const auto& stack : a_stacks) {
				const auto baseId = F4T::DropMirrorRefs::ResolveItemFormId(stack.at("itemKey").get<std::string>());
				if (!baseId) {
					return std::nullopt;
				}
				counts[static_cast<std::uint32_t>(*baseId)] += stack.at("count").get<std::uint64_t>();
			}
			return counts;
		}

		bool HasDeficit(const ViewBag& a_view, const SlotCounts& a_current)
		{
			for (const auto& [slot, expected] : BuiltSlots(a_view.stacks)) {
				const auto found = a_current.find(slot);
				const std::uint64_t current = found == a_current.end() ? 0 : found->second;
				if (current < expected) {
					return true;
				}
			}
			return false;
		}

		bool RetryDue(const std::string& a_key, double a_now)
		{
			const auto found = g_retries.find(a_key);
			return found == g_retries.end() || a_now >= found->second.nextAt;
		}

		void NoteFailure(const std::string& a_key, double a_now)
		{
			auto& retry = g_retries[a_key];
			retry.delay = retry.delay <= 0.0 ? kRetryMinimumSeconds : (std::min)(retry.delay * 2.0, kRetryMaximumSeconds);
			retry.nextAt = a_now + retry.delay;
		}

		void NoteSuccess(const std::string& a_key)
		{
			g_retries.erase(a_key);
		}

		std::map<std::string, ViewBag>::iterator RemoveView(std::map<std::string, ViewBag>::iterator a_view)
		{
			if (auto bag = a_view->second.ref.get()) {
				bag->Disable();
				bag->MarkAsDeleted();
			}
			const auto prefix = a_view->first + "|";
			for (auto retry = g_retries.begin(); retry != g_retries.end();) {
				retry = retry->first.rfind(prefix, 0) == 0 ? g_retries.erase(retry) : std::next(retry);
			}
			return g_views.erase(a_view);
		}

		bool CreateView(RE::Actor& a_player, const nlohmann::json& a_bag)
		{
			const auto& stacks = a_bag.at("stacks");
			const auto built = CountStacks(stacks);
			if (!built) {
				return false;
			}
			for (const auto& stack : stacks) {
				if (std::string_view{ stack.at("itemKey").get_ref<const std::string&>() } == F4T::DeathBagLocal::PipboyArmorItemKey) {
					return false;
				}
			}
			const auto& position = a_bag.at("location").at("position");
			RE::NiPoint3 location;
			location.x = static_cast<float>(position.at(0).get<double>());
			location.y = static_cast<float>(position.at(1).get<double>());
			location.z = static_cast<float>(position.at(2).get<double>());
			auto handle = F4T::DeathBagLocal::SpawnView(a_player, location);
			auto bag = handle.get();
			if (!bag) {
				return false;
			}
			if (!F4T::DeathBagLocal::FillView(*bag, stacks)) {
				bag->MarkAsDeleted();
				return false;
			}
			const auto counted = ReadKeyedCounts(*bag);
			if (!counted || *counted != *built) {
				bag->MarkAsDeleted();
				return false;
			}
			const auto deathId = a_bag.at("deathId").get<std::string>();
			g_views.insert_or_assign(deathId, ViewBag{ deathId, stacks, *built, std::move(handle), 0.0, true });
			return true;
		}

		std::uint64_t CountFormInBag(RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object)
		{
			auto* list = a_bag.inventoryList;
			if (!list) {
				return 0;
			}
			std::uint64_t total = 0;
			const RE::BSAutoReadLock lock{ list->rwLock };
			for (const auto& item : list->data) {
				if (item.object != &a_object) {
					continue;
				}
				for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get()) {
					total += static_cast<std::uint64_t>(stack->GetCount());
				}
			}
			return total;
		}

		std::uint64_t ReturnSurplusStacks(RE::Actor& a_player, RE::TESObjectREFR& a_bag, RE::TESBoundObject& a_object, std::uint64_t a_surplus)
		{
			std::vector<StackRef> refs;
			{
				auto* list = a_bag.inventoryList;
				if (!list) {
					return 0;
				}
				const RE::BSAutoReadLock lock{ list->rwLock };
				for (const auto& item : list->data) {
					if (item.object != &a_object) {
						continue;
					}
					std::uint32_t index = 0;
					for (auto* stack = item.stackData.get(); stack; stack = stack->nextStack.get(), ++index) {
						refs.push_back(StackRef{ index, static_cast<std::uint64_t>(stack->GetCount()), !stack->extra });
					}
				}
			}
			std::sort(refs.begin(), refs.end(), [](const StackRef& a_left, const StackRef& a_right) {
				if (a_left.plain != a_right.plain) {
					return a_left.plain;
				}
				return a_left.index > a_right.index;
			});
			std::uint64_t moved = 0;
			for (const auto& ref : refs) {
				if (moved >= a_surplus) {
					break;
				}
				const auto take = (std::min)(ref.count, a_surplus - moved);
				if (take == 0) {
					continue;
				}
				const auto before = CountFormInBag(a_bag, a_object);
				RE::TESObjectREFR::RemoveItemData remove{ &a_object, static_cast<std::int32_t>(take) };
				remove.stackData.push_back(ref.index);
				remove.reason = RE::ITEM_REMOVE_REASON::kNone;
				remove.otherContainer = &a_player;
				a_bag.RemoveItem(remove);
				const auto after = CountFormInBag(a_bag, a_object);
				const auto taken = before > after ? before - after : 0;
				if (taken == 0) {
					break;
				}
				moved += (std::min)(taken, take);
			}
			return moved;
		}

		bool ReturnSurplusByForm(RE::Actor* a_player, RE::TESObjectREFR& a_bag, const ViewBag& a_view, double a_now)
		{
			const auto built = BuiltForms(a_view.stacks);
			const auto current = ReadFormCounts(a_bag);
			if (!built || !current) {
				return false;
			}
			for (const auto& [form, expected] : *built) {
				const auto found = current->find(form);
				const std::uint64_t have = found == current->end() ? 0 : found->second;
				if (have < expected) {
					return false;
				}
			}
			bool clean = true;
			for (const auto& [form, count] : *current) {
				const auto found = built->find(form);
				const std::uint64_t expected = found == built->end() ? 0 : found->second;
				if (count <= expected) {
					continue;
				}
				const auto retryKey = a_view.deathId + "|formsurplus|" + std::to_string(form);
				if (!RetryDue(retryKey, a_now)) {
					clean = false;
					continue;
				}
				const auto surplus = count - expected;
				auto* object = RE::TESForm::GetFormByID<RE::TESBoundObject>(form);
				std::uint64_t moved = 0;
				if (a_player && object) {
					moved = ReturnSurplusStacks(*a_player, a_bag, *object, surplus);
				}
				if (moved == surplus) {
					NoteSuccess(retryKey);
					continue;
				}
				NoteFailure(retryKey, a_now);
				F4T::Networking::Detail::LogThrottledWarning(
					"death_bag_view_form_surplus",
					"A death bag container holds items the record does not list; they are returned to the player and the container is kept until that succeeds.");
				clean = false;
			}
			return clean;
		}

		bool ReturnSurplus(RE::Actor* a_player, RE::TESObjectREFR& a_bag, const ViewBag& a_view, double a_now)
		{
			const auto current = ReadSlotCounts(a_bag);
			if (!current) {
				return ReturnSurplusByForm(a_player, a_bag, a_view, a_now);
			}
			const auto built = BuiltSlots(a_view.stacks);
			bool clean = true;
			for (const auto& [slot, count] : *current) {
				const auto found = built.find(slot);
				const std::uint64_t expected = found == built.end() ? 0 : found->second;
				if (count <= expected) {
					continue;
				}
				const auto retryKey = a_view.deathId + "|surplus|" + slot.first;
				if (!RetryDue(retryKey, a_now)) {
					clean = false;
					continue;
				}
				const auto surplus = count - expected;
				const auto baseId = F4T::DropMirrorRefs::ResolveItemFormId(slot.first);
				auto* object = baseId ? RE::TESForm::GetFormByID<RE::TESBoundObject>(*baseId) : nullptr;
				std::uint64_t moved = 0;
				if (a_player && object && surplus <= kMaximumStackCount) {
					moved = F4T::DeathBagLocal::ReturnFromMirror(*a_player, a_bag, *object, static_cast<std::uint32_t>(surplus), slot.second);
				}
				if (moved == surplus) {
					NoteSuccess(retryKey);
					continue;
				}
				NoteFailure(retryKey, a_now);
				F4T::Networking::Detail::LogThrottledWarning(
					"death_bag_view_surplus",
					"A death bag container holds items outside its record; they are returned to the player and the container is kept until that succeeds.");
				clean = false;
			}
			return clean;
		}

		bool Settle(RE::Actor* a_player, const ViewBag& a_view, double a_now)
		{
			auto bag = a_view.ref.get();
			if (!bag) {
				return true;
			}
			const auto keyed = ReadKeyedCounts(*bag);
			const auto current = ReadSlotCounts(*bag);
			if (keyed && current) {
				if (!DetectTakes(a_view.built, *keyed).empty() || HasDeficit(a_view, *current)) {
					return false;
				}
				return ReturnSurplus(a_player, *bag, a_view, a_now);
			}
			return ReturnSurplusByForm(a_player, *bag, a_view, a_now);
		}

		bool HoldsNothing(RE::TESObjectREFR& a_reference)
		{
			const auto counts = ReadKeyedCounts(a_reference);
			return counts && counts->empty();
		}

		void Reconcile(RE::Actor& a_player, RE::TESObjectCELL& a_cell, const std::optional<std::string>& a_cellKey, double a_now)
		{
			std::optional<std::string> worldspaceKey;
			if (a_cell.IsExterior() && a_cell.worldSpace) {
				worldspaceKey = F4T::InventoryDropGate::TranslateItemKey(a_cell.worldSpace);
			}
			std::map<std::string, nlohmann::json> wanted;
			if (a_cellKey && g_latest.is_array()) {
				for (const auto& bag : g_latest) {
					if (!IsTakeable(bag) || !LocationMatches(bag, *a_cellKey, worldspaceKey)) {
						continue;
					}
					wanted.insert_or_assign(bag.at("deathId").get<std::string>(), bag);
				}
			}
			g_blocked = false;
			for (auto it = g_views.begin(); it != g_views.end();) {
				const bool listedNow = g_latest.is_array() && std::any_of(g_latest.begin(), g_latest.end(), [&it](const auto& a_bag) { return a_bag.is_object() && a_bag.value("deathId", std::string{}) == it->first; });
				const bool delisted = it->second.listed && !listedNow;
				it->second.listed = it->second.listed || listedNow;
				const auto want = wanted.find(it->first);
				auto bag = it->second.ref.get();
				const bool keep = bag && want != wanted.end() && want->second.at("stacks") == it->second.stacks;
				const bool grace = bag && want == wanted.end() && a_now < it->second.graceUntil;
				if (keep || grace) {
					++it;
					continue;
				}
				if (!bag) {
					it = RemoveView(it);
					continue;
				}
				const bool settled = Settle(&a_player, it->second, a_now);
				if (settled && (want != wanted.end() || HoldsNothing(*bag) || delisted)) {
					it = RemoveView(it);
					continue;
				}
				if (!settled) {
					g_blocked = true;
				}
				++it;
			}
			for (const auto& entry : wanted) {
				if (g_views.contains(entry.first)) {
					continue;
				}
				const auto createKey = entry.first + "|create";
				if (!RetryDue(createKey, a_now)) {
					continue;
				}
				if (CreateView(a_player, entry.second)) {
					NoteSuccess(createKey);
				} else {
					NoteFailure(createKey, a_now);
				}
			}
		}

		std::uint64_t NextSequence()
		{
			if (g_operationSequence == 0) {
				g_operationSequence = static_cast<std::uint64_t>(
					std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
			}
			return ++g_operationSequence;
		}

		void RequestWithdraw(const ViewBag& a_view, const Take& a_take, double a_nowSeconds)
		{
			if (a_take.count == 0 || !CanWithdraw(a_view.stacks, a_take.itemKey)) {
				return;
			}
			const auto inventory = F4T::InventorySync::ServerInventory();
			if (!inventory || !g_inFlight.TryBegin(a_view.deathId, a_take.itemKey, a_nowSeconds)) {
				return;
			}
			const auto operationId = OperationIdFor(NextSequence());
			auto packet = F4T::DeathBagWire::EncodeWithdraw(
				a_view.deathId,
				a_take.itemKey,
				static_cast<std::uint32_t>(a_take.count),
				static_cast<std::uint32_t>(inventory->revision),
				operationId, true);
			if (packet) {
				if (!F4T::DeathBagGrant::TrackWithdrawal(a_view.deathId, operationId)) {
					REX::WARN("Death bag withdrawal is waiting for a save or unresolved requests; the item stays in its container.");
					return;
				}
			}
			if (!packet || !F4T::Networking::SendDeathBagWithdraw(*packet, F4T::Networking::GetConnectionGeneration())) {
				F4T::DeathBagGrant::ForgetWithdrawal(a_view.deathId, operationId);
				REX::WARN("Death bag withdraw was not sent; the item stays in its container.");
			}
		}

		std::uint64_t ReturnTake(RE::Actor& a_player, const ViewBag& a_view, RE::TESObjectREFR& a_bag, const Take& a_take)
		{
			const auto baseId = F4T::DropMirrorRefs::ResolveItemFormId(a_take.itemKey);
			auto* object = baseId ? RE::TESForm::GetFormByID<RE::TESBoundObject>(*baseId) : nullptr;
			if (!object || a_take.count == 0 || a_take.count > kMaximumStackCount) {
				return 0;
			}
			return F4T::DeathBagLocal::ReturnToMirror(a_player, a_bag, *object, static_cast<std::uint32_t>(a_take.count), CandidateMetadata(a_view.stacks, a_take.itemKey));
		}

		void CheckTakes(RE::Actor& a_player, double a_nowSeconds)
		{
			for (auto& entry : g_views) {
				auto& view = entry.second;
				auto bag = view.ref.get();
				if (!bag) {
					continue;
				}
				const auto keyed = ReadKeyedCounts(*bag);
				if (keyed) {
					for (const auto& take : DetectTakes(view.built, *keyed)) {
						const auto retryKey = view.deathId + "|take|" + take.itemKey;
						if (!RetryDue(retryKey, a_nowSeconds)) {
							continue;
						}
						const auto returned = ReturnTake(a_player, view, *bag, take);
						if (returned < take.count) {
							NoteFailure(retryKey, a_nowSeconds);
							F4T::Networking::Detail::LogThrottledWarning(
								"death_bag_view_return",
								"Death bag view take could not be returned to its container; it is retried and not withdrawn.");
						} else {
							NoteSuccess(retryKey);
						}
						if (returned > 0) {
							RequestWithdraw(view, Take{ take.itemKey, returned }, a_nowSeconds);
						}
					}
				}
				(void)ReturnSurplus(&a_player, *bag, view, a_nowSeconds);
			}
		}

		bool ViewMismatch(const ViewBag& a_view)
		{
			auto bag = a_view.ref.get();
			if (!bag) {
				return false;
			}
			if (const auto current = ReadSlotCounts(*bag)) {
				return *current != BuiltSlots(a_view.stacks);
			}
			const auto forms = ReadFormCounts(*bag);
			const auto built = BuiltForms(a_view.stacks);
			return !forms || !built || *forms != *built;
		}
	}

	void NoteDiscovery(const nlohmann::json& a_bags)
	{
		g_latest = a_bags;
		g_discoveryPending = true;
	}

	void Tick(double a_nowSeconds)
	{
		g_now = a_nowSeconds;
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* cell = player ? player->GetParentCell() : nullptr;
		if (!player || !cell || !cell->IsAttached()) {
			return;
		}
		const auto cellKey = F4T::InventoryDropGate::TranslateItemKey(cell);
		const std::string blockedKey = "reconcile|blocked";
		const bool discovered = g_discoveryPending;
		const bool blockedDue = g_blocked && RetryDue(blockedKey, a_nowSeconds);
		const bool reconcile = discovered || cellKey != g_lastCellKey || blockedDue;
		if (reconcile || a_nowSeconds >= g_nextCheckSeconds) {
			g_nextCheckSeconds = a_nowSeconds + kCheckIntervalSeconds;
			CheckTakes(*player, a_nowSeconds);
		}
		if (reconcile) {
			g_discoveryPending = false;
			if (discovered) {
				g_inFlight.Clear();
			}
			if (cellKey != g_lastCellKey) {
				SweepCellSavedContainers(*cell);
			}
			Reconcile(*player, *cell, cellKey, a_nowSeconds);
			g_lastCellKey = cellKey;
			if (g_blocked) {
				NoteFailure(blockedKey, a_nowSeconds);
			} else {
				NoteSuccess(blockedKey);
			}
		}
		if (a_nowSeconds >= g_nextDiscoverySeconds) {
			g_nextDiscoverySeconds = a_nowSeconds + kDiscoveryIntervalSeconds;
			(void)F4T::Networking::SendDeathBagDiscover(F4T::DeathBagWire::EncodeDiscovery(), F4T::Networking::GetConnectionGeneration());
		}
	}

	void Reset()
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		g_blocked = false;
		for (auto it = g_views.begin(); it != g_views.end();) {
			if (Settle(player, it->second, g_now)) {
				it = RemoveView(it);
			} else {
				g_blocked = true;
				++it;
			}
		}
		g_latest = nlohmann::json::array();
		g_inFlight.Clear();
		g_discoveryPending = false;
		g_nextDiscoverySeconds = 0.0;
		g_nextCheckSeconds = 0.0;
		g_lastCellKey.reset();
	}

	void Adopt(const std::string& a_deathId, RE::ObjectRefHandle a_bag, const nlohmann::json& a_stacks)
	{
		const auto built = CountStacks(a_stacks);
		if (!built) {
			return;
		}
		if (const auto existing = g_views.find(a_deathId); existing != g_views.end()) {
			if (!Settle(RE::PlayerCharacter::GetSingleton(), existing->second, g_now)) {
				REX::WARN("Death bag adoption skipped: the existing container for this bag is not settled.");
				return;
			}
			(void)RemoveView(existing);
		}
		g_views.insert_or_assign(a_deathId, ViewBag{ a_deathId, a_stacks, *built, std::move(a_bag), g_now + kAdoptGraceSeconds });
	}

	void Void(const std::string& a_deathId, bool a_emptied)
	{
		if (const auto found = g_views.find(a_deathId); found != g_views.end()) {
			if (a_emptied || Settle(RE::PlayerCharacter::GetSingleton(), found->second, g_now)) {
				(void)RemoveView(found);
			} else {
				g_blocked = true;
			}
		}
		nlohmann::json kept = nlohmann::json::array();
		if (g_latest.is_array()) {
			for (const auto& bag : g_latest) {
				if (!bag.is_object() || bag.value("deathId", std::string{}) != a_deathId) {
					kept.push_back(bag);
				}
			}
		}
		g_latest = std::move(kept);
	}

	bool IsMirrored(const std::string& a_deathId)
	{
		return g_views.contains(a_deathId);
	}

	bool IsViewReference(const RE::TESObjectREFR* a_reference)
	{
		if (!a_reference) {
			return false;
		}
		return std::any_of(g_views.begin(), g_views.end(), [a_reference](const auto& a_entry) { return a_entry.second.ref.get().get() == a_reference; });
	}

	bool SnapshotHold()
	{
		bool mismatch = false;
		for (const auto& entry : g_views) {
			if (ViewMismatch(entry.second)) {
				mismatch = true;
				break;
			}
		}
		if (!mismatch) {
			g_holdStart.reset();
			g_holdWarned = false;
			return false;
		}
		const auto now = std::chrono::steady_clock::now();
		if (!g_holdStart) {
			g_holdStart = now;
		}
		if (std::chrono::duration<double>(now - *g_holdStart).count() < kHoldCapSeconds) {
			return true;
		}
		if (!g_holdWarned) {
			g_holdWarned = true;
			REX::WARN("Death bag container mismatch persisted for over 30 s; owned inventory snapshots resume while it is retried.");
		}
		return false;
	}
}
