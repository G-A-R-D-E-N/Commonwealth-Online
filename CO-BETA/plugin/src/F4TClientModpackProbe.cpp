#include "pch.h"

#include "F4TClientModpackProbe.h"

#include "F4TContentManifest.h"
#include "F4TIdentityProtocol.h"
#include "F4TLoadOrderSync.h"
#include "F4TModpackAutoFixClient.h"
#include "F4TModpackMismatchReport.h"
#include "F4TModpackProbe.h"
#include "F4TModpackProbeBudget.h"
#include "F4TNetworkingInternal.h"
#include "F4TServerModpackCache.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
	using F4T::ClientModpackProbe::ProbeProgress;
	using F4T::ClientModpackProbe::ProbeProgressCallback;
	using F4T::ClientModpackProbe::ProbeResult;
	using F4T::ModpackManifest::Entry;
	using F4T::ModpackManifest::EntryKind;
	using F4T::ModpackManifest::Manifest;
	using F4T::ModpackManifest::ProbeBudget;
	using F4T::ModpackManifest::ProbeBudgetExhausted;
	using F4T::ModpackManifest::ProbeBudgetKind;
	using F4T::ModpackManifest::ProbeBudgetLimit;
	using F4T::ModpackManifest::ProbeHashBytesFit;
	using F4T::ModpackManifest::kMaximumProbeEntries;
	using F4T::ModpackManifest::kMaximumProbeHashedBytes;
	using F4T::ModpackManifest::kMaximumProbeMilliseconds;

	constexpr std::wstring_view kDataDirectoryName = L"Data";
	constexpr std::wstring_view kNativeDirectoryName = L"F4SE";
	constexpr std::wstring_view kNativePluginsDirectoryName = L"Plugins";
	constexpr std::string_view kNativePathPrefix = "f4se/plugins/";
	constexpr std::size_t kMaximumFailureCodeBytes = 64;
	constexpr std::size_t kMaximumFailureReasonBytes = 256;
	constexpr std::size_t kMaximumFailureDetailBytes = 1600;
	constexpr std::size_t kMaximumExtraNativeNames = 8;
	constexpr std::string_view kMismatchFallback = "Your modpack does not match this server.";
	constexpr std::string_view kLoadOrderUnavailable =
		"The game's plugin list could not be read. Restart Fallout 4 and join again.";
	constexpr std::string_view kExtraNativesHeader = "Extra F4SE plugins (allowed, not required):";

	std::string LowerAscii(std::string_view a_text)
	{
		std::string lowered{ a_text };
		for (auto& character : lowered) {
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
		}
		return lowered;
	}

	std::string NativeDisplayName(std::string_view a_name)
	{
		const auto slash = a_name.rfind('/');
		return slash == std::string_view::npos ? std::string{ a_name } : std::string{ a_name.substr(slash + 1) };
	}

	bool EndsWithIgnoreCase(std::string_view a_name, std::string_view a_suffix) noexcept
	{
		if (a_name.size() < a_suffix.size()) {
			return false;
		}
		const auto start = a_name.size() - a_suffix.size();
		for (std::size_t index = 0; index < a_suffix.size(); ++index) {
			auto character = a_name[start + index];
			if (character >= 'A' && character <= 'Z') {
				character = static_cast<char>(character - 'A' + 'a');
			}
			if (character != a_suffix[index]) {
				return false;
			}
		}
		return true;
	}

	std::string SanitizeFailureDetail(std::string_view a_text, std::size_t a_limit, bool a_keepLineBreaks)
	{
		std::string text{ a_text.substr(0, a_text.size() < a_limit ? a_text.size() : a_limit) };
		for (auto& character : text) {
			const auto value = static_cast<unsigned char>(character);
			if (value >= 0x20 && value != 0x7F) {
				continue;
			}
			character = (a_keepLineBreaks && character == '\n') ? '\n' : ' ';
		}
		return text;
	}

	std::optional<std::wstring> WidenUtf8(std::string_view a_text)
	{
		if (a_text.empty() || a_text.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)())) {
			return std::nullopt;
		}
		const auto length = ::MultiByteToWideChar(
			CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), nullptr, 0);
		if (length <= 0) {
			return std::nullopt;
		}
		std::wstring wide(static_cast<std::size_t>(length), L'\0');
		if (::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, a_text.data(), static_cast<int>(a_text.size()), wide.data(), length) != length) {
			return std::nullopt;
		}
		return wide;
	}

	std::optional<std::filesystem::path> ModuleFilePath()
	{
		HMODULE module = nullptr;
		const auto address = reinterpret_cast<LPCWSTR>(reinterpret_cast<std::uintptr_t>(&ModuleFilePath));
		if (!::GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				address,
				&module) ||
			!module) {
			return std::nullopt;
		}
		std::array<wchar_t, 32768> path{};
		const auto length = ::GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size()) {
			return std::nullopt;
		}
		return std::filesystem::path{ std::wstring{ path.data(), length } };
	}

	std::optional<std::filesystem::path> GameExecutableDirectory()
	{
		std::array<wchar_t, 32768> path{};
		const auto length = ::GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
		if (length == 0 || length >= path.size()) {
			return std::nullopt;
		}
		return std::filesystem::path{ std::wstring{ path.data(), length } }.parent_path();
	}

	bool IsDirectory(const std::filesystem::path& a_path)
	{
		std::error_code error;
		return std::filesystem::is_directory(a_path, error) && !error;
	}

	std::optional<std::filesystem::path> GameDataDirectory()
	{
		const auto executableDirectory = GameExecutableDirectory();
		if (!executableDirectory) {
			return std::nullopt;
		}
		const auto candidate = *executableDirectory / kDataDirectoryName;
		return IsDirectory(candidate) ? std::optional{ candidate } : std::nullopt;
	}

	std::optional<std::filesystem::path> ModuleDataDirectory()
	{
		const auto module = ModuleFilePath();
		if (!module) {
			return std::nullopt;
		}
		const auto candidate = module->parent_path().parent_path().parent_path();
		if (candidate.filename() != kDataDirectoryName || !IsDirectory(candidate)) {
			return std::nullopt;
		}
		return candidate;
	}

	std::optional<std::filesystem::path> ResolveDataRelative(const std::filesystem::path& a_root, std::string_view a_name)
	{
		auto resolved = a_root;
		std::size_t cursor = 0;
		while (true) {
			const auto next = a_name.find('/', cursor);
			const auto segment = a_name.substr(cursor, next == std::string_view::npos ? std::string_view::npos : next - cursor);
			if (segment.empty() || segment == "." || segment == "..") {
				return std::nullopt;
			}
			const auto wide = WidenUtf8(segment);
			if (!wide) {
				return std::nullopt;
			}
			resolved /= *wide;
			if (next == std::string_view::npos) {
				return resolved;
			}
			cursor = next + 1;
		}
	}

	bool IsRegularFile(const std::filesystem::path& a_path)
	{
		std::error_code error;
		const auto status = std::filesystem::status(a_path, error);
		return !error && status.type() == std::filesystem::file_type::regular;
	}

	std::optional<std::int64_t> LastWriteTicks(const std::filesystem::path& a_path)
	{
		std::error_code error;
		const auto time = std::filesystem::last_write_time(a_path, error);
		if (error) {
			return std::nullopt;
		}
		return static_cast<std::int64_t>(time.time_since_epoch().count());
	}

	std::vector<std::string> ListNativePlugins(const std::filesystem::path& a_dataDirectory)
	{
		std::vector<std::string> names;
		try {
			const auto directory = a_dataDirectory / kNativeDirectoryName / kNativePluginsDirectoryName;
			std::error_code error;
			if (!std::filesystem::is_directory(directory, error) || error) {
				return names;
			}
			for (std::filesystem::directory_iterator entry(directory, error), end; !error && entry != end; entry.increment(error)) {
				const auto path = entry->path();
				if (!IsRegularFile(path)) {
					continue;
				}
				const auto filename = path.filename().string();
				if (!EndsWithIgnoreCase(filename, ".dll")) {
					continue;
				}
				names.push_back(std::string{ kNativePathPrefix } + filename);
			}
		} catch (...) {
			names.clear();
		}
		return names;
	}

	struct EntryProbe
	{
		std::optional<Entry> entry;
		std::uint64_t hashedBytes{};
		bool overBudget{};
		std::uint64_t sizeBytes{};
		std::int64_t modifiedTicks{};
	};

	EntryProbe ProbeEntry(const Entry& a_required, const std::filesystem::path& a_path, const ProbeBudget& a_budget)
	{
		if (!IsRegularFile(a_path)) {
			return {};
		}
		std::error_code error;
		const auto size = std::filesystem::file_size(a_path, error);
		if (error) {
			return {};
		}
		const auto bytes = static_cast<std::uint64_t>(size);
		const auto ticks = LastWriteTicks(a_path).value_or(0);
		Entry probed = a_required;
		probed.sha256Hex.clear();
		probed.sizeBytes.reset();
		if (a_required.sizeBytes && a_required.sha256Hex.empty()) {
			probed.sizeBytes = bytes;
			return EntryProbe{ std::move(probed), 0, false, bytes, ticks };
		}
		if (!ProbeHashBytesFit(a_budget, bytes)) {
			return EntryProbe{ std::nullopt, 0, true, bytes, ticks };
		}
		const auto hash = F4T::ContentManifest::Sha256HexFile(a_path);
		if (!hash) {
			return {};
		}
		probed.sha256Hex = *hash;
		if (a_required.sizeBytes) {
			probed.sizeBytes = bytes;
		}
		return EntryProbe{ std::move(probed), bytes, false, bytes, ticks };
	}

	struct ProbeRun
	{
		ProbeBudget budget;
		std::chrono::steady_clock::time_point started{ std::chrono::steady_clock::now() };
	};

	std::atomic<ProbeProgressCallback> g_progressCallback{ nullptr };
	thread_local std::chrono::steady_clock::time_point t_lastProgress{};

	constexpr auto kProgressInterval = std::chrono::milliseconds{ 200 };

	void ReportProgress(std::size_t a_checked, std::size_t a_total, std::uint64_t a_hashedBytes)
	{
		const auto callback = g_progressCallback.load(std::memory_order_acquire);
		if (!callback) {
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		const bool throttled = t_lastProgress != std::chrono::steady_clock::time_point{} && now - t_lastProgress < kProgressInterval;
		if (throttled && !(a_total > 0 && a_checked >= a_total)) {
			return;
		}
		t_lastProgress = now;
		callback(ProbeProgress{ a_checked, a_total, a_hashedBytes });
	}

	void RefreshElapsed(ProbeRun& a_run)
	{
		const auto elapsed = std::chrono::steady_clock::now() - a_run.started;
		const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
		a_run.budget.elapsedMilliseconds = milliseconds > 0 ? static_cast<std::uint64_t>(milliseconds) : 0;
	}

	std::string FormatBudgetDetail(const ProbeResult& a_probed)
	{
		const auto checked = a_probed.checkedEntries;
		const auto total = a_probed.totalEntries;
		switch (a_probed.budgetLimit.value_or(ProbeBudgetKind::entries)) {
		case ProbeBudgetKind::entries:
			return std::format(
				"Your modpack was not fully checked: the {}-entry probe limit was reached after {} of {} files. "
				"Ask the server owner for a smaller manifest.",
				kMaximumProbeEntries,
				checked,
				total);
		case ProbeBudgetKind::hashedBytes:
			return std::format(
				"Your modpack was not fully checked: the {} MiB hashed-content limit was reached after {} of {} files. "
				"Ask the server owner for a smaller manifest.",
				kMaximumProbeHashedBytes / (1024ull * 1024ull),
				checked,
				total);
		case ProbeBudgetKind::elapsedMilliseconds:
		default:
			return std::format(
				"Your modpack was not fully checked: the {} second probe time limit was reached after {} of {} files.",
				kMaximumProbeMilliseconds / 1000,
				checked,
				total);
		}
	}

	std::string FormatExtraNatives(const std::vector<std::string>& a_names)
	{
		if (a_names.empty()) {
			return {};
		}
		auto names = a_names;
		std::sort(names.begin(), names.end(), [](const std::string& a_lhs, const std::string& a_rhs) {
			return LowerAscii(a_lhs) < LowerAscii(a_rhs);
		});
		std::string text{ kExtraNativesHeader };
		text += '\n';
		const auto shown = names.size() < kMaximumExtraNativeNames ? names.size() : kMaximumExtraNativeNames;
		for (std::size_t index = 0; index < shown; ++index) {
			text += "  ";
			text += NativeDisplayName(names[index]);
			text += '\n';
		}
		if (names.size() > shown) {
			text += "  ...and " + std::to_string(names.size() - shown) + " more\n";
		}
		return text;
	}
}

namespace F4T::ClientModpackProbe
{
	std::optional<std::filesystem::path> ResolveDataDirectory()
	{
		if (const auto game = GameDataDirectory()) {
			return game;
		}
		if (const auto module = ModuleDataDirectory()) {
			return module;
		}
		std::error_code error;
		const auto current = std::filesystem::current_path(error);
		if (error) {
			return std::nullopt;
		}
		const auto data = current / kDataDirectoryName;
		return IsDirectory(data) ? std::optional{ data } : std::nullopt;
	}

	ProbeResult ProbeClientManifest(
		const Manifest& a_required,
		const std::filesystem::path& a_dataDirectory,
		const std::vector<std::string>& a_loadedPluginNames)
	{
		ProbeResult result;
		try {
			const auto required = ModpackManifest::NormalizeManifest(a_required);
			if (!required) {
				return result;
			}
			result.dataDirectoryResolved = true;
			bool requiresPlugins = false;
			for (const auto& entry : required->entries) {
				if (entry.kind == EntryKind::plugin) {
					requiresPlugins = true;
					break;
				}
			}
			result.loadOrderKnown = !a_loadedPluginNames.empty() || !requiresPlugins;
			if (!result.loadOrderKnown) {
				return result;
			}
			result.loadedPlugins = a_loadedPluginNames;
			const auto ranks = F4T::ModpackProbe::RankRequiredPlugins(*required, a_loadedPluginNames);
			result.totalEntries = required->entries.size();
			ProbeRun run;
			Manifest probed;
			probed.entries.reserve(required->entries.size() < ModpackManifest::kMaximumProbeEntries
					? required->entries.size()
					: ModpackManifest::kMaximumProbeEntries);
			std::size_t nextRank = 0;
			std::vector<std::uint32_t> acceptedRanks;
			for (const auto& entry : required->entries) {
				RefreshElapsed(run);
				if (ProbeBudgetExhausted(run.budget)) {
					result.truncated = true;
					result.budgetLimit = ProbeBudgetLimit(run.budget);
					break;
				}
				++run.budget.entries;
				result.checkedEntries = run.budget.entries;
				ReportProgress(run.budget.entries, result.totalEntries, run.budget.hashedBytes);
				std::uint32_t rank = 0;
				if (entry.kind == EntryKind::plugin) {
					if (nextRank >= ranks.size()) {
						continue;
					}
					rank = ranks[nextRank++];
					if (rank == F4T::ModpackProbe::kMissingPluginOrder) {
						continue;
					}
				}
				const auto path = ResolveDataRelative(a_dataDirectory, entry.name);
				if (!path) {
					continue;
				}
				auto item = ProbeEntry(entry, *path, run.budget);
				if (item.overBudget) {
					result.truncated = true;
					result.budgetLimit = ProbeBudgetKind::hashedBytes;
					break;
				}
				if (!item.entry) {
					continue;
				}
				run.budget.hashedBytes += item.hashedBytes;
				result.fileFacts.push_back(ModpackProbeCache::FileFact{ entry.name, item.sizeBytes, item.modifiedTicks });
				if (entry.kind == EntryKind::plugin) {
					acceptedRanks.push_back(rank);
				}
				probed.entries.push_back(std::move(*item.entry));
			}
			if (result.truncated) {
				return result;
			}
			const auto orders = F4T::ModpackProbe::ContiguousOrdersFromRanks(acceptedRanks);
			std::size_t pluginIndex = 0;
			for (auto& probedEntry : probed.entries) {
				if (probedEntry.kind == EntryKind::plugin) {
					probedEntry.order = orders[pluginIndex++];
				}
			}
			std::uint32_t nextOrder = static_cast<std::uint32_t>(acceptedRanks.size());
			for (const auto& name : F4T::ModpackProbe::FindExtraPlugins(*required, a_loadedPluginNames)) {
				RefreshElapsed(run);
				if (ProbeBudgetExhausted(run.budget)) {
					result.truncated = true;
					result.budgetLimit = ProbeBudgetLimit(run.budget);
					return result;
				}
				++run.budget.entries;
				const auto path = ResolveDataRelative(a_dataDirectory, name);
				if (!path || !IsRegularFile(*path)) {
					continue;
				}
				std::error_code error;
				const auto size = std::filesystem::file_size(*path, error);
				if (error) {
					continue;
				}
				Entry entry;
				entry.kind = EntryKind::plugin;
				entry.pluginType = F4T::ModpackProbe::PluginTypeFromName(name);
				entry.name = name;
				entry.sizeBytes = static_cast<std::uint64_t>(size);
				entry.order = nextOrder++;
				probed.entries.push_back(std::move(entry));
			}
			result.hashedBytes = run.budget.hashedBytes;
			result.extraNatives = F4T::ModpackProbe::FindExtraNatives(*required, ListNativePlugins(a_dataDirectory));
			if (!probed.entries.empty()) {
				result.manifest = ModpackManifest::NormalizeManifest(std::move(probed));
			}
			return result;
		} catch (...) {
			return ProbeResult{};
		}
	}

	void SetProbeProgressCallback(ProbeProgressCallback a_callback)
	{
		g_progressCallback.store(a_callback, std::memory_order_release);
	}

	std::optional<std::string> CachedFingerprint(const std::string& a_serverKey)
	{
		try {
			if (a_serverKey.empty()) {
				return std::nullopt;
			}
			const auto blob = ServerModpackCache::Lookup(a_serverKey);
			if (!blob) {
				return std::nullopt;
			}
			const auto cached = ModpackProbeCache::ParseEntry(*blob);
			if (!cached) {
				Forget(a_serverKey);
				return std::nullopt;
			}
			const auto dataDirectory = ResolveDataDirectory();
			if (!dataDirectory) {
				return std::nullopt;
			}
			std::vector<ModpackProbeCache::FileFact> current;
			current.reserve(cached->files.size());
			for (const auto& file : cached->files) {
				ModpackProbeCache::FileFact fact{ file.path, ModpackProbeCache::kMissingFileSize, 0 };
				const auto path = ResolveDataRelative(*dataDirectory, file.path);
				if (path && IsRegularFile(*path)) {
					std::error_code error;
					const auto size = std::filesystem::file_size(*path, error);
					const auto ticks = LastWriteTicks(*path);
					if (!error && ticks) {
						fact.sizeBytes = static_cast<std::uint64_t>(size);
						fact.modifiedTicks = *ticks;
					}
				}
				current.push_back(std::move(fact));
			}
			if (ModpackProbeCache::Validate(*cached, current, F4T::LoadOrderSync::LoadedPluginNames()) != ModpackProbeCache::Match::hit) {
				Forget(a_serverKey);
				return std::nullopt;
			}
			return cached->fingerprint;
		} catch (...) {
			return std::nullopt;
		}
	}

	void RememberMatch(const std::string& a_serverKey, const std::string& a_fingerprint, const ProbeResult& a_probed)
	{
		try {
			if (a_serverKey.empty() || a_fingerprint.size() != 64 || a_probed.fileFacts.empty() || a_probed.loadedPlugins.empty()) {
				return;
			}
			const ModpackProbeCache::Entry entry{ a_fingerprint, a_probed.fileFacts, a_probed.loadedPlugins };
			const auto blob = ModpackProbeCache::SerializeEntry(entry);
			if (!blob) {
				return;
			}
			if (!ServerModpackCache::Store(a_serverKey, *blob)) {
				F4T::Networking::Detail::LogThrottledWarning(
					"modpack_cache_store_failed",
					"Could not save the modpack check cache; the next join will hash the files again.");
			}
		} catch (...) {
		}
	}

	void Forget(const std::string& a_serverKey)
	{
		try {
			if (!a_serverKey.empty()) {
				(void)ServerModpackCache::Clear(a_serverKey);
			}
		} catch (...) {
		}
	}

	ProbeResult ProbeLocalManifest(const Manifest& a_required)
	{
		try {
			const auto dataDirectory = ResolveDataDirectory();
			if (!dataDirectory) {
				return ProbeResult{};
			}
			return ProbeClientManifest(a_required, *dataDirectory, F4T::LoadOrderSync::LoadedPluginNames());
		} catch (...) {
			return ProbeResult{};
		}
	}

	std::string BuildMismatchDetail(const Manifest& a_required, const ProbeResult& a_probed)
	{
		if (a_required.entries.empty()) {
			return std::string{ kMismatchFallback };
		}
		try {
			if (!a_probed.dataDirectoryResolved) {
				return ModpackManifest::FormatRequiredModpackList(a_required);
			}
			if (!a_probed.loadOrderKnown) {
				return std::string{ kLoadOrderUnavailable };
			}
			std::string detail;
			if (a_probed.truncated) {
				detail = FormatBudgetDetail(a_probed);
				detail += '\n';
				detail += ModpackManifest::FormatRequiredModpackList(a_required, true);
			} else if (a_probed.manifest) {
				const auto report = ModpackManifest::BuildModpackMismatchReport(a_required, *a_probed.manifest);
				if (report) {
					detail = ModpackManifest::FormatModpackMismatchReport(*report);
				} else {
					detail = ModpackManifest::FormatRequiredModpackList(a_required);
				}
			} else {
				detail = ModpackManifest::FormatRequiredModpackList(a_required);
			}
			const auto warnings = FormatExtraNatives(a_probed.extraNatives);
			if (!warnings.empty()) {
				if (!detail.empty()) {
					detail += '\n';
				}
				detail += warnings;
			}
			return detail;
		} catch (...) {
			return std::string{ kMismatchFallback };
		}
	}

	Networking::ConnectFailure MakeSessionRejectionFailure(
		const GnsProtocolAdapter::SessionRejection& a_rejection,
		std::string_view a_serverKey)
	{
		Networking::ConnectFailure failure{
			.kind = Networking::ConnectFailureKind::SessionRejected,
			.code = SanitizeFailureDetail(a_rejection.code, kMaximumFailureCodeBytes, false)
		};
		std::string reason = a_rejection.reason;
		failure.modpackUrl = F4T::Identity::ExtractModpackPageUrl(reason);
		std::string detail{ SanitizeFailureDetail(reason, kMaximumFailureReasonBytes, true) };
		if (const auto name = ModpackManifest::FormatRequiredModpackName(a_rejection.requiredModpackName); !name.empty()) {
			detail = detail.empty() ? name : name + '\n' + detail;
		}
		if (a_rejection.requiredModpack) {
			const auto probed = ProbeLocalManifest(*a_rejection.requiredModpack);
			if (!probed.extraNatives.empty()) {
				std::string names;
				for (std::size_t index = 0; index < probed.extraNatives.size() && index < kMaximumExtraNativeNames; ++index) {
					if (!names.empty()) {
						names += ", ";
					}
					names += NativeDisplayName(probed.extraNatives[index]);
				}
				if (probed.extraNatives.size() > kMaximumExtraNativeNames) {
					names += std::format(", and {} more", probed.extraNatives.size() - kMaximumExtraNativeNames);
				}
				F4T::Networking::Detail::LogWarningWithLocalPlayerPrefix(std::format(
					"Extra F4SE plugins are loaded but not required by this server: {}",
					names));
			}
			const auto mismatch = BuildMismatchDetail(*a_rejection.requiredModpack, probed);
			if (!mismatch.empty()) {
				if (!detail.empty()) {
					detail += '\n';
				}
				detail += mismatch;
			}
			if (probed.manifest) {
				if (const auto matching = ModpackManifest::MatchingFingerprint(*a_rejection.requiredModpack, *probed.manifest)) {
					failure.matchingModpackFingerprint = *matching;
					RememberMatch(std::string{ a_serverKey }, *matching, probed);
				} else if (!probed.truncated && probed.loadOrderKnown && a_rejection.code == "modpack_mismatch") {
					F4T::ModpackAutoFixClient::NoteMismatch(a_serverKey, a_rejection.requiredModpackFingerprint, *a_rejection.requiredModpack, *probed.manifest);
				}
			}
		}
		failure.detail = SanitizeFailureDetail(detail, kMaximumFailureDetailBytes, true);
		return failure;
	}
}
