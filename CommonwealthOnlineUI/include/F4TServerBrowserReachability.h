#pragma once

#include "F4TServerBrowserData.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <optional>
#include <utility>
#include <vector>

namespace F4T::ServerBrowser::Reachability
{
	struct CheckTarget
	{
		std::string host;
		int         port{};
	};

	using UpdateSink = std::function<void(std::vector<ReachabilityUpdate>)>;

	struct PreparedList
	{
		std::vector<CheckTarget>                           targets;
		std::vector<std::pair<std::size_t, std::uint64_t>> epochs;
	};

	PreparedList Prepare(std::vector<ServerEntry>& a_servers);
	void         Run(const std::vector<CheckTarget>& a_targets, std::stop_token a_stop, const UpdateSink& a_sink);
	bool         IsCurrentEpoch(const std::string& a_host, int a_port, std::uint64_t a_epoch);
	void         NoteAnswer(const std::string& a_host, int a_port, int a_milliseconds);
	std::optional<ReachabilityResult> CurrentResult(const std::string& a_host, int a_port);
	void         ClearCached(const std::string& a_host, int a_port);
}
