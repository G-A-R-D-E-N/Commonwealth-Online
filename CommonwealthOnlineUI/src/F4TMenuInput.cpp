#include "pch.h"

#include "F4TAlertSystem.h"
#include "F4TMenuInput.h"
#include "F4TServerBrowserBridge.h"
#include "F4TMainMenuBridge.h"
#include "F4TPauseMenuBridge.h"
#include "F4TAdminView.h"
#include "F4TChatView.h"
#include "F4TFactionView.h"
#include "F4TInfoViews.h"
#include "F4TQuestView.h"
#include "F4TTradeView.h"
#include "F4TPrismaUI.h"

#include <REX/W32/XINPUT.h>

#include <Windows.h>
#include <algorithm>
#include <chrono>
#include <cmath>

namespace F4T::MenuInput
{
	namespace
	{
		std::atomic_bool g_browserActive{ false };
		std::atomic_bool g_mainMenuActive{ false };
		std::atomic_bool g_pauseMenuActive{ false };
		std::atomic_bool g_wheelActive{ false };
		std::atomic_bool g_worldPanelActive{ false };
		std::atomic_bool g_tradeActive{ false };
		std::atomic_bool g_sleepActive{ false };
		std::atomic_bool g_characterSelectActive{ false };
		std::atomic_bool g_chatActive{ false };
		std::atomic_bool g_registered{ false };
		RE::BSTSmartPointer<RE::BSInputEnableLayer> g_menuInputBlockLayer;
		RE::BSTSmartPointer<RE::BSInputEnableLayer> g_wheelInputBlockLayer;
		RE::BSTSmartPointer<RE::BSInputEnableLayer> g_worldPanelInputBlockLayer;

		[[nodiscard]] bool AnyMenuActive()
		{
			return g_browserActive.load() || g_mainMenuActive.load() || g_pauseMenuActive.load() || g_wheelActive.load() || g_worldPanelActive.load() || g_tradeActive.load() || g_sleepActive.load() || g_characterSelectActive.load() || g_chatActive.load();
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

		// Constraint: the first kDpadButtonCount kGamepadButtons entries are the D-pad, the only buttons that repeat.
		constexpr std::size_t kDpadButtonCount = 4;
		bool g_buttonHeld[std::size(kGamepadButtons)]{};
		std::chrono::steady_clock::time_point g_lastButtonDownTime[std::size(kGamepadButtons)]{};
		std::chrono::steady_clock::time_point g_buttonRepeatTime[std::size(kGamepadButtons)]{};
		const char* g_activeStickButton = nullptr;
		std::chrono::steady_clock::time_point g_stickRepeatTime{};
		float g_lastLeftX = 0.f;
		float g_lastLeftY = 0.f;
		float g_lastRightX = 0.f;
		float g_lastRightY = 0.f;
		bool g_axesWereActive = false;
		bool g_browserOwnedLastPoll = false;
		bool g_alertAnswersLastPoll = false;
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

		bool ReadPadState(REX::W32::XINPUT_STATE& a_state)
		{
			static DWORD padIndex = 0xFFFFFFFF;
			static std::chrono::steady_clock::time_point nextScan{};
			const auto xinputGetState = GetXInputGetState();
			if (!xinputGetState) {
				return false;
			}
			if (padIndex != 0xFFFFFFFF && xinputGetState(padIndex, std::addressof(a_state)) == ERROR_SUCCESS) {
				return true;
			}
			padIndex = 0xFFFFFFFF;
			const auto now = std::chrono::steady_clock::now();
			if (now < nextScan) {
				return false;
			}
			nextScan = now + std::chrono::seconds(1);
			for (DWORD i = 0; i < 4; ++i) {
				if (xinputGetState(i, std::addressof(a_state)) == ERROR_SUCCESS) {
					padIndex = i;
					return true;
				}
			}
			return false;
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

			if (g_mainMenuActive.load()) {
				F4T::MainMenu::MainMenuBridge::ForwardControllerInput(a_button, a_phase);
			} else if (g_pauseMenuActive.load() || g_wheelActive.load() || g_tradeActive.load() || g_sleepActive.load() || g_characterSelectActive.load() || g_chatActive.load()) {
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

			REX::W32::XINPUT_STATE state{};
			if (!ReadPadState(state)) {
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

		// Constraint: the wheel opens in unpaused gameplay, so it needs its own layer that stops movement, look, Activate and attack.
		constexpr RE::UserEvents::USER_EVENT_FLAG kBlockedWheelEvents[] = {
			RE::UserEvents::USER_EVENT_FLAG::kMovement,
			RE::UserEvents::USER_EVENT_FLAG::kLooking,
			RE::UserEvents::USER_EVENT_FLAG::kActivate,
			RE::UserEvents::USER_EVENT_FLAG::kFighting,
			RE::UserEvents::USER_EVENT_FLAG::kMenu,
		};
		void AcquireInputBlock(RE::BSTSmartPointer<RE::BSInputEnableLayer>& a_layer, const char* a_layerName)
		{
			if (a_layer) {
				return;
			}

			auto* inputEnableManager = RE::BSInputEnableManager::GetSingleton();
			if (!inputEnableManager) {
				REX::WARN("[MenuInput] BSInputEnableManager unavailable; input layer {} not applied.", a_layerName);
				return;
			}

			RE::BSTSmartPointer<RE::BSInputEnableLayer> layer;
			if (!inputEnableManager->AllocateNewLayer(layer, a_layerName)) {
				REX::WARN("[MenuInput] Failed to allocate input layer {}.", a_layerName);
				return;
			}

			for (const auto flag : kBlockedWheelEvents) {
				inputEnableManager->EnableUserEvent(
					layer->layerID,
					flag,
					false,
					RE::UserEvents::SENDER_ID::kScript);
			}

			a_layer = std::move(layer);
		}

		void AcquireWheelInputBlock()
		{
			AcquireInputBlock(g_wheelInputBlockLayer, "CommonwealthOnline_InteractWheel");
		}

		void ReleaseWheelInputBlock()
		{
			g_wheelInputBlockLayer.reset();
		}

	[[nodiscard]] bool AnyScrollViewVisible()
	{
		return F4T::FactionView::IsCurrentlyVisible() || F4T::QuestView::IsCurrentlyVisible() || F4T::AdminView::IsCurrentlyVisible() || F4T::TradeView::IsCurrentlyVisible() || F4T::InfoViews::IsCurrentlyVisible();
	}

	void ForwardScrollAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY)
	{
		F4T::FactionView::ForwardControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
		F4T::QuestView::ForwardControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
		F4T::AdminView::ForwardControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
		F4T::TradeView::ForwardControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
		F4T::InfoViews::ForwardControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
	}

	void ForwardMovedAxes(float a_leftX, float a_leftY, float a_rightX, float a_rightY, bool a_axesActive)
	{
		g_lastLeftX = a_leftX;
		g_lastLeftY = a_leftY;
		g_lastRightX = a_rightX;
		g_lastRightY = a_rightY;
		g_axesWereActive = a_axesActive;
		if (g_pauseMenuActive.load() || g_wheelActive.load()) {
			F4T::PauseMenu::PauseMenuBridge::ForwardControllerAxes(a_leftX, a_leftY, a_rightX, a_rightY);
		}
		ForwardScrollAxes(a_leftX, a_leftY, a_rightX, a_rightY);
	}

	void PollChatChord()
	{
		static bool chordWasDown = false;
		REX::W32::XINPUT_STATE state{};
		if (!ReadPadState(state)) {
			chordWasDown = false;
			return;
		}
		constexpr std::uint16_t kChordMask = REX::W32::XINPUT_GAMEPAD_LEFT_SHOULDER | REX::W32::XINPUT_GAMEPAD_RIGHT_SHOULDER | REX::W32::XINPUT_GAMEPAD_DPAD_UP;
		const bool chordDown = (state.gamepad.buttons & kChordMask) == kChordMask;
		if (chordDown && !chordWasDown && !F4T::ChatView::IsOpen()) {
			F4T::ChatView::Open("ControllerChord");
		}
		chordWasDown = chordDown;
	}

	void PollGamepadInternal()
	{
		const bool browserFocused = g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerFocused();
		const bool browserOwned = g_browserActive.load() && F4T::PrismaUI::IsBrowserControllerInputOwned();
		if (g_browserOwnedLastPoll && !browserOwned && g_browserActive.load()) {
			SyncPolledGamepadBaseline();
		}
		g_browserOwnedLastPoll = browserOwned;
		const bool menuActive = AnyMenuActive();
		const bool alertAnswers = !menuActive && F4T::Alerts::AcceptsControllerAnswers();
		if (alertAnswers && !g_alertAnswersLastPoll) {
			SyncPolledGamepadBaseline();
		}
		g_alertAnswersLastPoll = alertAnswers;
		if (!F4T::PrismaUI::IsGameForeground()) {
			return;
		}
		if (!menuActive && !alertAnswers && !AnyScrollViewVisible()) {
			PollChatChord();
			return;
		}
		if (browserOwned) {
			return;
		}

		REX::W32::XINPUT_STATE state{};
		if (!ReadPadState(state)) {
			return;
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
		const bool axesMoved = axesChanged || (g_axesWereActive && !axesActive);
		const bool axesBecameActive = axesActive && !g_axesWereActive;

		if (!menuActive && !alertAnswers) {
			if (axesMoved) {
				ForwardMovedAxes(leftX, leftY, rightX, rightY, axesActive);
			}
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
					g_buttonRepeatTime[i] = pollTime;
					if (alertAnswers) {
						F4T::Alerts::ResolveControllerButton(kGamepadButtons[i].name);
						g_buttonHeld[i] = true;
						continue;
					}
					// Restore gamepad pointer mode before dispatching so JS back/select
					// handlers do not race a same-frame mouse-mode flip.
					NotifyGamepadActivity();
					sawPadActivity = true;
				}
				ForwardButtonPhase(kGamepadButtons[i].name, down ? "down" : "up");
				g_buttonHeld[i] = down;
			} else if (down && i < kDpadButtonCount && pollTime - g_buttonRepeatTime[i] >= kStickInitialRepeatDelay) {
				ForwardButtonPhase(kGamepadButtons[i].name, "down");
				g_buttonRepeatTime[i] = pollTime - (kStickInitialRepeatDelay - kStickRepeatInterval);
			}
		}

		if (axesBecameActive && !sawPadActivity && !alertAnswers) {
			NotifyGamepadActivity();
		}

		if (axesMoved) {
			ForwardMovedAxes(leftX, leftY, rightX, rightY, axesActive);
		}
		if (alertAnswers) {
			return;
		}

		if (browserFocused) {
			g_activeStickButton = nullptr;
			return;
		}
		// Constraint: the wheel selects by stick angle, so the stick must not also emit dpad steps.
		if (g_wheelActive.load()) {
			if (g_activeStickButton) {
				ForwardButtonPhase(g_activeStickButton, "up");
				g_activeStickButton = nullptr;
			}
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

	bool IsGamepadConnected()
	{
		const auto* inputDeviceManager = RE::BSInputDeviceManager::GetSingleton();
		return inputDeviceManager && inputDeviceManager->IsGamepadConnected();
	}

	void LogInputState()
	{
		const bool engineConnected = IsGamepadConnected();
		bool xinputConnected = false;
		REX::W32::XINPUT_STATE state{};
		xinputConnected = ReadPadState(state);

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

	bool IsPauseMenuActive()
	{
		return g_pauseMenuActive.load();
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

	void SetSleepActive(bool a_active)
	{
		g_sleepActive.store(a_active);
		if (a_active) {
			SyncPolledGamepadBaseline();
			AcquireMenuInputBlock();
			return;
		}
		SyncPolledGamepadBaseline();
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}

	void SetTradeActive(bool a_active)
	{
		g_tradeActive.store(a_active);
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

	void SetCharacterSelectActive(bool a_active)
	{
		g_characterSelectActive.store(a_active);
		SyncPolledGamepadBaseline();
		if (a_active) {
			AcquireMenuInputBlock();
			return;
		}
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}

	void SetChatActive(bool a_active)
	{
		if (g_chatActive.exchange(a_active) == a_active) {
			return;
		}
		SyncPolledGamepadBaseline();
		if (a_active) {
			AcquireMenuInputBlock();
			return;
		}
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}

	void SetWheelActive(bool a_active)
	{
		g_wheelActive.store(a_active);
		if (a_active) {
			SyncPolledGamepadBaseline();
			AcquireWheelInputBlock();
			return;
		}

		ReleaseWheelInputBlock();

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

	void SetWorldPanelActive(bool a_active)
	{
		if (g_worldPanelActive.exchange(a_active) == a_active) {
			return;
		}
		if (a_active) {
			SyncPolledGamepadBaseline();
			AcquireInputBlock(g_worldPanelInputBlockLayer, "CommonwealthOnline_FactionWorld");
			return;
		}
		g_worldPanelInputBlockLayer.reset();
		SyncPolledGamepadBaseline();
		if (!AnyMenuActive()) {
			ReleaseMenuInputBlock();
		}
	}
}
