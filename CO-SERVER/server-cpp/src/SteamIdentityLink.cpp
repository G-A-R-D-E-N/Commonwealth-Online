#include "SteamIdentityLink.h"

#include "AdminControlServer.h"

#include "F4TSteamHostLink.h"
#include "F4TSteamId.h"

#include <fstream>
#include <iostream>
#include <system_error>
#include <utility>

namespace CommonwealthOnline::Server
{
	namespace
	{
		using Json = nlohmann::json;

		constexpr std::uint32_t kLoopbackNetwork = 127;

		std::optional<std::uint16_t> ReadPort(const Json& a_request)
		{
			const auto found = a_request.find("port");
			if (found == a_request.end() || !found->is_number_integer()) {
				return std::nullopt;
			}
			const auto value = found->get<std::int64_t>();
			if (value <= 0 || value > 65535) {
				return std::nullopt;
			}
			return static_cast<std::uint16_t>(value);
		}

		std::string ReadSteamId(const Json& a_request)
		{
			const auto found = a_request.find("steamId");
			return found != a_request.end() && found->is_string() ? found->get<std::string>() : std::string{};
		}
	}

	std::string_view SteamIdSourceName(SteamIdSource a_source) noexcept
	{
		switch (a_source) {
		case SteamIdSource::SteamTunnel:
			return "steam-tunnel";
		case SteamIdSource::HostSelf:
			return "host-self";
		case SteamIdSource::WebApi:
			return "steam-web-api";
		case SteamIdSource::None:
			break;
		}
		return "none";
	}

	bool SteamIdentityLink::Bind(std::uint16_t a_localPort, std::string_view a_steamId, bool a_passwordRequired)
	{
		const auto steamId = F4T::SteamId::ParseIndividual(a_steamId);
		if (a_localPort == 0 || !steamId) {
			return false;
		}
		std::scoped_lock lock(_mutex);
		if (!_bindings.contains(a_localPort) && _bindings.size() >= F4T::SteamHostLink::kMaximumBindings) {
			return false;
		}
		_bindings[a_localPort] = Binding{ *steamId, a_passwordRequired };
		return true;
	}

	bool SteamIdentityLink::Unbind(std::uint16_t a_localPort)
	{
		std::scoped_lock lock(_mutex);
		return _bindings.erase(a_localPort) != 0;
	}

	void SteamIdentityLink::Clear()
	{
		std::scoped_lock lock(_mutex);
		_bindings.clear();
		_hostSteamId = 0;
	}

	bool SteamIdentityLink::SetHost(std::string_view a_steamId)
	{
		std::uint64_t hostSteamId = 0;
		if (!a_steamId.empty()) {
			const auto parsed = F4T::SteamId::ParseIndividual(a_steamId);
			if (!parsed) {
				return false;
			}
			hostSteamId = *parsed;
		}
		std::scoped_lock lock(_mutex);
		_hostSteamId = hostSteamId;
		return true;
	}

	std::optional<VerifiedSteamId> SteamIdentityLink::Resolve(
		std::uint32_t a_remoteIpv4HostOrder,
		std::uint16_t a_remotePort,
		std::string_view a_claimedSteamId) const
	{
		if ((a_remoteIpv4HostOrder >> 24) != kLoopbackNetwork) {
			return std::nullopt;
		}
		std::scoped_lock lock(_mutex);
		if (const auto bound = _bindings.find(a_remotePort); bound != _bindings.end()) {
			return VerifiedSteamId{ std::to_string(bound->second.steamId), SteamIdSource::SteamTunnel, bound->second.passwordRequired };
		}
		if (_hostSteamId != 0 && a_claimedSteamId == std::to_string(_hostSteamId)) {
			return VerifiedSteamId{ std::string{ a_claimedSteamId }, SteamIdSource::HostSelf, false };
		}
		return std::nullopt;
	}

	std::size_t SteamIdentityLink::BindingCount() const
	{
		std::scoped_lock lock(_mutex);
		return _bindings.size();
	}

	void SteamIdentityLink::SetPolicy(F4T::SteamAdmission::HostPolicy a_policy)
	{
		std::scoped_lock lock(_mutex);
		_policy = a_policy;
	}

	F4T::SteamAdmission::HostPolicy SteamIdentityLink::Policy() const
	{
		std::scoped_lock lock(_mutex);
		return _policy;
	}

	void SteamIdentityLink::SetApplySettingsHandler(LiveSettingsHandler a_handler)
	{
		std::scoped_lock lock(_mutex);
		_applySettingsHandler = std::move(a_handler);
	}

	void SteamIdentityLink::SetServerConfigProvider(ServerConfigProvider a_provider)
	{
		std::scoped_lock lock(_mutex);
		_serverConfigProvider = std::move(a_provider);
	}

	Json SteamIdentityLink::HandleRequest(const Json& a_request)
	{
		const auto found = a_request.find("cmd");
		const auto command = found != a_request.end() && found->is_string() ? found->get<std::string>() : std::string{};
		if (command == F4T::SteamHostLink::kApplySettingsCommand) {
			const auto document = a_request.find("document");
			if (document == a_request.end() || !document->is_object()) {
				return AdminControlServer::Failure("Server settings need a document object.", &a_request);
			}
			LiveSettingsHandler handler;
			{
				std::scoped_lock lock(_mutex);
				handler = _applySettingsHandler;
			}
			if (!handler) {
				return AdminControlServer::Failure("Live server settings are unavailable.", &a_request);
			}
			const auto result = handler(*document);
			return result.ok ? AdminControlServer::Success(result.data, result.message, &a_request) :
			                   AdminControlServer::Failure(result.message, &a_request, result.data);
		}
		if (command == F4T::SteamHostLink::kServerConfigCommand) {
			ServerConfigProvider provider;
			{
				std::scoped_lock lock(_mutex);
				provider = _serverConfigProvider;
			}
			if (!provider) {
				return AdminControlServer::Failure("The running server configuration is unavailable.", &a_request);
			}
			return AdminControlServer::Success(provider(), {}, &a_request);
		}
		if (command == F4T::SteamHostLink::kBindCommand) {
			const auto port = ReadPort(a_request);
			const auto passwordRequired = a_request.find("passwordRequired");
			const bool flagValid = passwordRequired == a_request.end() || passwordRequired->is_boolean();
			if (!port || !flagValid || !Bind(*port, ReadSteamId(a_request), passwordRequired != a_request.end() && passwordRequired->get<bool>())) {
				return AdminControlServer::Failure("Steam bind needs a loopback port and an individual SteamID64, and the binding table must have room.", &a_request);
			}
			return AdminControlServer::Success(nullptr, "Steam identity bound.", &a_request);
		}
		if (command == F4T::SteamHostLink::kUnbindCommand) {
			const auto port = ReadPort(a_request);
			if (!port) {
				return AdminControlServer::Failure("Steam unbind needs a loopback port.", &a_request);
			}
			return AdminControlServer::Success(Json{ { "removed", Unbind(*port) } }, "Steam identity unbound.", &a_request);
		}
		if (command == F4T::SteamHostLink::kClearCommand) {
			Clear();
			return AdminControlServer::Success(nullptr, "Steam identity bindings cleared.", &a_request);
		}
		if (command == F4T::SteamHostLink::kHostCommand) {
			if (!SetHost(ReadSteamId(a_request))) {
				return AdminControlServer::Failure("Steam host needs an individual SteamID64 or an empty string.", &a_request);
			}
			return AdminControlServer::Success(nullptr, "Steam host identity set.", &a_request);
		}
		if (command == F4T::SteamHostLink::kPolicyCommand) {
			const auto policy = Policy();
			return AdminControlServer::Success(Json{ { "friendsOnly", policy.friendsOnly }, { "serverPasswordSet", policy.serverPasswordSet } }, {}, &a_request);
		}
		return AdminControlServer::Failure("Unknown Steam host link command.", &a_request);
	}

	void ApplySteamHostPolicy(SteamIdentityLink& a_link, bool a_friendsOnly, bool a_serverPasswordSet)
	{
		a_link.SetPolicy(F4T::SteamAdmission::HostPolicy{ a_friendsOnly, a_serverPasswordSet });
		std::cout << "Steam hosting admission: " << (a_friendsOnly ? "friends only" : "friends only off")
		          << (a_serverPasswordSet ? ", join password set" : ", no join password")
		          << (!a_friendsOnly && !a_serverPasswordSet ? " (non-friends still refused: set server_password)" : "") << std::endl;
	}

	SteamHostLinkPublication::~SteamHostLinkPublication()
	{
		std::error_code ignored;
		if (!_path.empty()) {
			std::filesystem::remove(_path, ignored);
		}
		if (!_tokenPath.empty()) {
			std::filesystem::remove(_tokenPath, ignored);
		}
	}

	bool SteamHostLinkPublication::Publish(
		const std::filesystem::path& a_portPath,
		const std::filesystem::path& a_tokenPath,
		std::uint16_t a_linkPort,
		std::string& a_error)
	{
		std::error_code error;
		std::filesystem::create_directories(a_portPath.parent_path(), error);
		if (error) {
			a_error = "Could not create the Steam host link directory.";
			return false;
		}
		auto tempPath = a_portPath;
		tempPath += ".tmp";
		{
			std::ofstream output(tempPath, std::ios::binary | std::ios::trunc);
			output << a_linkPort << '\n';
			if (!output.good()) {
				a_error = "Could not write the Steam host link port file.";
				return false;
			}
		}
		std::filesystem::rename(tempPath, a_portPath, error);
		if (error) {
			std::filesystem::remove(tempPath, error);
			a_error = "Could not publish the Steam host link port file.";
			return false;
		}
		_path = a_portPath;
		_tokenPath = a_tokenPath;
		return true;
	}
}
