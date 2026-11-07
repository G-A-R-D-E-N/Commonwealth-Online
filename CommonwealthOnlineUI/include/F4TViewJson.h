#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace F4T::ViewJson
{
	inline std::string Dump(const nlohmann::json& a_json, int a_indent = -1, bool a_ensureAscii = false)
	{
		return a_json.dump(a_indent, ' ', a_ensureAscii, nlohmann::json::error_handler_t::replace);
	}
}
