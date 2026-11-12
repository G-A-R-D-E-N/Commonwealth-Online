#pragma once

#include <string_view>

namespace F4T::SessionExitSaveNotice
{
	void ShowSaving();
	void ShowRetrying();
	void ShowSaved();
	void ShowFailed(std::string_view a_message);
	void Dismiss();
}
