#include "F4TCharacterCreatorLifecycle.h"

#include <cassert>

using F4T::CharacterCreator::Lifecycle::Gate;
using F4T::CharacterCreator::Lifecycle::IsSurfaceReady;
using F4T::CharacterCreator::Lifecycle::View;

namespace
{
	View ReadyView()
	{
		View view;
		view.menuOpen = true;
		view.apiAvailable = true;
		view.handleValid = true;
		view.domReady = true;
		view.shown = true;
		view.focused = true;
		return view;
	}

	bool LegacySuppressionRequested(bool a_menuOpen)
	{
		return a_menuOpen;
	}

	void LegacyRuleSuppressesAnUnreadySurface()
	{
		auto view = ReadyView();
		view.apiAvailable = false;
		assert(LegacySuppressionRequested(view.menuOpen));
		assert(!IsSurfaceReady(view));

		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void MissingApiNeverSuppresses()
	{
		auto view = ReadyView();
		view.apiAvailable = false;
		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void ZeroHandleNeverSuppresses()
	{
		auto view = ReadyView();
		view.handleValid = false;
		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void DomNotReadyNeverSuppresses()
	{
		auto view = ReadyView();
		view.domReady = false;
		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void UnshownViewNeverSuppresses()
	{
		auto view = ReadyView();
		view.shown = false;
		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void ClosedMenuNeverSuppresses()
	{
		auto view = ReadyView();
		view.menuOpen = false;
		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void UnfocusedViewNeverSuppresses()
	{
		auto view = ReadyView();
		view.focused = false;
		Gate gate;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());
	}

	void FocusLossRestoresBothAndRecoverySuppresses()
	{
		Gate gate;
		auto view = ReadyView();
		(void)gate.Evaluate(view);
		view.focused = false;
		const auto lost = gate.Evaluate(view);
		assert(lost.restoreRender && lost.restoreInput);
		view.focused = true;
		const auto recovered = gate.Evaluate(view);
		assert(recovered.suppressRender && recovered.suppressInput);
	}

	void ReadySurfaceSuppressesRenderThenInput()
	{
		Gate gate;
		const auto decision = gate.Evaluate(ReadyView());
		assert(decision.suppressRender);
		assert(decision.suppressInput);
		assert(!decision.restoreRender);
		assert(!decision.restoreInput);
		assert(gate.IsSuppressed());

		assert(gate.Evaluate(ReadyView()).IsIdle());
	}

	void InvalidationRestoresBoth()
	{
		Gate gate;
		(void)gate.Evaluate(ReadyView());
		assert(gate.IsSuppressed());

		auto view = ReadyView();
		view.handleValid = false;
		const auto decision = gate.Evaluate(view);
		assert(decision.restoreRender);
		assert(decision.restoreInput);
		assert(!decision.suppressRender);
		assert(!decision.suppressInput);
		assert(!gate.IsSuppressed());

		assert(gate.Evaluate(view).IsIdle());
	}

	void RecreateFailureRestoresBoth()
	{
		Gate gate;
		(void)gate.Evaluate(ReadyView());
		assert(gate.IsSuppressed());

		auto view = ReadyView();
		view.handleValid = false;
		view.domReady = false;
		view.shown = false;
		const auto decision = gate.Evaluate(view);
		assert(decision.restoreRender);
		assert(decision.restoreInput);
		assert(!gate.IsSuppressed());
	}

	void CloseRestoresBoth()
	{
		Gate gate;
		(void)gate.Evaluate(ReadyView());
		assert(gate.IsSuppressed());

		auto view = ReadyView();
		view.menuOpen = false;
		const auto decision = gate.Evaluate(view);
		assert(decision.restoreRender);
		assert(decision.restoreInput);
		assert(!gate.IsSuppressed());

		assert(gate.Evaluate(view).IsIdle());
	}

	void DeferredReadinessSuppressesOnlyOnceReady()
	{
		Gate gate;
		auto view = ReadyView();
		view.domReady = false;
		view.shown = false;
		assert(gate.Evaluate(view).IsIdle());
		assert(!gate.IsSuppressed());

		view.domReady = true;
		view.shown = true;
		const auto decision = gate.Evaluate(view);
		assert(decision.suppressRender);
		assert(decision.suppressInput);
		assert(gate.IsSuppressed());
	}
}

int main()
{
	LegacyRuleSuppressesAnUnreadySurface();
	MissingApiNeverSuppresses();
	ZeroHandleNeverSuppresses();
	DomNotReadyNeverSuppresses();
	UnshownViewNeverSuppresses();
	ClosedMenuNeverSuppresses();
	UnfocusedViewNeverSuppresses();
	FocusLossRestoresBothAndRecoverySuppresses();
	ReadySurfaceSuppressesRenderThenInput();
	InvalidationRestoresBoth();
	RecreateFailureRestoresBoth();
	CloseRestoresBoth();
	DeferredReadinessSuppressesOnlyOnceReady();
	return 0;
}
