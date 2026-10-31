#pragma once

#include "F4TSteamAdmission.h"
#include "AdminCommandDispatcher.h"
#include "F4TSteamHostLink.h"

#include <nlohmann/json.hpp>

#include <functional>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace CommonwealthOnline::Server
{
	enum class SteamIdSource : std::uint8_t
	{
		None,
		SteamTunnel,
		HostSelf,
		WebApi
	};

	[[nodiscard]] std::string_view SteamIdSourceName(SteamIdSource a_source) noexcept;

	struct VerifiedSteamId
	{
		std::string steamId;
		SteamIdSource source{ SteamIdSource::None };
		bool passwordRequired{};
	};

	class SteamIdentityLink
	{
	public:
		[[nodiscard]] bool Bind(std::uint16_t a_localPort, std::string_view a_steamId, bool a_passwordRequired = false);
		bool Unbind(std::uint16_t a_localPort);
		void Clear();
		[[nodiscard]] bool SetHost(std::string_view a_steamId);
		[[nodiscard]] std::optional<VerifiedSteamId> Resolve(
			std::uint32_t a_remoteIpv4HostOrder,
			std::uint16_t a_remotePort,
			std::string_view a_claimedSteamId) const;
		[[nodiscard]] std::size_t BindingCount() const;
		void SetPolicy(F4T::SteamAdmission::HostPolicy a_policy);
		[[nodiscard]] F4T::SteamAdmission::HostPolicy Policy() const;
		using LiveSettingsHandler = std::function<AdminActionResult(const nlohmann::json&)>;
		using ServerConfigProvider = std::function<nlohmann::json()>;
		[[nodiscard]] nlohmann::json HandleRequest(const nlohmann::json& a_request);
		void SetApplySettingsHandler(LiveSettingsHandler a_handler);
		void SetServerConfigProvider(ServerConfigProvider a_provider);

	private:
		mutable std::mutex _mutex;
		struct Binding
		{
			std::uint64_t steamId{};
			bool passwordRequired{};
		};

		std::unordered_map<std::uint16_t, Binding> _bindings;
		LiveSettingsHandler _applySettingsHandler;
		ServerConfigProvider _serverConfigProvider;
		std::uint64_t _hostSteamId{};
		F4T::SteamAdmission::HostPolicy _policy{};
	};

	void ApplySteamHostPolicy(SteamIdentityLink& a_link, bool a_friendsOnly, bool a_serverPasswordSet);

	class SteamHostLinkPublication
	{
	public:
		SteamHostLinkPublication() = default;
		SteamHostLinkPublication(const SteamHostLinkPublication&) = delete;
		SteamHostLinkPublication& operator=(const SteamHostLinkPublication&) = delete;
		~SteamHostLinkPublication();

		[[nodiscard]] bool Publish(
			const std::filesystem::path& a_portPath,
			const std::filesystem::path& a_tokenPath,
			std::uint16_t a_linkPort,
			std::string& a_error);

	private:
		std::filesystem::path _path;
		std::filesystem::path _tokenPath;
	};
}
