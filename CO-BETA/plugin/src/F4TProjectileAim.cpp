#include "pch.h"

#include "F4TProjectileAim.h"

#include "F4TClientCompatibility.h"
#include "F4TDebugLogging.h"
#include "F4TExecutingRuntime.h"
#include "F4THookThreadTable.h"
#include "F4TNetworking.h"
#include "F4TProxyActorController.h"
#include "F4TVtableGuard.h"

#include "RE/B/BeamProjectile.h"
#include "RE/C/ConeProjectile.h"
#include "RE/F/FlameProjectile.h"
#include "RE/G/GrenadeProjectile.h"
#include "RE/M/MissileProjectile.h"
#include "RE/P/Projectile.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <deque>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace F4T::ProjectileAim
{
	namespace
	{
		constexpr std::size_t kAddInitialVelocitySlot = 0xE7;
		constexpr std::size_t kMaximumQueued = 64;

		using AddInitialVelocityFn = void (*)(RE::Projectile*);

		struct ClassHook
		{
			const char* name;
			REL::ID vtable;
			bool sharedBody;
			bool beam;
		};

		const std::array<ClassHook, 6> kClassHooks{ {
			{ "Projectile", RE::VTABLE::Projectile[0], true, false },
			{ "Missile", RE::VTABLE::MissileProjectile[0], true, false },
			{ "Beam", RE::VTABLE::BeamProjectile[0], true, true },
			{ "Cone", RE::VTABLE::ConeProjectile[0], true, false },
			{ "Flame", RE::VTABLE::FlameProjectile[0], true, false },
			{ "Grenade", RE::VTABLE::GrenadeProjectile[0], false, false },
		} };

		struct ExpectedBodies
		{
			std::uintptr_t shared;
			std::uintptr_t grenade;
		};

		constexpr ExpectedBodies kOgBodies{ 0xFD5210, 0xFB9B10 };
		constexpr ExpectedBodies kNgBodies{ 0xDC9740, 0xDAC620 };
		constexpr ExpectedBodies kAeBodies{ 0xE4F850, 0xE32670 };

		struct VerifiedSlot
		{
			const ClassHook* classHook;
			VtableGuard::SlotRequest request;
		};

		AddInitialVelocityFn g_originalShared = nullptr;
		AddInitialVelocityFn g_originalGrenade = nullptr;
		std::atomic<bool> g_installed{ false };
		std::atomic<bool> g_refused{ false };

		std::mutex g_aimOutboundMutex;
		std::deque<F4T::ShotReplay::Direction> g_aimOutbound;
		std::mutex g_remoteAimMutex;
		F4T::ShotReplay::RemoteAimQueue g_remoteAimQueue;

		void LogLine(std::string_view a_message)
		{
			F4T::DebugLogging::Write("ProjectileAim", a_message);
		}

		std::optional<F4T::ShotReplay::Direction> TakeRemoteAim(std::uint32_t a_playerId)
		{
			std::lock_guard lock{ g_remoteAimMutex };
			return g_remoteAimQueue.Take(a_playerId, std::chrono::steady_clock::now());
		}

		void RedirectShot(RE::Projectile* a_projectile, bool a_beam)
		{
			if (!a_projectile) {
				return;
			}
			try {
				const auto shooter = a_projectile->shooter.get();
				if (!shooter.get()) {
					return;
				}
				const auto formId = shooter->GetFormID();
				if (formId == 0) {
					return;
				}
				const auto playerId = F4T::ProxyActorController::TryGetRemotePlayerIdForProxyFormId(formId);
				if (!playerId) {
					return;
				}
				const auto direction = TakeRemoteAim(*playerId);
				if (!direction) {
					return;
				}
				const auto dx = static_cast<float>(direction->x);
				const auto dy = static_cast<float>(direction->y);
				const auto dz = static_cast<float>(direction->z);
				a_projectile->movementDirection.x = dx;
				a_projectile->movementDirection.y = dy;
				a_projectile->movementDirection.z = dz;
				const float clampedZ = std::clamp(dz, -1.0F, 1.0F);
				a_projectile->data.angle.x = -std::asin(clampedZ);
				a_projectile->data.angle.z = static_cast<float>(std::atan2(dx, dy));
				a_projectile->desiredTarget.reset();
				const float speed = std::sqrt(
					a_projectile->velocity.x * a_projectile->velocity.x +
					a_projectile->velocity.y * a_projectile->velocity.y +
					a_projectile->velocity.z * a_projectile->velocity.z);
				if (std::isfinite(speed) && speed > 0.0F) {
					a_projectile->velocity.x = dx * speed;
					a_projectile->velocity.y = dy * speed;
					a_projectile->velocity.z = dz * speed;
				}
				if (a_beam) {
					static const std::string version = REX::FModule::GetExecutingModule().GetFileVersion().string();
					LogLine(std::format("redirected beam shot for player {} on runtime {}", *playerId, version));
				}
			} catch (...) {
			}
		}

		template <bool Beam>
		void AddInitialVelocityHook(RE::Projectile* a_projectile)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::AimRedirect);
			if (g_originalShared) {
				g_originalShared(a_projectile);
			}
			RedirectShot(a_projectile, Beam);
		}

		void GrenadeAddInitialVelocityHook(RE::Projectile* a_projectile)
		{
			F4T::HookThread::Note(F4T::HookThread::Id::AimRedirect);
			if (g_originalGrenade) {
				g_originalGrenade(a_projectile);
			}
			RedirectShot(a_projectile, false);
		}

		VtableGuard::SlotRequest MakeSlotRequest(
			std::uintptr_t a_vtableAddress,
			std::uintptr_t a_replacement,
			const VtableGuard::AddressRange& a_codeSection,
			const VtableGuard::AddressRange& a_vtableSection)
		{
			VtableGuard::SlotRequest request{};
			request.vtableAddress = a_vtableAddress;
			request.slotIndex = kAddInitialVelocitySlot;
			request.replacement = a_replacement;
			request.codeSection = a_codeSection;
			request.vtableSection = a_vtableSection;
			request.slotValue = VtableGuard::ReadSlot(request);
			return request;
		}

		void RollbackWrites(
			const VerifiedSlot* a_written,
			std::size_t a_count,
			AddInitialVelocityFn a_shared,
			AddInitialVelocityFn a_grenade)
		{
			for (std::size_t i = 0; i < a_count; ++i) {
				const bool shared = a_written[i].classHook->sharedBody;
				AddInitialVelocityFn original = shared ? a_shared : a_grenade;
				if (original == nullptr) {
					continue;
				}
				REL::Relocation<std::uintptr_t> vtable{ a_written[i].classHook->vtable };
				vtable.write_vfunc(
					kAddInitialVelocitySlot, reinterpret_cast<std::uintptr_t>(original));
			}
		}

		bool InstallAttempt()
		{
			const auto target = F4T::ExecutingRuntime::Target();
			if (!target) {
				LogLine("aim redirect not installed: the runtime is not a verified version (OG 1.10.163, NG 1.10.984, AE 1.11.240 only)");
				return false;
			}
			const ExpectedBodies& bodies = *target == F4T::ClientCompatibility::BuildTarget::OG
				? kOgBodies
				: *target == F4T::ClientCompatibility::BuildTarget::NG ? kNgBodies : kAeBodies;

			const auto module = REX::FModule::GetExecutingModule();
			const auto moduleBase = module.GetBaseAddress();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");
			const VtableGuard::AddressRange codeSection{ text.GetAddress(), text.GetAddress() + text.GetSize() };
			const VtableGuard::AddressRange vtableSection{ rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };
			std::array<VerifiedSlot, 6> verified{};

			for (std::size_t i = 0; i < kClassHooks.size(); ++i) {
				const ClassHook& classHook = kClassHooks[i];
				const std::uintptr_t replacement = REX::UNRESTRICTED_CAST<std::uintptr_t>(
					classHook.beam ? &AddInitialVelocityHook<true>
											 : classHook.sharedBody ? &AddInitialVelocityHook<false>
																			  : &GrenadeAddInitialVelocityHook);
				// TODO-verify: slot ids per docs/code-notes/CO-BETA-plugin.md
				REL::Relocation<std::uintptr_t> vtable{ classHook.vtable };
				auto request = MakeSlotRequest(vtable.address(), replacement, codeSection, vtableSection);
				if (VtableGuard::VerifySlot(request) != VtableGuard::SlotVerdict::kOk) {
					LogLine(std::format("aim redirect not installed: {} vtable slot failed verification", classHook.name));
					return false;
				}
				const std::uintptr_t expected = classHook.sharedBody ? bodies.shared : bodies.grenade;
				if (!VtableGuard::SlotMatchesExpectedRva(request.slotValue, moduleBase, expected)) {
					LogLine(std::format("aim redirect not installed: {} vtable slot does not hold this runtime's engine function", classHook.name));
					return false;
				}
				verified[i] = VerifiedSlot{ &classHook, request };
			}

			AddInitialVelocityFn shared = g_originalShared;
			AddInitialVelocityFn grenade = g_originalGrenade;
			for (const VerifiedSlot& slot : verified) {
				AddInitialVelocityFn& original = slot.classHook->sharedBody ? shared : grenade;
				if (original == nullptr) {
					original = reinterpret_cast<AddInitialVelocityFn>(slot.request.slotValue);
				}
			}
			std::array<VerifiedSlot, 6> written{};
			std::size_t writtenCount = 0;
			for (const VerifiedSlot& slot : verified) {
				REL::Relocation<std::uintptr_t> vtable{ slot.classHook->vtable };
				vtable.write_vfunc(kAddInitialVelocitySlot, slot.request.replacement);
				written[writtenCount++] = slot;
				if (!VtableGuard::WriteSucceeded(
						*reinterpret_cast<std::uintptr_t*>(slot.request.vtableAddress + kAddInitialVelocitySlot * sizeof(std::uintptr_t)),
						slot.request.replacement)) {
					LogLine(std::format("aim redirect not installed: {} vtable slot write failed", slot.classHook->name));
					RollbackWrites(written.data(), writtenCount, shared, grenade);
					return false;
				}
			}

			g_originalShared = shared;
			g_originalGrenade = grenade;
			g_installed.store(true, std::memory_order_release);
			LogLine("installed aim redirect on six projectile vtables");
			return true;
		}
	}

	void Install()
	{
		if (g_installed.load(std::memory_order_acquire) || g_refused.load(std::memory_order_acquire)) {
			return;
		}
		if (!InstallAttempt()) {
			g_refused.store(true, std::memory_order_release);
		}
	}

	void Tick()
	{
		std::deque<F4T::ShotReplay::Direction> aimOutbound;
		{
			std::lock_guard lock{ g_aimOutboundMutex };
			aimOutbound.swap(g_aimOutbound);
		}
		if (!aimOutbound.empty() && F4T::Networking::IsConnectedToServer()) {
			for (const auto& direction : aimOutbound) {
				F4T::Networking::SendProjectileAimPacket(direction.x, direction.y, direction.z);
			}
		}
	}

	void QueueAimForSend(const F4T::ShotReplay::Direction& a_direction)
	{
		const auto normalized = F4T::ShotReplay::NormalizeDirection(a_direction);
		if (!normalized) {
			return;
		}
		std::lock_guard lock{ g_aimOutboundMutex };
		if (g_aimOutbound.size() < kMaximumQueued) {
			g_aimOutbound.emplace_back(*normalized);
		}
	}

	void HandleRelayedAimPacket(const nlohmann::json& a_packet)
	{
		const auto playerId = F4T::ShotReplay::ParseRelayedPlayerId(a_packet);
		const auto direction = F4T::ShotReplay::ParseProjectileAim(a_packet);
		if (!playerId || !direction) {
			return;
		}
		std::lock_guard lock{ g_remoteAimMutex };
		g_remoteAimQueue.Push(*playerId, *direction, std::chrono::steady_clock::now());
	}

	bool IsInstalled()
	{
		return g_installed.load(std::memory_order_acquire);
	}
}
