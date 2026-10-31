#pragma once

#include <cstdint>

namespace F4T::CharacterCreator::Lifecycle
{
	struct View
	{
		bool menuOpen{ false };
		bool apiAvailable{ false };
		bool handleValid{ false };
		bool domReady{ false };
		bool shown{ false };
		bool focused{ false };
	};

	struct Decision
	{
		bool suppressRender{ false };
		bool restoreRender{ false };
		bool suppressInput{ false };
		bool restoreInput{ false };

		[[nodiscard]] bool IsIdle() const noexcept
		{
			return !suppressRender && !restoreRender && !suppressInput && !restoreInput;
		}
	};

	[[nodiscard]] constexpr bool IsSurfaceReady(const View& a_view) noexcept
	{
		return a_view.menuOpen && a_view.apiAvailable && a_view.handleValid && a_view.domReady &&
			a_view.shown && a_view.focused;
	}

	class Gate
	{
	public:
		[[nodiscard]] Decision Evaluate(const View& a_view) noexcept
		{
			const bool wantSuppressed = IsSurfaceReady(a_view);
			if (wantSuppressed == m_suppressed) {
				return Decision{};
			}

			m_suppressed = wantSuppressed;

			Decision decision{};
			decision.suppressRender = wantSuppressed;
			decision.restoreRender = !wantSuppressed;
			decision.suppressInput = wantSuppressed;
			decision.restoreInput = !wantSuppressed;
			return decision;
		}

		[[nodiscard]] bool IsSuppressed() const noexcept
		{
			return m_suppressed;
		}

	private:
		bool m_suppressed{ false };
	};
}
