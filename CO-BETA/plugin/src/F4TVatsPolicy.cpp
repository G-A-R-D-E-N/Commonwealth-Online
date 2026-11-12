#include "pch.h"

#include "F4TVatsPolicy.h"

#include "F4TVatsPolicyCommands.h"

#include "F4TConsoleGuard.h"
#include "F4TDebugLogging.h"
#include "F4TNetworking.h"

#include <optional>

namespace F4T::VatsPolicy
{
	namespace
	{
		std::optional<F4T::WorldPolicy::Vats> g_applied;
		Snapshot g_snapshot;
		bool g_snapshotTaken{};

		bool ReadBool(RE::GameSettingCollection* a_games, std::string_view a_setting, bool& a_out) noexcept
		{
			char names[2][64]{};
			std::snprintf(names[0], sizeof(names[0]), "%.*s:VATS", static_cast<int>(a_setting.size()), a_setting.data());
			std::snprintf(names[1], sizeof(names[1]), "%.*s", static_cast<int>(a_setting.size()), a_setting.data());
			for (const auto* name : names) {
				auto* setting = a_games->GetSetting(name);
				if (setting != nullptr && setting->GetType() == RE::Setting::SETTING_TYPE::kBinary) {
					a_out = setting->GetBinary();
					return true;
				}
			}
			return false;
		}

		bool ReadFloat(RE::GameSettingCollection* a_games, std::string_view a_setting, float& a_out) noexcept
		{
			char names[2][64]{};
			std::snprintf(names[0], sizeof(names[0]), "%.*s", static_cast<int>(a_setting.size()), a_setting.data());
			std::snprintf(names[1], sizeof(names[1]), "%.*s:VATS", static_cast<int>(a_setting.size()), a_setting.data());
			for (const auto* name : names) {
				auto* setting = a_games->GetSetting(name);
				if (setting != nullptr && setting->GetType() == RE::Setting::SETTING_TYPE::kFloat) {
					a_out = setting->GetFloat();
					return true;
				}
			}
			return false;
		}

		Snapshot CaptureSnapshot() noexcept
		{
			Snapshot snapshot;
			auto* games = RE::GameSettingCollection::GetSingleton();
			if (games == nullptr) {
				return snapshot;
			}
			snapshot.valid = ReadFloat(games, kMaxEngageDistanceSetting, snapshot.maxEngageDistance) &&
				ReadFloat(games, kEnterMultSetting, snapshot.enterMult) &&
				ReadFloat(games, kExitMultSetting, snapshot.exitMult) &&
				ReadFloat(games, kTargetSelectMultSetting, snapshot.targetSelectMult) &&
				ReadFloat(games, kMagicSlowdownMultSetting, snapshot.magicSlowdownMult) &&
				ReadBool(games, kMovementEventsSetting, snapshot.movementEvents);
			if (!snapshot.valid) {
				snapshot = Snapshot{};
			}
			return snapshot;
		}

		void RunCommands(const std::vector<std::string>& a_commands)
		{
			for (const auto& command : a_commands) {
				F4T::ConsoleGuard::ExecutePluginCommand(command.c_str());
			}
		}
	}

	void ApplyForPolicy(const F4T::WorldPolicy::Policy& a_policy)
	{
		if (g_applied && *g_applied == a_policy.vats) {
			return;
		}
		if (!g_snapshotTaken) {
			g_snapshot = CaptureSnapshot();
			g_snapshotTaken = true;
		}
		RunCommands(ApplyCommands(a_policy.vats, g_snapshot));
		g_applied = a_policy.vats;
		F4T::DebugLogging::Log(
			"Vats",
			"applied mode={} commands={} snapshot={}",
			F4T::WorldPolicy::VatsName(a_policy.vats),
			ApplyCommands(a_policy.vats, g_snapshot).size(),
			g_snapshot.valid ? "exact" : "partial");
	}

	void Reassert()
	{
		if (!g_applied) {
			return;
		}
		RunCommands(ApplyCommands(*g_applied, g_snapshot));
		F4T::DebugLogging::Log(
			"Vats",
			"reasserted mode={}",
			F4T::WorldPolicy::VatsName(*g_applied));
	}

	void Restore()
	{
		if (!g_applied) {
			return;
		}
		RunCommands(RestoreCommands(g_snapshot));
		F4T::DebugLogging::Log(
			"Vats",
			"restored mode={} snapshot={}",
			F4T::WorldPolicy::VatsName(*g_applied),
			g_snapshot.valid ? "exact" : "partial");
		g_applied.reset();
		g_snapshot = Snapshot{};
		g_snapshotTaken = false;
	}

	void OnGameLoaded()
	{
		if (F4T::Networking::IsConnectedToServer()) {
			Reassert();
		} else {
			Restore();
		}
	}
}
