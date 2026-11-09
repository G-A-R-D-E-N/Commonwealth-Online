#pragma once

#include <cstddef>
#include <string_view>

namespace F4T::UIClient::JoinErrors
{
	void CopyError(char* a_errorBuf, std::size_t a_errorBufSize, std::string_view a_error);
	std::string_view PasswordRejectionMessage(std::string_view a_code);
}
