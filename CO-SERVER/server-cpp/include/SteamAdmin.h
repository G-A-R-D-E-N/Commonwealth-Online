#pragma once

#include "AdminCommandDispatcher.h"
#include "BanStore.h"
#include "SteamIdentityRegistry.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server
{
	struct SteamAdminConnection
	{
		std::uint32_t connectionId{};
		std::uint32_t playerId{};
		std::uint32_t remoteIpv4{};
		std::string remoteAddress;
		bool active{};
		std::string profileId;
		std::string steamId;
	};

	struct SteamAdminDependencies
	{
		BanStore* bans{};
		std::function<std::vector<SteamAdminConnection>()> connections;
		std::function<void(std::uint32_t, std::string_view, std::string_view)> endSession;
		std::function<void(const nlohmann::json&)> saveIdentities;
		std::function<std::string(std::string, std::optional<std::string>)> withPersistenceWarning;
		std::function<double()> unixTimeSeconds;
	};

	class SteamAdmin
	{
	public:
		[[nodiscard]] static std::optional<SteamAdmin> Create(
			const nlohmann::json& a_stored,
			SteamAdminDependencies a_dependencies);

		void RecordIdentity(
			std::uint32_t a_connectionId,
			std::string_view a_profileId,
			std::string_view a_nickname,
			std::string_view a_steamId,
			SteamIdSource a_source,
			double a_now);

		void SweepIfDue(double a_now);

		void InstallHooks(AdminCommandHooks& a_hooks);

	private:
		explicit SteamAdmin(SteamAdminDependencies a_dependencies);

		[[nodiscard]] double Now() const;
		[[nodiscard]] std::string WithWarning(std::string a_message) const;
		void Persist();
		void Sweep(double a_now);
		[[nodiscard]] std::size_t EndSessions(
			const std::function<bool(const SteamAdminConnection&)>& a_match,
			std::string_view a_code,
			std::string_view a_reason);
		[[nodiscard]] static std::string AddressKey(const SteamAdminConnection& a_connection);
		[[nodiscard]] std::size_t EndSessionsForIp(std::string_view a_ip, std::string_view a_code, std::string_view a_reason);
		[[nodiscard]] std::size_t EndSessionsForProfile(std::string_view a_profileId, std::string_view a_code, std::string_view a_reason);
		[[nodiscard]] std::size_t EndSessionsForSteamId(std::string_view a_steamId, std::string_view a_code, std::string_view a_reason);
		[[nodiscard]] AdminActionResult BanSteamIdResult(std::string_view a_steamId, std::string_view a_reason);

		SteamAdminDependencies _dependencies;
		SteamIdentityRegistry _identities;
		double _nextSweepUnix{};
	};
}
