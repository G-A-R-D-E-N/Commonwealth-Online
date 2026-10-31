#include "pch.h"

#include "F4TMenuInput.h"
#include "F4TCharacterCreator.h"
#include "F4TServerBrowserBridge.h"
#include "F4TMainMenuBridge.h"
#include "F4TPauseMenuBridge.h"
#include "F4TPrismaUI.h"

#include <REX/W32/XINPUT.h>

#include <Windows.h>
#include <algorithm>
#include <cmath>

namespace F4T::MenuInput
{
	namespace
	{
		std::atomic_bool g_browserActive{ false };
		std::atomic_bool g_mainMenuActive{ false };
		std::atomic_bool g_pauseMenuActive{ false };
		std::atomic_bool g_characterCreatorActive{ false };
		std::atomic_bool g_registered{ false };
		RE::BSTSmartPointer<RE::BSInputEnableLayer> g_menuInputBlockLayer;

		[[nodiscard]] bool AnyMenuActive()
		{
			return g_browserActive.load() || g_mainMenuActive.load() || g_pauseMenuActive.load() || g_characterCreatorActive.load();
		}

		using XInputGetStateFn = DWORD(WINAPI*)(DWORD, REX::W32::XINPUT_STATE*);

		struct GamepadButtonDef
		{
			std::uint16_t mask;
			const char*   name;
		};

		constexpr GamepadButtonDef kGamepadButtons[] = {
			{ REX::W32::XINPUT_GAMEPAD_DPAD_UP, "dpad_up" },
			{ REX::W32::XINPUT_GAMEPAD_DPAD_DOWN, "dpad_down" },
			{ REX::W32::XINPUT_GAMEPAD_DPAD_LEFT, "dpad_left" },
			{ REX::W32::XINPUT_GAMEPAD_DPAD_RIGHT, "dpad_right" },
			{ REX::W32::XINPUT_GAMEPAD_A, "a" },
			{ REX::W32::XINPUT_GAMEPAD_B, "b" },
			{ REX::W32::XINPUT_GAMEPAD_X, "x" },
			{ REX::W32::XINPUT_GAMEPAD_Y, "y" },
			{ REX::W32::XINPUT_GAMEPAD_LEFT_SHOULDER, "lb" },
			{ REX::W32::XINPUT_GAMEPAD_RIGHT_SHOULDER, "rb" },
		};

		bool g_buttonHeld[std::size(kGamepadButtons)]{};
		std::chrono::steady_clock::time_point g_lastButtonDownTime[std::size(kGamepadButtons)]{};
		const char* g_activeStickButton = nullptr;
		std::chrono::steady_clock::time_point g_stickRepeatTime{};
		float g_lastLeftX = 0.f;
		float g_lastLeftY = 0.f;
		float g_lastRightX = 0.f;
		float g_lastRightY = 0.f;
		bool g_axesWereActive = false;
		bool g_browserOwnedLastPoll = false;
		constexpr auto kStickInitialRepeatDelay = std::chrono::milliseconds(500);
		constexpr auto kStickRepeatInterval = std::chrono::milliseconds(180);
		constexpr auto kDigitalButtonDebounce = std::chrono::milliseconds(160);

		[[nodiscard]] float NormalizeThumbAxis(std::int16_t a_value, std::int16_t a_deadzone)
		{
			const auto absValue = std::abs(a_value);
			if (absValue <= a_deadzone) {
				return 0.f;
			}

			const float sign = a_value < 0 ? -1.f : 1.f;
			const float magnitude =
				(static_cast<float>(absValue) - static_cast<float>(a_deadzone)) /
				(32767.f - static_cast<float>(a_deadzone));
			return sign * std::clamp(magnitude, 0.f, 1.f);
		}

		void NotifyGamepadActivity()
		{
			REX::INFO("[MenuInput] Verified XInput gamepad activity; switching pointer mode.");
			F4T::PrismaUI::PreferGamepadPointerMode();
		}

		XInputGetStateFn GetXInputGetState()
		{
			static XInputGetStateFn fn = nullptr;
			static bool             tried = false;
			if (!tried) {
				tried = true;
				struct XInputDll
				{
					const wchar_t* wideName;
					const char*    logName;
				};
				const XInputDll dllNames[] = {
					{ L"xinput1_4.dll", "xinput1_4.dll" },
					{ L"xinput1_3.dll", "xinput1_3.dll" },
					{ L"xinput9_1_0.dll", "xinput9_1_0.dll" }
				};
				for (const auto& dll : dllNames) {
					const auto module = ::LoadLibraryExW(dll.wideName, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
					if (!module) {
						continue;
					}

					fn = reinterpret_cast<XInputGetStateFn>(::GetProcAddress(module, "XInputGetState"));
					if (fn) {
						REX::INFO("[MenuInput] Using {} for gamepad polling.", dll.logName);
						break;
					}
				}

				if (!fn) {
					REX::WARN("[MenuInput] XInputGetState unavailable; gamepad browser navigation disabled.");
				}
			}

			return fn;
		}

		const char* UserEventToButton(std::string_view a_userEvent)
		{
			if (a_userEvent == "Accept"sv) {
				return "a";
			}
			if (a_userEvent == "Cancel"sv || a_userEvent == "MenuCancel"sv) {
				return "b";
			}
			if (a_userEvent == "XButton"sv) {
				return "x";
			}
			if (a_userEvent == "YButton"sv) {
				return "y";
			}
			if (a_userEvent == "LeftShoulder"sv || a_userEvent == "LBumper"sv) {
				return "lb";
			}
			if (a_userEvent == "RightShoulder"sv || a_userEvent == "RBumper"sv) {
				return "rb";
			}
			if (a_userEvent == "Up"sv) {
				return "dpad_up";
			}
			if (a_userEvent == "Down"sv) {
				return "dpad_down";
			}
			if (a_userEvent == "Left"sv) {
				return "dpad_left";
			}
			if (a_userEvent == "Right"sv) {
				return "dpad_right";
			}

			return nullptr;
		}

		const char* ThumbstickDirectionToButton(RE::DIRECTION_VAL a_direction)
		{
			switch (a_direction) {
			case RE::DIRECTION_VAL::kUp:
				return "dpad_up";
			case RE::DIRECTION_VAL::kDown:
				return "dpad_down";
			case RE::DIRECTION_VAL::kLeft:
				return "dpad_left";
			case RE::DIRECTION_VAL::kRight:
				return "dpad_right";
			default:
				return nullptr;
			}
		}

		const char* StickAxesToButton(std::int16_t a_x, std::int16_t a_y)
		{
			constexpr auto deadzone = REX::W32::XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
			const auto       up = a_y > deadzone;
			const auto       down = a_y < -static_cast<std::int16_t>(deadzone);
			const auto       left = a_x < -static_cast<std::int16_t>(deadzone);
			const auto       right = a_x > deadzone;

			if (up && !left && !right) {
				return "dpad_up";
			}
			if (down && !left && !right) {
				return "dpad_down";
			}
			if (left && !up && !down) {
				return "dpad_left";
			}
			if (right && !up && !down) {
				return "dpad_right";
			}

			return nullptr;
		}

		void ForwardButtonPhase(const char* a_button, const char* a_phase)
		{
			if (!a_button || !a_phase) {
				return;
			}

			if (g_characterCreatorActive.load()) {
				F4T::CharacterCreator::DispatchControllerInput(a_button, a_phase);
			} else if (g_mainMenuActive.load()) {
				F4T::MainMenu::MainMenuBridge::ForwardControllerInput(a_button, a_phase);
			} else if (g_pauseMenuActive.load()) {
				F4T::PauseMenu::PauseMenuBridge::ForwardControllerInput(a_button, a_phase);
			} else if (g_browserActive.load()) {
				F4T::ServerBrowser::ServerBrowserBridge::ForwardControllerInput(a_button, a_phase);
			}
		}

		void SyncPolledGamepadBaseline()
		{
			for (auto& held : g_buttonHeld) {
				held = false;
			}
			g_activeStickButton = nullptr;
			g_stickRepeatTime = {};
			g_lastLeftX = 0.f;
			g_lastLeftY = 0.f;
			g_lastRightX = 0.f;
			g_lastRightY = 0.f;
			g_axesWereActive = false;

			const auto xinputGetState = GetXInputGetState();
			if (!xinputGetState) {
				return;
			}

			REX::W32::XINPUT_STATE state{};
			if (xinputGetState(0, std::addressof(state)) != ERROR_SUCCESS) {
				return;
			}

			const auto buttons = state.gamepad.buttons;
			for (std::size_t i = 0; i < std::size(kGamepadButtons); ++i) {
				g_buttonHeld[i] = (buttons & kGamepadButtons[i].mask) != 0;
			}

			g_activeStickButton = StickAxesToButton(state.gamepad.thumbLX, state.gamepad.thumbLY);
			g_stickRepeatTime = std::chrono::steady_clock::now();

			g_lastLeftX = NormalizeThumbAxis(
				state.gamepad.thumbLX, REX::W32::XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			g_lastLeftY = NormalizeThumbAxis(
				state.gamepad.thumbLY, REX::W32::XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
			g_lastRightX = NormalizeThumbAxis(
				state.gamepad.thumbRX, REX::W32::XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			g_lastRightY = NormalizeThumbAxis(
				state.gamepad.thumbRY, REX::W32::XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
			g_axesWereActive =
				g_lastLeftX != 0.f || g_lastLeftY != 0.f || g_lastRightX != 0.f || g_lastRightY != 0.f;
		}

		void AcquireMenuInputBlock()
		{
			if (g_menuInputBlockLayer) {
				return;
			}

			auto* inputEnableManager = RE::BSInputEnableManager::GetSingleton();
			if (!inputEnableManager) {
				REX::WARN("[MenuInput] BSInputEnableManager unavailable; main menu may keep gamepad focus.");
				return;
			}

			RE::BSTSmartPointer<RE::BSInputEnableLayer> layer;
			if (!inputEnableManager->AllocateNewLayer(layer, "CommonwealthOnline_ServerBrowser")) {
				REX::WARN("[MenuInput] Failed to allocate input layer for server browser.");
				return;
			}

			inputEnableManager->EnableUserEvent(
				layer->layerID,
				RE::UserEvents::USER_EVENT_FLAG::kMenu,
				false,
				RE::UserEvents::SENDER_ID::kScript);

			g_menuInputBlockLayer = std::move(layer);
		}

		void ReleaseMenuInputBlock()
		{
			g_menuInputBlockLayer.reset();
		}

	void PollGamepadInternal()
	{
		const bool browserFocused = g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerFocused();
		const bool browserOwned = g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerInputOwned();
		if (g_browserOwnedLastPoll && !browserOwned && g_browserActive.load()) {
			SyncPolledGamepadBaseline();
		}
		g_browserOwnedLastPoll = browserOwned;
		if (browserOwned || !AnyMenuActive() || !F4T::PrismaUI::IsGameForeground()) {
			return;
		}

		const auto xinputGetState = GetXInputGetState();
		if (!xinputGetState) {
			return;
		}

		REX::W32::XINPUT_STATE state{};
		if (xinputGetState(0, std::addressof(state)) != ERROR_SUCCESS) {
			return;
		}

		bool sawPadActivity = false;

		const auto buttons = state.gamepad.buttons;
		const auto pollTime = std::chrono::steady_clock::now();
		for (std::size_t i = 0; i < std::size(kGamepadButtons); ++i) {
			const auto down = (buttons & kGamepadButtons[i].mask) != 0;
			if (down != g_buttonHeld[i]) {
				if (down) {
					if (pollTime - g_lastButtonDownTime[i] < kDigitalButtonDebounce) {
						g_buttonHeld[i] = true;
						continue;
					}
					g_lastButtonDownTime[i] = pollTime;
					// Restore gamepad pointer mode before dispatching so JS back/select
					// handlers do not race a same-frame mouse-mode flip.
					NotifyGamepadActivity();
					sawPadActivity = true;
				}
				ForwardButtonPhase(kGamepadButtons[i].name, down ? "down" : "up");
				g_buttonHeld[i] = down;
			}
		}

		const auto leftX = NormalizeThumbAxis(
			state.gamepad.thumbLX, REX::W32::XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
		const auto leftY = NormalizeThumbAxis(
			state.gamepad.thumbLY, REX::W32::XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
		const auto rightX = NormalizeThumbAxis(
			state.gamepad.thumbRX, REX::W32::XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
		const auto rightY = NormalizeThumbAxis(
			state.gamepad.thumbRY, REX::W32::XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);

		constexpr float kAxisEpsilon = 0.02f;
		const bool axesActive = leftX != 0.f || leftY != 0.f || rightX != 0.f || rightY != 0.f;
		const bool axesChanged =
			std::fabs(leftX - g_lastLeftX) > kAxisEpsilon ||
			std::fabs(leftY - g_lastLeftY) > kAxisEpsilon ||
			std::fabs(rightX - g_lastRightX) > kAxisEpsilon ||
			std::fabs(rightY - g_lastRightY) > kAxisEpsilon;

		const bool axesBecameActive = axesActive && !g_axesWereActive;
		if (axesBecameActive && !sawPadActivity) {
			NotifyGamepadActivity();
		}

		if (g_pauseMenuActive.load() && (axesChanged || (g_axesWereActive && !axesActive))) {
			g_lastLeftX = leftX;
			g_lastLeftY = leftY;
			g_lastRightX = rightX;
			g_lastRightY = rightY;
			g_axesWereActive = axesActive;
			F4T::PauseMenu::PauseMenuBridge::ForwardControllerAxes(leftX, leftY, rightX, rightY);
		} else if (axesChanged) {
			g_lastLeftX = leftX;
			g_lastLeftY = leftY;
			g_lastRightX = rightX;
			g_lastRightY = rightY;
			g_axesWereActive = axesActive;
		}

		if (browserFocused) {
			g_activeStickButton = nullptr;
			return;
		}
		const auto* stickButton = StickAxesToButton(state.gamepad.thumbLX, state.gamepad.thumbLY);
		if (stickButton != g_activeStickButton) {
			if (g_activeStickButton) {
				ForwardButtonPhase(g_activeStickButton, "up");
			}

			g_activeStickButton = stickButton;
			if (stickButton) {
				ForwardButtonPhase(stickButton, "down");
			}

			g_stickRepeatTime = std::chrono::steady_clock::now();
			return;
		}

		if (!stickButton) {
			return;
		}

		const auto now = std::chrono::steady_clock::now();
		if (now - g_stickRepeatTime < kStickInitialRepeatDelay) {
			return;
		}

		ForwardButtonPhase(stickButton, "down");
		g_stickRepeatTime = now - (kStickInitialRepeatDelay - kStickRepeatInterval);
	}
	}

	bool ForwardButtonEvent(const RE::ButtonEvent* a_event)
	{
		if ((g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerInputOwned()) || !a_event || !AnyMenuActive()) {
			return false;
		}

		if (a_event->device == RE::INPUT_DEVICE::kGamepad) {
			return false;
		}

		const auto* buttonName = UserEventToButton(a_event->QUserEvent());
		if (!buttonName) {
			return false;
		}

		const char* phase = nullptr;
		if (a_event->QJustPressed()) {
			phase = "down";
		} else if (a_event->QReleased()) {
			phase = "up";
		}

		if (phase) {
			ForwardButtonPhase(buttonName, phase);
			return true;
		}

		return false;
	}

	bool ForwardThumbstickEvent(const RE::ThumbstickEvent* a_event)
	{
		if (!a_event || !AnyMenuActive()) {
			return false;
		}

		if (a_event->idCode != static_cast<std::int32_t>(RE::ThumbstickEvent::THUMBSTICK_ID::kLeft)) {
			return false;
		}

		if (a_event->currDir == a_event->prevDir) {
			return false;
		}

		bool forwarded = false;
		if (a_event->prevDir != RE::DIRECTION_VAL::kNone) {
			if (const auto* buttonName = ThumbstickDirectionToButton(a_event->prevDir)) {
				ForwardButtonPhase(buttonName, "up");
				forwarded = true;
			}
		}

		if (a_event->currDir != RE::DIRECTION_VAL::kNone) {
			if (const auto* buttonName = ThumbstickDirectionToButton(a_event->currDir)) {
				ForwardButtonPhase(buttonName, "down");
				forwarded = true;
			}
		}

		return forwarded;
	}

	bool IsGamepadConnected()
	{
		const auto* inputDeviceManager = RE::BSInputDeviceManager::GetSingleton();
		return inputDeviceManager && inputDeviceManager->IsGamepadConnected();
	}

	void LogInputState()
	{
		const bool engineConnected = IsGamepadConnected();
		bool xinputConnected = false;
		if (const auto xinputGetState = GetXInputGetState()) {
			REX::W32::XINPUT_STATE state{};
			xinputConnected = xinputGetState(0, std::addressof(state)) == ERROR_SUCCESS;
		}

		REX::INFO(
			"[MenuInput] Input state engineConnected={} xinputConnected={} startupPointerMode=mouseUntilActivity",
			engineConnected,
			xinputConnected);
	}

	void Register()
	{
		if (g_registered.exchange(true)) {
			return;
		}

		REX::INFO("[MenuInput] Browser gamepad forwarding registered.");
	}

	void PollGamepad()
	{
		PollGamepadInternal();
	}

	void SetBrowserActive(bool a_active)
	{
		g_browserActive.store(a_active);
		if (a_active) {
			AcquireMenuInputBlock();
			return;
		}
		for (auto& held : g_buttonHeld) {
			held = false;
		}
		g_activeStickButton = nullptr;
		g_stickRepeatTime = {};
		g_lastLeftX = 0.f;
		g_lastLeftY = 0.f;
		g_lastRightX = 0.f;
		g_lastRightY = 0.f;
		g_axesWereActive = false;
		if (AnyMenuActive()) {
			SyncPolledGamepadBaseline();
		} else {
			ReleaseMenuInputBlock();
		}
	}

	void SetMainMenuActive(bool a_active)
	{
		g_mainMenuActive.store(a_active);
		if (a_active) {
			SyncPolledGamepadBaseline();
			AcquireMenuInputBlock();
			return;
		}

		for (auto& held : g_buttonHeld) {
			held = false;
		}
		g_activeStickButton = nullptr;
		g_stickRepeatTime = {};
		g_lastLeftX = 0.f;
		g_lastLeftY = 0.f;
		g_lastRightX = 0.f;
		g_lastRightY = 0.f;
		g_axesWereActive = false;
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}

	void SetPauseMenuActive(bool a_active)
	{
		g_pauseMenuActive.store(a_active);
		if (a_active) {
			SyncPolledGamepadBaseline();
			AcquireMenuInputBlock();
			return;
		}

		for (auto& held : g_buttonHeld) {
			held = false;
		}
		g_activeStickButton = nullptr;
		g_stickRepeatTime = {};
		g_lastLeftX = 0.f;
		g_lastLeftY = 0.f;
		g_lastRightX = 0.f;
		g_lastRightY = 0.f;
		g_axesWereActive = false;
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}

	void SetCharacterCreatorActive(bool a_active)
	{
		g_characterCreatorActive.store(a_active);
		if (a_active) {
			SyncPolledGamepadBaseline();
			AcquireMenuInputBlock();
			return;
		}

		for (auto& held : g_buttonHeld) {
			held = false;
		}
		g_activeStickButton = nullptr;
		g_stickRepeatTime = {};
		g_lastLeftX = 0.f;
		g_lastLeftY = 0.f;
		g_lastRightX = 0.f;
		g_lastRightY = 0.f;
		g_axesWereActive = false;
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}
}
