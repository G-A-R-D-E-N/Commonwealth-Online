#pragma once

#include "CommonwealthOnlineUI_API.h"

#include <cstddef>
#include <optional>
#include <string>

namespace F4T::UIClient
{
	bool BeginJoin(
		const COMMONWEALTH_ONLINE_UI_API::JoinRequest* a_request,
		std::optional<std::string>                     a_serverPassword,
		COMMONWEALTH_ONLINE_UI_API::JoinCompleteFn     a_onComplete,
		void*                                          a_userData,
		char*                                          a_errorBuf,
		std::size_t                                    a_errorBufSize);

	void DisconnectFromSession();
}
