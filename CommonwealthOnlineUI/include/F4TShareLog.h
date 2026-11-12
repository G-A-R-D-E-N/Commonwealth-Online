#pragma once

#include <string>

namespace F4T::ShareLog
{
	struct Result
	{
		bool        success{ false };
		bool        clipboardCopied{ false };
		std::string savedPath;
		std::string error;
	};

	[[nodiscard]] Result Share();
}
