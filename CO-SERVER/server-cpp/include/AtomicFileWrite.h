#pragma once

#include <filesystem>
#include <string_view>

namespace CommonwealthOnline::Server
{
	void WriteFileAtomically(
		const std::filesystem::path& a_destination,
		std::string_view a_contents,
		bool* a_committed = nullptr);
}
