#pragma once

#include <cstddef>
#include <string_view>

namespace F4T::QuestUI
{
	bool Attach();

	bool BuildStateJson(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize);

	void HandleAction(std::string_view a_actionJson);
	void RequestRefresh();
	void Tick();
}
