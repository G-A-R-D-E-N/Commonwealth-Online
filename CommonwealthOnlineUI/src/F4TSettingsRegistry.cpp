#include "pch.h"

#include "F4TSettingsRegistry.h"

#include <vector>

namespace F4T::Settings
{
	namespace
	{
		bool g_initialized = false;
		std::vector<SettingDefinition> g_definitions;
		std::vector<AudioChannelSetting> g_audioChannels;

		SettingOption Opt(nlohmann::json a_value, std::string a_label)
		{
			return SettingOption{ std::move(a_value), std::move(a_label) };
		}

		void BuildRegistry()
		{
			g_definitions.clear();
			g_audioChannels.clear();

			g_definitions.push_back({
				.id = "interface.dialogueSubtitles",
				.engineKey = "bDialogueSubtitles:Interface",
				.category = "interface",
				.label = "Dialogue Subtitles",
				.description = "Show subtitles during NPC dialogue.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "interface.generalSubtitles",
				.engineKey = "bGeneralSubtitles:Interface",
				.category = "interface",
				.label = "General Subtitles",
				.description = "Show subtitles for general spoken lines.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "interface.dialogueCamera",
				.engineKey = "bDialogueCameraEnable:Interface",
				.category = "interface",
				.label = "Dialogue Camera",
				.description = "Enable the cinematic dialogue camera.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "interface.showCompass",
				.engineKey = "bShowCompass:Interface",
				.category = "interface",
				.label = "Show Compass",
				.description = "Display the compass on the HUD.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::HUDRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
			});
			g_definitions.push_back({
				.id = "interface.crosshair",
				.engineKey = "bCrosshairEnabled:MAIN",
				.category = "interface",
				.label = "Crosshair",
				.description = "Show the crosshair while aiming.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::HUDRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
			});
			g_definitions.push_back({
				.id = "interface.activeEffectWidget",
				.engineKey = "uHUDActiveEffectWidget:Interface",
				.category = "interface",
				.label = "Active Effect Widget",
				.description = "HUD active-effect widget display mode.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::UnsignedInteger,
				.applyMethod = ApplyMethod::HUDRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
				.minimum = 0,
				.maximum = 2,
				.step = 1,
				.options = {
					Opt(0, "Off"),
					Opt(1, "On"),
					Opt(2, "Always On"),
				},
			});

			g_definitions.push_back({
				.id = "controls.gamepadEnabled",
				.engineKey = "bGamepadEnable:General",
				.category = "controls",
				.label = "Controller Enabled",
				.description = "Allow gamepad input.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::InputRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
			});
			g_definitions.push_back({
				.id = "controls.mouseSensitivity",
				.engineKey = "fMouseHeadingSensitivity:Controls",
				.category = "controls",
				.label = "Mouse Sensitivity",
				.description = "Look sensitivity for mouse aiming.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Float,
				.applyMethod = ApplyMethod::InputRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
				.minimum = 0.0,
				.maximum = 100.0,
				.step = 1.0,
				.uiUsesPercent = true,
			});
			g_definitions.push_back({
				.id = "controls.gamepadSensitivity",
				.engineKey = "fGamepadHeadingSensitivity:Controls",
				.category = "controls",
				.label = "Controller Sensitivity",
				.description = "Look sensitivity for gamepad aiming.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Float,
				.applyMethod = ApplyMethod::InputRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
				.minimum = 0.0,
				.maximum = 100.0,
				.step = 1.0,
				.uiUsesPercent = true,
			});
			g_definitions.push_back({
				.id = "controls.alwaysRun",
				.engineKey = "bAlwaysRunByDefault:Controls",
				.category = "controls",
				.label = "Always Run",
				.description = "Run by default instead of walking.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "controls.invertY",
				.engineKey = "bInvertYValues:Controls",
				.category = "controls",
				.label = "Invert Y",
				.description = "Invert vertical look axis.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "controls.rumble",
				.engineKey = "bGamePadRumble:Controls",
				.category = "controls",
				.label = "Controller Vibration",
				.description = "Enable gamepad rumble feedback.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::InputRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
			});

			g_definitions.push_back({
				.id = "save.onPause",
				.engineKey = "bSaveOnPause:MAIN",
				.category = "save",
				.label = "Save On Pause",
				.description = "Create an autosave when opening the pause menu.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "save.onTravel",
				.engineKey = "bSaveOnTravel:MAIN",
				.category = "save",
				.label = "Save On Travel",
				.description = "Create an autosave when fast travelling.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "save.onWait",
				.engineKey = "bSaveOnWait:MAIN",
				.category = "save",
				.label = "Save On Wait",
				.description = "Create an autosave when waiting.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "save.onRest",
				.engineKey = "bSaveOnRest:MAIN",
				.category = "save",
				.label = "Save On Rest",
				.description = "Create an autosave when sleeping.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "save.autosaveInterval",
				.engineKey = "fAutosaveEveryXMins:SaveGame",
				.category = "save",
				.label = "Autosave Interval",
				.description = "Minutes between timed autosaves.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Float,
				.applyMethod = ApplyMethod::AutosaveRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
				.minimum = 5.0,
				.maximum = 60.0,
				.step = 5.0,
				.options = {
					Opt(5.0, "5 minutes"),
					Opt(10.0, "10 minutes"),
					Opt(15.0, "15 minutes"),
					Opt(20.0, "20 minutes"),
					Opt(30.0, "30 minutes"),
					Opt(60.0, "60 minutes"),
				},
			});

			g_definitions.push_back({
				.id = "gameplay.difficulty",
				.engineKey = "iDifficulty:GamePlay",
				.category = "gameplay",
				.label = "Difficulty",
				.description = "Combat and loot difficulty. Survival is excluded until its transition path is confirmed.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Integer,
				.applyMethod = ApplyMethod::DifficultyChange,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
				.minimum = 0,
				.maximum = 4,
				.step = 1,
				.options = {
					Opt(0, "Very Easy"),
					Opt(1, "Easy"),
					Opt(2, "Normal"),
					Opt(3, "Hard"),
					Opt(4, "Very Hard"),
				},
			});
			g_definitions.push_back({
				.id = "gameplay.floatingQuestMarkers",
				.engineKey = "bShowFloatingQuestMarkers:GamePlay",
				.category = "gameplay",
				.label = "Floating Quest Markers",
				.description = "Show floating quest markers in the world.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});
			g_definitions.push_back({
				.id = "gameplay.questMarkers",
				.engineKey = "bShowQuestMarkers:GamePlay",
				.category = "gameplay",
				.label = "Quest Markers",
				.description = "Show quest markers on the compass and map.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
			});

			g_definitions.push_back({
				.id = "audio.master",
				.engineKey = "fAudioMasterVolume:AudioMenu",
				.category = "audio",
				.label = "Master Volume",
				.description = "Master audio volume.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Float,
				.applyMethod = ApplyMethod::AudioRefresh,
				.applyTiming = ApplyTiming::Immediate,
				.minimum = 0.0,
				.maximum = 1.0,
				.step = 0.05,
				.uiUsesPercent = true,
			});

			g_audioChannels = {
				{ "audio.channel0", "fVal0:AudioMenu", "uID0:AudioMenu", 554685, "Voice" },
				{ "audio.channel1", "fVal1:AudioMenu", "uID1:AudioMenu", 466532, "Music" },
				{ "audio.channel2", "fVal2:AudioMenu", "uID2:AudioMenu", 94881, "Effects" },
				{ "audio.channel3", "fVal3:AudioMenu", "uID3:AudioMenu", 1007612, "Footsteps" },
				{ "audio.channel4", "fVal4:AudioMenu", "uID4:AudioMenu", 138006, "Radio" },
				{ "audio.channel5", "fVal5:AudioMenu", "uID5:AudioMenu", 0, std::nullopt },
				{ "audio.channel6", "fVal6:AudioMenu", "uID6:AudioMenu", 0, std::nullopt },
				{ "audio.channel7", "fVal7:AudioMenu", "uID7:AudioMenu", 0, std::nullopt },
			};

			for (const auto& channel : g_audioChannels) {
				if (channel.identifier == 0) {
					continue;
				}
				g_definitions.push_back({
					.id = channel.settingID,
					.engineKey = channel.valueKey,
					.category = "audio",
					.label = channel.displayName.value(),
					.description = "Volume of this audio category.",
					.source = SettingSource::INIPref,
					.type = SettingValueType::Float,
					.applyMethod = ApplyMethod::AudioRefresh,
					.applyTiming = ApplyTiming::Immediate,
					.minimum = 0.0,
					.maximum = 1.0,
					.step = 0.05,
					.showInUI = true,
					.uiUsesPercent = true,
				});
			}

			g_definitions.push_back({
				.id = "display.resolution",
				.engineKey = "special:resolution",
				.category = "display",
				.label = "Resolution",
				.description = "Output resolution. Set in the Fallout 4 launcher. Changing it in game is not supported.",
				.source = SettingSource::Special,
				.type = SettingValueType::Resolution,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.editable = false,
			});
			g_definitions.push_back({
				.id = "display.windowMode",
				.engineKey = "special:windowMode",
				.category = "display",
				.label = "Window Mode",
				.description = "Windowed, borderless, or fullscreen. Set in the Fallout 4 launcher. Changing it in game is not supported.",
				.source = SettingSource::Special,
				.type = SettingValueType::WindowMode,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.editable = false,
				.options = {
					Opt("windowed", "Windowed"),
					Opt("borderless", "Borderless"),
					Opt("fullscreen", "Fullscreen"),
				},
			});
			g_definitions.push_back({
				.id = "display.vsync",
				.engineKey = "iPresentInterval:Display",
				.category = "display",
				.label = "VSync",
				.description = "Present interval (0 = off, 1 = on).",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Integer,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.minimum = 0,
				.maximum = 1,
				.step = 1,
				.requiresRestart = true,
				.options = {
					Opt(0, "Off"),
					Opt(1, "On"),
				},
			});
			g_definitions.push_back({
				.id = "display.antiAliasing",
				.engineKey = "sAntiAliasing:Display",
				.category = "display",
				.label = "Anti-Aliasing",
				.description = "Anti-aliasing mode used by the renderer.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::String,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.options = {
					Opt("", "Off"),
					Opt("FXAA", "FXAA"),
					Opt("TAA", "TAA"),
				},
			});
			g_definitions.push_back({
				.id = "display.shadowResolution",
				.engineKey = "iShadowMapResolution:Display",
				.category = "display",
				.label = "Shadow Quality",
				.description = "Shadow map resolution.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Integer,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.options = {
					Opt(1024, "Low (1024)"),
					Opt(2048, "Medium (2048)"),
					Opt(4096, "High (4096)"),
				},
			});
			g_definitions.push_back({
				.id = "display.anisotropy",
				.engineKey = "iMaxAnisotropy:Display",
				.category = "display",
				.label = "Anisotropic Filtering",
				.description = "Texture filtering quality.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Integer,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.options = {
					Opt(0, "Off"),
					Opt(2, "2x"),
					Opt(4, "4x"),
					Opt(8, "8x"),
					Opt(16, "16x"),
				},
			});
			g_definitions.push_back({
				.id = "display.ambientOcclusion",
				.engineKey = "bSAOEnable:Display",
				.category = "display",
				.label = "Ambient Occlusion",
				.description = "Enable screen-space ambient occlusion.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});
			g_definitions.push_back({
				.id = "display.wetnessMaterials",
				.engineKey = "bEnableWetnessMaterials:Display",
				.category = "display",
				.label = "Wetness Materials",
				.description = "Enable wetness material effects.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});
			g_definitions.push_back({
				.id = "display.rainOcclusion",
				.engineKey = "bEnableRainOcclusion:Display",
				.category = "display",
				.label = "Rain Occlusion",
				.description = "Enable rain occlusion under cover.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.showInUI = false,
			});
			g_definitions.push_back({
				.id = "display.volumetricLighting",
				.engineKey = "bVolumetricLightingEnable:Display",
				.category = "display",
				.label = "Volumetric Lighting",
				.description = "Enable volumetric lighting.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});
			g_definitions.push_back({
				.id = "display.depthOfField",
				.engineKey = "bDoDepthOfField:Imagespace",
				.category = "display",
				.label = "Depth Of Field",
				.description = "Enable depth of field effects.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});
			g_definitions.push_back({
				.id = "display.motionBlur",
				.engineKey = "bMBEnable:Imagespace",
				.category = "display",
				.label = "Motion Blur",
				.description = "Enable motion blur.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});
			g_definitions.push_back({
				.id = "display.lensFlare",
				.engineKey = "bLensFlare:Imagespace",
				.category = "display",
				.label = "Lens Flare",
				.description = "Enable lens flare effects.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
				.showInUI = false,
			});
			g_definitions.push_back({
				.id = "display.screenSpaceReflections",
				.engineKey = "bScreenSpaceReflections:LightingShader",
				.category = "display",
				.label = "Screen-Space Reflections",
				.description = "Enable screen-space reflections.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});
			g_definitions.push_back({
				.id = "display.decals",
				.engineKey = "bDecals:Decals",
				.category = "display",
				.label = "Decals",
				.description = "Enable world decals such as bullet impacts.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::DisplayChange,
				.applyTiming = ApplyTiming::RequiresRestart,
				.requiresRestart = true,
			});

			g_definitions.push_back({
				.id = "interface.hudOpacity",
				.engineKey = "fHUDOpacity:MAIN",
				.category = "appearance",
				.label = "HUD Opacity",
				.description = "Overall opacity of the gameplay HUD.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Float,
				.applyMethod = ApplyMethod::HUDRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
				.minimum = 0.0,
				.maximum = 1.0,
				.step = 0.05,
				.uiUsesPercent = true,
			});
			g_definitions.push_back({
				.id = "interface.hudColour",
				.engineKey = "special:hudColour",
				.category = "appearance",
				.label = "HUD Colour",
				.description = "Primary HUD accent colour (RGB).",
				.source = SettingSource::Special,
				.type = SettingValueType::Colour,
				.applyMethod = ApplyMethod::HUDRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
			});
			g_definitions.push_back({
				.id = "pipboy.colour",
				.engineKey = "special:pipboyColour",
				.category = "appearance",
				.label = "Pip-Boy Colour",
				.description = "Pip-Boy effect colour.",
				.source = SettingSource::Special,
				.type = SettingValueType::Colour,
				.applyMethod = ApplyMethod::PipBoyRefresh,
				.applyTiming = ApplyTiming::RequiresReverseEngineering,
			});

			// Creations menu music: registered for API completeness, hidden from UI
			// until a dedicated Creations settings surface exists.
			g_definitions.push_back({
				.id = "creations.musicOn",
				.engineKey = "bMusicOn:CreationsMenu",
				.category = "creations",
				.label = "Creations Menu Music",
				.description = "Play music in the Creations menu.",
				.source = SettingSource::INIPref,
				.type = SettingValueType::Boolean,
				.applyMethod = ApplyMethod::Direct,
				.showInUI = false,
			});

		}
	}

	void SettingsRegistry::Initialize()
	{
		if (g_initialized) {
			return;
		}
		BuildRegistry();
		g_initialized = true;
		REX::INFO("[Settings] Registry initialised with {} definitions and {} audio channels",
			g_definitions.size(), g_audioChannels.size());
	}

	bool SettingsRegistry::IsInitialized()
	{
		return g_initialized;
	}

	const SettingDefinition* SettingsRegistry::Find(std::string_view a_id)
	{
		Initialize();
		for (const auto& definition : g_definitions) {
			if (definition.id == a_id) {
				return &definition;
			}
		}
		return nullptr;
	}

	std::span<const SettingDefinition> SettingsRegistry::GetAll()
	{
		Initialize();
		return g_definitions;
	}

	std::vector<const SettingDefinition*> SettingsRegistry::GetByCategory(std::string_view a_category)
	{
		Initialize();
		std::vector<const SettingDefinition*> result;
		for (const auto& definition : g_definitions) {
			if (definition.category == a_category) {
				result.push_back(&definition);
			}
		}
		return result;
	}

	std::span<const AudioChannelSetting> SettingsRegistry::GetAudioChannels()
	{
		Initialize();
		return g_audioChannels;
	}
}
