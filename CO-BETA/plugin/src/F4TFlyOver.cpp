#include "pch.h"

#include "F4TFlyOver.h"

#include "RE/A/Actor.h"
#include "RE/B/BSTSmartPointer.h"
#include "RE/C/ControlMap.h"
#include "RE/F/FreeCameraState.h"
#include "RE/H/HUDMenu.h"
#include "RE/P/PlayerCamera.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/P/PlayerControls.h"
#include "RE/T/TESCameraState.h"
#include "RE/U/UI.h"

#include <chrono>
#include <string_view>

namespace F4T::FlyOver
{
	namespace
	{
		constexpr double kHardTimeoutSeconds = 30.0;
		constexpr RE::CameraState kRejectedStates[]{
			RE::CameraStates::kVATS,
			RE::CameraStates::kFurniture,
			RE::CameraStates::kPCTransition,
			RE::CameraStates::kTween,
		};

		struct Takeover
		{
			RE::PlayerCamera*                      camera{ nullptr };
			RE::BSTSmartPointer<RE::TESCameraState> previousState{};
			RE::FreeCameraState*                   freeState{ nullptr };
			bool                                   previousEnabled{ false };
			bool                                   savedBlockPlayerInput{ false };
			bool                                   blockPlayerInputCaptured{ false };
			bool                                   inputContextPushed{ false };
			RE::NiPoint2                           savedMoveInput{};
			RE::NiPoint2                           savedLookInput{};
			bool                                   inputCaptured{ false };
			RE::HUDMenu*                           hudMenu{ nullptr };
			bool                                   savedHudVisible{ true };
			bool                                   hudCaptured{ false };
			bool                                   savedGhost{ false };
			bool                                   ghostCaptured{ false };
		};

		Path               g_path;
		Takeover           g_takeover;
		RestoreGuard       g_guard;
		bool               g_running{ false };
		bool               g_exiting{ false };
		double             g_startSeconds{ 0.0 };
		double             g_exitStartSeconds{ 0.0 };
		CameraSample       g_exitSample{};

		double NowSeconds()
		{
			using clock = std::chrono::steady_clock;
			static const auto epoch = clock::now();
			return std::chrono::duration<double>(clock::now() - epoch).count();
		}

		void ZeroFreeCameraInput(RE::FreeCameraState& a_state)
		{
			a_state.upDown = {};
			a_state.leftThumbstick = {};
			a_state.rightThumbstick = {};
			a_state.worldZDirection = 0;
		}

		void ZeroPlayerInput()
		{
			auto* controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				return;
			}
			controls->data.moveInputVec = {};
			controls->data.lookInputVec = {};
		}

		void WriteSample(const CameraSample& a_sample)
		{
			auto* freeState = g_takeover.freeState;
			if (!freeState) {
				return;
			}
			freeState->translation = RE::NiPoint3{ a_sample.x, a_sample.y, a_sample.z };
			freeState->rotation.x = a_sample.yaw;
			freeState->rotation.y = a_sample.pitch;
			ZeroFreeCameraInput(*freeState);
			ZeroPlayerInput();
		}

		bool CapturePlayerState()
		{
			auto* controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				return false;
			}
			g_takeover.savedBlockPlayerInput = controls->blockPlayerInput;
			g_takeover.blockPlayerInputCaptured = true;
			g_takeover.savedMoveInput = controls->data.moveInputVec;
			g_takeover.savedLookInput = controls->data.lookInputVec;
			g_takeover.inputCaptured = true;
			controls->blockPlayerInput = true;
			ZeroPlayerInput();
			return true;
		}

		void ReleasePlayerState()
		{
			auto* controls = RE::PlayerControls::GetSingleton();
			if (!controls) {
				return;
			}
			if (g_takeover.inputCaptured) {
				controls->data.moveInputVec = g_takeover.savedMoveInput;
				controls->data.lookInputVec = g_takeover.savedLookInput;
			}
			if (g_takeover.blockPlayerInputCaptured) {
				controls->blockPlayerInput = g_takeover.savedBlockPlayerInput;
			}
		}

		using SetGhostFn = void (*)(RE::Actor*, bool);

		void SetPlayerGhost(bool a_ghost)
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			// Constraint: SetGhost ids on OG, NG and AE; source:docs/code-notes/CO-BETA-plugin.md (live join fly-over camera).
#if defined(F4T_TARGET_OG)
			static REL::Relocation<SetGhostFn> func{ REL::ID{ 1377793 } };
#elif defined(F4T_TARGET_NG) || defined(F4T_TARGET_AE)
			static REL::Relocation<SetGhostFn> func{ REL::ID{ 2229669 } };
#else
#error "A Commonwealth Online runtime target is required"
#endif
			func(player, a_ghost);
		}

		void CaptureGhost()
		{
			auto* player = RE::PlayerCharacter::GetSingleton();
			if (!player) {
				return;
			}
			g_takeover.savedGhost = player->GetGhost();
			g_takeover.ghostCaptured = true;
			SetPlayerGhost(true);
		}

		void RestoreGhost()
		{
			if (g_takeover.ghostCaptured) {
				SetPlayerGhost(g_takeover.savedGhost);
				g_takeover.ghostCaptured = false;
			}
		}

		void ApplyHudHidden(bool a_hidden)
		{
			auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return;
			}
			const auto hud = ui->GetMenu<RE::HUDMenu>();
			if (!hud.get()) {
				return;
			}
			if (!g_takeover.hudCaptured) {
				g_takeover.hudMenu = hud.get();
				g_takeover.savedHudVisible = hud->menuCanBeVisible;
				g_takeover.hudCaptured = true;
			}
			hud->menuCanBeVisible = !a_hidden;
			hud->OnMenuDisplayStateChanged();
		}

		void RestoreHud()
		{
			if (!g_takeover.hudCaptured) {
				return;
			}
			if (g_takeover.hudMenu) {
				g_takeover.hudMenu->menuCanBeVisible = g_takeover.savedHudVisible;
				g_takeover.hudMenu->OnMenuDisplayStateChanged();
			}
			g_takeover.hudCaptured = false;
			g_takeover.hudMenu = nullptr;
		}

		bool CurrentCameraIsFree()
		{
			auto* camera = g_takeover.camera;
			if (!camera || !g_takeover.freeState) {
				return false;
			}
			const auto current = camera->GetCameraCurrentState();
			return current.get() == g_takeover.freeState;
		}

		bool StateIsSafeToTakeOver(const RE::PlayerCamera& a_camera)
		{
			for (const auto state : kRejectedStates) {
				if (a_camera.QCameraEquals(state)) {
					return false;
				}
			}
			return true;
		}

		void ClearTakeover()
		{
			g_takeover = {};
			g_running = false;
			g_exiting = false;
			g_exitSample = {};
		}

		bool InstallTakeover()
		{
			auto* camera = RE::PlayerCamera::GetSingleton();
			if (!camera || !StateIsSafeToTakeOver(*camera)) {
				return false;
			}
			g_takeover.camera = camera;
			g_takeover.previousState = camera->GetCameraCurrentState();
			if (!g_takeover.previousState) {
				ClearTakeover();
				return false;
			}
			g_takeover.previousEnabled = camera->enabled;

			camera->ToggleFreeCameraMode(false);
			camera->SetEnabled(true);

			auto* controlMap = RE::ControlMap::GetSingleton();
			if (!controlMap) {
				Restore(ExitReason::SessionTeardown);
				return false;
			}
			controlMap->PushInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kTFC);
			g_takeover.inputContextPushed = true;

			if (!CapturePlayerState()) {
				Restore(ExitReason::SessionTeardown);
				return false;
			}

			const auto freeState = camera->GetState<RE::FreeCameraState>();
			g_takeover.freeState = freeState.get();
			if (!g_takeover.freeState) {
				Restore(ExitReason::SessionTeardown);
				return false;
			}
			ZeroFreeCameraInput(*g_takeover.freeState);
			ApplyHudHidden(true);
			CaptureGhost();
			return true;
		}
	}

	bool HasPathForCurrentWorld()
	{
		Path probe;
		const auto load = BuildPathForPlayer(probe);
		return load.loaded;
	}

	bool Start()
	{
		if (g_running) {
			return true;
		}
		g_guard.MarkTakeoverInstalled();
		if (!BuildPathForPlayer(g_path).loaded) {
			g_guard.AbandonAfterWorldReset();
			return false;
		}
		if (!InstallTakeover()) {
			return false;
		}
		g_running = true;
		g_exiting = false;
		g_exitSample = {};
		g_startSeconds = NowSeconds();
		return true;
	}

	bool IsRunning()
	{
		return g_running;
	}

	double ElapsedSeconds()
	{
		return g_running ? NowSeconds() - g_startSeconds : 0.0;
	}

	std::uint32_t RestoreCount()
	{
		return g_guard.EngineRestores();
	}

	bool Tick()
	{
		if (!g_running) {
			return true;
		}
		if (g_exiting) {
			WriteSample(g_exitSample);
			return false;
		}
		const auto elapsed = ElapsedSeconds();
		WriteSample(g_path.Sample(elapsed));
		if (!CurrentCameraIsFree()) {
			Restore(ExitReason::SessionTeardown);
			return true;
		}
		if (elapsed >= kHardTimeoutSeconds) {
			Restore(ExitReason::WatchdogTimeout);
			return true;
		}
		return false;
	}

	void RequestExit()
	{
		if (!g_running || g_exiting) {
			return;
		}
		g_exiting = true;
		g_exitStartSeconds = NowSeconds();
		g_exitSample = g_path.Sample(g_exitStartSeconds - g_startSeconds);
	}

	bool ExitHoldElapsed(double a_holdSeconds)
	{
		if (!g_exiting) {
			return false;
		}
		return NowSeconds() - g_exitStartSeconds >= a_holdSeconds;
	}

	void Restore(ExitReason a_reason)
	{
		if (!g_guard.IsInstalled()) {
			ClearTakeover();
			return;
		}
		const bool worldReset = IsWorldReset(a_reason);
		const auto requested = g_guard.Restore(a_reason);
		if (requested && !worldReset) {
			auto* camera = g_takeover.camera;
			if (camera && g_takeover.freeState && CurrentCameraIsFree()) {
				camera->SetState(g_takeover.previousState.get());
			}
			if (camera) {
				camera->SetEnabled(g_takeover.previousEnabled);
			}
		}
		if (g_takeover.inputContextPushed) {
			if (auto* controlMap = RE::ControlMap::GetSingleton()) {
				controlMap->PopInputContext(RE::UserEvents::INPUT_CONTEXT_ID::kTFC);
			}
			g_takeover.inputContextPushed = false;
		}
		ReleasePlayerState();
		RestoreHud();
		if (!worldReset) {
			RestoreGhost();
		}
		ClearTakeover();
	}

	void Shutdown()
	{
		Restore(ExitReason::Quit);
	}
}
