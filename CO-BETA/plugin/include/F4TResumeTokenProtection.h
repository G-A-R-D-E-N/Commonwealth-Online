#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace F4T::ResumeTokenProtection
{
	std::optional<std::string> Protect(const std::string& a_plaintext);
	std::optional<std::string> Unprotect(const std::string& a_protectedBase64);
	std::uint32_t LastError() noexcept;
}
