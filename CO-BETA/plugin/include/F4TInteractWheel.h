#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace F4T::InteractWheel
{
	void Tick();
	void OpenForActivation();
	void Close(std::string_view a_reason);
	[[nodiscard]] bool IsOpen();
	[[nodiscard]] std::string BuildStateJson();
	void HandleAction(std::string_view a_actionJson);
}
