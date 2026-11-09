#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace F4T::Settings
{
	enum class SettingSource
	{
		INIPref,
		INI,
		GameSetting,
		Special
	};

	enum class SettingValueType
	{
		Boolean,
		Integer,
		UnsignedInteger,
		Float,
		String,
		Colour,
		Resolution,
		WindowMode
	};

	enum class ApplyMethod
	{
		Direct,
		HUDRefresh,
		AudioRefresh,
		InputRefresh,
		DifficultyChange,
		DisplayChange,
		PipBoyRefresh,
		AutosaveRefresh,
		RequiresRestart,
		Unsupported
	};

	enum class ApplyTiming
	{
		Immediate,
		OnMenuClose,
		RequiresRendererReset,
		RequiresRestart,
		RequiresReverseEngineering
	};

	struct ColourValue
	{
		std::uint8_t r{ 0 };
		std::uint8_t g{ 0 };
		std::uint8_t b{ 0 };

		bool operator==(const ColourValue&) const = default;
	};

	struct ResolutionValue
	{
		std::int32_t width{ 0 };
		std::int32_t height{ 0 };

		bool operator==(const ResolutionValue&) const = default;
	};

	enum class WindowMode
	{
		Windowed,
		Borderless,
		Fullscreen
	};

	struct SettingOption
	{
		nlohmann::json value;
		std::string    label;
	};

	struct SettingDefinition
	{
		std::string_view id;
		std::string_view engineKey;
		std::string_view category;
		std::string_view label;
		std::string_view description;
		SettingSource    source{ SettingSource::INIPref };
		SettingValueType type{ SettingValueType::Boolean };
		ApplyMethod      applyMethod{ ApplyMethod::Direct };
		ApplyTiming      applyTiming{ ApplyTiming::Immediate };

		double minimum{ 0.0 };
		double maximum{ 0.0 };
		double step{ 0.0 };

		bool persist{ true };
		bool requiresRestart{ false };
		bool editable{ true };
		bool showInUI{ true };
		bool uiUsesPercent{ false };

		std::vector<SettingOption> options;
	};

	struct AudioChannelSetting
	{
		std::string_view              settingID;
		std::string_view              valueKey;
		std::string_view              identifierKey;
		std::uint32_t                 identifier{ 0 };
		std::optional<std::string_view> displayName;
	};

	struct SettingError
	{
		std::string code;
		std::string message;

		nlohmann::json ToJson() const
		{
			return nlohmann::json{
				{ "code", code },
				{ "message", message }
			};
		}
	};

	struct SettingResult
	{
		bool                       success{ false };
		std::optional<SettingError> error;
		nlohmann::json             data{ nullptr };

		nlohmann::json ToJson() const
		{
			nlohmann::json json{
				{ "success", success }
			};
			if (error) {
				json["error"] = error->ToJson();
			}
			if (!data.is_null()) {
				json["data"] = data;
			}
			return json;
		}
	};

	[[nodiscard]] inline std::string_view ToString(SettingValueType a_type)
	{
		switch (a_type) {
		case SettingValueType::Boolean:
			return "boolean";
		case SettingValueType::Integer:
			return "integer";
		case SettingValueType::UnsignedInteger:
			return "unsignedInteger";
		case SettingValueType::Float:
			return "float";
		case SettingValueType::String:
			return "string";
		case SettingValueType::Colour:
			return "colour";
		case SettingValueType::Resolution:
			return "resolution";
		case SettingValueType::WindowMode:
			return "windowMode";
		}
		return "unknown";
	}

	[[nodiscard]] inline std::string_view ToString(ApplyTiming a_timing)
	{
		switch (a_timing) {
		case ApplyTiming::Immediate:
			return "immediate";
		case ApplyTiming::OnMenuClose:
			return "onMenuClose";
		case ApplyTiming::RequiresRendererReset:
			return "requiresRendererReset";
		case ApplyTiming::RequiresRestart:
			return "requiresRestart";
		case ApplyTiming::RequiresReverseEngineering:
			return "requiresReverseEngineering";
		}
		return "unknown";
	}

	[[nodiscard]] inline std::string_view ToString(WindowMode a_mode)
	{
		switch (a_mode) {
		case WindowMode::Windowed:
			return "windowed";
		case WindowMode::Borderless:
			return "borderless";
		case WindowMode::Fullscreen:
			return "fullscreen";
		}
		return "windowed";
	}

	[[nodiscard]] inline std::optional<WindowMode> WindowModeFromString(std::string_view a_value)
	{
		if (a_value == "windowed") {
			return WindowMode::Windowed;
		}
		if (a_value == "borderless") {
			return WindowMode::Borderless;
		}
		if (a_value == "fullscreen") {
			return WindowMode::Fullscreen;
		}
		return std::nullopt;
	}
}
