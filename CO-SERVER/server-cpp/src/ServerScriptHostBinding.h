#pragma once

#include "ServerProtocol.h"
#include "ServerScriptApi.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server::Scripting
{
	using ScriptFrameSender = std::function<void(std::vector<OutboundFrame>&)>;

	class ScriptHostBinding
	{
	public:
		ScriptHostBinding(ProtocolHost& a_protocol, const nlohmann::json& a_world, ScriptFrameSender a_send);
		~ScriptHostBinding();
		ScriptHostBinding(const ScriptHostBinding&) = delete;
		ScriptHostBinding& operator=(const ScriptHostBinding&) = delete;

		[[nodiscard]] ScriptInstaller Installer();

	private:
		std::optional<std::vector<OutboundFrame>> RouteChat(std::uint32_t a_playerId, std::string_view a_text);
		void Emit(std::vector<OutboundFrame> a_frames);
		void Notice(std::uint32_t a_playerId, std::string_view a_text);
		[[nodiscard]] bool IsAdmin(std::uint32_t a_playerId) const;
		[[nodiscard]] std::optional<std::uint32_t> ConnectionFor(std::uint32_t a_playerId) const;
		[[nodiscard]] nlohmann::json ChatMessage(std::uint32_t a_connectionId, std::string_view a_text, std::optional<std::uint64_t> a_messageId = std::nullopt);

		ProtocolHost& _protocol;
		const nlohmann::json& _world;
		ScriptFrameSender _send;
		ScriptHost _host;
		ScriptCommandRegistry _commands;
		ScriptViewRegistry _views;
		std::optional<std::vector<OutboundFrame>> _routed;
	};
}
