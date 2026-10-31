#pragma once

#include "PrismaUI_F4_API.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::PrismaViewState
{
	struct Snapshot
	{
		const char*                name{ "View" };
		PrismaView                 handle{ 0 };
		bool                       apiAvailable{ false };
		bool                       handleValid{ false };
		bool                       domReady{ false };
		bool                       desiredVisible{ false };
		bool                       desiredFocused{ false };
		bool                       currentlyVisible{ false };
		bool                       currentlyFocused{ false };
		PRISMA_UI_API::ViewRole    desiredRole{ PRISMA_UI_API::ViewRole::kUnspecified };
		bool                       roleAssigned{ false };
		bool                       showCalled{ false };
		bool                       focusCalled{ false };
		bool                       focusSucceeded{ false };
		bool                       createFailed{ false };
		std::uint32_t              createAttempts{ 0 };
	};

	struct ApplyResult
	{
		bool applied{ false };
		bool showRequested{ false };
		bool hideRequested{ false };
		bool focusRequested{ false };
		bool unfocusRequested{ false };
		bool pending{ false };
		const char* pendingReason{ nullptr };
	};

	class Controller
	{
	public:
		explicit Controller(const char* a_name) noexcept;

		void SetApi(PRISMA_UI_API::IVPrismaUI1* a_api) noexcept;
		void SetFocusArgs(bool a_pauseGame, bool a_disableFocusMenu) noexcept;
		void SetDesiredRole(PRISMA_UI_API::ViewRole a_role) noexcept;

		[[nodiscard]] const char* GetName() const noexcept;
		[[nodiscard]] PrismaView GetHandle() const noexcept;
		[[nodiscard]] bool IsDomReady() const noexcept;
		[[nodiscard]] bool IsDesiredVisible() const noexcept;
		[[nodiscard]] bool IsDesiredFocused() const noexcept;
		[[nodiscard]] bool IsCurrentlyVisible() const noexcept;
		[[nodiscard]] bool IsCurrentlyFocused() const noexcept;
		[[nodiscard]] bool IsHandleValid() const noexcept;
		[[nodiscard]] bool IsRoleAssigned() const noexcept;
		[[nodiscard]] bool DidCreateFail() const noexcept;
		[[nodiscard]] bool CanRetryCreate() const noexcept;
		[[nodiscard]] PRISMA_UI_API::ViewRole GetDesiredRole() const noexcept;

		void SetDesiredVisible(bool a_visible) noexcept;
		void SetDesiredFocused(bool a_focused) noexcept;
		void SetDesired(bool a_visible, bool a_focused) noexcept;

		PrismaView EnsureCreated(
			const char* a_htmlPath,
			PRISMA_UI_API::OnDomReadyCallback a_onDomReady,
			bool a_runtimeReady) noexcept;

		void AllowCreateRetry() noexcept;

		bool AssignRoleIfNeeded() noexcept;

		bool MarkDomReady(PrismaView a_view) noexcept;

		void InvalidateApplied() noexcept;

		void ClearHandle() noexcept;

		ApplyResult ApplyState(std::string_view a_reason) noexcept;

		[[nodiscard]] Snapshot GetSnapshot() const noexcept;
		[[nodiscard]] std::string FormatSnapshot() const;
		[[nodiscard]] std::string FormatDiagnostics() const;

		void LogSnapshot(std::string_view a_reason) const;
		void LogDiagnostics(std::string_view a_reason) const;

		[[nodiscard]] bool IsFullyAppliedForVisibleSurface() const noexcept;

	private:
		void LogTransition(std::string_view a_action, std::string_view a_detail) const;
		[[nodiscard]] std::string FormatStateLine(std::string_view a_reason) const;

		const char*                   m_name{ "View" };
		PRISMA_UI_API::IVPrismaUI1*    m_api{ nullptr };
		PrismaView                    m_handle{ 0 };
		bool                          m_domReady{ false };
		bool                          m_desiredVisible{ false };
		bool                          m_desiredFocused{ false };
		bool                          m_currentlyVisible{ false };
		bool                          m_currentlyFocused{ false };
		bool                          m_focusPauseGame{ false };
		bool                          m_focusDisableFocusMenu{ true };
		PRISMA_UI_API::ViewRole       m_desiredRole{ PRISMA_UI_API::ViewRole::kUnspecified };
		bool                          m_roleAssigned{ false };
		bool                          m_roleFailureLogged{ false };
		bool                          m_showCalled{ false };
		bool                          m_focusCalled{ false };
		bool                          m_focusSucceeded{ false };
		bool                          m_createFailed{ false };
		bool                          m_createRetryAllowed{ false };
		std::uint32_t                 m_createAttempts{ 0 };
	};

	void ResetViewCursorPolicyCache();
	void SetViewCursorPolicyCached(PrismaView, PRISMA_UI_API::CursorPolicy);
}
