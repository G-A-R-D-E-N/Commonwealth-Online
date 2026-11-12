#pragma once

#include <string>

namespace F4T::TradeWindow
{
	void Attach();
	[[nodiscard]] bool Attached();
	void NoteStateChanged();
	void PushRawState(const std::string& a_stateJson);
}
