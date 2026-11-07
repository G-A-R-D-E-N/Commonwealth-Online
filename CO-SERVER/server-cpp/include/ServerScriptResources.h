#pragma once

#include "AdminCommandDispatcher.h"
#include "ServerScriptRuntime.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommonwealthOnline::Server::Scripting
{
	enum class ResourceState : std::uint8_t
	{
		Invalid,
		Pending,
		Running,
		Stopped,
		Failed
	};

	struct ResourceManifest
	{
		std::string name;
		std::string version;
		std::vector<std::string> permissions;
		std::vector<std::string> dependencies;
		std::vector<std::string> views;
		int priority{};
		bool restartOnError{};
	};

	struct ResourceApproval
	{
		std::vector<std::string> permissions;
		std::string digest;
	};

	struct ResourceEntry
	{
		ResourceManifest manifest;
		std::vector<std::pair<std::string, std::string>> scripts;
		std::string fingerprint;
		std::string digest;
		std::string detail;
		ResourceState state{ ResourceState::Pending };
		bool desired{ true };
		std::vector<double> restartTimes;
		double cpuWindowStart{};
		double cpuBusySeconds{};
		std::unique_ptr<ScriptRuntime> runtime;
	};

	using ScriptInstaller = std::function<void(ScriptRuntime&, const std::vector<std::string>&)>;
	using CpuMeter = std::function<double()>;

	class ResourceManager
	{
	public:
		explicit ResourceManager(std::filesystem::path a_root, Limits a_limits = {}, LogSink a_log = {}, ScriptInstaller a_installer = {}, CpuMeter a_cpuMeter = {});
		ResourceManager(const ResourceManager&) = delete;
		ResourceManager& operator=(const ResourceManager&) = delete;

		void Refresh();
		void Tick(double a_now);
		[[nodiscard]] nlohmann::json List() const;
		[[nodiscard]] std::string Ensure(std::string_view a_name);
		[[nodiscard]] std::string Stop(std::string_view a_name);
		[[nodiscard]] std::string Approve(std::string_view a_name);

	private:
		void Scan();
		void Reconcile();
		bool StartEntry(ResourceEntry& a_entry);
		void Retire(ResourceEntry& a_entry, std::string a_detail);
		std::string Activate(ResourceEntry& a_entry);
		[[nodiscard]] bool Approved(const ResourceEntry& a_entry) const;
		[[nodiscard]] std::string UnapprovedDetail(const ResourceEntry& a_entry) const;
		[[nodiscard]] bool DependenciesRunning(const ResourceEntry& a_entry) const;
		void LoadApprovals();
		[[nodiscard]] std::string SaveApprovals() const;

		std::filesystem::path _root;
		Limits _limits;
		LogSink _log;
		ScriptInstaller _installer;
		CpuMeter _cpuMeter;
		double _now{};
		std::size_t _cursor{};
		bool _started{};
		std::map<std::string, ResourceApproval> _approvals;
		std::map<std::string, ResourceEntry, std::less<>> _entries;
	};

	[[nodiscard]] std::string ReadBoundedText(const std::filesystem::path& a_path, std::uintmax_t a_maxBytes, const std::string& a_label);
	void BindAdminHooks(ResourceManager& a_manager, AdminCommandHooks& a_hooks);
}
