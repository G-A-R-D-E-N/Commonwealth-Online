#pragma once

#include "F4TServerBrowserData.h"

#include <stop_token>
#include <vector>

namespace F4T::ServerBrowser::Reachability
{
	void Check(std::vector<ServerEntry>& a_servers, std::stop_token a_stop);
}
