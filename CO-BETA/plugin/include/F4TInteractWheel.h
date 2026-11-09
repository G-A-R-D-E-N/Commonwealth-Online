#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::InteractWheel
{
	void Tick();
	[[nodiscard]] bool IsBlockedByMenu();
	void OpenForActivation();
	void Close(std::string_view a_reason);
	void CloseForToken(std::uint32_t a_token, std::string_view a_reason);
	[[nodiscard]] std::uint32_t OpenToken();
	[[nodiscard]] bool IsOpen();
	[[nodiscard]] std::string BuildStateJson();
	void HandleAction(std::string_view a_actionJson);
	[[nodiscard]] bool CoWindowOpen();
	[[nodiscard]] bool VanillaMenuOpen(bool a_includePauseMenu);
}
