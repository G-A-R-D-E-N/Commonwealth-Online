#include "ServerScriptApi.h"

#include <string>
#include <string_view>
#include <utility>

namespace CommonwealthOnline::Server::Scripting
{
	bool ScriptViewRegistry::Add(std::string_view a_resource, const void* a_owner, ScriptViewMessage a_run)
	{
		const auto found = _views.find(a_resource);
		if (found != _views.end() && found->second.owner != a_owner) {
			return false;
		}
		_views.insert_or_assign(std::string{ a_resource }, Entry{ a_owner, std::move(a_run) });
		return true;
	}

	void ScriptViewRegistry::RemoveOwner(const void* a_owner)
	{
		std::erase_if(_views, [a_owner](const auto& a_item) { return a_item.second.owner == a_owner; });
	}

	bool ScriptViewRegistry::Dispatch(std::string_view a_resource, std::uint32_t a_playerId, const nlohmann::json& a_data)
	{
		const auto found = _views.find(a_resource);
		if (found == _views.end() || !found->second.run) {
			return false;
		}
		const auto run = found->second.run;
		run(a_playerId, a_data);
		return true;
	}
}
