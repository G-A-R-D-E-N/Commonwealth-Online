#pragma once

#include "AdminCommandDispatcher.h"
#include "F4TAdmission.h"
#include "F4TWorkshopRaids.h"
#include "ServerConfiguration.h"
#include "F4TWorldPolicy.h"
#include "ServerProtocol.h"
#include "ServerRuntimeSupport.h"
#include "SteamIdentityLink.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string_view>
#include <memory>
#include <optional>
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
		SteamIdentityLink* steamLink{};
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
	[[nodiscard]] AdminActionResult ApplyAdminMapPublic(ProtocolHost& a_protocol, std::optional<bool> a_enabled);
	[[nodiscard]] AdminActionResult ApplyAdminWorldMode(ServerLiveSettingsContext& a_context, std::string_view a_mode);
	[[nodiscard]] nlohmann::json RaidSettingsJson(const F4T::WorkshopRaids::RaidConfig& a_config);
	[[nodiscard]] AdminActionResult ApplyAdminRaidSettings(ServerLiveSettingsContext& a_context, const nlohmann::json& a_patch);
	[[nodiscard]] bool LoadRaidSettingsFile(const std::filesystem::path& a_path, F4T::WorkshopRaids::RaidConfig& a_config, std::string& a_error);
	[[nodiscard]] F4T::WorldPolicy::Policy DeriveServerWorldPolicy(
		const F4T::WorldPolicy::Data& a_data,
		F4T::WorldPolicy::Mode a_mode,
		bool a_doorsShared,
		F4T::WorldPolicy::Vats a_vats,
		const std::vector<F4T::QuestProtocol::OverrideEntry>& a_overrides);
}
