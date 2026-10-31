#pragma once

#include "F4TConnectFailure.h"
#include "F4TGnsProtocolAdapter.h"
#include "F4TModpackProbeBudget.h"
#include "F4TModpackProbeCache.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace F4T::ClientModpackProbe
{
	struct ProbeProgress
	{
		std::size_t checkedEntries{};
		std::size_t totalEntries{};
		std::uint64_t hashedBytes{};
	};

	using ProbeProgressCallback = void (*)(const ProbeProgress&);

	void SetProbeProgressCallback(ProbeProgressCallback a_callback);

	struct ProbeResult
	{
		std::optional<ModpackManifest::Manifest> manifest;
		bool truncated{};
		bool dataDirectoryResolved{};
		bool loadOrderKnown{};
		std::vector<std::string> extraNatives;
		std::vector<F4T::ModpackProbeCache::FileFact> fileFacts;
		std::vector<std::string> loadedPlugins;
		std::optional<ModpackManifest::ProbeBudgetKind> budgetLimit;
		std::size_t checkedEntries{};
		std::size_t totalEntries{};
		std::uint64_t hashedBytes{};
	};

	[[nodiscard]] std::optional<std::filesystem::path> ResolveDataDirectory();

	[[nodiscard]] ProbeResult ProbeClientManifest(
		const ModpackManifest::Manifest& a_required,
		const std::filesystem::path& a_dataDirectory,
		const std::vector<std::string>& a_loadedPluginNames);

	[[nodiscard]] ProbeResult ProbeLocalManifest(const ModpackManifest::Manifest& a_required);

	[[nodiscard]] std::string BuildMismatchDetail(const ModpackManifest::Manifest& a_required, const ProbeResult& a_probed);

	[[nodiscard]] Networking::ConnectFailure MakeSessionRejectionFailure(
		const GnsProtocolAdapter::SessionRejection& a_rejection,
		std::string_view a_serverKey = {});

	[[nodiscard]] std::optional<std::string> CachedFingerprint(const std::string& a_serverKey);
	void RememberMatch(const std::string& a_serverKey, const std::string& a_fingerprint, const ProbeResult& a_probed);
	void Forget(const std::string& a_serverKey);
}
