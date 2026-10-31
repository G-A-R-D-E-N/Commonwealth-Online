#pragma once

namespace F4T::InteractWheelUi
{
	void Register();
	void Open();
	void Close();
	void NotifyStateChanged();
	[[nodiscard]] bool IsVisible();
}
