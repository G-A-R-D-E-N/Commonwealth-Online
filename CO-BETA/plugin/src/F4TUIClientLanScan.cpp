#include "pch.h"

#include "F4TUIClientLanScan.h"

#include "F4TDebugLogging.h"
#include "F4TLanDiscovery.h"
#include "F4TUIClientJoinErrors.h"

#include <cstring>
#include <format>
#include <nlohmann/json.hpp>
#include <string>

namespace F4T::UIClientLanScan
{
	using namespace F4T::UIClient::JoinErrors;

	bool ScanToJson(
		char*       a_jsonOut,
		std::size_t a_jsonOutSize,
		char*       a_errorBuf,
		std::size_t a_errorBufSize)
	{
		try {
			std::string error;
			const auto discovered = F4T::LanDiscovery::Scan(error);
			F4T::DebugLogging::Log(
				"UIClient",
				"LAN scan complete servers={} error={}",
				discovered.size(),
				error.empty() ? std::string{ "<none>" } : F4T::DebugLogging::SanitizeLogText(error));

			nlohmann::json servers = nlohmann::json::array();
			for (const auto& server : discovered) {
				servers.push_back(nlohmann::json{
					{ "id", std::format("local:{}:{}", server.host, server.port) },
					{ "name", server.name },
					{ "host", server.host },
					{ "port", server.port },
					{ "region", "Local" },
					{ "worldspace", "Commonwealth" },
					{ "mode", "Co-op" },
					{ "players", server.players },
					{ "maxPlayers", server.maxPlayers },
					{ "ping", server.ping },
					{ "status", server.players >= server.maxPlayers ? "Full" : "Open" },
					{ "modded", false },
					{ "passworded", server.passworded },
					{ "favorite", false },
					{ "description", server.description }
				});
			}

			if (discovered.empty() && !error.empty()) {
				CopyError(a_errorBuf, a_errorBufSize, error);
				return false;
			}

			const auto dumped = servers.dump();
			if (dumped.size() + 1 > a_jsonOutSize) {
				CopyError(a_errorBuf, a_errorBufSize, "LAN scan JSON exceeds output buffer.");
				return false;
			}

			std::memcpy(a_jsonOut, dumped.c_str(), dumped.size() + 1);

			return true;
		} catch (const std::exception& ex) {
			CopyError(a_errorBuf, a_errorBufSize, std::format("LAN scan failed: {}", ex.what()));
			return false;
		} catch (...) {
			CopyError(a_errorBuf, a_errorBufSize, "LAN scan failed.");
			return false;
		}
	}
}
