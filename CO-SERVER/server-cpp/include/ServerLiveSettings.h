#pragma once

#include "AdminCommandDispatcher.h"
#include "F4TAdmission.h"
#include "ServerConfiguration.h"
#include "F4TWorldPolicy.h"
#include "ServerProtocol.h"
#include "ServerRuntimeSupport.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <memory>
#include <vector>

namespace CommonwealthOnline::Server
{
	class DirectoryRegistrar;
	class IdentityGate;
	class LanDiscoveryService;
	class PersistenceGate;

	struct ServerLiveSettingsContext
	{
		Configuration::Options* options{};
		ProtocolHost* protocol{};
		PersistenceGate* persistence{};
		IdentityGate* identityGate{};
		std::atomic_size_t* activePlayers{};
		F4T::Admission::AdmissionRegistry* sessionAdmissions{};
		RuntimeSupport::AdmissionContext* admission{};
		LanDiscoveryService* discovery{};
		std::unique_ptr<DirectoryRegistrar>* directory{};
		F4T::WorldPolicy::Data* worldPolicyData{};
		F4T::WorldPolicy::Mode* worldMode{};
		const std::filesystem::path* baseDirectory{};
		const std::filesystem::path* worldPolicyPath{};
		std::uint16_t localPort{};
		std::function<void(std::vector<OutboundFrame>)> sendFrames;
		std::function<void(const nlohmann::json&)> broadcastPacket;
	};

	[[nodiscard]] AdminActionResult ApplyServerLiveSettings(ServerLiveSettingsContext& a_context, const nlohmann::json& a_document);
	[[nodiscard]] AdminActionResult ApplyAdminWorldMode(ServerLiveSettingsContext& a_context, std::string_view a_mode);
}
