#include "pch.h"

#include "F4TSettingsApplyHandlers.h"
#include "F4TSettingsRefreshRoute.h"
#include "F4TSettingsRegistry.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>

namespace F4T::Settings
{
	namespace
	{
		std::string GetLogPrefix()
		{
			return "[Settings]";
		}

		bool QueueHUDColourBroadcast()
		{
			static std::atomic_bool s_gateLogged{ false };
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			const auto sourceId = HUDColourSourceAddressLibraryId({ version[0], version[1], version[2], version[3] });
			if (!sourceId) {
				if (!s_gateLogged.exchange(true)) {
					REX::WARN("{} HUD colour refresh gated: no verified ColorUpdateEvent source for runtime {}",
						GetLogPrefix(), version.string());
				}
				return true;
			}

			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				REX::WARN("{} Game task interface unavailable; HUD colour refresh skipped", GetLogPrefix());
				return true;
			}

			taskInterface->AddTask([id = *sourceId]() {
				// RE-Work/subsystems/commonwealth-online-re/live-settings-refresh-co77/README.md (ColorUpdateEvent source global, tier 1 broadcast)
				using EventSource_t = RE::BSTGlobalEvent::EventSource<RE::ColorUpdateEvent>;
				REL::Relocation<EventSource_t**> source{ REL::ID{ id } };
				if (!*source) {
					return;
				}
				(*source)->Notify(RE::ColorUpdateEvent{});
			});
			return true;
		}

		// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (ids in F4TSettingsRefreshRoute.h)
		std::array<std::uint16_t, 4> RunningVersion()
		{
			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			return { version[0], version[1], version[2], version[3] };
		}

		bool AddGameTask(std::function<void()> a_task)
		{
			const auto* taskInterface = F4SE::GetTaskInterface();
			if (!taskInterface) {
				REX::WARN("{} Game task interface unavailable; refresh skipped", GetLogPrefix());
				return true;
			}

			taskInterface->AddTask(std::move(a_task));
			return true;
		}

		RE::Setting* FindIniSetting(const std::string& a_key)
		{
			const auto findInList = [](RE::INISettingCollection* a_collection, const std::string& a_name) -> RE::Setting* {
				return a_collection ? a_collection->GetSetting(a_name) : nullptr;
			};
			const auto find = [&findInList](const std::string& a_name) -> RE::Setting* {
				if (auto* setting = findInList(RE::INIPrefSettingCollection::GetSingleton(), a_name)) {
					return setting;
				}
				return findInList(RE::INISettingCollection::GetSingleton(), a_name);
			};

			if (auto* setting = find(a_key)) {
				return setting;
			}
			const auto colon = a_key.find(':');
			return colon == std::string::npos ? nullptr : find(a_key.substr(0, colon));
		}

		bool QueueHUDOpacityRefresh()
		{
			static std::atomic_bool s_gateLogged{ false };
			const auto version = RunningVersion();
			const auto wrapperId = HUDOpacityPostAddressLibraryId(version);
			if (!wrapperId) {
				if (!s_gateLogged.exchange(true)) {
					REX::WARN("{} HUD opacity refresh gated: no verified wrapper for runtime {}.{}.{}.{}",
						GetLogPrefix(), version[0], version[1], version[2], version[3]);
				}
				return true;
			}

			return AddGameTask([id = *wrapperId]() {
				using Post_t = void (*)();
				REL::Relocation<Post_t> post{ REL::ID{ id } };
				post();
			});
		}

		// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (G4.3 audio master volume case body; G4.5 gamepad enable case 4)
		bool QueueAudioMasterRefresh(std::string_view a_settingId, float a_previousValue, float a_rawValue)
		{
			static std::atomic_uint32_t s_gateCount{ 0 };
			const auto version = RunningVersion();
			const auto singletonId = AudioManagerSingletonAddressLibraryId(version);
			const auto setterId = AudioMasterVolumeSetterAddressLibraryId(version);
			const auto curveId = AudioVolumeCurveAddressLibraryId(version);
			if (!singletonId || !setterId || !curveId) {
				const auto occurrence = ++s_gateCount;
				if (occurrence == 1 || occurrence % 100 == 0) {
					REX::WARN("{} Audio master refresh gated (occurrence {}): no verified setter and curve for runtime {}.{}.{}.{}",
						GetLogPrefix(), occurrence, version[0], version[1], version[2], version[3]);
				}
				return true;
			}

			return AddGameTask([settingId = std::string(a_settingId), previous = a_previousValue, raw = std::clamp(a_rawValue, 0.0f, 1.0f),
				singletonAlId = *singletonId, setterAlId = *setterId, curveAlId = *curveId]() {
				using Curve_t = float (*)(float);
				using Setter_t = void (*)(RE::BSAudioManager*, float);
				REL::Relocation<RE::BSAudioManager**> manager{ REL::ID{ singletonAlId } };
				REL::Relocation<Curve_t> curve{ REL::ID{ curveAlId } };
				REL::Relocation<Setter_t> setter{ REL::ID{ setterAlId } };
				if (!*manager) {
					REX::WARN("{} [RT-VERIFY 385-C] {} skipped: BSAudioManager singleton is null previous={} raw={}",
						GetLogPrefix(), settingId, previous, raw);
					return;
				}
				// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md
				const auto curved = curve(raw);
				const bool initialized = (*manager)->stateFlags.all(RE::BSAudioManager::State::ManagerInitialized);
				REX::INFO("{} [RT-VERIFY 385-C] {} manager={:p} gateInitialized={} previous={} raw={} curved={} setter AL {}",
					GetLogPrefix(), settingId, static_cast<const void*>(*manager), initialized, previous, raw, curved, setterAlId);
				setter(*manager, curved);
				REX::INFO("{} {} setter returned", GetLogPrefix(), settingId);
			});
		}

		// constraint: must match BSSoundCategoryHandle, 8 bytes holding one BSISoundCategory pointer (source:E:/GardenEngine/re-answers/2026-10-20-og-master-volume-curve.md).
		struct SoundCategoryHandle
		{
			RE::BSISoundCategory* category;
		};

		bool QueueAudioChannelRefresh(std::string_view a_settingId, float a_previousValue, float a_rawValue)
		{
			static std::atomic_uint32_t s_gateCount{ 0 };
			const auto channels = SettingsRegistry::GetAudioChannels();
			const auto channel = std::find_if(channels.begin(), channels.end(), [&a_settingId](const AudioChannelSetting& a_entry) {
				return a_entry.settingID == a_settingId;
			});
			if (channel == channels.end()) {
				REX::WARN("{} audio channel {} has no channel entry; apply skipped", GetLogPrefix(), a_settingId);
				return false;
			}
			if (channel->identifier == 0) {
				REX::WARN("{} audio channel {} has identifier 0; apply skipped", GetLogPrefix(), a_settingId);
				return true;
			}
			const auto version = RunningVersion();
			const auto singletonId = AudioManagerSingletonAddressLibraryId(version);
			const auto curveId = AudioVolumeCurveAddressLibraryId(version);
			const auto setVolumeId = SoundCategorySetVolumeAddressLibraryId(version);
			if (!singletonId || !curveId || !setVolumeId) {
				const auto occurrence = ++s_gateCount;
				if (occurrence == 1 || occurrence % 100 == 0) {
					REX::WARN("{} Audio channel refresh gated (occurrence {}): no verified singleton, curve and SetVolume for runtime {}.{}.{}.{}",
						GetLogPrefix(), occurrence, version[0], version[1], version[2], version[3]);
				}
				return true;
			}

			// constraint: SetVolume must not run with a null BSAudioManager (source:E:/GardenEngine/re-answers/2026-10-20-audio-setvolume-null-and-master-startup.md).
			// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps2/README.md
			return AddGameTask([settingId = std::string(a_settingId), previous = a_previousValue, raw = std::clamp(a_rawValue, 0.0f, 1.0f), formId = channel->identifier,
				singletonAlId = *singletonId, curveAlId = *curveId, setVolumeAlId = *setVolumeId]() {
				using Curve_t = float (*)(float);
				using SetVolume_t = void (*)(SoundCategoryHandle*, float);
				REL::Relocation<RE::BSAudioManager**> manager{ REL::ID{ singletonAlId } };
				auto* form = RE::TESForm::GetFormByNumericID(formId);
				if (!*manager || !form || form->GetFormType() != RE::ENUM_FORM_ID::kSNCT) {
					// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md
					REX::WARN("{} [RT-VERIFY 385-C] {} form {:08X} skipped: manager={:p} form={:p} previous={} raw={}",
						GetLogPrefix(), settingId, formId, static_cast<const void*>(*manager), static_cast<const void*>(form), previous, raw);
					return;
				}
				SoundCategoryHandle handle{ static_cast<RE::BSISoundCategory*>(form->As<RE::BGSSoundCategory>()) };
				// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md
				REL::Relocation<Curve_t> curve{ REL::ID{ curveAlId } };
				REL::Relocation<SetVolume_t> setVolume{ REL::ID{ setVolumeAlId } };
				const auto curved = curve(raw);
				const bool initialized = (*manager)->stateFlags.all(RE::BSAudioManager::State::ManagerInitialized);
				// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-gaps2/README.md
				REX::INFO("{} [RT-VERIFY 385-C] {} form {:08X} handle={:p} previous={} raw={} curved={} gateInitialized={} SetVolume AL {}",
					GetLogPrefix(), settingId, formId, static_cast<const void*>(handle.category), previous, raw, curved, initialized, setVolumeAlId);
				setVolume(&handle, curved);
				REX::INFO("{} {} form {:08X} SetVolume returned", GetLogPrefix(), settingId, formId);
			});
		}

		bool QueueGamepadEnableRefresh(bool a_enable)
		{
			const auto version = RunningVersion();
			const auto managerId = InputDeviceManagerAddressLibraryId(version);
			const auto setterId = RumbleSetterAddressLibraryId(version);
			if (!managerId || !setterId) {
				return true;
			}

			return AddGameTask([enable = a_enable, managerAlId = *managerId, setterAlId = *setterId]() {
				using Setter_t = void (*)(bool);
				REL::Relocation<RE::BSInputDeviceManager**> manager{ REL::ID{ managerAlId } };
				REL::Relocation<Setter_t> setter{ REL::ID{ setterAlId } };
				if (!*manager) {
					return;
				}
				const auto* rumbleSetting = FindIniSetting("bGamePadRumble:Controls");
				if (!rumbleSetting || rumbleSetting->GetType() != RE::Setting::SETTING_TYPE::kBinary) {
					return;
				}
				(*manager)->queuedGamepadEnableValue = enable;
				(*manager)->valueQueued = true;
				setter(enable && rumbleSetting->GetBinary());
			});
		}

		// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-imagespace/README.md
		bool QueuePipBoyColourCells()
		{
			const auto version = RunningVersion();
			const auto redId = PipBoyRedCellAddressLibraryId(version);
			const auto greenId = PipBoyGreenCellAddressLibraryId(version);
			const auto blueId = PipBoyBlueCellAddressLibraryId(version);
			if (!redId || !greenId || !blueId) {
				REX::WARN("{} [RT-VERIFY 385-A] Pip-Boy colour skipped: no cell ids for runtime {}.{}.{}.{}",
					GetLogPrefix(), version[0], version[1], version[2], version[3]);
				return true;
			}

			return AddGameTask([cellIds = std::array<std::uint64_t, 3>{ *redId, *greenId, *blueId }]() {
				constexpr std::array<const char*, 3> keys{ "fPipboyEffectColorR:Pipboy", "fPipboyEffectColorG:Pipboy", "fPipboyEffectColorB:Pipboy" };
				constexpr std::array<const char*, 3> channels{ "R", "G", "B" };
				for (std::size_t i = 0; i < cellIds.size(); ++i) {
					auto* setting = FindIniSetting(keys[i]);
					if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kFloat) {
						REX::WARN("{} [RT-VERIFY 385-A] Pip-Boy {} cell skipped: setting {} missing or not a float",
							GetLogPrefix(), channels[i], keys[i]);
						continue;
					}
					// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-imagespace/README.md
					auto* cell = reinterpret_cast<float*>(REL::ID{ cellIds[i] }.address());
					const float before = *cell;
					// Constraint: engine clamps Pip-Boy colour to [0.005, 1.0] on OG, NG and AE; source:docs/code-notes/CommonwealthOnlineUI.md.
					*cell = std::clamp(setting->GetFloat(), 0.005F, 1.0F);
					REX::INFO("{} [RT-VERIFY 385-A] Pip-Boy {} cell AL {} before={} written={} from {}",
						GetLogPrefix(), channels[i], cellIds[i], before, *cell, keys[i]);
				}
			});
		}
	}

	bool ApplyHUDSettings(const SettingDefinition& a_definition)
	{
		const auto route = RouteHUDRefresh(a_definition.id);
		if (route == HUDRefreshRoute::ColourBroadcast) {
			return QueueHUDColourBroadcast();
		}
		if (route == HUDRefreshRoute::OpacityPost) {
			return QueueHUDOpacityRefresh();
		}
		if (route == HUDRefreshRoute::NoCallNeeded) {
			// TODO(RE): per-frame reader cadence and HUD child-list membership are unproven (runtime checks 2 and 3).
			REX::INFO("{} ApplyHUDSettings id={} — engine re-reads the value; no refresh call",
				GetLogPrefix(), a_definition.id);
			return true;
		}

		REX::INFO("{} ApplyHUDSettings placeholder id={} — values persisted; live HUD refresh pending RE",
			GetLogPrefix(), a_definition.id);
		return true;
	}

	bool ApplyAudioSetting(const SettingDefinition& a_definition, float a_previousValue, float a_value)
	{
		if (!std::isfinite(a_value)) {
			REX::WARN("{} audio apply skipped id={}: non-finite value {}", GetLogPrefix(), a_definition.id, a_value);
			return true;
		}
		if (a_definition.id == "audio.master") {
			return QueueAudioMasterRefresh(a_definition.id, a_previousValue, a_value);
		}
		return QueueAudioChannelRefresh(a_definition.id, a_previousValue, a_value);
	}

	bool ApplyDifficulty(const SettingDefinition& a_definition, std::int32_t a_oldDifficulty, std::int32_t a_newDifficulty)
	{
		if (a_newDifficulty < 0 || a_newDifficulty > 4) {
			REX::WARN("{} ApplyDifficulty rejected out-of-range value {}", GetLogPrefix(), a_newDifficulty);
			return false;
		}

		if (a_oldDifficulty < 0 || a_oldDifficulty > 4) {
			REX::WARN("{} ApplyDifficulty survival or unknown previous difficulty; transition skipped", GetLogPrefix());
			return true;
		}
		if (a_oldDifficulty == a_newDifficulty) {
			return true;
		}

		const auto version = RunningVersion();
		const auto transitionId = DifficultyTransitionAddressLibraryId(version);
		const auto objectId = DifficultyObjectAddressLibraryId(version);
		if (!transitionId || !objectId) {
			return true;
		}

		return AddGameTask([key = std::string(a_definition.engineKey), oldDifficulty = a_oldDifficulty,
			newDifficulty = a_newDifficulty, transitionAlId = *transitionId, objectAlId = *objectId]() {
			using Transition_t = void (*)(void*, std::int32_t, bool, bool);
			REL::Relocation<void**> object{ REL::ID{ objectAlId } };
			REL::Relocation<Transition_t> transition{ REL::ID{ transitionAlId } };
			if (!*object) {
				return;
			}
			auto* setting = FindIniSetting(key);
			if (!setting || setting->GetType() != RE::Setting::SETTING_TYPE::kInt) {
				return;
			}
			setting->SetInt(oldDifficulty);
			transition(*object, newDifficulty, false, true);
		});
	}

	bool ApplyInputSettings(const SettingDefinition& a_definition, const nlohmann::json& a_newValue)
	{
		if (a_definition.id == "controls.rumble" && a_newValue.is_boolean()) {
			const auto version = RunningVersion();
			const auto setterId = RumbleSetterAddressLibraryId(version);
			const auto gateId = RumbleGateAddressLibraryId(version);
			if (!setterId || !gateId) {
				return true;
			}
			return AddGameTask([rumble = a_newValue.get<bool>(), setterAlId = *setterId, gateAlId = *gateId]() {
				using Setter_t = void (*)(bool);
				REL::Relocation<Setter_t> setter{ REL::ID{ setterAlId } };
				REL::Relocation<std::uint8_t*> gate{ REL::ID{ gateAlId } };
				setter(rumble && *gate != 0);
			});
		}

		if (a_definition.id == "controls.gamepadEnabled" && a_newValue.is_boolean()) {
			return QueueGamepadEnableRefresh(a_newValue.get<bool>());
		}

		// TODO(RE): invert and sensitivity are read live; the per-frame root of their readers is unproven on all runtimes.
		REX::INFO("{} ApplyInputSettings placeholder — values persisted; live input refresh pending RE",
			GetLogPrefix());
		return true;
	}

	bool ApplyPipBoyColour()
	{
		// TODO(RE): image-space effect UpdateParams cadence is unproven; the data model compares the descriptors every frame.
		REX::INFO("{} ApplyPipBoyColour — writing the Pip-Boy colour cells from the stored INI values", GetLogPrefix());
		return QueuePipBoyColourCells();
	}

	bool ApplyAutosaveSettings()
	{
		// TODO(#139): no refresh call exists; runtime check 1 must confirm this Setting is the one the autosave consumer reads.
		REX::INFO("{} ApplyAutosaveSettings — value persisted; the engine reads the interval live, no refresh call",
			GetLogPrefix());
		return true;
	}

	bool ApplyDisplaySetting(const SettingDefinition& a_definition)
	{
		REX::INFO("{} ApplyDisplaySetting id={} timing={} — persisted only; no renderer call",
			GetLogPrefix(), a_definition.id, ToString(a_definition.applyTiming));
		return true;
	}

	bool ApplySettingChange(
		const SettingDefinition& a_definition,
		const nlohmann::json& a_oldValue,
		const nlohmann::json& a_newValue)
	{
		REX::INFO("{} apply route id={} method={} timing={} engineKey={} old={} new={}", GetLogPrefix(), a_definition.id,
			static_cast<std::int32_t>(a_definition.applyMethod), ToString(a_definition.applyTiming), a_definition.engineKey,
			a_oldValue.dump(), a_newValue.dump());
		switch (a_definition.applyMethod) {
		case ApplyMethod::Direct:
			return true;
		case ApplyMethod::HUDRefresh:
			return ApplyHUDSettings(a_definition);
		case ApplyMethod::AudioRefresh: {
			const float previous = a_oldValue.is_number() ? a_oldValue.get<float>() : 0.0f;
			const float value = a_newValue.is_number() ? a_newValue.get<float>() : 0.0f;
			return ApplyAudioSetting(a_definition, previous, value);
		}
		case ApplyMethod::InputRefresh:
			return ApplyInputSettings(a_definition, a_newValue);
		case ApplyMethod::DifficultyChange: {
			const auto oldDifficulty = a_oldValue.is_number_integer() ? a_oldValue.get<std::int32_t>() : -1;
			const auto newDifficulty = a_newValue.is_number_integer() ? a_newValue.get<std::int32_t>() : -1;
			return ApplyDifficulty(a_definition, oldDifficulty, newDifficulty);
		}
		case ApplyMethod::DisplayChange:
			return ApplyDisplaySetting(a_definition);
		case ApplyMethod::PipBoyRefresh:
			return ApplyPipBoyColour();
		case ApplyMethod::AutosaveRefresh:
			return ApplyAutosaveSettings();
		case ApplyMethod::RequiresRestart:
			REX::INFO("{} Setting {} requires restart — persisted for next launch",
				GetLogPrefix(), a_definition.id);
			return true;
		case ApplyMethod::Unsupported:
			REX::WARN("{} APPLY_NOT_SUPPORTED for {}", GetLogPrefix(), a_definition.id);
			return false;
		}
		return false;
	}
}
