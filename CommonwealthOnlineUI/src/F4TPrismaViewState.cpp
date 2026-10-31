#include "pch.h"

#include "F4TPrismaCapabilities.h"
#include "F4TPrismaViewState.h"
#include "SessionCallbacks.h"

#include <format>
#include <string>

namespace F4T::PrismaViewState
{
	namespace
	{
		std::string GetLogPrefix(const char* a_name)
		{
			return std::string{ COUI::Session::GetLogPrefix() } + " [PrismaUI] [" +
				(a_name ? a_name : "View") + "]";
		}

		void LogInfo(const char* a_name, std::string_view a_message)
		{
			const auto message = GetLogPrefix(a_name) + " " + std::string(a_message);
			REX::INFO(std::string_view{ message });
		}

		void LogWarn(const char* a_name, std::string_view a_message)
		{
			const auto message = GetLogPrefix(a_name) + " " + std::string(a_message);
			REX::WARN(std::string_view{ message });
		}

		void LogCritical(const char* a_name, std::string_view a_message)
		{
			const auto message = GetLogPrefix(a_name) + " " + std::string(a_message);
			REX::CRITICAL(std::string_view{ message });
		}
	}

	Controller::Controller(const char* a_name) noexcept :
		m_name(a_name && a_name[0] ? a_name : "View")
	{
	}

	void Controller::SetApi(PRISMA_UI_API::IVPrismaUI1* a_api) noexcept
	{
		m_api = a_api;
	}

	void Controller::SetFocusArgs(bool a_pauseGame, bool a_disableFocusMenu) noexcept
	{
		m_focusPauseGame = a_pauseGame;
		m_focusDisableFocusMenu = a_disableFocusMenu;
	}

	void Controller::SetDesiredRole(PRISMA_UI_API::ViewRole a_role) noexcept
	{
		if (m_desiredRole == a_role) {
			return;
		}
		m_desiredRole = a_role;
		m_roleAssigned = false;
		m_roleFailureLogged = false;
	}

	const char* Controller::GetName() const noexcept
	{
		return m_name;
	}

	PrismaView Controller::GetHandle() const noexcept
	{
		return m_handle;
	}

	bool Controller::IsDomReady() const noexcept
	{
		return m_domReady;
	}

	bool Controller::IsDesiredVisible() const noexcept
	{
		return m_desiredVisible;
	}

	bool Controller::IsDesiredFocused() const noexcept
	{
		return m_desiredFocused;
	}

	bool Controller::IsCurrentlyVisible() const noexcept
	{
		return m_currentlyVisible;
	}

	bool Controller::IsCurrentlyFocused() const noexcept
	{
		return m_currentlyFocused && m_api && m_handle != 0 &&
			m_api->IsValid(m_handle) && m_api->HasFocus(m_handle);
	}

	bool Controller::IsHandleValid() const noexcept
	{
		return m_api != nullptr && m_handle != 0 && m_api->IsValid(m_handle);
	}

	bool Controller::IsRoleAssigned() const noexcept
	{
		return m_roleAssigned;
	}

	bool Controller::DidCreateFail() const noexcept
	{
		return m_createFailed;
	}

	bool Controller::CanRetryCreate() const noexcept
	{
		return m_createFailed && m_createRetryAllowed && m_createAttempts < 2;
	}

	PRISMA_UI_API::ViewRole Controller::GetDesiredRole() const noexcept
	{
		return m_desiredRole;
	}

	void Controller::SetDesiredVisible(bool a_visible) noexcept
	{
		m_desiredVisible = a_visible;
	}

	void Controller::SetDesiredFocused(bool a_focused) noexcept
	{
		m_desiredFocused = a_focused;
	}

	void Controller::SetDesired(bool a_visible, bool a_focused) noexcept
	{
		m_desiredVisible = a_visible;
		m_desiredFocused = a_focused;
	}

	void Controller::AllowCreateRetry() noexcept
	{
		if (m_createFailed && m_createAttempts < 2) {
			m_createRetryAllowed = true;
			LogInfo(m_name, "CreateView retry permitted (single later attempt)");
		}
	}

	bool Controller::AssignRoleIfNeeded() noexcept
	{
		if (m_roleAssigned || m_handle == 0 || !IsHandleValid()) {
			return m_roleAssigned;
		}

		if (m_desiredRole == PRISMA_UI_API::ViewRole::kUnspecified) {
			return false;
		}

		if (!F4T::PrismaCapabilities::HasV10()) {
			return false;
		}

		if (!F4T::PrismaCapabilities::TrySetViewRole(m_handle, m_desiredRole)) {
			if (!m_roleFailureLogged) {
				m_roleFailureLogged = true;
				LogWarn(m_name, std::format(
					"Role assignment rejected handle={} desiredRole={} runtimeRole={} createAttempt={}",
					m_handle,
					F4T::PrismaCapabilities::RoleName(m_desiredRole),
					F4T::PrismaCapabilities::RoleName(F4T::PrismaCapabilities::TryGetViewRole(m_handle)),
					m_createAttempts));
			}
			return false;
		}

		m_roleAssigned = true;
		m_roleFailureLogged = false;
		LogInfo(m_name, std::format(
			"Assigned role={} handle={}",
			F4T::PrismaCapabilities::RoleName(m_desiredRole),
			m_handle));
		return true;
	}

	PrismaView Controller::EnsureCreated(
		const char* a_htmlPath,
		PRISMA_UI_API::OnDomReadyCallback a_onDomReady,
		bool a_runtimeReady) noexcept
	{
		if (!m_api) {
			LogWarn(m_name, "EnsureCreated skipped — API unavailable.");
			return 0;
		}

		if (!a_runtimeReady) {
			return 0;
		}

		if (m_handle != 0 && m_api->IsValid(m_handle)) {
			AssignRoleIfNeeded();
			return m_handle;
		}

		// Bound CreateView retries: after a zero-handle failure, wait for AllowCreateRetry.
		if (m_createFailed && !m_createRetryAllowed) {
			return 0;
		}

		if (m_createAttempts >= 2) {
			return 0;
		}

		// Preserve desiredVisible / desiredFocused / desiredRole across recreate.
		m_domReady = false;
		m_currentlyVisible = false;
		m_currentlyFocused = false;
		m_roleAssigned = false;
		m_roleFailureLogged = false;
		m_showCalled = false;
		m_focusCalled = false;
		m_focusSucceeded = false;
		m_handle = 0;
		m_createRetryAllowed = false;
		++m_createAttempts;

		LogInfo(m_name, std::format(
			"Calling CreateView path='{}' attempt={} desiredVisible={} desiredFocused={} desiredRole={}",
			a_htmlPath ? a_htmlPath : "",
			m_createAttempts,
			m_desiredVisible,
			m_desiredFocused,
			F4T::PrismaCapabilities::RoleName(m_desiredRole)));

		m_handle = m_api->CreateView(a_htmlPath, a_onDomReady);
		ResetViewCursorPolicyCache();

		LogInfo(m_name, std::format(
			"CreateView complete handle={} valid={}",
			m_handle,
			m_handle != 0 && m_api->IsValid(m_handle)));

		if (m_handle == 0) {
			m_createFailed = true;
			LogCritical(m_name, std::format(
				"CreateView returned handle=0 (attempt={}). Desired state preserved; vanilla UI left usable. No Show/Focus/Hide/Unfocus on this handle.",
				m_createAttempts));
			return 0;
		}

		m_createFailed = false;

		// Assign role immediately after successful create when V10 is available.
		AssignRoleIfNeeded();

		// Do not assume Prisma creation visibility. Force a known hidden baseline until
		// DomReady + ApplyState can honor desiredVisible/desiredFocused.
		LogInfo(m_name, std::format("Calling Hide(handle={}) reason=PostCreateBaseline", m_handle));
		m_api->Hide(m_handle);
		m_currentlyVisible = false;
		m_currentlyFocused = false;
		LogInfo(m_name, "Hide complete");

		return m_handle;
	}

	bool Controller::MarkDomReady(PrismaView a_view) noexcept
	{
		if (a_view == 0 || a_view != m_handle) {
			LogWarn(m_name, std::format(
				"Ignoring stale DomReady for handle={} (current={})",
				a_view,
				m_handle));
			return false;
		}

		m_domReady = true;
		AssignRoleIfNeeded();
		LogInfo(m_name, std::format("DOM ready handle={}", m_handle));
		return true;
	}

	void Controller::InvalidateApplied() noexcept
	{
		m_domReady = false;
		m_currentlyVisible = false;
		m_currentlyFocused = false;
		m_roleAssigned = false;
		m_roleFailureLogged = false;
		m_showCalled = false;
		m_focusCalled = false;
		m_focusSucceeded = false;
	}

	void Controller::ClearHandle() noexcept
	{
		m_handle = 0;
		InvalidateApplied();
	}

	static bool s_haveAppliedCursorPolicy = false;
	static PrismaView s_appliedCursorPolicyView = 0;
	static PRISMA_UI_API::CursorPolicy s_appliedCursorPolicy = PRISMA_UI_API::CursorPolicy::Default;

	void ResetViewCursorPolicyCache()
	{
		s_haveAppliedCursorPolicy = false;
	}

	void SetViewCursorPolicyCached(PrismaView a_view, PRISMA_UI_API::CursorPolicy a_policy)
	{
		if (s_haveAppliedCursorPolicy && s_appliedCursorPolicyView == a_view && s_appliedCursorPolicy == a_policy) {
			return;
		}
		if (!PRISMA_UI_API::SetViewCursorPolicy(a_view, a_policy)) {
			return;
		}
		s_haveAppliedCursorPolicy = true;
		s_appliedCursorPolicyView = a_view;
		s_appliedCursorPolicy = a_policy;
	}

	ApplyResult Controller::ApplyState(std::string_view a_reason) noexcept
	{
		ApplyResult result{};

		if (!m_api) {
			result.pending = true;
			result.pendingReason = "ApiUnavailable";
			LogInfo(m_name, FormatStateLine(a_reason));
			LogInfo(m_name, "ApplyState pending — API unavailable");
			return result;
		}

		if (m_handle == 0) {
			result.pending = true;
			result.pendingReason = "HandleZero";
			LogInfo(m_name, FormatStateLine(a_reason));
			LogInfo(m_name, "ApplyState pending — handle is 0 (CreateView failed or not yet created)");
			return result;
		}

		if (!m_api->IsValid(m_handle)) {
			result.pending = true;
			result.pendingReason = "HandleInvalid";
			LogInfo(m_name, FormatStateLine(a_reason));
			LogInfo(m_name, std::format(
				"ApplyState pending — view invalid handle={}",
				m_handle));
			return result;
		}

		AssignRoleIfNeeded();

		if (!m_domReady) {
			result.pending = true;
			result.pendingReason = "DomNotReady";
			LogInfo(m_name, FormatStateLine(a_reason));
			LogInfo(m_name, "ApplyState pending — DOM not ready");
			return result;
		}

		const bool alreadyVisible = m_currentlyVisible;
		const bool alreadyFocused = m_currentlyFocused && m_api->HasFocus(m_handle);
		const bool needsShow = m_desiredVisible && !alreadyVisible;
		const bool needsHide = !m_desiredVisible && (alreadyVisible || !m_api->IsHidden(m_handle));
		const bool needsFocus = m_desiredVisible && m_desiredFocused && !alreadyFocused;
		const bool needsUnfocus =
			(m_desiredVisible && !m_desiredFocused && (m_currentlyFocused || m_api->HasFocus(m_handle))) ||
			(!m_desiredVisible && (m_currentlyFocused || m_desiredFocused || m_api->HasFocus(m_handle)));

		if (!needsShow && !needsHide && !needsFocus && !needsUnfocus) {
			result.applied = true;
			return result;
		}

		LogInfo(m_name, FormatStateLine(a_reason));

		if (m_desiredVisible) {
			if (needsShow) {
				LogInfo(m_name, std::format("Calling Show(handle={})", m_handle));
				m_api->Show(m_handle);
				m_currentlyVisible = true;
				m_showCalled = true;
				result.showRequested = true;
				LogInfo(m_name, "Show complete");
			}

			if (needsFocus) {
				auto* apiV10 = F4T::PrismaCapabilities::GetV10();
				const bool useOverlayFocus =
					m_desiredRole == PRISMA_UI_API::ViewRole::kWidget &&
					F4T::PrismaCapabilities::HasInputRegions() &&
					apiV10 != nullptr;
				LogInfo(m_name, std::format(
					"Calling {}(handle={} pauseGame={} disableFocusMenu={})",
					useOverlayFocus ? "FocusOverlay" : "Focus",
					m_handle,
					m_focusPauseGame,
					m_focusDisableFocusMenu));
				m_focusCalled = true;
				const bool focused = useOverlayFocus
					? apiV10->FocusOverlay(m_handle, m_focusPauseGame, m_focusDisableFocusMenu)
					: m_api->Focus(m_handle, m_focusPauseGame, m_focusDisableFocusMenu);
				m_currentlyFocused = focused;
				m_focusSucceeded = focused;
				result.focusRequested = true;
				LogInfo(m_name, std::format("Focus complete success={}", focused));
				if (!focused) {
					LogWarn(m_name, "Focus returned false — currentlyFocused left false");
				}
			} else if (needsUnfocus) {
				LogInfo(m_name, std::format("Calling Unfocus(handle={})", m_handle));
				m_api->Unfocus(m_handle);
				m_currentlyFocused = false;
				m_focusSucceeded = false;
				result.unfocusRequested = true;
				LogInfo(m_name, "Unfocus complete");
			}
		} else {
			if (needsUnfocus) {
				LogInfo(m_name, std::format("Calling Unfocus(handle={})", m_handle));
				m_api->Unfocus(m_handle);
				m_currentlyFocused = false;
				m_focusSucceeded = false;
				result.unfocusRequested = true;
				LogInfo(m_name, "Unfocus complete");
			}

			if (needsHide) {
				LogInfo(m_name, std::format("Calling Hide(handle={})", m_handle));
				m_api->Hide(m_handle);
				m_currentlyVisible = false;
				result.hideRequested = true;
				LogInfo(m_name, "Hide complete");
			}

			m_currentlyFocused = false;
			m_focusSucceeded = false;
		}

		ResetViewCursorPolicyCache();
		result.applied = true;
		LogInfo(m_name, FormatStateLine(std::string(a_reason) + ":done"));
		return result;
	}

	Snapshot Controller::GetSnapshot() const noexcept
	{
		Snapshot snap{};
		snap.name = m_name;
		snap.handle = m_handle;
		snap.apiAvailable = m_api != nullptr;
		snap.handleValid = IsHandleValid();
		snap.domReady = m_domReady;
		snap.desiredVisible = m_desiredVisible;
		snap.desiredFocused = m_desiredFocused;
		snap.currentlyVisible = m_currentlyVisible;
		snap.currentlyFocused = m_currentlyFocused;
		snap.desiredRole = m_desiredRole;
		snap.roleAssigned = m_roleAssigned;
		snap.showCalled = m_showCalled;
		snap.focusCalled = m_focusCalled;
		snap.focusSucceeded = m_focusSucceeded;
		snap.createFailed = m_createFailed;
		snap.createAttempts = m_createAttempts;
		return snap;
	}

	std::string Controller::FormatSnapshot() const
	{
		const auto snap = GetSnapshot();
		return std::format(
			"{}: handle={} valid={} domReady={} desiredVisible={} currentVisible={} desiredFocused={} currentFocused={} role={} roleAssigned={}",
			snap.name,
			snap.handle,
			snap.handleValid,
			snap.domReady,
			snap.desiredVisible,
			snap.currentlyVisible,
			snap.desiredFocused,
			snap.currentlyFocused,
			F4T::PrismaCapabilities::RoleName(snap.desiredRole),
			snap.roleAssigned);
	}

	std::string Controller::FormatDiagnostics() const
	{
		const auto snap = GetSnapshot();
		const auto runtimeRole = F4T::PrismaCapabilities::TryGetViewRole(snap.handle);
		return std::format(
			"{}:\n"
			"  handle={}\n"
			"  valid={}\n"
			"  createFailed={} createAttempts={}\n"
			"  roleDesired={} roleAssigned={} roleRuntime={}\n"
			"  domReady={}\n"
			"  desiredVisible={} currentVisible={} showCalled={}\n"
			"  desiredFocused={} currentFocused={} focusCalled={} focusSucceeded={}",
			snap.name,
			snap.handle,
			snap.handleValid,
			snap.createFailed,
			snap.createAttempts,
			F4T::PrismaCapabilities::RoleName(snap.desiredRole),
			snap.roleAssigned,
			F4T::PrismaCapabilities::RoleName(runtimeRole),
			snap.domReady,
			snap.desiredVisible,
			snap.currentlyVisible,
			snap.showCalled,
			snap.desiredFocused,
			snap.currentlyFocused,
			snap.focusCalled,
			snap.focusSucceeded);
	}

	void Controller::LogSnapshot(std::string_view a_reason) const
	{
		LogInfo(m_name, std::format("StateDump reason={} {}", a_reason, FormatSnapshot()));
	}

	void Controller::LogDiagnostics(std::string_view a_reason) const
	{
		LogInfo(m_name, std::format("Diagnostics reason={}\n{}", a_reason, FormatDiagnostics()));
	}

	bool Controller::IsFullyAppliedForVisibleSurface() const noexcept
	{
		if (!m_desiredVisible || !IsHandleValid() || !m_domReady || !m_currentlyVisible || !m_showCalled) {
			return false;
		}

		if (m_desiredFocused && (!m_focusCalled || !m_focusSucceeded || !m_currentlyFocused)) {
			return false;
		}

		if (F4T::PrismaCapabilities::HasV10() &&
			m_desiredRole != PRISMA_UI_API::ViewRole::kUnspecified &&
			!m_roleAssigned) {
			return false;
		}

		return true;
	}

	void Controller::LogTransition(std::string_view a_action, std::string_view a_detail) const
	{
		LogInfo(m_name, std::format("{} {}", a_action, a_detail));
	}

	std::string Controller::FormatStateLine(std::string_view a_reason) const
	{
		return std::format(
			"ApplyState reason={} handle={} valid={} domReady={} desiredVisible={} desiredFocused={} currentVisible={} currentFocused={} role={} roleAssigned={} api={}",
			a_reason,
			m_handle,
			IsHandleValid(),
			m_domReady,
			m_desiredVisible,
			m_desiredFocused,
			m_currentlyVisible,
			m_currentlyFocused,
			F4T::PrismaCapabilities::RoleName(m_desiredRole),
			m_roleAssigned,
			m_api != nullptr);
	}
}
