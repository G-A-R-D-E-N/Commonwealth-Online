#include "pch.h"

#include "F4TSettingsApplyHandlers.h"
#include "F4TSettingsRefreshRoute.h"
#include "F4TSettingsPersistence.h"
#include "F4TSettingsRegistry.h"
#include "F4TSettingsService.h"
#include "F4TSettingsServiceInternal.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace F4T::Settings
{
	namespace
	{
		using detail::Fail;
		using detail::GetLogPrefix;
		using detail::LookupKey;
		using detail::Ok;

		bool TryGetBinary(RE::Setting& a_setting, bool& a_out)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kBinary) {
				return false;
			}
			a_out = a_setting.GetBinary();
			return true;
		}

		bool TryGetInt(RE::Setting& a_setting, std::int32_t& a_out)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kInt) {
				return false;
			}
			a_out = a_setting.GetInt();
			return true;
		}

		bool TryGetUInt(RE::Setting& a_setting, std::uint32_t& a_out)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kUInt) {
				return false;
			}
			a_out = a_setting.GetUInt();
			return true;
		}

		bool TryGetFloat(RE::Setting& a_setting, float& a_out)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kFloat) {
				return false;
			}
			a_out = a_setting.GetFloat();
			return true;
		}

		bool TryGetString(RE::Setting& a_setting, std::string& a_out)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kString) {
				return false;
			}
			a_out = std::string{ a_setting.GetString() };
			return true;
		}

		bool SettingTypeMatches(const RE::Setting& a_setting, SettingValueType a_expected)
		{
			using Type = RE::Setting::SETTING_TYPE;
			switch (a_expected) {
			case SettingValueType::Boolean:
				return a_setting.GetType() == Type::kBinary;
			case SettingValueType::Integer:
				return a_setting.GetType() == Type::kInt;
			case SettingValueType::UnsignedInteger:
				return a_setting.GetType() == Type::kUInt;
			case SettingValueType::Float:
				return a_setting.GetType() == Type::kFloat;
			case SettingValueType::String:
				return a_setting.GetType() == Type::kString;
			default:
				return true;
			}
		}

		nlohmann::json ColourToJson(const ColourValue& a_colour)
		{
			return nlohmann::json{
				{ "r", a_colour.r },
				{ "g", a_colour.g },
				{ "b", a_colour.b }
			};
		}

		std::optional<ColourValue> ColourFromJson(const nlohmann::json& a_value)
		{
			if (!a_value.is_object() || !a_value.contains("r") || !a_value.contains("g") || !a_value.contains("b")) {
				return std::nullopt;
			}
			const auto clampByte = [](const nlohmann::json& channel) -> std::optional<std::uint8_t> {
				if (!channel.is_number()) {
					return std::nullopt;
				}
				const double raw = channel.get<double>();
				if (raw < 0.0 || raw > 255.0) {
					return std::nullopt;
				}
				return static_cast<std::uint8_t>(std::lround(raw));
			};
			const auto r = clampByte(a_value["r"]);
			const auto g = clampByte(a_value["g"]);
			const auto b = clampByte(a_value["b"]);
			if (!r || !g || !b) {
				return std::nullopt;
			}
			return ColourValue{ *r, *g, *b };
		}

		bool WriteBinary(RE::Setting& a_setting, bool a_value)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kBinary) {
				return false;
			}
			a_setting.SetBinary(a_value);
			return true;
		}

		bool WriteInt(RE::Setting& a_setting, std::int32_t a_value)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kInt) {
				return false;
			}
			a_setting.SetInt(a_value);
			return true;
		}

		bool WriteUInt(RE::Setting& a_setting, std::uint32_t a_value)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kUInt) {
				return false;
			}
			a_setting.SetUInt(a_value);
			return true;
		}

		bool WriteFloat(RE::Setting& a_setting, float a_value)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kFloat) {
				return false;
			}
			a_setting.SetFloat(a_value);
			return true;
		}

		bool WriteString(RE::Setting& a_setting, std::string_view a_value)
		{
			if (a_setting.GetType() != RE::Setting::SETTING_TYPE::kString) {
				return false;
			}
			char* copy = _strdup(std::string(a_value).c_str());
			if (!copy) {
				return false;
			}
			a_setting.SetString(copy);
			return true;
		}

		bool PersistResolved(const SettingDefinition& a_definition, RE::Setting* a_setting)
		{
			if (!a_setting) {
				return false;
			}
			return PersistSetting(a_definition, *a_setting);
		}

		SettingDefinition MakeTransientPrefDefinition(std::string_view a_id, std::string_view a_engineKey)
		{
			SettingDefinition definition{};
			definition.id = a_id;
			definition.engineKey = a_engineKey;
			definition.source = SettingSource::INIPref;
			definition.persist = true;
			return definition;
		}

		std::optional<std::uint64_t> AudioVolumeCurveId()
		{
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			return AudioVolumeCurveAddressLibraryId({ version[0], version[1], version[2], version[3] });  // constraint: curve id is verified for OG, NG and AE only source:E:/GardenEngine/re-answers/2026-10-20-audio-setvolume-null-and-master-startup.md
		}

		float AudioMasterEngineToUI(float a_engineValue)
		{
			const auto id = AudioVolumeCurveId();
			if (!id) {
				return a_engineValue;
			}
			REL::Relocation<float (*)(float)> curve{ REL::ID{ *id } };  // constraint: fAudioMasterVolume stores the curved value source:E:/GardenEngine/re-answers/2026-10-20-audio-setvolume-null-and-master-startup.md
			return InvertIncreasingCurve(curve.get(), a_engineValue);
		}

		std::optional<float> AudioMasterUIToEngine(float a_uiValue)
		{
			const auto id = AudioVolumeCurveId();
			if (!id) {
				return std::nullopt;
			}
			REL::Relocation<float (*)(float)> curve{ REL::ID{ *id } };  // constraint: fAudioMasterVolume stores the curved value source:E:/GardenEngine/re-answers/2026-10-20-audio-setvolume-null-and-master-startup.md
			return curve(a_uiValue);
		}
	}

	nlohmann::json SettingsService::ReadSettingValue(const SettingDefinition& a_definition, bool& a_available)
	{
		a_available = false;

		if (a_definition.id == "interface.hudColour") {
			auto* r = LookupKey("iHUDColorR:Interface");
			auto* g = LookupKey("iHUDColorG:Interface");
			auto* b = LookupKey("iHUDColorB:Interface");
			std::int32_t rv = 0;
			std::int32_t gv = 0;
			std::int32_t bv = 0;
			if (!r || !g || !b || !TryGetInt(*r, rv) || !TryGetInt(*g, gv) || !TryGetInt(*b, bv)) {
				REX::WARN("{} Missing or invalid HUD colour engine keys", GetLogPrefix());
				return nullptr;
			}
			a_available = true;
			return ColourToJson(ColourValue{
				static_cast<std::uint8_t>(std::clamp(rv, 0, 255)),
				static_cast<std::uint8_t>(std::clamp(gv, 0, 255)),
				static_cast<std::uint8_t>(std::clamp(bv, 0, 255))
			});
		}

		if (a_definition.id == "pipboy.colour") {
			auto* r = LookupKey("fPipboyEffectColorR:Pipboy");
			auto* g = LookupKey("fPipboyEffectColorG:Pipboy");
			auto* b = LookupKey("fPipboyEffectColorB:Pipboy");
			float rv = 0.0f;
			float gv = 0.0f;
			float bv = 0.0f;
			if (!r || !g || !b || !TryGetFloat(*r, rv) || !TryGetFloat(*g, gv) || !TryGetFloat(*b, bv)) {
				REX::WARN("{} Missing or invalid Pip-Boy colour engine keys", GetLogPrefix());
				return nullptr;
			}
			const auto toByte = [](float channel) {
				return static_cast<std::uint8_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
			};
			a_available = true;
			return ColourToJson(ColourValue{ toByte(rv), toByte(gv), toByte(bv) });
		}

		if (a_definition.id == "display.resolution") {
			auto* w = LookupKey("iSize W:Display");
			auto* h = LookupKey("iSize H:Display");
			std::int32_t width = 0;
			std::int32_t height = 0;
			if (!w || !h || !TryGetInt(*w, width) || !TryGetInt(*h, height)) {
				REX::WARN("{} Missing or invalid resolution engine keys", GetLogPrefix());
				return nullptr;
			}
			a_available = true;
			return nlohmann::json{
				{ "width", width },
				{ "height", height }
			};
		}

		if (a_definition.id == "display.windowMode") {
			auto* full = LookupKey("bFull Screen:Display");
			auto* borderless = LookupKey("bBorderless:Display");
			bool fullValue = false;
			bool borderlessValue = false;
			if (!full || !borderless || !TryGetBinary(*full, fullValue) || !TryGetBinary(*borderless, borderlessValue)) {
				REX::WARN("{} Missing or invalid window mode engine keys", GetLogPrefix());
				return nullptr;
			}
			a_available = true;
			if (fullValue) {
				return "fullscreen";
			}
			if (borderlessValue) {
				return "borderless";
			}
			return "windowed";
		}

		auto* setting = ResolveEngineSetting(a_definition);
		if (!setting) {
			REX::WARN("{} Missing engine setting id={} engineKey={}",
				GetLogPrefix(), a_definition.id, a_definition.engineKey);
			return nullptr;
		}

		a_available = true;
		if (!SettingTypeMatches(*setting, a_definition.type)) {
			REX::WARN("{} Type mismatch id={} engineKey={} — marking unsupported",
				GetLogPrefix(), a_definition.id, a_definition.engineKey);
			a_available = false;
			return nullptr;
		}

		switch (a_definition.type) {
		case SettingValueType::Boolean: {
			bool value = false;
			if (!TryGetBinary(*setting, value)) {
				a_available = false;
				return nullptr;
			}
			return value;
		}
		case SettingValueType::Integer: {
			std::int32_t value = 0;
			if (!TryGetInt(*setting, value)) {
				a_available = false;
				return nullptr;
			}
			return value;
		}
		case SettingValueType::UnsignedInteger: {
			std::uint32_t value = 0;
			if (!TryGetUInt(*setting, value)) {
				a_available = false;
				return nullptr;
			}
			return value;
		}
		case SettingValueType::Float: {
			float engineValue = 0.0f;
			if (!TryGetFloat(*setting, engineValue)) {
				a_available = false;
				return nullptr;
			}
			if (a_definition.id == "controls.mouseSensitivity") {
				return EngineToUISensitivity(engineValue, false);
			}
			if (a_definition.id == "controls.gamepadSensitivity") {
				return EngineToUISensitivity(engineValue, true);
			}
			if (a_definition.id == "audio.master") {
				return AudioMasterEngineToUI(engineValue);
			}
			return engineValue;
		}
		case SettingValueType::String: {
			std::string value;
			if (!TryGetString(*setting, value)) {
				a_available = false;
				return nullptr;
			}
			return value;
		}
		default:
			a_available = false;
			return nullptr;
		}
	}

	SettingResult SettingsService::ValidateAndNormalize(
		const SettingDefinition& a_definition,
		const nlohmann::json& a_value,
		nlohmann::json& a_normalized)
	{
		switch (a_definition.type) {
		case SettingValueType::Boolean: {
			if (!a_value.is_boolean()) {
				return Fail("TYPE_MISMATCH", "Expected boolean value.");
			}
			a_normalized = a_value;
			return Ok();
		}
		case SettingValueType::Integer: {
			if (!a_value.is_number_integer() && !a_value.is_number_unsigned()) {
				return Fail("TYPE_MISMATCH", "Expected integer value.");
			}
			const auto raw = a_value.get<std::int64_t>();
			if (!a_definition.options.empty()) {
				bool matched = false;
				for (const auto& option : a_definition.options) {
					if (option.value.is_number() && option.value.get<std::int64_t>() == raw) {
						matched = true;
						break;
					}
				}
				if (!matched) {
					return Fail("VALUE_OUT_OF_RANGE", "Integer value is not an allowed option.");
				}
			} else if (a_definition.minimum != 0.0 || a_definition.maximum != 0.0) {
				if (raw < static_cast<std::int64_t>(a_definition.minimum) ||
					raw > static_cast<std::int64_t>(a_definition.maximum)) {
					return Fail("VALUE_OUT_OF_RANGE", "Integer value is outside the allowed range.");
				}
			}
			a_normalized = static_cast<std::int32_t>(raw);
			return Ok();
		}
		case SettingValueType::UnsignedInteger: {
			if (!a_value.is_number_integer() && !a_value.is_number_unsigned()) {
				return Fail("TYPE_MISMATCH", "Expected unsigned integer value.");
			}
			const auto raw = a_value.get<std::int64_t>();
			if (raw < 0) {
				return Fail("VALUE_OUT_OF_RANGE", "Unsigned value cannot be negative.");
			}
			if (!a_definition.options.empty()) {
				bool matched = false;
				for (const auto& option : a_definition.options) {
					if (option.value.is_number() && option.value.get<std::uint64_t>() == static_cast<std::uint64_t>(raw)) {
						matched = true;
						break;
					}
				}
				if (!matched) {
					return Fail("VALUE_OUT_OF_RANGE", "Unsigned value is not an allowed option.");
				}
			} else if (a_definition.minimum != 0.0 || a_definition.maximum != 0.0) {
				if (raw < static_cast<std::int64_t>(a_definition.minimum) ||
					raw > static_cast<std::int64_t>(a_definition.maximum)) {
					return Fail("VALUE_OUT_OF_RANGE", "Unsigned value is outside the allowed range.");
				}
			}
			a_normalized = static_cast<std::uint32_t>(raw);
			return Ok();
		}
		case SettingValueType::Float: {
			if (!a_value.is_number()) {
				return Fail("TYPE_MISMATCH", "Expected float value.");
			}
			double raw = a_value.get<double>();
			if (!a_definition.options.empty()) {
				bool matched = false;
				for (const auto& option : a_definition.options) {
					if (option.value.is_number() && std::abs(option.value.get<double>() - raw) < 0.0001) {
						matched = true;
						raw = option.value.get<double>();
						break;
					}
				}
				if (!matched) {
					return Fail("VALUE_OUT_OF_RANGE", "Float value is not an allowed option.");
				}
			} else if (a_definition.minimum != 0.0 || a_definition.maximum != 0.0) {
				if (raw < a_definition.minimum || raw > a_definition.maximum) {
					raw = std::clamp(raw, a_definition.minimum, a_definition.maximum);
				}
			}
			a_normalized = raw;
			return Ok();
		}
		case SettingValueType::String: {
			if (!a_value.is_string()) {
				return Fail("TYPE_MISMATCH", "Expected string value.");
			}
			const auto raw = a_value.get<std::string>();
			if (!a_definition.options.empty()) {
				bool matched = false;
				for (const auto& option : a_definition.options) {
					if (option.value.is_string() && option.value.get<std::string>() == raw) {
						matched = true;
						break;
					}
				}
				if (!matched) {
					return Fail("VALUE_OUT_OF_RANGE", "String value is not an allowed option.");
				}
			}
			a_normalized = raw;
			return Ok();
		}
		case SettingValueType::Colour: {
			const auto colour = ColourFromJson(a_value);
			if (!colour) {
				return Fail("TYPE_MISMATCH", "Expected colour object with r/g/b in 0-255.");
			}
			a_normalized = ColourToJson(*colour);
			return Ok();
		}
		case SettingValueType::Resolution: {
			if (!a_value.is_object() || !a_value.contains("width") || !a_value.contains("height")) {
				return Fail("TYPE_MISMATCH", "Expected resolution object with width/height.");
			}
			if (!a_value["width"].is_number_integer() || !a_value["height"].is_number_integer()) {
				return Fail("TYPE_MISMATCH", "Resolution width/height must be integers.");
			}
			const auto width = a_value["width"].get<std::int32_t>();
			const auto height = a_value["height"].get<std::int32_t>();
			if (width < 640 || height < 480 || width > 7680 || height > 4320) {
				return Fail("VALUE_OUT_OF_RANGE", "Resolution is outside the supported range.");
			}
			a_normalized = nlohmann::json{ { "width", width }, { "height", height } };
			return Ok();
		}
		case SettingValueType::WindowMode: {
			if (!a_value.is_string() || !WindowModeFromString(a_value.get<std::string>())) {
				return Fail("VALUE_OUT_OF_RANGE", "Window mode must be windowed, borderless, or fullscreen.");
			}
			a_normalized = a_value;
			return Ok();
		}
		}
		return Fail("TYPE_MISMATCH", "Unsupported setting type.");
	}

	bool SettingsService::WriteSettingValue(
		const SettingDefinition& a_definition,
		const nlohmann::json& a_value,
		std::string& a_errorCode,
		std::string& a_errorMessage)
	{
		a_errorCode.clear();
		a_errorMessage.clear();

		if (!a_definition.editable) {
			a_errorCode = "APPLY_NOT_SUPPORTED";
			a_errorMessage = "This setting is read-only in Commonwealth Online.";
			return false;
		}

		if (a_definition.id == "interface.hudColour") {
			const auto colour = ColourFromJson(a_value);
			if (!colour) {
				a_errorCode = "TYPE_MISMATCH";
				a_errorMessage = "Invalid HUD colour.";
				return false;
			}
			auto* r = LookupKey("iHUDColorR:Interface");
			auto* g = LookupKey("iHUDColorG:Interface");
			auto* b = LookupKey("iHUDColorB:Interface");
			if (!r || !g || !b) {
				a_errorCode = "SETTING_UNAVAILABLE";
				a_errorMessage = "HUD colour engine keys unavailable.";
				return false;
			}
			WriteInt(*r, colour->r);
			WriteInt(*g, colour->g);
			WriteInt(*b, colour->b);
			const auto defR = MakeTransientPrefDefinition(a_definition.id, "iHUDColorR:Interface");
			const auto defG = MakeTransientPrefDefinition(a_definition.id, "iHUDColorG:Interface");
			const auto defB = MakeTransientPrefDefinition(a_definition.id, "iHUDColorB:Interface");
			const bool persisted =
				PersistResolved(defR, r) &&
				PersistResolved(defG, g) &&
				PersistResolved(defB, b);
			if (!persisted) {
				a_errorCode = "PERSIST_FAILED";
				a_errorMessage = "Failed to persist HUD colour.";
				return false;
			}
			return true;
		}

		if (a_definition.id == "pipboy.colour") {
			const auto colour = ColourFromJson(a_value);
			if (!colour) {
				a_errorCode = "TYPE_MISMATCH";
				a_errorMessage = "Invalid Pip-Boy colour.";
				return false;
			}
			auto* r = LookupKey("fPipboyEffectColorR:Pipboy");
			auto* g = LookupKey("fPipboyEffectColorG:Pipboy");
			auto* b = LookupKey("fPipboyEffectColorB:Pipboy");
			if (!r || !g || !b) {
				a_errorCode = "SETTING_UNAVAILABLE";
				a_errorMessage = "Pip-Boy colour engine keys unavailable.";
				return false;
			}
			// Constraint: engine clamps Pip-Boy colour to [0.005, 1.0] on OG, NG and AE; source:docs/code-notes/CommonwealthOnlineUI.md.
			const auto toEngine = [](std::uint8_t a_channel) { return std::clamp(a_channel / 255.0f, 0.005F, 1.0F); };
			WriteFloat(*r, toEngine(colour->r));
			WriteFloat(*g, toEngine(colour->g));
			WriteFloat(*b, toEngine(colour->b));
			const auto defR = MakeTransientPrefDefinition(a_definition.id, "fPipboyEffectColorR:Pipboy");
			const auto defG = MakeTransientPrefDefinition(a_definition.id, "fPipboyEffectColorG:Pipboy");
			const auto defB = MakeTransientPrefDefinition(a_definition.id, "fPipboyEffectColorB:Pipboy");
			const bool persisted =
				PersistResolved(defR, r) &&
				PersistResolved(defG, g) &&
				PersistResolved(defB, b);
			if (!persisted) {
				a_errorCode = "PERSIST_FAILED";
				a_errorMessage = "Failed to persist Pip-Boy colour.";
				return false;
			}
			return true;
		}

		if (a_definition.id == "display.resolution") {
			const auto width = a_value.value("width", 0);
			const auto height = a_value.value("height", 0);
			auto* w = LookupKey("iSize W:Display");
			auto* h = LookupKey("iSize H:Display");
			if (!w || !h) {
				a_errorCode = "SETTING_UNAVAILABLE";
				a_errorMessage = "Resolution engine keys unavailable.";
				return false;
			}
			WriteInt(*w, width);
			WriteInt(*h, height);
			const auto defW = MakeTransientPrefDefinition(a_definition.id, "iSize W:Display");
			const auto defH = MakeTransientPrefDefinition(a_definition.id, "iSize H:Display");
			if (!(PersistResolved(defW, w) && PersistResolved(defH, h))) {
				a_errorCode = "PERSIST_FAILED";
				a_errorMessage = "Failed to persist resolution.";
				return false;
			}
			return true;
		}

		if (a_definition.id == "display.windowMode") {
			const auto mode = WindowModeFromString(a_value.get<std::string>());
			auto* full = LookupKey("bFull Screen:Display");
			auto* borderless = LookupKey("bBorderless:Display");
			if (!mode || !full || !borderless) {
				a_errorCode = "SETTING_UNAVAILABLE";
				a_errorMessage = "Window mode engine keys unavailable.";
				return false;
			}

			bool fullValue = false;
			bool borderlessValue = false;
			switch (*mode) {
			case WindowMode::Fullscreen:
				fullValue = true;
				borderlessValue = false;
				break;
			case WindowMode::Borderless:
				fullValue = false;
				borderlessValue = true;
				break;
			case WindowMode::Windowed:
				fullValue = false;
				borderlessValue = false;
				break;
			}

			WriteBinary(*full, fullValue);
			WriteBinary(*borderless, borderlessValue);
			const auto defFull = MakeTransientPrefDefinition(a_definition.id, "bFull Screen:Display");
			const auto defBorderless = MakeTransientPrefDefinition(a_definition.id, "bBorderless:Display");
			if (!(PersistResolved(defFull, full) && PersistResolved(defBorderless, borderless))) {
				a_errorCode = "PERSIST_FAILED";
				a_errorMessage = "Failed to persist window mode.";
				return false;
			}
			return true;
		}

		auto* setting = ResolveEngineSetting(a_definition);
		if (!setting) {
			a_errorCode = "SETTING_UNAVAILABLE";
			a_errorMessage = "Engine setting unavailable.";
			return false;
		}

		bool wrote = false;
		switch (a_definition.type) {
		case SettingValueType::Boolean:
			wrote = WriteBinary(*setting, a_value.get<bool>());
			break;
		case SettingValueType::Integer:
			wrote = WriteInt(*setting, a_value.get<std::int32_t>());
			break;
		case SettingValueType::UnsignedInteger:
			wrote = WriteUInt(*setting, a_value.get<std::uint32_t>());
			break;
		case SettingValueType::Float: {
			float engineValue = static_cast<float>(a_value.get<double>());
			if (a_definition.id == "controls.mouseSensitivity") {
				engineValue = UIToEngineSensitivity(engineValue, false);
			} else if (a_definition.id == "controls.gamepadSensitivity") {
				engineValue = UIToEngineSensitivity(engineValue, true);
			} else if (a_definition.minimum != 0.0 || a_definition.maximum != 0.0) {
				engineValue = static_cast<float>(std::clamp(
					static_cast<double>(engineValue),
					a_definition.minimum,
					a_definition.maximum));
			}
			if (a_definition.id == "audio.master") {
				const auto curvedMaster = AudioMasterUIToEngine(engineValue);
				if (!curvedMaster) {
					static std::uint32_t s_skippedMasterWrites{ 0 };
					const auto skipped = ++s_skippedMasterWrites;
					if (skipped == 1 || skipped % 100 == 0) {
						REX::WARN("{} [RT-VERIFY 385-C] {} skipped: no audio volume curve id for this runtime ui={} count={}",
							GetLogPrefix(), a_definition.id, a_value.get<double>(), skipped);
					}
					a_errorCode = "AUDIO_CURVE_UNAVAILABLE";
					a_errorMessage = "Audio master curve is not available for this runtime; value not written.";
					return false;
				}
				engineValue = *curvedMaster;
			}
			if (a_definition.id.starts_with("audio.")) {
				REX::INFO("{} [RT-VERIFY 385-C] store id={} ui={} engine={} masterCurve={}", GetLogPrefix(), a_definition.id,
					a_value.get<double>(), engineValue, a_definition.id == "audio.master");
			}
			wrote = WriteFloat(*setting, engineValue);
			break;
		}
		case SettingValueType::String:
			wrote = WriteString(*setting, a_value.get<std::string>());
			break;
		default:
			a_errorCode = "APPLY_NOT_SUPPORTED";
			a_errorMessage = "Write path not implemented for this type.";
			return false;
		}

		if (!wrote) {
			a_errorCode = "TYPE_MISMATCH";
			a_errorMessage = "Failed to write engine setting value.";
			return false;
		}

		if (!PersistSetting(a_definition, *setting)) {
			a_errorCode = "PERSIST_FAILED";
			a_errorMessage = "Failed to persist setting through its collection.";
			return false;
		}
		return true;
	}
}
