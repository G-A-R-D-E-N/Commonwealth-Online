#include "pch.h"

#include "F4TF4SEPermanentTask.h"
#include "F4TProbeFlush.h"
#include "F4TProbeManifest.h"
#include "F4TProbeRegistry.h"
#include "F4TProbeSites.h"
#include "F4TProbeVersionGate.h"

#include <Windows.h>

#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace
{
	std::filesystem::path ExecutingFolder()
	{
		HMODULE module{ nullptr };
		const auto flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
		if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&ExecutingFolder), &module)) {
			return {};
		}
		std::array<wchar_t, MAX_PATH> buffer{};
		const auto length = GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) {
			return {};
		}
		return std::filesystem::path(buffer.data()).parent_path();
	}

	void Log(std::string_view a_message)
	{
		REX::WARN("Commonwealth Online probe: {}", a_message);
	}

	F4T::ProbeRegistry::InstallOutcome SelfTestInstaller(
		const F4T::ProbeRegistry::RuntimeSettings&,
		F4T::ProbeRegistry::HookPlan&)
	{
		return F4T::ProbeRegistry::InstallOutcome::Enabled;
	}

	void ScheduleTick()
	{
		const auto* tasks = F4SE::GetTaskInterface();
		if (!tasks) {
			REX::WARN("Commonwealth Online probe: task interface unavailable; the ring will not be flushed");
			return;
		}
		F4T::F4SECapabilities::SchedulePermanentTask(tasks, F4SE::GetF4SEVersion(), []() { F4T::ProbeFlush::Tick(); });
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se);

	const auto fileVersion = REX::FModule::GetExecutingModule().GetFileVersion();
	const F4T::ProbeVersionGate::RuntimeVersion version{
		fileVersion.major(),
		fileVersion.minor(),
		fileVersion.patch(),
		fileVersion.build()
	};
	if (!F4T::ProbeVersionGate::IsSupported(version)) {
		REX::WARN(
			"Commonwealth Online probe: runtime {} is not supported; installing zero probes",
			fileVersion.string());
		return true;
	}

	const auto folder = ExecutingFolder();
	if (folder.empty()) {
		REX::WARN("Commonwealth Online probe: cannot resolve the plugin folder; installing zero probes");
		return true;
	}

	F4T::ProbeManifest::Document manifest;
	std::string error;
	if (!F4T::ProbeManifest::Load(folder / "CommonwealthOnlineProbe.json", manifest, error)) {
		REX::WARN("Commonwealth Online probe: manifest rejected ({}); installing zero probes", error);
		return true;
	}

	if (!F4T::ProbeRegistry::Register({ F4T::ProbeRegistry::SelfTestProbeId, "selftest", &SelfTestInstaller })) {
		REX::WARN("Commonwealth Online probe: self-test probe could not be registered; installing zero probes");
		return true;
	}

	F4T::ProbeSites::RegisterAll();
	F4T::ProbeRegistry::Install(manifest, version, &Log);
	F4T::ProbeFlush::Start();
	ScheduleTick();
	return true;
}
