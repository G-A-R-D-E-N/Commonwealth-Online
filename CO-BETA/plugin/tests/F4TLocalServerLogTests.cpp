#include "F4TLocalServerLogLine.h"

#include <cstdio>
#include <string>

int main()
{
	const auto line = F4T::LocalServer::SelectLastLogLine(
		"missing optional texture pack\naddress already in use\n");
	assert(line == "address already in use");
	const auto cr = F4T::LocalServer::SelectLastLogLine(
		"missing optional texture pack\r\naddress already in use\r\n");
	assert(cr == "address already in use");
	const auto trailingBlank = F4T::LocalServer::SelectLastLogLine(
		"missing optional texture pack\naddress already in use\n\n");
	assert(trailingBlank == "address already in use");
	std::printf("last log line is address already in use\n");
	return line == "address already in use" && cr == trailingBlank ? 0 : 1;
}
