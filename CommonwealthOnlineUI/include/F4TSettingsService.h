#pragma once

#include "F4TSettingsTypes.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <unordered_map>

namespace RE
{
	class Setting;
}

namespace F4T::Settings
{
	[[nodiscard]] float EngineToUISensitivity(float a_engineValue, bool a_gamepad);
	[[nodiscard]] float UIToEngineSensitivity(float a_uiValue, bool a_gamepad);

	class SettingsService
	{
	public:
		static void Initialize();

		[[nodiscard]] static SettingResult GetAllSettings();
		[[nodiscard]] static SettingResult GetSettingsByCategory(std::string_view a_category);
		[[nodiscard]] static SettingResult SetSetting(std::string_view a_id, const nlohmann::json& a_value);
		[[nodiscard]] static SettingResult ApplyPendingSettings();
		[[nodiscard]] static SettingResult ResetCategory(std::string_view a_category);
		[[nodiscard]] static SettingResult ResetAllSettings();
		[[nodiscard]] static SettingResult CancelPendingSettings();
		[[nodiscard]] static SettingResult CaptureBaseline();

		[[nodiscard]] static bool HasPendingChanges();
		[[nodiscard]] static nlohmann::json BuildSnapshotJson();
		static void RefreshOne(std::string_view a_id);
		[[nodiscard]] static nlohmann::json BuildOneDto(std::string_view a_id);

	private:
		struct RuntimeState
		{
			nlohmann::json baseline;   // Values captured when the settings menu opened.
			nlohmann::json committed;  // Last applied / engine values.
			nlohmann::json pending;    // Unapplied UI edits.
			bool           dirty{ false };
			bool           available{ false };
		};

		static void EnsureInitialized();
		static void RefreshFromEngine();
		[[nodiscard]] static nlohmann::json ReadSettingValue(const SettingDefinition& a_definition, bool& a_available);
		[[nodiscard]] static SettingResult ValidateAndNormalize(
			const SettingDefinition& a_definition,
			const nlohmann::json& a_value,
			nlohmann::json& a_normalized);
		[[nodiscard]] static bool WriteSettingValue(
			const SettingDefinition& a_definition,
			const nlohmann::json& a_value,
			std::string& a_errorCode,
			std::string& a_errorMessage);
		[[nodiscard]] static bool ApplySetting(
			const SettingDefinition& a_definition,
			const nlohmann::json& a_oldValue,
			const nlohmann::json& a_newValue,
			bool a_persisted);
		[[nodiscard]] static RE::Setting* ResolveEngineSetting(const SettingDefinition& a_definition);
		[[nodiscard]] static nlohmann::json MakeSettingDto(
			const SettingDefinition& a_definition,
			const RuntimeState& a_state);

		static inline bool                                     s_initialized{ false };
		static inline std::unordered_map<std::string, RuntimeState> s_states;
	};
}
