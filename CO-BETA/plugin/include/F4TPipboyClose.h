#pragma once

#include <string_view>

namespace F4T::PipboyClose
{
	void Install();
	void ForceClose(std::string_view a_reason);
	void SetState(bool a_session, bool a_inputGate);
	void OnMenuEdge();
	bool ClosePending();
	void LogState(std::string_view a_when, bool a_inputLayerHeld);
}
