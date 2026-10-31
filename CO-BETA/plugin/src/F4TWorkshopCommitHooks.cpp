#include "pch.h"

#include "F4TWorkshopCommitHooks.h"
#include "F4THookThreadTable.h"

#include "F4TClientCompatibility.h"
#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TInlineHook.h"
#include "F4TVtableGuard.h"
#include "F4TWorkshopObserver.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace F4T::WorkshopCommitHooks
{
	namespace
	{
		// source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-place-rotation/README.md (OG entry bytes; the expected prologue must match)
		constexpr std::array<std::uint8_t, 20> kPlacePrologueOg{
			0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x41, 0x54, 0x41, 0x56, 0x48, 0x8D, 0xA8, 0x38, 0xFE, 0xFF, 0xFF
		};
		// source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-place-rotation/README.md (NG and AE entry bytes; the expected prologue must match)
		constexpr std::array<std::uint8_t, 20> kPlacePrologueNgAe{
			0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x55, 0x53, 0x41, 0x55, 0x41, 0x56, 0x48, 0x8D, 0xA8, 0xD8, 0xFD, 0xFF, 0xFF
		};

		// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-1: OG 0x20B9E0, 23-byte instruction boundary)
		constexpr std::array<std::uint8_t, 23> kUpdateActiveEditPrologueOg{
			0x88, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x41,
			0x55, 0x41, 0x56, 0x48, 0x8D, 0xAC, 0x24, 0x78, 0xFD, 0xFF, 0xFF
		};
		// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-1: NG 0x343040, AE 0x397780, 25-byte instruction boundary)
		constexpr std::array<std::uint8_t, 25> kUpdateActiveEditPrologueNg{
			0x88, 0x54, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08, 0x55, 0x56, 0x41,
			0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0x00, 0xFE, 0xFF, 0xFF
		};
		// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-1: NG and AE entry bytes are identical)
		constexpr auto& kUpdateActiveEditPrologueAe = kUpdateActiveEditPrologueNg;

		// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185/README.md (RequestScrapCallback slot 1 must be the accept body)
		constexpr std::size_t kScrapAcceptSlot = 1;
		// source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-scrap/README.md (ObjectRefHandle offset this+0x10)
		constexpr std::size_t kScrapRefHandleOffset = 0x10;
		// source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md (section 13.9 item 1: Call is slot 1 of the ExamineConfirmMenu primary vtable)
		constexpr std::size_t kConfirmCallSlot = 1;
		// source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md (section 12 item 7: bit 9 of the race flag dword and of the ref form flags)
		constexpr std::uint32_t kActorAngleBit = 9;

		using PlaceFn = bool (*)(const RE::Workshop::ContextData&);
		using UpdateActiveEditFn = void (*)(const RE::Workshop::ContextData&, bool);
		using ScrapAcceptFn = void (*)(void*);
		using ConfirmCallFn = void (*)(void*, const Scaleform::GFx::FunctionHandler::Params&);

		F4T::ClientCompatibility::BuildTarget g_target{ F4T::ClientCompatibility::BuildTarget::AE };
		F4T::InlineHook::Result g_placeHook{};
		F4T::InlineHook::Result g_updateActiveEditHook{};
		std::uintptr_t g_scrapAcceptOriginal{ 0 };
		std::uintptr_t g_confirmCallOriginal{ 0 };
		F4T::WorkshopObserver::ReleaseTracker g_release{};
		F4T::WorkshopObserver::ScrapTracker g_scrap{};
		bool g_grabSeen{ false };
		bool g_inScrapAccept{ false };

		class GrabSink final : public RE::BSTEventSink<RE::Workshop::ItemGrabbedEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::Workshop::ItemGrabbedEvent&,
				RE::BSTEventSource<RE::Workshop::ItemGrabbedEvent>*) override
			{
				g_grabSeen = true;
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		GrabSink g_grabSink{};

		std::optional<bool> ActorAngleFlag(const RE::TESObjectREFR& a_ref)
		{
			if (((a_ref.GetFormFlags() >> kActorAngleBit) & 1U) != 0) {
				return true;
			}
			if (g_target == F4T::ClientCompatibility::BuildTarget::OG) {
				// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185 (comment 16670, WS-2: OG predicate 0x1E6CD0, id 1200091)
				using Predicate = bool (*)(const RE::TESObjectREFR*);
				static REL::Relocation<Predicate> predicate{ REL::ID{ 1200091 } };
				return predicate(&a_ref);
			}
			const auto* base = a_ref.GetObjectReference();
			const auto* npc = base ? base->As<RE::TESNPC>() : nullptr;
			const auto* race = npc ? npc->GetFormRace() : nullptr;
			return race != nullptr && ((static_cast<std::uint32_t>(race->data.flags) >> kActorAngleBit) & 1U) != 0;
		}

		bool IsSpline(const RE::TESObjectREFR& a_ref)
		{
			// source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md (section 12 item 4: IsSpline is base form type 0x35, BGSBendableSpline)
			const auto* base = a_ref.GetObjectReference();
			return base != nullptr && base->GetFormType() == RE::ENUM_FORM_ID::kBNDS;
		}

		std::string DescribeHeld(const RE::TESObjectREFR& a_ref)
		{
			// source:RE-Work/subsystems/commonwealth-online-re/workshop-rotate-commit-co185/README.md (angle offsets +0xC0..C8 in OBJ_REFR)
			const auto& angle = a_ref.data.angle;
			const auto position = a_ref.GetPosition();
			const auto isActor = a_ref.GetFormType() == RE::ENUM_FORM_ID::kACHR;
			const auto effective = F4T::WorkshopObserver::EffectiveAngle(
				{ angle.x, angle.y, angle.z }, isActor, isActor ? ActorAngleFlag(a_ref) : std::nullopt);
			const auto used = effective
			                      ? std::format("x={} y={} z={}", effective->x, effective->y, effective->z)
			                      : std::string{ "unknown" };
			return std::format(
				"raw angle x={} y={} z={} rad, effective angle {}, position x={} y={} z={}",
				angle.x, angle.y, angle.z, used, position.x, position.y, position.z);
		}

		bool PlaceHook(const RE::Workshop::ContextData& a_context)
		{
			if (!g_placeHook.hook) {
				return false;
			}
			F4T::HookThread::Note(F4T::HookThread::Id::Place);
			// source:RE-Work/subsystems/commonwealth-online-re/workshop-rotate-commit-co185/README.md (held handle; angle offsets +0xC0..C8 in OBJ_REFR)
			try {
				auto* handle = RE::Workshop::GetPlacementItem();
				const auto held = handle->get();
				const auto firstReport = g_release.OnPlaceAttempt(handle->get_handle());
				if (g_inScrapAccept) {
					F4T::DebugLogging::Write("WorkshopCommit", "placement attempt: reached from a scrap accept, not reported");
				} else if (!firstReport) {
					F4T::DebugLogging::Write("WorkshopCommit", "placement attempt: second half of a wire release already reported");
				} else if (const auto* refr = held.get()) {
					F4T::DebugLogging::Log("WorkshopCommit", "placement attempt: {}", DescribeHeld(*refr));
				} else {
					F4T::DebugLogging::Write("WorkshopCommit", "placement attempt: no held reference");
				}
			} catch (...) {
			}
			return g_placeHook.hook->original<PlaceFn>()(a_context);
		}

		void UpdateActiveEditHook(const RE::Workshop::ContextData& a_context, bool a_flag)
		{
			if (!g_updateActiveEditHook.hook) {
				return;
			}
			F4T::HookThread::Note(F4T::HookThread::Id::UpdateActiveEdit);
			// source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md (section 11.3: ItemGrabbed is sent only at the tail of the grab arm)
			auto* handle = RE::Workshop::GetPlacementItem();
			const auto held = handle->get();
			const auto heldHandle = handle->get_handle();
			const auto* refr = held.get();
			std::string before;
			try {
				if (refr) {
					before = DescribeHeld(*refr);
				}
			} catch (...) {
				before.clear();
			}
			g_grabSeen = false;
			g_updateActiveEditHook.hook->original<UpdateActiveEditFn>()(a_context, a_flag);
			try {
				if (refr != nullptr && !g_grabSeen && g_release.OnReleaseArm(heldHandle, IsSpline(*refr))) {
					F4T::DebugLogging::Log("WorkshopCommit", "move release: {}", before);
				}
			} catch (...) {
			}
		}

		void ScrapAcceptHook(void* a_callback)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ScrapAccept);
			try {
				const auto source = g_scrap.OnAccept();
				if (a_callback) {
					const auto handle = *reinterpret_cast<const std::uint32_t*>(
						static_cast<const std::uint8_t*>(a_callback) + kScrapRefHandleOffset);
					F4T::DebugLogging::Log(
						"WorkshopCommit",
						"scrap accept: ref handle {:#010x}, decided by {}",
						handle,
						source == F4T::WorkshopObserver::ScrapSource::kClick ? "click (route c)" : "button (route b)");
				}
			} catch (...) {
			}
			// source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md (section 12 item 4: slot 1 can reach PCR through CancelActiveEdit for a wire)
			g_inScrapAccept = true;
			reinterpret_cast<ScrapAcceptFn>(g_scrapAcceptOriginal)(a_callback);
			g_inScrapAccept = false;
		}

		void ConfirmCallHook(void* a_menu, const Scaleform::GFx::FunctionHandler::Params& a_params)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::ConfirmCall);
			try {
				const auto userData = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(a_params.userData));
				g_scrap.OnConfirmCall(userData);
				F4T::DebugLogging::Log(
					"WorkshopCommit",
					"confirm click: {}",
					userData == F4T::WorkshopObserver::kScrapCancelId ? "cancel" : "accept");
			} catch (...) {
			}
			reinterpret_cast<ConfirmCallFn>(g_confirmCallOriginal)(a_menu, a_params);
		}

		std::optional<F4T::ClientCompatibility::BuildTarget> VerifiedTarget()
		{
			return F4T::ExecutingRuntime::Target();
		}

		bool InstallInline(
			REL::ID a_id,
			std::span<const std::uint8_t> a_prologue,
			void* a_destination,
			F4T::InlineHook::Result& a_result,
			std::string_view a_label)
		{
			const auto text = REX::FModule::GetExecutingModule().GetSection(".text");
			F4T::InlineHook::Request request{};
			request.target = a_id.address();
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.expectedPrologue = a_prologue;
			request.destination = a_destination;
			F4T::InlineHook::Install(request, nullptr, a_result);
			if (a_result.verdict != F4T::InlineHookGuard::InstallVerdict::kOk) {
				REX::WARN("Workshop {} observer NOT installed ({}).", a_label, F4T::InlineHookGuard::Describe(a_result.verdict));
				return false;
			}
			F4T::DebugLogging::Write("WorkshopCommit", std::format("{} observer installed", a_label));
			return true;
		}

		bool InstallPlacement(F4T::ClientCompatibility::BuildTarget a_target)
		{
			// source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-place-rotation/README.md (Address Library ids: OG 1058211, NG and AE 2195155)
			const auto placeId = a_target == F4T::ClientCompatibility::BuildTarget::OG ? REL::ID{ 1058211 } : REL::ID{ 2195155 };
			const auto& prologue = a_target == F4T::ClientCompatibility::BuildTarget::OG ? kPlacePrologueOg : kPlacePrologueNgAe;
			static_cast<void>(RE::Workshop::GetPlacementItem());
			return InstallInline(placeId, prologue, reinterpret_cast<void*>(&PlaceHook), g_placeHook, "placement");
		}

		bool InstallUpdateActiveEdit(F4T::ClientCompatibility::BuildTarget a_target)
		{
			using Target = F4T::ClientCompatibility::BuildTarget;
			const std::span<const std::uint8_t> prologue = a_target == Target::OG ? std::span<const std::uint8_t>{ kUpdateActiveEditPrologueOg }
			                                            : a_target == Target::NG ? std::span<const std::uint8_t>{ kUpdateActiveEditPrologueNg }
			                                                                     : std::span<const std::uint8_t>{ kUpdateActiveEditPrologueAe };
			// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185/README.md (UpdateActiveEdit Address Library ids: OG 69261, NG and AE 2195152)
			const auto updateId = a_target == Target::OG ? REL::ID{ 69261 } : REL::ID{ 2195152 };
			RE::Workshop::RegisterForItemGrabbed(&g_grabSink);
			if (!InstallInline(updateId, prologue, reinterpret_cast<void*>(&UpdateActiveEditHook), g_updateActiveEditHook, "move release")) {
				RE::Workshop::UnregisterForItemGrabbed(&g_grabSink);
				return false;
			}
			return true;
		}

		bool InstallVtableSlot(
			REL::ID a_vtableId,
			std::size_t a_slot,
			std::uintptr_t a_expectedRva,
			std::uintptr_t a_replacement,
			std::uintptr_t& a_original,
			std::string_view a_label)
		{
			REL::Relocation<std::uintptr_t> vtable{ a_vtableId };
			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");

			VtableGuard::SlotRequest request{};
			request.vtableAddress = vtable.address();
			request.slotIndex = a_slot;
			request.replacement = a_replacement;
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			const auto slotAddress = request.vtableAddress + (a_slot * sizeof(std::uintptr_t));
			request.slotValue = VtableGuard::ReadSlot(request);
			const auto verdict = VtableGuard::VerifySlot(request);
			if (verdict != VtableGuard::SlotVerdict::kOk) {
				REX::WARN("Workshop {} observer NOT installed ({}).", a_label, VtableGuard::Describe(verdict));
				return false;
			}

			if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), a_expectedRva)) {
				REX::WARN("Workshop {} observer NOT installed: the slot does not hold this runtime's expected engine function.", a_label);
				return false;
			}

			a_original = request.slotValue;
			vtable.write_vfunc(a_slot, a_replacement);
			if (!VtableGuard::WriteSucceeded(*reinterpret_cast<const std::uintptr_t*>(slotAddress), request.replacement)) {
				REX::WARN("Workshop {} observer FAILED to install: vtable slot did not take the write.", a_label);
				return false;
			}
			F4T::DebugLogging::Write("WorkshopCommit", std::format("{} observer installed on vtable slot {}", a_label, a_slot));
			return true;
		}

		bool InstallScrapAccept()
		{
			// source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-scrap/README.md (Address Library vtable id 1124584)
			return InstallVtableSlot(
				REL::ID{ 1124584 },
				kScrapAcceptSlot,
				// source:RE-Work/subsystems/commonwealth-online-re/workshop-commit-co185/findings.jsonl (scrap-confirm-slot1-identity-{og,ng,ae}: RequestScrapCallback vtable slot 1 expected RVA)
				F4T::ExecutingRuntime::RvaFor(g_target, 0x247BD0, 0x36D280, 0x3C19C0),
				REX::UNRESTRICTED_CAST<std::uintptr_t>(&ScrapAcceptHook),
				g_scrapAcceptOriginal,
				"scrap accept");
		}

		bool InstallConfirmCall()
		{
			// source:RE-Work/subsystems/commonwealth-online-re/workshop-followup-co185-scrap/README.md (ExamineConfirmMenu primary vtable id 289899 on all three bins)
			return InstallVtableSlot(
				REL::ID{ 289899 },
				kConfirmCallSlot,
				// source:RE-Work/subsystems/commonwealth-online-re/co245-workshop/README.md (section 13.3: ExamineConfirmMenu Call expected RVA)
				F4T::ExecutingRuntime::RvaFor(g_target, 0xB14F30, 0xA07CA0, 0xA5BAE0),
				REX::UNRESTRICTED_CAST<std::uintptr_t>(&ConfirmCallHook),
				g_confirmCallOriginal,
				"scrap confirm click");
		}
	}

	bool Install()
	{
		const auto target = VerifiedTarget();
		if (!target) {
			REX::WARN("Workshop commit observers NOT installed: the game runtime is not a verified version.");
			return false;
		}

		g_target = *target;
		F4T::DebugLogging::Write(
			"WorkshopCommit",
			"rotation convention: raw ref angle x,y,z in radians; non-actor matrix is Rz(z)*Ry(y)*Rx(x), right-handed R(+angle); axis names are a convention");
		const auto placed = InstallPlacement(*target);
		const auto released = InstallUpdateActiveEdit(*target);
		const auto scrapped = InstallScrapAccept();
		const auto clicked = InstallConfirmCall();
		return placed && released && scrapped && clicked;
	}
}
