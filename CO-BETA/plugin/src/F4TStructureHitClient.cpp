#include "pch.h"

#include "F4TStructureHitClient.h"

#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4TNetworking.h"
#include "F4TNetworkingInternal.h"
#include "F4TSettlementCaptureClient.h"
#include "F4TVtableGuard.h"
#include "F4TWeaponClassification.h"
#include "F4TWorkshopMaterials.h"
#include "F4TWorkshopProtocol.h"
#include "F4TWorkshopRaids.h"
#include "F4TWorkshopReplicationClient.h"

#include "RE/B/BGSKeyword.h"
#include "RE/M/MissileProjectile.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/Projectile.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESObjectWEAP.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace F4T::StructureHitClient
{
	namespace
	{
		using ExplosiveKind = WorkshopRaids::ExplosiveKind;
		using WeaponKind = WorkshopMaterials::WeaponKind;

		constexpr std::uint32_t kWeaponTypeRifleKeywordFormId{ 0x0004A0A1 };
		constexpr std::uint32_t kWeaponTypeHeavyGunKeywordFormId{ 0x0004A0A3 };

		std::optional<WeaponKind> WeaponKindFor(std::uint32_t a_formID)
		{
			if (a_formID == 0) {
				return WeaponKind::Melee;
			}
			const auto* weapon = RE::TESForm::GetFormByID<RE::TESObjectWEAP>(a_formID);
			if (!weapon) {
				return std::nullopt;
			}
			const auto animation = WeaponClassification::ClassifyWeapon(weapon);
			if (WeaponClassification::IsMeleeArchetype(animation)) {
				return WeaponKind::Melee;
			}
			if (animation != WeaponClassification::WeaponAnimationType::kRangedGun) {
				return std::nullopt;
			}
			static const RE::BGSKeyword* heavyKeyword{ nullptr };
			if (!heavyKeyword) {
				heavyKeyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kWeaponTypeHeavyGunKeywordFormId);
			}
			static const RE::BGSKeyword* rifleKeyword{ nullptr };
			if (!rifleKeyword) {
				rifleKeyword = RE::TESForm::GetFormByID<RE::BGSKeyword>(kWeaponTypeRifleKeywordFormId);
			}
			if (heavyKeyword && weapon->HasKeyword(heavyKeyword)) {
				return WeaponKind::Heavy;
			}
			if (rifleKeyword && weapon->HasKeyword(rifleKeyword)) {
				return WeaponKind::Rifle;
			}
			return WeaponKind::Light;
		}

		constexpr int kDiagnosticLinesPerSecond{ 10 };

		bool ShouldLogStructureHitDiagnostic()
		{
			using Clock = std::chrono::steady_clock;
			static Clock::time_point windowStart{};
			static int linesInWindow{ 0 };
			const auto now = Clock::now();
			if (now - windowStart >= std::chrono::seconds{ 1 }) {
				windowStart = now;
				linesInWindow = 0;
			}
			if (linesInWindow >= kDiagnosticLinesPerSecond) {
				return false;
			}
			++linesInWindow;
			return true;
		}

		std::string_view WeaponKindLabel(const std::optional<WeaponKind>& a_kind)
		{
			if (!a_kind) {
				return "none";
			}
			switch (*a_kind) {
			case WeaponKind::Melee:
				return "Melee";
			case WeaponKind::Light:
				return "Light";
			case WeaponKind::Rifle:
				return "Rifle";
			case WeaponKind::Heavy:
				return "Heavy";
			case WeaponKind::Explosive:
				return "Explosive";
			}
			return "none";
		}

		void LogStructureHit(std::string_view a_line)
		{
			if (ShouldLogStructureHitDiagnostic()) {
				F4T::Networking::Detail::LogStatusWithLocalPlayerPrefix(a_line);
			}
		}

		void QueuePacket(nlohmann::json a_packet)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}
			taskInterface->AddTask([packet = std::move(a_packet)]() {
				if (!F4T::Networking::IsConnectedToServer()) {
					return;
				}
				if (!F4T::Networking::Detail::SendPacketBytes(packet.dump())) {
					F4T::DebugLogging::Log("StructureHit", "Could not send a structureHit packet.");
				}
			});
		}

		void SendStructureHit(const WorkshopProtocol::StructureHit& a_hit)
		{
			WorkshopReplicationClient::NoteLocalStructureHit({ a_hit.buildZoneId, a_hit.objectId });
			QueuePacket(WorkshopProtocol::MakeStructureHit(a_hit));
		}

		class StructureHitEventSink : public RE::BSTEventSink<RE::TESHitEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::TESHitEvent& a_event,
				RE::BSTEventSource<RE::TESHitEvent>*) override
			{
				const auto explosive = WorkshopRaids::ExplosiveKindForSource(a_event.sourceFormID);
				const auto localPlayer = RE::PlayerCharacter::GetSingleton();
				const auto eventCause = a_event.cause;
				const bool causeIsLocalPlayer = localPlayer && eventCause && eventCause.get() == static_cast<RE::TESObjectREFR*>(localPlayer);
				const auto eventTarget = a_event.target;
				if (!eventTarget || !causeIsLocalPlayer) {
					return RE::BSEventNotifyControl::kContinue;
				}

				auto placed = WorkshopReplicationClient::FindPlacedObject(*eventTarget.get());
				if (!placed) {
					if (const auto zone = SettlementCaptureClient::BuildZoneForWorkbench(*eventTarget.get())) {
						placed = WorkshopReplicationClient::PlacedObject{ *zone, F4T::WorkshopRaids::WorkbenchObjectId };
					}
				}
				if (!placed) {
					const auto* baseForm = eventTarget->GetObjectReference();
					LogStructureHit(std::format(
						"StructureHit non-workshop target={:08X} baseType={}",
						eventTarget->GetFormID(),
						baseForm ? RE::TESForm::GetFormTypeString(baseForm->GetFormType()) : "none"));
					return RE::BSEventNotifyControl::kContinue;
				}

				const auto weaponKind = explosive ?
					std::optional<WeaponKind>{ WeaponKind::Explosive } :
					WeaponKindFor(a_event.sourceFormID);
				const char* dropReason = nullptr;
				if (explosive && (a_event.usesHitData || a_event.projectileFormID != 0)) {
					dropReason = "explosive_with_hit_data";
				} else if (!weaponKind) {
					dropReason = "unclassified_weapon";
				}

				LogStructureHit(std::format(
					"StructureHit target={:08X} cause={:08X} localCause={} usesHitData={} sourceFormID={:08X} projectileFormID={:08X} weapon={} outcome={} reason={}",
					eventTarget->GetFormID(),
					eventCause ? eventCause->GetFormID() : 0U,
					causeIsLocalPlayer,
					a_event.usesHitData,
					a_event.sourceFormID,
					a_event.projectileFormID,
					WeaponKindLabel(weaponKind),
					dropReason ? "dropped" : "sent",
					dropReason ? dropReason : "none"));
				if (dropReason) {
					return RE::BSEventNotifyControl::kContinue;
				}

				SendStructureHit(WorkshopProtocol::StructureHit{ placed->buildZoneId, placed->objectId, explosive.value_or(ExplosiveKind{}), *weaponKind });
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		constexpr std::size_t kMaxProjectileImpacts{ 8 };
		constexpr std::size_t kProcessImpactsSlot{ 208 };
		using ProcessImpactsFn = bool (*)(RE::MissileProjectile*);
		std::uintptr_t g_missileProcessImpactsOriginal{ 0 };

		struct ProjectileImpacts
		{
			std::array<RE::ObjectRefHandle, kMaxProjectileImpacts> targets{};
			std::size_t count{ 0 };
			std::uint32_t weaponFormID{ 0 };
		};

		void CollectLocalImpacts(RE::MissileProjectile* a_projectile, ProjectileImpacts& a_impacts)
		{
			const auto localPlayer = RE::PlayerCharacter::GetSingleton();
			if (!localPlayer || a_projectile->shooter != localPlayer->GetHandle() || a_projectile->explosion) {
				return;
			}
			for (const auto& impact : a_projectile->impacts) {
				if (a_impacts.count == kMaxProjectileImpacts) {
					break;
				}
				if (impact.processed || impact.impactDecalQuery.data != static_cast<std::uint64_t>(-1)) {
					continue;
				}
				a_impacts.targets[a_impacts.count++] = impact.collidee;
			}
			if (a_impacts.count > 0 && a_projectile->weaponSource.object) {
				a_impacts.weaponFormID = a_projectile->weaponSource.object->GetFormID();
			}
		}
		void ReportProjectileImpacts(const ProjectileImpacts& a_impacts)
		{
			const auto weaponKind = a_impacts.weaponFormID == 0 ? std::optional<WeaponKind>{} : WeaponKindFor(a_impacts.weaponFormID);
			for (std::size_t i = 0; i < a_impacts.count; ++i) {
				const auto target = a_impacts.targets[i].get();
				if (!target) {
					continue;
				}
				const auto placed = WorkshopReplicationClient::FindPlacedObject(*target);
				if (!placed) {
					continue;
				}
				LogStructureHit(std::format(
					"StructureHit projectile target={:08X} weaponFormID={:08X} weapon={} outcome={} reason={}",
					target->GetFormID(),
					a_impacts.weaponFormID,
					WeaponKindLabel(weaponKind),
					weaponKind ? "sent" : "dropped",
					weaponKind ? "none" : "unclassified_weapon"));
				if (weaponKind) {
					SendStructureHit(WorkshopProtocol::StructureHit{ placed->buildZoneId, placed->objectId, ExplosiveKind{}, *weaponKind });
				}
			}
		}

		void QueueProjectileImpacts(ProjectileImpacts a_impacts)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				return;
			}
			taskInterface->AddTask([impacts = std::move(a_impacts)]() {
				ReportProjectileImpacts(impacts);
			});
		}

		bool ProcessImpactsHook(RE::MissileProjectile* a_projectile)
		{
			ProjectileImpacts impacts{};
			try {
				CollectLocalImpacts(a_projectile, impacts);
			} catch (...) {
			}
			const bool processed = reinterpret_cast<ProcessImpactsFn>(g_missileProcessImpactsOriginal)(a_projectile);
			if (impacts.count > 0) {
				try {
					QueueProjectileImpacts(std::move(impacts));
				} catch (...) {
				}
			}
			return processed;
		}

		struct ProcessImpactsRva
		{
			std::uintptr_t expected;
		};

		// rva E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/ranged-hit-identity-co56/README.md
		constexpr ProcessImpactsRva kProcessImpactsOg{ 0xFC4110 };
		constexpr ProcessImpactsRva kProcessImpactsNg{ 0xDB68A0 };
		constexpr ProcessImpactsRva kProcessImpactsAe{ 0xE3C8D0 };

		bool InstallProcessImpactsHook()
		{
			const auto target = F4T::ExecutingRuntime::Target();
			if (!target) {
				F4T::DebugLogging::Log("StructureHit", "Projectile impact hook not installed: the runtime is not a verified version.");
				return false;
			}
			using Build = F4T::ClientCompatibility::BuildTarget;
			const auto& entry = *target == Build::OG ? kProcessImpactsOg : *target == Build::NG ? kProcessImpactsNg : kProcessImpactsAe;

			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");
			REL::Relocation<std::uintptr_t> table{ RE::VTABLE::MissileProjectile[0] /* E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/ranged-hit-identity-co56/README.md */ };
			VtableGuard::SlotRequest request{};
			request.vtableAddress = table.address();
			request.slotIndex = kProcessImpactsSlot;
			request.replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(&ProcessImpactsHook);
			request.codeSection = { text.GetAddress(), text.GetAddress() + text.GetSize() };
			request.vtableSection = { rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			request.slotValue = VtableGuard::ReadSlot(request);
			if (VtableGuard::VerifySlot(request) != VtableGuard::SlotVerdict::kOk ||
				!VtableGuard::SlotMatchesExpectedRva(request.slotValue, module.GetBaseAddress(), entry.expected)) {
				F4T::DebugLogging::Log("StructureHit", "Projectile impact hook not installed: the hook slot does not hold this runtime's engine function.");
				return false;
			}
			g_missileProcessImpactsOriginal = request.slotValue;
			table.write_vfunc(kProcessImpactsSlot, request.replacement);
			if (!VtableGuard::WriteSucceeded(
					*reinterpret_cast<const std::uintptr_t*>(request.vtableAddress + kProcessImpactsSlot * sizeof(std::uintptr_t)),
					request.replacement)) {
				F4T::DebugLogging::Log("StructureHit", "Projectile impact hook failed to install: the hook slot did not take the write.");
				return false;
			}
			F4T::DebugLogging::Log("StructureHit", "Registered the projectile impact hook.");
			return true;
		}
	}

	bool Install()
	{
		static bool installed = false;
		if (installed) {
			return true;
		}
		auto* eventSource = RE::TESHitEvent::GetEventSource();
		if (!eventSource) {
			F4T::DebugLogging::Log("StructureHit", "TESHitEvent source unavailable; workshop structure hits will not be sent.");
			return false;
		}
		static StructureHitEventSink s_sink;
		eventSource->RegisterSink(&s_sink);
		installed = true;
		F4T::DebugLogging::Log("StructureHit", "Registered the workshop structure hit sink.");
		InstallProcessImpactsHook();
		return true;
	}
}
