#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace F4T::Http
{
	inline constexpr std::size_t kMaxResponseBytes = 512 * 1024;

	inline bool AppendResponseChunk(std::string& a_body, std::string_view a_chunk, std::string& a_error)
	{
		if (a_chunk.size() > kMaxResponseBytes - a_body.size()) {
			a_error = "Server directory response exceeds 512 KiB.";
			return false;
		}

		a_body.append(a_chunk);
		return true;
	}

	bool Get(const std::string& a_url, std::string& a_outBody, std::string& a_outError, unsigned long a_timeoutMs = 10000);
}
