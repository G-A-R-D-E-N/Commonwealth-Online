#include "ServerScriptResources.h"
#include "F4TContentManifest.h"
#include "F4TStateBagProtocol.h"

#include "F4TModpackContentPolicy.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace CommonwealthOnline::Server::Scripting
{
	namespace
	{
		constexpr std::array<std::string_view, 5> kStateNames{ "invalid", "pending", "running", "stopped", "failed" };
		constexpr std::uintmax_t kMaxScriptBytes{ 1024U * 1024U };
		constexpr std::uintmax_t kMaxManifestBytes{ 64U * 1024U };
		constexpr std::uintmax_t kMaxResourceBytes{ 8U * 1024U * 1024U };

		std::string ReadText(const std::filesystem::path& a_path)
		{
			std::ifstream in(a_path, std::ios::binary);
			if (!in) {
				throw std::invalid_argument("cannot read " + a_path.string());
			}
			return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
		}

		bool Contained(const std::filesystem::path& a_base, const std::filesystem::path& a_full)
		{
			return std::mismatch(a_base.begin(), a_base.end(), a_full.begin(), a_full.end()).first == a_base.end();
		}

		std::string LogSafe(std::string a_text)
		{
			std::replace_if(a_text.begin(), a_text.end(), [](unsigned char a_char) { return a_char < 0x20U || a_char == 0x7FU; }, '?');
			return a_text;
		}

		bool CanRestart(const ResourceEntry& a_entry, double a_now, const Limits& a_limits)
		{
			if (!a_entry.manifest.restartOnError) {
				return false;
			}
			const auto recent = std::count_if(a_entry.restartTimes.begin(), a_entry.restartTimes.end(), [a_now, &a_limits](double a_restart) {
				return a_now - a_restart < a_limits.restartWindowSeconds;
			});
			return static_cast<std::size_t>(recent) < static_cast<std::size_t>(a_limits.restartLimit);
		}

		void RecordRestart(ResourceEntry& a_entry, double a_now, const Limits& a_limits)
		{
			std::erase_if(a_entry.restartTimes, [a_now, &a_limits](double a_restart) { return a_now - a_restart >= a_limits.restartWindowSeconds; });
			a_entry.restartTimes.push_back(a_now);
		}

		double SteadySeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::string CodeDigest(std::string_view a_manifestText, const std::vector<std::pair<std::string, std::string>>& a_scripts)
		{
			std::string material;
			const auto append = [&material](std::string_view a_part) {
				material += std::to_string(a_part.size());
				material += ':';
				material += a_part;
			};
			append(a_manifestText);
			for (const auto& [file, text] : a_scripts) {
				append(file);
				append(text);
			}
			return F4T::ContentManifest::Sha256Hex(material);
		}

		ResourceEntry ParseEntry(const std::filesystem::path& a_directory)
		{
			ResourceEntry entry;
			try {
				const auto base = std::filesystem::canonical(a_directory);
				const auto manifest = std::filesystem::canonical(a_directory / "co-resource.json");
				if (!Contained(base, manifest)) {
					throw std::invalid_argument("co-resource.json escapes the resource");
				}
				if (!std::filesystem::is_regular_file(manifest)) {
					throw std::invalid_argument("co-resource.json is not a regular file");
				}
				const auto manifestText = ReadBoundedText(manifest, kMaxManifestBytes, "co-resource.json");
				const auto json = nlohmann::json::parse(manifestText);
				entry.fingerprint = manifestText;
				entry.manifest.name = json.value("name", std::string{});
				if (entry.manifest.name != a_directory.filename().string()) {
					throw std::invalid_argument("name must match the directory name");
				}
				if (!F4T::StateBagProtocol::IsValidResourceName(entry.manifest.name)) {
					throw std::invalid_argument("name is not a valid resource name");
				}
				entry.manifest.version = json.value("version", std::string{});
				entry.manifest.permissions = json.value("permissions", std::vector<std::string>{});
				entry.manifest.dependencies = json.value("dependencies", std::vector<std::string>{});
				entry.manifest.views = json.value("views", std::vector<std::string>{});
				for (const auto& view : entry.manifest.views) {
					if (!F4T::ModpackManifest::IsValidViewName(entry.manifest.name + "/" + view)) {
						throw std::invalid_argument("views entry is not a resource-relative view path: " + view);
					}
				}
				entry.manifest.priority = json.value("priority", 0);
				const auto behavior = json.value("errorBehavior", std::string{ "stop" });
				if (behavior != "stop" && behavior != "restart") {
					throw std::invalid_argument("errorBehavior must be stop or restart");
				}
				entry.manifest.restartOnError = behavior == "restart";
				std::uintmax_t total{};
				for (const auto& file : json.value("server", std::vector<std::string>{})) {
					const auto full = std::filesystem::weakly_canonical(base / file);
					if (file.empty() || !Contained(base, full)) {
						throw std::invalid_argument("server file escapes the resource: " + file);
					}
					if (!std::filesystem::is_regular_file(full)) {
						throw std::invalid_argument("server entry is not a regular file: " + file);
					}
					const auto text = ReadBoundedText(full, kMaxScriptBytes, "server file " + file);
					total += text.size();
					if (total > kMaxResourceBytes) {
						throw std::invalid_argument("server files are larger than " + std::to_string(kMaxResourceBytes) + " bytes in total");
					}
					entry.fingerprint += text;
					entry.scripts.emplace_back(std::filesystem::path(file).lexically_normal().generic_string(), text);
				}
				entry.digest = CodeDigest(manifestText, entry.scripts);
				entry.state = ResourceState::Pending;
			} catch (const std::exception& a_error) {
				entry.state = ResourceState::Invalid;
				entry.detail = a_error.what();
			}
			return entry;
		}

		AdminActionResult ToAction(const std::string& a_error, const char* a_success)
		{
			return a_error.empty() ? AdminActionResult{ true, a_success, {} } : AdminActionResult{ false, a_error, {} };
		}
	}

	std::string ReadBoundedText(const std::filesystem::path& a_path, std::uintmax_t a_maxBytes, const std::string& a_label)
	{
		std::ifstream in(a_path, std::ios::binary);
		if (!in) {
			throw std::invalid_argument("cannot read " + a_path.string());
		}
		std::string text;
		std::array<char, 4096> chunk{};
		for (;;) {
			const auto room = a_maxBytes + 1U - text.size();
			in.read(chunk.data(), static_cast<std::streamsize>(std::min<std::uintmax_t>(chunk.size(), room)));
			const auto got = static_cast<std::size_t>(in.gcount());
			if (text.size() + got > a_maxBytes) {
				throw std::invalid_argument(a_label + " is larger than " + std::to_string(a_maxBytes) + " bytes");
			}
			text.append(chunk.data(), got);
			if (!in) {
				return text;
			}
		}
	}

	ResourceManager::ResourceManager(std::filesystem::path a_root, Limits a_limits, LogSink a_log, ScriptInstaller a_installer, CpuMeter a_cpuMeter) :
		_root{ std::move(a_root) },
		_limits{ a_limits },
		_log{ std::move(a_log) },
		_installer{ std::move(a_installer) },
		_cpuMeter{ std::move(a_cpuMeter) }
	{
		if (!_log) {
			_log = [](std::string_view a_line) { std::cout << a_line << std::endl; };
		}
		if (!_cpuMeter) {
			_cpuMeter = SteadySeconds;
		}
	}

	void ResourceManager::Refresh()
	{
		Scan();
		Reconcile();
	}

	void ResourceManager::Tick(double a_now)
	{
		_now = a_now;
		if (!_started) {
			_started = true;
			Refresh();
		}
		std::vector<ResourceEntry*> running;
		for (auto& [key, entry] : _entries) {
			if (entry.runtime) {
				running.push_back(&entry);
			}
		}
		const auto tickEnd = std::chrono::steady_clock::now() + _limits.callbackBudget;
		const std::size_t count = running.size();
		const std::size_t first = count ? _cursor % count : 0;
		bool failed = false;
		for (std::size_t turn = 0; turn < count && std::chrono::steady_clock::now() < tickEnd; ++turn) {
			const std::size_t index = (first + turn) % count;
			_cursor = index + 1;
			ResourceEntry& entry = *running[index];
			const double started = _cpuMeter();
			entry.runtime->Tick(a_now);
			const double spent = _cpuMeter() - started;
			if (entry.runtime->GetFault() != Fault::None) {
				Retire(entry, entry.runtime->GetError());
				failed = true;
				continue;
			}
			entry.cpuBusySeconds += spent;
			const double elapsed = a_now - entry.cpuWindowStart;
			if (elapsed > 0.0 && elapsed >= _limits.cpuShareWindowSeconds) {
				const double share = entry.cpuBusySeconds / elapsed;
				entry.cpuWindowStart = a_now;
				entry.cpuBusySeconds = 0.0;
				if (share > _limits.cpuShareThreshold) {
					Retire(entry, "sustained CPU use: " + std::to_string(static_cast<int>(share * 100.0)) + "% of manager time over " + std::to_string(static_cast<int>(elapsed)) + " s");
					failed = true;
				}
			}
		}
		if (failed) {
			Reconcile();
		}
	}

	void ResourceManager::Retire(ResourceEntry& a_entry, std::string a_detail)
	{
		a_entry.detail = std::move(a_detail);
		a_entry.runtime.reset();
		if (CanRestart(a_entry, _now, _limits)) {
			RecordRestart(a_entry, _now, _limits);
			a_entry.state = ResourceState::Pending;
		} else {
			a_entry.state = ResourceState::Failed;
		}
	}

	nlohmann::json ResourceManager::List() const
	{
		auto list = nlohmann::json::array();
		for (const auto& [name, entry] : _entries) {
			list.push_back(nlohmann::json{
				{ "name", name },
				{ "version", entry.manifest.version },
				{ "state", std::string{ kStateNames[static_cast<std::size_t>(entry.state)] } },
				{ "detail", entry.detail },
				{ "permissions", entry.manifest.permissions },
				{ "approved", Approved(entry) } });
		}
		return list;
	}

	std::string ResourceManager::Ensure(std::string_view a_name)
	{
		const auto entry = _entries.find(a_name);
		if (entry == _entries.end()) {
			return "unknown resource";
		}
		return Activate(entry->second);
	}

	std::string ResourceManager::Stop(std::string_view a_name)
	{
		const auto entry = _entries.find(a_name);
		if (entry == _entries.end()) {
			return "unknown resource";
		}
		if (entry->second.state == ResourceState::Invalid) {
			return entry->second.detail;
		}
		entry->second.desired = false;
		entry->second.runtime.reset();
		entry->second.state = ResourceState::Stopped;
		entry->second.detail = "stopped by admin";
		Reconcile();
		return {};
	}

	std::string ResourceManager::Approve(std::string_view a_name)
	{
		const auto entry = _entries.find(a_name);
		if (entry == _entries.end()) {
			return "unknown resource";
		}
		if (entry->second.state == ResourceState::Invalid) {
			return entry->second.detail;
		}
		const auto previous = _approvals;
		_approvals[entry->first] = ResourceApproval{ entry->second.manifest.permissions, entry->second.digest };
		if (const auto error = SaveApprovals(); !error.empty()) {
			_approvals = previous;
			return error;
		}
		return Activate(entry->second);
	}

	std::string ResourceManager::Activate(ResourceEntry& a_entry)
	{
		if (a_entry.state == ResourceState::Invalid) {
			return a_entry.detail;
		}
		a_entry.desired = true;
		a_entry.restartTimes.clear();
		if (a_entry.state != ResourceState::Running) {
			a_entry.state = ResourceState::Pending;
		}
		Reconcile();
		return a_entry.state == ResourceState::Running ? std::string{} : a_entry.detail;
	}

	void ResourceManager::Scan()
	{
		std::map<std::string, ResourceEntry, std::less<>> found;
		try {
			LoadApprovals();
			if (std::filesystem::is_directory(_root)) {
				for (const auto& item : std::filesystem::directory_iterator(_root)) {
					if (item.is_directory() && std::filesystem::exists(item.path() / "co-resource.json")) {
						found.emplace(item.path().filename().string(), ParseEntry(item.path()));
					}
				}
			}
		} catch (const std::exception& a_error) {
			_log(std::string{ "[scripts] scan failed: " } + a_error.what());
			return;
		}
		std::erase_if(_entries, [&found](const auto& a_item) { return !found.contains(a_item.first); });
		const auto announce = [this](const std::string& a_name, const ResourceEntry& a_entry) {
			if (a_entry.state == ResourceState::Pending && !Approved(a_entry)) {
				_log("[scripts] " + LogSafe(a_name) + " " + UnapprovedDetail(a_entry));
			}
		};
		for (auto& [name, fresh] : found) {
			const auto existing = _entries.find(name);
			if (existing == _entries.end()) {
				announce(name, fresh);
				_entries.emplace(name, std::move(fresh));
			} else if (existing->second.state == ResourceState::Invalid || existing->second.fingerprint != fresh.fingerprint || existing->second.digest != fresh.digest) {
				fresh.desired = existing->second.desired;
				existing->second = std::move(fresh);
				announce(name, existing->second);
			}
		}
	}

	void ResourceManager::Reconcile()
	{
		std::vector<ResourceEntry*> ordered;
		for (auto& [key, entry] : _entries) {
			ordered.push_back(&entry);
		}
		std::stable_sort(ordered.begin(), ordered.end(), [](const ResourceEntry* a_left, const ResourceEntry* a_right) {
			return a_left->manifest.priority > a_right->manifest.priority;
		});
		for (bool changed = true; changed;) {
			changed = false;
			for (ResourceEntry* entry : ordered) {
				if (!entry->desired || entry->state == ResourceState::Invalid || entry->state == ResourceState::Failed) {
					continue;
				}
				if (entry->runtime) {
					if (!Approved(*entry) || !DependenciesRunning(*entry)) {
						entry->runtime.reset();
						entry->state = ResourceState::Pending;
						entry->detail = Approved(*entry) ? std::string{} : UnapprovedDetail(*entry);
						changed = true;
					}
					continue;
				}
				if (!Approved(*entry)) {
					entry->state = ResourceState::Pending;
					entry->detail = UnapprovedDetail(*entry);
					continue;
				}
				if (!DependenciesRunning(*entry)) {
					entry->state = ResourceState::Pending;
					entry->detail = "waiting for dependencies";
					continue;
				}
				changed = StartEntry(*entry) || changed;
			}
		}
	}

	bool ResourceManager::StartEntry(ResourceEntry& a_entry)
	{
		try {
			for (;;) {
				a_entry.runtime = std::make_unique<ScriptRuntime>(a_entry.manifest.name, _limits, _log);
				if (_installer) _installer(*a_entry.runtime, a_entry.manifest.permissions);
				bool loaded = true;
				for (const auto& [file, text] : a_entry.scripts) {
					if (!a_entry.runtime->Load(text, file, _now)) {
						loaded = false;
						break;
					}
				}
				if (loaded) {
					a_entry.state = ResourceState::Running;
					a_entry.detail.clear();
					a_entry.cpuWindowStart = _now;
					a_entry.cpuBusySeconds = 0.0;
					return true;
				}
				const bool retry = CanRestart(a_entry, _now, _limits);
				a_entry.state = ResourceState::Failed;
				a_entry.detail = a_entry.runtime->GetError();
				a_entry.runtime.reset();
				if (!retry) {
					return false;
				}
				RecordRestart(a_entry, _now, _limits);
			}
		} catch (const std::exception& a_error) {
			a_entry.runtime.reset();
			a_entry.state = ResourceState::Failed;
			a_entry.detail = a_error.what();
			_log(std::string{ "[scripts] start failed for " } + a_entry.manifest.name + ": " + a_error.what());
			return false;
		}
	}

	bool ResourceManager::Approved(const ResourceEntry& a_entry) const
	{
		const auto granted = _approvals.find(a_entry.manifest.name);
		if (granted == _approvals.end() || granted->second.digest != a_entry.digest) {
			return false;
		}
		return std::all_of(a_entry.manifest.permissions.begin(), a_entry.manifest.permissions.end(), [&granted](const std::string& a_permission) {
			return std::find(granted->second.permissions.begin(), granted->second.permissions.end(), a_permission) != granted->second.permissions.end();
		});
	}

	std::string ResourceManager::UnapprovedDetail(const ResourceEntry& a_entry) const
	{
		const auto granted = _approvals.find(a_entry.manifest.name);
		return granted != _approvals.end() && granted->second.digest != a_entry.digest ? "code changed since approval" : "awaiting approval";
	}

	bool ResourceManager::DependenciesRunning(const ResourceEntry& a_entry) const
	{
		return std::all_of(a_entry.manifest.dependencies.begin(), a_entry.manifest.dependencies.end(), [this](const std::string& a_name) {
			const auto dependency = _entries.find(a_name);
			return dependency != _entries.end() && dependency->second.state == ResourceState::Running;
		});
	}

	void ResourceManager::LoadApprovals()
	{
		const auto path = _root / "approvals.json";
		if (!std::filesystem::exists(path)) {
			_approvals.clear();
			return;
		}
		try {
			const auto json = nlohmann::json::parse(ReadText(path));
			if (!json.is_object()) {
				throw std::invalid_argument("approvals.json must be an object");
			}
			std::map<std::string, ResourceApproval> loaded;
			for (const auto& [name, value] : json.items()) {
				if (!value.is_object() || !value.contains("digest") || !value.contains("permissions")) {
					_log("[scripts] approval ignored for " + LogSafe(name) + ": old format, approve it again");
					continue;
				}
				const auto& permissions = value.at("permissions");
				const bool wellFormed = permissions.is_array() && value.at("digest").is_string() && std::all_of(permissions.begin(), permissions.end(), [](const nlohmann::json& a_permission) { return a_permission.is_string(); });
				if (!wellFormed) {
					_log("[scripts] approval ignored for " + LogSafe(name) + ": malformed entry");
					continue;
				}
				loaded.emplace(name, ResourceApproval{ permissions.get<std::vector<std::string>>(), value.at("digest").get<std::string>() });
			}
			_approvals = std::move(loaded);
		} catch (const std::exception& a_error) {
			_log(std::string{ "[scripts] approvals.json ignored: " } + a_error.what());
		}
	}

	std::string ResourceManager::SaveApprovals() const
	{
		auto document = nlohmann::json::object();
		for (const auto& [name, approval] : _approvals) {
			document[name] = nlohmann::json{ { "permissions", approval.permissions }, { "digest", approval.digest } };
		}
		const auto temp = _root / "approvals.json.tmp";
		std::error_code error;
		{
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			out << document.dump(2);
			out.close();
			if (!out) {
				std::filesystem::remove(temp, error);
				return "cannot write approvals.json";
			}
		}
		std::filesystem::rename(temp, _root / "approvals.json", error);
		if (error) {
			std::filesystem::remove(temp, error);
			return "cannot write approvals.json";
		}
		return {};
	}

	void BindAdminHooks(ResourceManager& a_manager, AdminCommandHooks& a_hooks)
	{
		a_hooks.resources = [&a_manager] { return a_manager.List(); };
		a_hooks.ensureResource = [&a_manager](std::string_view a_name) { return ToAction(a_manager.Ensure(a_name), "resource ensured"); };
		a_hooks.stopResource = [&a_manager](std::string_view a_name) { return ToAction(a_manager.Stop(a_name), "resource stopped"); };
		a_hooks.refreshResources = [&a_manager] {
			a_manager.Refresh();
			return AdminActionResult{ true, "resources refreshed", {} };
		};
		a_hooks.approveResource = [&a_manager](std::string_view a_name) { return ToAction(a_manager.Approve(a_name), "resource approved"); };
	}
}
