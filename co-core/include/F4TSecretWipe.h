#pragma once

#include <cstddef>
#include <string>

namespace F4T::Secret
{
	inline void Wipe(std::string& a_secret) noexcept
	{
		volatile char* bytes = a_secret.data();
		for (std::size_t index = 0; index < a_secret.size(); ++index) {
			bytes[index] = 0;
		}
		a_secret.clear();
	}
}
