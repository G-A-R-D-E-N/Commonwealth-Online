#include "pch.h"

#include "F4TUIClientJoinErrors.h"

#include <algorithm>
#include <cstring>

namespace F4T::UIClient::JoinErrors
{
	void CopyError(char* a_errorBuf, std::size_t a_errorBufSize, std::string_view a_error)
	{
		if (!a_errorBuf || a_errorBufSize == 0) {
			return;
		}

		const auto copyLen = (std::min)(a_error.size(), a_errorBufSize - 1);
		std::memcpy(a_errorBuf, a_error.data(), copyLen);
		a_errorBuf[copyLen] = '\0';
	}

	std::string_view PasswordRejectionMessage(std::string_view a_code)
	{
		if (a_code == "password_required") {
			return "This server needs a password.";
		}
		if (a_code == "password_invalid") {
			return "Wrong password.";
		}
		return {};
	}
}
