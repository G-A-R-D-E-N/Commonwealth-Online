#include "F4TThrowableSync.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

#define CHECK(expr)                                                                         \
	do {                                                                                    \
		if (!(expr)) {                                                                      \
			std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #expr);         \
			std::abort();                                                                   \
		}                                                                                   \
	} while (false)

// TODO(RE): concept-level/unverified; every form id below is a synthetic fixture, unverified against game data.
int main()
{
	using namespace F4T;
	using ThrowableSync::ApplyAction;
	using ThrowableSync::ApplyPlan;
	using ThrowableSync::RemoteEquippedItem;
	using ThrowableSync::RemoteItemMod;

	static_assert(ThrowableSync::kRightHandEquipIndex == 0);
	static_assert(std::is_same_v<
		decltype(&ThrowableSync::ApplyThrowablePlan),
		ThrowableSync::ApplyOutcome (*)(RE::Actor&, ProxyActorController::detail::ProxyActorSlot&, const ApplyPlan&, bool,
			const std::vector<RemoteItemMod>*)>);

	const std::optional<RemoteEquippedItem> none{};
	const std::optional<RemoteEquippedItem> grenade{ RemoteEquippedItem{ "throwable", 0x0001F276, std::nullopt } };
	const std::optional<RemoteEquippedItem> molotov{ RemoteEquippedItem{ "throwable", 0x0001F278, std::nullopt } };
	const std::optional<RemoteEquippedItem> wrongSlot{ RemoteEquippedItem{ "rightHand", 0x0001F276, std::nullopt } };
	const std::optional<RemoteEquippedItem> zeroForm{ RemoteEquippedItem{ "throwable", 0, std::nullopt } };
	const std::optional<RemoteEquippedItem> modded{
		RemoteEquippedItem{ "throwable", 0x0001F276, std::vector<RemoteItemMod>{ { 0x00054321, 2, 3 } } }
	};

	CHECK(ThrowableSync::IsThrowableSlot("throwable"));
	CHECK(!ThrowableSync::IsThrowableSlot("rightHand"));
	CHECK(!ThrowableSync::IsThrowableSlot(""));
	CHECK(!ThrowableSync::IsThrowableSlot("Throwable"));

	CHECK(!ThrowableSync::IsValidThrowableItem(RemoteEquippedItem{}));
	CHECK(ThrowableSync::IsValidThrowableItem(*grenade));
	CHECK(ThrowableSync::IsValidThrowableItem(*modded));
	CHECK(!ThrowableSync::IsValidThrowableItem(*wrongSlot));
	CHECK(!ThrowableSync::IsValidThrowableItem(*zeroForm));

	// TODO(RE): concept-level/unverified; the index-0 ownership and capture rows below are synthetic fixtures.
	CHECK(ThrowableSync::RightHandEntryFormId(0x0000001F, false) == 0x0000001F);
	CHECK(ThrowableSync::RightHandEntryFormId(0x0001F276, true) == 0);
	CHECK(ThrowableSync::RightHandEntryFormId(0, false) == 0);
	CHECK(ThrowableSync::RightHandEntryFormId(0, true) == 0);

	const auto capturedGrenade = ThrowableSync::CaptureFromThrownWeapon(0x0001F276, true);
	CHECK(capturedGrenade && capturedGrenade->slot == "throwable" && capturedGrenade->formId == 0x0001F276);
	CHECK(!capturedGrenade->mods);
	const auto capturedMine = ThrowableSync::CaptureFromThrownWeapon(0x0001F278, true);
	CHECK(capturedMine && capturedMine->slot == "throwable" && capturedMine->formId == 0x0001F278);
	CHECK(!ThrowableSync::CaptureFromThrownWeapon(0x0001F276, false));
	CHECK(!ThrowableSync::CaptureFromThrownWeapon(0, true));

	// TODO(RE): concept-level/unverified; the ownership rows below are synthetic fixtures.
	CHECK(!ThrowableSync::HasThrowableRecipe(none, true));
	CHECK(!ThrowableSync::HasThrowableRecipe(grenade, false));
	CHECK(!ThrowableSync::HasThrowableRecipe(wrongSlot, true));
	CHECK(!ThrowableSync::HasThrowableRecipe(zeroForm, true));
	CHECK(ThrowableSync::HasThrowableRecipe(grenade, true));
	CHECK(ThrowableSync::HasThrowableRecipe(modded, true));

	CHECK(!ThrowableSync::ThrowableOwnsRightHand(none, 0, true));
	CHECK(!ThrowableSync::ThrowableOwnsRightHand(grenade, 0, false));
	CHECK(!ThrowableSync::ThrowableOwnsRightHand(grenade, 0x0000001F, true));
	CHECK(!ThrowableSync::ThrowableOwnsRightHand(grenade, 0x0001F276, true));
	CHECK(!ThrowableSync::ThrowableOwnsRightHand(wrongSlot, 0, true));
	CHECK(ThrowableSync::ThrowableOwnsRightHand(grenade, 0, true));
	CHECK(ThrowableSync::ThrowableOwnsRightHand(modded, 0, true));

	CHECK(!ThrowableSync::ThrowableUnresolved(none, 0, false));
	CHECK(!ThrowableSync::ThrowableUnresolved(wrongSlot, 0, false));
	CHECK(!ThrowableSync::ThrowableUnresolved(zeroForm, 0, false));
	CHECK(!ThrowableSync::ThrowableUnresolved(grenade, 0, true));
	CHECK(ThrowableSync::ThrowableUnresolved(grenade, 0, false));
	CHECK(!ThrowableSync::ThrowableUnresolved(grenade, 0x0000001F, false));
	CHECK(!ThrowableSync::ThrowableUnresolved(grenade, 0x0001F276, false));

	// TODO(RE): concept-level/unverified; the entry rows below are synthetic fixtures.
	const std::vector<RemoteEquippedItem> noEntries{};
	CHECK(!ThrowableSync::FindThrowableEntry(noEntries));

	const std::vector<RemoteEquippedItem> armorOnly{ { "body", 0x00000021, std::nullopt }, { "rightHand", 0x0000001F, std::nullopt } };
	CHECK(!ThrowableSync::FindThrowableEntry(armorOnly));

	const std::vector<RemoteEquippedItem> zeroThrowable{ { "throwable", 0, std::nullopt } };
	CHECK(!ThrowableSync::FindThrowableEntry(zeroThrowable));

	const std::vector<RemoteEquippedItem> gunThenThrowable{ { "rightHand", 0x0000001F, std::nullopt }, *grenade };
	const auto foundThrowable = ThrowableSync::FindThrowableEntry(gunThenThrowable);
	CHECK(foundThrowable && foundThrowable->slot == "throwable" && foundThrowable->formId == 0x0001F276);
	CHECK(!foundThrowable->mods);

	const std::vector<RemoteEquippedItem> throwableThenGun{ *grenade, { "rightHand", 0x0000001F, std::nullopt } };
	const auto firstThrowableEntry = ThrowableSync::FindThrowableEntry(throwableThenGun);
	CHECK(firstThrowableEntry && firstThrowableEntry->formId == 0x0001F276);

	const std::vector<RemoteEquippedItem> moddedThrowable{ { "body", 0x00000021, std::nullopt }, *modded };
	const auto moddedEntry = ThrowableSync::FindThrowableEntry(moddedThrowable);
	CHECK(moddedEntry && moddedEntry->formId == 0x0001F276 && moddedEntry->mods == modded->mods);

	CHECK((ThrowableSync::ResolveApplyPlan(none, 0, false) == ApplyPlan{ ApplyAction::kSkip, 0 }));
	CHECK((ThrowableSync::ResolveApplyPlan(none, 0x0001F276, true) == ApplyPlan{ ApplyAction::kSkip, 0 }));
	CHECK((ThrowableSync::ResolveApplyPlan(wrongSlot, 0, true) == ApplyPlan{ ApplyAction::kSkip, 0 }));
	CHECK((ThrowableSync::ResolveApplyPlan(zeroForm, 0, true) == ApplyPlan{ ApplyAction::kSkip, 0 }));
	CHECK((ThrowableSync::ResolveApplyPlan(grenade, 0, false) == ApplyPlan{ ApplyAction::kSkip, 0 }));
	CHECK((ThrowableSync::ResolveApplyPlan(grenade, 0x0001F276, true) == ApplyPlan{ ApplyAction::kSkip, 0 }));
	CHECK((ThrowableSync::ResolveApplyPlan(grenade, 0, true) == ApplyPlan{ ApplyAction::kApply, 0x0001F276 }));
	CHECK((ThrowableSync::ResolveApplyPlan(grenade, 0x0000001F, true) == ApplyPlan{ ApplyAction::kClear, 0x0001F276 }));
	CHECK((ThrowableSync::ResolveApplyPlan(modded, 0, true) == ApplyPlan{ ApplyAction::kApply, 0x0001F276 }));
	CHECK((ThrowableSync::ResolveApplyPlan(molotov, 0x0001F276, true) == ApplyPlan{ ApplyAction::kClear, 0x0001F278 }));
	CHECK((ThrowableSync::ResolveApplyPlan(molotov, 0x0000001F, true) == ApplyPlan{ ApplyAction::kClear, 0x0001F278 }));

	// TODO(RE): concept-level/unverified; the published right-hand entry of a grenade holder is a synthetic fixture.
	CHECK((ThrowableSync::ResolveApplyPlan(
			   grenade, ThrowableSync::RightHandEntryFormId(0x0001F276, true), true) ==
		ApplyPlan{ ApplyAction::kApply, 0x0001F276 }));
	CHECK((ThrowableSync::ResolveApplyPlan(
			   grenade, ThrowableSync::RightHandEntryFormId(0x0000001F, false), true) ==
		ApplyPlan{ ApplyAction::kClear, 0x0001F276 }));

	return 0;
}
