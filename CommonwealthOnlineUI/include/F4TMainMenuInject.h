#pragma once

namespace RE
{
	class UI;
}

namespace F4T::MainMenuInject
{
	bool IsMainMenuOnMainPanel(const RE::UI* a_ui);
	void SetMainMenuInputSuppressed(bool a_suppressed);
	void Install();
}
