#include "co_gns_client_bridge.h"

#include <array>

int main()
{
	std::array<char, 64> error{};
	co_gns_event event{};

	if (co_gns_client_create(nullptr, 0, nullptr, error.data(), error.size()) != 0) {
		return 1;
	}
	if (co_gns_client_poll(nullptr, &event, nullptr, 0) != -1) {
		return 2;
	}
	if (co_gns_client_send(nullptr, nullptr, 0, CO_GNS_DELIVERY_RELIABLE_ORDERED) != CO_GNS_SEND_ERROR) {
		return 3;
	}
	if (co_gns_client_disconnect(nullptr, 0, nullptr) != 0) {
		return 4;
	}
	co_gns_client_destroy(nullptr);
	return 0;
}
