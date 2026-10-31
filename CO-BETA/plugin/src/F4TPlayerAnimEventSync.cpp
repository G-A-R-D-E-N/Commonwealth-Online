#include "pch.h"

#include "F4TPlayerAnimEventSync.h"

#include "F4TAnimationGraphOverride.h"
#include "F4TDebugLogging.h"
#include "F4THookThreadTable.h"
#include "F4TNetworking.h"
#include "F4TProxyActorController.h"
#include "F4TShotReplay.h"
#include "F4TVtableGuard.h"

#include "RE/A/Actor.h"
#include "RE/B/BSFixedString.h"
#include "RE/I/IAnimationGraphManagerHolder.h"
#include "RE/P/PlayerCharacter.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace F4T::PlayerAnimEventSync
{
	namespace
	{
		using Holder = RE::IAnimationGraphManagerHolder;
		using Notify_t = bool (*)(Holder*, const RE::BSFixedString&);
		constexpr std::size_t kNotifySlot = 0x01;
		constexpr std::size_t kMaximumQueued = 64;

		Notify_t g_originalNotify = nullptr;
		constexpr std::uint32_t kMaximumInstallAttempts = 10;
		constexpr std::chrono::seconds kInstallRetryInterval{ 1 };
		std::atomic<std::uint32_t> g_installAttempts{ 0 };
		std::atomic<bool> g_installErrorLogged{ false };
		std::chrono::steady_clock::time_point g_lastInstallAttempt{};

		std::mutex g_outboundMutex;
		std::deque<std::string> g_outbound;
		std::mutex g_inboundMutex;
		std::deque<std::pair<std::uint32_t, std::string>> g_inbound;
		struct PendingReplay
		{
			std::uint32_t playerId{};
			std::string name;
			std::chrono::steady_clock::time_point since{};
			std::chrono::milliseconds window{};
		};
		constexpr std::chrono::milliseconds kRetryWindow{ 600 };
		constexpr std::chrono::milliseconds kRetryWindowWhileDrawing{ 1500 };
		// Game thread only: ReplayOnProxy, DropPendingReplays and RetryPendingReplays run from Tick.
		std::vector<PendingReplay> g_pendingReplays;

		bool NotifyHook(Holder* a_self, const RE::BSFixedString& a_event)
		{
			const bool ok = g_originalNotify(a_self, a_event);
			const char* name = a_event.c_str();
			if (ok && name && a_self == static_cast<const Holder*>(InstallState::holder.load(std::memory_order_acquire)) && F4T::ShotReplay::IsReplayableAnimEvent(name)) {
				F4T::HookThread::Note(F4T::HookThread::Id::PlayerAnimEventCapture);
				std::lock_guard lock{ g_outboundMutex };
				if (g_outbound.size() < kMaximumQueued) {
					g_outbound.emplace_back(name);
				}
			}
			return ok;
		}

		VtableGuard::SlotRequest MakeRequest(
			std::uintptr_t a_vtableAddress,
			std::uintptr_t a_replacement,
			const VtableGuard::AddressRange& a_code,
			const VtableGuard::AddressRange& a_vtable)
		{
			VtableGuard::SlotRequest request{};
			request.vtableAddress = a_vtableAddress;
			request.slotIndex = kNotifySlot;
			request.replacement = a_replacement;
			request.codeSection = a_code;
			request.vtableSection = a_vtable;
			request.slotValue = VtableGuard::ReadSlot(request);
			return request;
		}

		void NoteInstallFailure()
		{
			if (!g_installErrorLogged.exchange(true, std::memory_order_acq_rel)) {
				REX::CRITICAL("PlayerAnimEventSync failed to install the holder notify hook; retrying on the game thread.");
			}
		}

		void InstallOnLocalPlayer()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			auto* holder = static_cast<Holder*>(player);
			const auto module = REX::FModule::GetExecutingModule();
			const auto text = module.GetSection(".text");
			const auto rdata = module.GetSection(".rdata");
			const VtableGuard::AddressRange codeSection{ text.GetAddress(), text.GetAddress() + text.GetSize() };
			const VtableGuard::AddressRange vtableSection{ rdata.GetAddress(), rdata.GetAddress() + rdata.GetSize() };

			const auto vtbl = *reinterpret_cast<std::uintptr_t**>(holder);
			const auto request = MakeRequest(
				reinterpret_cast<std::uintptr_t>(vtbl), REX::UNRESTRICTED_CAST<std::uintptr_t>(&NotifyHook), codeSection, vtableSection);
			if (VtableGuard::VerifySlot(request) != VtableGuard::SlotVerdict::kOk) {
				NoteInstallFailure();
				return;
			}
			g_originalNotify = reinterpret_cast<Notify_t>(request.slotValue);
			REL::Relocation<std::uintptr_t> holderVtable{ reinterpret_cast<std::uintptr_t>(vtbl) };
			holderVtable.write_vfunc(kNotifySlot, &NotifyHook);
			if (!VtableGuard::WriteSucceeded(vtbl[kNotifySlot], REX::UNRESTRICTED_CAST<std::uintptr_t>(&NotifyHook))) {
				NoteInstallFailure();
				return;
			}
			InstallState::holder.store(holder, std::memory_order_release);
			F4T::DebugLogging::Write("PlayerAnimEvent", "hooked local player holder notify");
		}

		void ReplayOnProxy(RE::Actor* a_proxy, std::uint32_t a_playerId, const std::string& a_name)
		{
			if (F4T::AnimationGraphOverride::SendGraphEvent(a_proxy, a_name.c_str()) || !F4T::ShotReplay::IsRetryableAnimEvent(a_name)) {
				return;
			}
			const bool drawing = !a_proxy->GetWeaponMagicDrawn();
			if (drawing) {
				a_proxy->DrawWeaponMagicHands(true);
			}
			g_pendingReplays.push_back({ a_playerId, a_name, std::chrono::steady_clock::now(), drawing ? kRetryWindowWhileDrawing : kRetryWindow });
		}

		void DropPendingReplays(std::uint32_t a_playerId)
		{
			std::erase_if(g_pendingReplays, [a_playerId](const PendingReplay& a_pending) { return a_pending.playerId == a_playerId; });
		}

		void RetryPendingReplays()
		{
			const auto now = std::chrono::steady_clock::now();
			for (auto it = g_pendingReplays.begin(); it != g_pendingReplays.end();) {
				auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(it->playerId);
				const bool done = !proxy || F4T::AnimationGraphOverride::SendGraphEvent(proxy, it->name.c_str()) || now - it->since > it->window;
				it = done ? g_pendingReplays.erase(it) : it + 1;
			}
		}

		void TryInstallOnTick()
		{
			if (g_installAttempts.load(std::memory_order_acquire) >= kMaximumInstallAttempts) {
				return;
			}
			if (RE::PlayerCharacter::GetSingleton() == nullptr) {
				return;
			}
			const auto now = std::chrono::steady_clock::now();
			if (g_lastInstallAttempt != std::chrono::steady_clock::time_point{} && now - g_lastInstallAttempt < kInstallRetryInterval) {
				return;
			}
			g_lastInstallAttempt = now;
			g_installAttempts.fetch_add(1, std::memory_order_acq_rel);
			InstallOnLocalPlayer();
		}
	}

	void Tick()
	{
		if (!IsInstalled()) {
			TryInstallOnTick();
		}

		std::deque<std::string> outbound;
		{
			std::lock_guard lock{ g_outboundMutex };
			outbound.swap(g_outbound);
		}
		if (!outbound.empty() && F4T::Networking::IsConnectedToServer()) {
			for (const auto& name : outbound) {
				F4T::Networking::SendPlayerAnimEventPacket(name);
			}
		}

		std::deque<std::pair<std::uint32_t, std::string>> inbound;
		{
			std::lock_guard lock{ g_inboundMutex };
			inbound.swap(g_inbound);
		}
		for (const auto& [playerId, name] : inbound) {
			if (F4T::ShotReplay::IsRetryableAnimEvent(name)) {
				DropPendingReplays(playerId);
			}
			if (auto* proxy = F4T::ProxyActorController::TryGetProxyActorForRemotePlayerId(playerId)) {
				ReplayOnProxy(proxy, playerId, name);
			}
		}
		RetryPendingReplays();
	}

	void HandleRelayedPacket(const nlohmann::json& a_packet)
	{
		const auto playerId = F4T::ShotReplay::ParseRelayedPlayerId(a_packet);
		const auto name = F4T::ShotReplay::ParsePlayerAnimEvent(a_packet);
		if (!playerId || !name) {
			return;
		}
		std::lock_guard lock{ g_inboundMutex };
		if (g_inbound.size() < kMaximumQueued) {
			g_inbound.emplace_back(*playerId, *name);
		}
	}
}
