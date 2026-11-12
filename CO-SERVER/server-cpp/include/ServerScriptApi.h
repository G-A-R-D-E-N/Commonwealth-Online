#pragma once

#include "ServerScriptHost.h"
#include "ServerScriptResources.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace CommonwealthOnline::Server::Scripting
{
	using ScriptCommandRun = std::function<void(const std::vector<std::string>&, std::uint32_t)>;

	enum class ScriptCommandResult : std::uint8_t
	{
		NoMatch,
		Ran,
		Refused
	};

	class ScriptCommandRegistry
	{
	public:
		[[nodiscard]] bool Add(std::string_view a_name, const void* a_owner, ScriptCommandRun a_run, bool a_adminOnly = false);
		void RemoveOwner(const void* a_owner);
		[[nodiscard]] ScriptCommandResult Dispatch(std::string_view a_line, std::uint32_t a_playerId, bool a_isAdmin);

	private:
		struct Entry
		{
			const void* owner{};
			ScriptCommandRun run;
			bool adminOnly{};
		};

		std::map<std::string, Entry, std::less<>> _commands;
	};

	using ScriptViewMessage = std::function<void(std::uint32_t, const nlohmann::json&)>;

	class ScriptViewRegistry
	{
	public:
		[[nodiscard]] bool Add(std::string_view a_resource, const void* a_owner, ScriptViewMessage a_run);
		void RemoveOwner(const void* a_owner);
		bool Dispatch(std::string_view a_resource, std::uint32_t a_playerId, const nlohmann::json& a_data);

	private:
		struct Entry
		{
			const void* owner{};
			ScriptViewMessage run;
		};

		std::map<std::string, Entry, std::less<>> _views;
	};

	[[nodiscard]] ScriptInstaller MakeScriptApiInstaller(
		ScriptHost a_host,
		ScriptCommandRegistry& a_commands,
		ScriptViewRegistry& a_views);
}
