#include "pch.h"

#include "F4TSettingsApplyHandlers.h"
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
		constexpr float kMouseSensitivityEngineMax = 0.1f;
		constexpr float kGamepadSensitivityEngineMax = 1.0f;

		using detail::Fail;
		using detail::GetLogPrefix;
		using detail::LookupKey;
		using detail::Ok;

		bool ValuesEqual(const nlohmann::json& a_lhs, const nlohmann::json& a_rhs)
		{
			return a_lhs == a_rhs;
		}

	}

	float EngineToUISensitivity(float a_engineValue, bool a_gamepad)
	{
		const float maxEngine = a_gamepad ? kGamepadSensitivityEngineMax : kMouseSensitivityEngineMax;
		const float clamped = std::clamp(a_engineValue, 0.0f, maxEngine);
		return (clamped / maxEngine) * 100.0f;
	}

	float UIToEngineSensitivity(float a_uiValue, bool a_gamepad)
	{
		const float maxEngine = a_gamepad ? kGamepadSensitivityEngineMax : kMouseSensitivityEngineMax;
		const float clampedUi = std::clamp(a_uiValue, 0.0f, 100.0f);
		return (clampedUi / 100.0f) * maxEngine;
	}

	void SettingsService::EnsureInitialized()
	{
		if (!s_initialized) {
			Initialize();
		}
	}

	void SettingsService::Initialize()
	{
		if (s_initialized) {
			return;
		}

		// Registry only — do not touch RE::Setting collections here.
		// DomReady runs before it is always safe to query prefs; engine reads are
		// deferred to CaptureBaseline / GetAllSettings when the settings UI opens.
		SettingsRegistry::Initialize();
		s_states.clear();
		s_initialized = true;

#if defined(_DEBUG) || defined(COUI_SETTINGS_DEBUG)
		{
			const float mouseEngine = UIToEngineSensitivity(50.0f, false);
			const float mouseUi = EngineToUISensitivity(mouseEngine, false);
			const float padEngine = UIToEngineSensitivity(50.0f, true);
			const float padUi = EngineToUISensitivity(padEngine, true);
			if (std::abs(mouseUi - 50.0f) > 0.01f || std::abs(padUi - 50.0f) > 0.01f) {
				REX::WARN("{} Sensitivity conversion self-check failed mouseUi={} padUi={}",
					GetLogPrefix(), mouseUi, padUi);
			} else {
				REX::INFO("{} Sensitivity conversion self-check passed", GetLogPrefix());
			}
		}
#endif

		REX::INFO("{} Service initialised (engine snapshot deferred)", GetLogPrefix());
	}

	RE::Setting* SettingsService::ResolveEngineSetting(const SettingDefinition& a_definition)
	{
		if (a_definition.source == SettingSource::Special) {
			return nullptr;
		}
		if (auto* fromCollection = ResolveCollectionSetting(a_definition)) {
			return fromCollection;
		}
		return LookupKey(a_definition.engineKey);
	}

	bool SettingsService::ApplySetting(
		const SettingDefinition& a_definition,
		const nlohmann::json& a_oldValue,
		const nlohmann::json& a_newValue,
		bool a_persisted)
	{
		const bool applied = ApplySettingChange(a_definition, a_oldValue, a_newValue);
		REX::INFO("{} Applied {}: engineKey={} old={} new={} persisted={}",
			GetLogPrefix(),
			a_definition.id,
			a_definition.engineKey,
			a_oldValue.dump(),
			a_newValue.dump(),
			a_persisted);
		return applied;
	}

	nlohmann::json SettingsService::MakeSettingDto(
		const SettingDefinition& a_definition,
		const RuntimeState& a_state)
	{
		nlohmann::json options = nlohmann::json::array();
		for (const auto& option : a_definition.options) {
			options.push_back({
				{ "value", option.value },
				{ "label", option.label }
			});
		}

		const auto& value = a_state.dirty ? a_state.pending : a_state.committed;
		nlohmann::json dto{
			{ "id", a_definition.id },
			{ "category", a_definition.category },
			{ "label", a_definition.label },
			{ "description", a_definition.description },
			{ "type", ToString(a_definition.type) },
			{ "value", value },
			{ "defaultValue", a_state.baseline },
			{ "minimum", (a_definition.minimum != 0.0 || a_definition.maximum != 0.0)
				? nlohmann::json(a_definition.minimum)
				: nullptr },
			{ "maximum", (a_definition.minimum != 0.0 || a_definition.maximum != 0.0)
				? nlohmann::json(a_definition.maximum)
				: nullptr },
			{ "step", a_definition.step != 0.0 ? nlohmann::json(a_definition.step) : nullptr },
			{ "requiresRestart", a_definition.requiresRestart },
			{ "editable", a_definition.editable },
			{ "applyTiming", ToString(a_definition.applyTiming) },
			{ "supported", a_state.available },
			{ "dirty", a_state.dirty },
			{ "showInUI", a_definition.showInUI },
			{ "uiUsesPercent", a_definition.uiUsesPercent },
			{ "options", options }
		};
		return dto;
	}

	void SettingsService::RefreshFromEngine()
	{
		for (const auto& definition : SettingsRegistry::GetAll()) {
			if (!definition.showInUI) {
				continue;
			}
			RefreshOne(definition.id);
		}
	}

	void SettingsService::RefreshOne(std::string_view a_id)
	{
		EnsureInitialized();
		const auto* definition = SettingsRegistry::Find(a_id);
		if (!definition) {
			return;
		}

		RuntimeState state{};
		bool available = false;
		nlohmann::json value = nullptr;
		try {
			value = ReadSettingValue(*definition, available);
		} catch (const std::exception& ex) {
			REX::WARN("{} Exception reading {}: {}", GetLogPrefix(), definition->id, ex.what());
			available = false;
			value = nullptr;
		} catch (...) {
			REX::WARN("{} Unknown exception reading {}", GetLogPrefix(), definition->id);
			available = false;
			value = nullptr;
		}
		state.available = available;
		state.baseline = available ? value : nullptr;
		state.committed = state.baseline;
		state.pending = state.baseline;
		state.dirty = false;
		s_states[std::string(definition->id)] = std::move(state);
	}

	nlohmann::json SettingsService::BuildOneDto(std::string_view a_id)
	{
		EnsureInitialized();
		const auto* definition = SettingsRegistry::Find(a_id);
		if (!definition) {
			return nullptr;
		}
		const auto it = s_states.find(std::string(definition->id));
		if (it == s_states.end()) {
			return nullptr;
		}
		return MakeSettingDto(*definition, it->second);
	}

	nlohmann::json SettingsService::BuildSnapshotJson()
	{
		EnsureInitialized();
		nlohmann::json settings = nlohmann::json::array();
		for (const auto& definition : SettingsRegistry::GetAll()) {
			const auto it = s_states.find(std::string(definition.id));
			if (it == s_states.end()) {
				continue;
			}
			settings.push_back(MakeSettingDto(definition, it->second));
		}
		return nlohmann::json{
			{ "settings", settings },
			{ "hasPendingChanges", HasPendingChanges() }
		};
	}

	SettingResult SettingsService::GetAllSettings()
	{
		EnsureInitialized();
		RefreshFromEngine();
		return Ok(BuildSnapshotJson());
	}

	SettingResult SettingsService::GetSettingsByCategory(std::string_view a_category)
	{
		EnsureInitialized();
		nlohmann::json settings = nlohmann::json::array();
		for (const auto* definition : SettingsRegistry::GetByCategory(a_category)) {
			const auto it = s_states.find(std::string(definition->id));
			if (it == s_states.end()) {
				continue;
			}
			settings.push_back(MakeSettingDto(*definition, it->second));
		}
		return Ok(nlohmann::json{
			{ "category", a_category },
			{ "settings", settings }
		});
	}

	SettingResult SettingsService::SetSetting(std::string_view a_id, const nlohmann::json& a_value)
	{
		EnsureInitialized();
		const auto* definition = SettingsRegistry::Find(a_id);
		if (!definition) {
			REX::WARN("{} SETTING_NOT_FOUND id={}", GetLogPrefix(), a_id);
			return Fail("SETTING_NOT_FOUND", "The requested setting is not registered.");
		}

		if (!definition->editable) {
			return Fail("APPLY_NOT_SUPPORTED", "This setting is read-only in Commonwealth Online.");
		}

		auto it = s_states.find(std::string(definition->id));
		if (it == s_states.end() || !it->second.available) {
			return Fail("SETTING_UNAVAILABLE", "The requested setting is unavailable in the engine.");
		}

		nlohmann::json normalized;
		auto validation = ValidateAndNormalize(*definition, a_value, normalized);
		if (!validation.success) {
			REX::WARN("{} Validation failed id={} code={}",
				GetLogPrefix(), definition->id, validation.error->code);
			return validation;
		}

		it->second.pending = normalized;
		it->second.dirty = !ValuesEqual(it->second.pending, it->second.committed);
		REX::INFO("{} Pending change id={} dirty={} value={}",
			GetLogPrefix(), definition->id, it->second.dirty, normalized.dump());
		return Ok(MakeSettingDto(*definition, it->second));
	}

	SettingResult SettingsService::ApplyPendingSettings()
	{
		EnsureInitialized();

		std::size_t appliedCount = 0;
		for (const auto& definition : SettingsRegistry::GetAll()) {
			auto it = s_states.find(std::string(definition.id));
			if (it == s_states.end() || !it->second.dirty || !it->second.available) {
				continue;
			}

			std::string errorCode;
			std::string errorMessage;
			const auto oldValue = it->second.committed;
			const auto newValue = it->second.pending;
			if (!WriteSettingValue(definition, newValue, errorCode, errorMessage)) {
				return Fail(errorCode.empty() ? "PERSIST_FAILED" : errorCode,
					errorMessage.empty() ? "Failed to apply setting." : errorMessage);
			}

			if (!ApplySetting(definition, oldValue, newValue, true)) {
				return Fail("APPLY_NOT_SUPPORTED",
					std::format("Apply handler rejected setting {}.", definition.id));
			}

			it->second.committed = newValue;
			it->second.pending = newValue;
			it->second.dirty = false;
			++appliedCount;
		}

		REX::INFO("{} ApplyPendingSettings complete count={}", GetLogPrefix(), appliedCount);
		return Ok(BuildSnapshotJson());
	}

	SettingResult SettingsService::CancelPendingSettings()
	{
		EnsureInitialized();
		for (auto& [id, state] : s_states) {
			(void)id;
			state.pending = state.committed;
			state.dirty = false;
		}
		REX::INFO("{} Cancelled pending settings", GetLogPrefix());
		return Ok(BuildSnapshotJson());
	}

	SettingResult SettingsService::ResetCategory(std::string_view a_category)
	{
		EnsureInitialized();
		for (const auto* definition : SettingsRegistry::GetByCategory(a_category)) {
			auto it = s_states.find(std::string(definition->id));
			if (it == s_states.end()) {
				continue;
			}
			it->second.pending = it->second.baseline;
			it->second.dirty = !ValuesEqual(it->second.pending, it->second.committed);
		}
		REX::INFO("{} Reset category {} to menu-open values", GetLogPrefix(), a_category);
		return Ok(BuildSnapshotJson());
	}

	SettingResult SettingsService::ResetAllSettings()
	{
		EnsureInitialized();
		for (auto& [id, state] : s_states) {
			(void)id;
			state.pending = state.baseline;
			state.dirty = !ValuesEqual(state.pending, state.committed);
		}
		REX::INFO("{} Reset all settings to menu-open values", GetLogPrefix());
		return Ok(BuildSnapshotJson());
	}

	SettingResult SettingsService::CaptureBaseline()
	{
		EnsureInitialized();
		RefreshFromEngine();
		REX::INFO("{} Captured settings baseline from engine", GetLogPrefix());
		return Ok(BuildSnapshotJson());
	}

	bool SettingsService::HasPendingChanges()
	{
		EnsureInitialized();
		for (const auto& [id, state] : s_states) {
			(void)id;
			if (state.dirty) {
				return true;
			}
		}
		return false;
	}
}
