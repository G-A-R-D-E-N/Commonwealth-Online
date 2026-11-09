#include "ServerProtocol.h"
#include "ServerReadySurvivalPolicy.h"
#include "ServerLiveSettingsDelivery.h"

#include "F4TAdmission.h"
#include "F4TConnectionPolicy.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

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

	const auto welcomeFrames = host.HandleConnected(42, 1.0);
	const auto welcome = nlohmann::json::parse(welcomeFrames.at(0).payload);
	assert(welcome.at("survivalMode") == true);
	assert(welcome.at("survivalNeeds") == false);
	host.SetSurvivalMode(false);
	const auto hello = nlohmann::json{ { "type", "hello" }, { "protocolVersion", 2 } }.dump();
	const std::vector<std::uint8_t> helloBytes(hello.begin(), hello.end());
	auto readyFrames = host.HandleMessage(42, helloBytes, 2.0, 2.0);
	bool foundReady = false;
	for (auto& frame : readyFrames) {
		const auto packet = nlohmann::json::parse(frame.payload);
		if (packet.value("type", std::string{}) == "sessionReady") {
			assert(packet.at("survivalMode") == false);
			host.SetSurvivalMode(true);
			assert(CommonwealthOnline::Server::RefreshReadySurvivalPolicy(frame, host));
			assert(nlohmann::json::parse(frame.payload).at("survivalMode") == true);
			foundReady = true;
		}
	}
	assert(foundReady);

	CommonwealthOnline::Server::LiveSettingsDelivery delivery;
	delivery.Queue(1, nlohmann::json{ { "survivalMode", false } });
	delivery.Queue(2, nlohmann::json{ { "survivalMode", false } });
	std::size_t attempts = 0;
	delivery.Flush([&](std::uint32_t, const nlohmann::json&) { ++attempts; return false; });
	assert(attempts == 2);
	delivery.Queue(1, nlohmann::json{ { "survivalMode", true } });
	delivery.Erase(2);
	delivery.Flush([&](std::uint32_t id, const nlohmann::json& packet) {
		assert(id == 1 && packet.at("survivalMode") == true);
		++attempts;
		return true;
	});
	delivery.Flush([&](std::uint32_t, const nlohmann::json&) { assert(false); return true; });
	assert(attempts == 3);

	F4T::Admission::AdmissionRegistry admission{ 2 };
	const auto first = admission.Begin({ .ip = "10.0.0.1" }, 10.0);
	assert(first.admitted);
	assert(admission.Reject(first.ticketId, "capacity_checked"));
	assert(F4T::ConnectionPolicy::CanActivate(1, 2));
	assert(!F4T::ConnectionPolicy::CanActivate(2, 2));
	assert(!F4T::ConnectionPolicy::CanActivate(3, 2));
	admission.SetMaximumPlayers(0);
	const auto denied = admission.Begin({ .ip = "10.0.0.2" }, 11.0);
	assert(!denied.admitted);
	assert(denied.code == "server_full");
	admission.SetMaximumPlayers(6);
	const auto allowed = admission.Begin({ .ip = "10.0.0.3" }, 12.0);
	assert(allowed.admitted);

	return 0;
}
