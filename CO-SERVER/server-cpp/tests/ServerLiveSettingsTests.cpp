#include "ServerProtocol.h"

#include "F4TAdmission.h"
#include "F4TConnectionPolicy.h"

#include <cassert>
#include <string>

int main()
{
	using CommonwealthOnline::Server::ProtocolHost;

	ProtocolHost host{ "Test server", "Test description" };
	const auto capabilities = [&host] {
		const auto packet = host.ServerInfoPacket();
		std::string joined;
		for (const auto& capability : packet.at("capabilities")) {
			joined += capability.get<std::string>() + "\n";
		}
		return joined;
	};

	host.SetChatEnabled(true);
	assert(capabilities().find("chat-v1") != std::string::npos);
	host.SetChatEnabled(false);
	assert(capabilities().find("chat-v1") == std::string::npos);
	host.SetChatEnabled(true);
	assert(capabilities().find("chat-v1") != std::string::npos);

	assert(capabilities().find("factions-v1") == std::string::npos);
	assert(host.SetFactionsEnabled(true, nullptr));
	assert(capabilities().find("factions-v1") != std::string::npos);
	assert(host.SetFactionsEnabled(false, nullptr));
	assert(capabilities().find("factions-v1") == std::string::npos);
	assert(host.SetFactionsEnabled(true, nullptr));
	assert(capabilities().find("factions-v1") != std::string::npos);

	host.SetIdentity("Renamed server", "New description");
	host.SetFriendlyFire(false, true);
	host.SetAdvertisedMaximumPlayers(6);
	const auto info = host.ServerInfoPacket();
	assert(info.at("serverName") == "Renamed server");
	assert(info.at("serverDescription") == "New description");
	assert(info.at("friendlyFire") == false);
	assert(info.at("factionFriendlyFire") == true);
	assert(info.at("maxPlayers") == 6);

	F4T::Admission::AdmissionRegistry admission{ 2 };
	const auto first = admission.Begin({ .ip = "10.0.0.1" }, 10.0, 0);
	assert(first.admitted);
	assert(admission.Reject(first.ticketId, "capacity_checked"));
	assert(F4T::ConnectionPolicy::CanActivate(1, 2));
	assert(!F4T::ConnectionPolicy::CanActivate(2, 2));
	assert(!F4T::ConnectionPolicy::CanActivate(3, 2));
	admission.SetMaximumPlayers(0);
	const auto denied = admission.Begin({ .ip = "10.0.0.2" }, 11.0, 0);
	assert(!denied.admitted);
	assert(denied.code == "server_full");
	admission.SetMaximumPlayers(6);
	const auto allowed = admission.Begin({ .ip = "10.0.0.3" }, 12.0, 0);
	assert(allowed.admitted);

	return 0;
}
