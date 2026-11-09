#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace F4T::Settings
{
	enum class HUDRefreshRoute : std::uint8_t
	{
		ColourBroadcast,
		OpacityPost,
		NoCallNeeded,
		Gated
	};

	[[nodiscard]] inline HUDRefreshRoute RouteHUDRefresh(std::string_view a_settingId)
	{
		if (a_settingId == "interface.hudColour") {
			return HUDRefreshRoute::ColourBroadcast;
		}
		if (a_settingId == "interface.hudOpacity") {
			return HUDRefreshRoute::OpacityPost;
		}
		// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (G4.6 crosshair and compass readers)
		if (a_settingId == "interface.showCompass" || a_settingId == "interface.crosshair") {
			return HUDRefreshRoute::NoCallNeeded;
		}
		return HUDRefreshRoute::Gated;
	}

	[[nodiscard]] inline std::optional<std::uint64_t> HUDColourSourceAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 1226590;  // RE-Work/subsystems/commonwealth-online-re/live-settings-refresh-co77/README.md (OG AL id verified)
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2695540;  // RE-Work/subsystems/commonwealth-online-re/live-settings-refresh-co77/README.md (NG AL id verified)
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 4802831;  // RE-Work/subsystems/commonwealth-online-re/live-settings-refresh-co77/README.md (AE AL id verified)
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (G4.1 opacity wrapper; findings hud-opacity-wrapper-ids NG and AE, hud-opacity-wrapper-body OG)
	[[nodiscard]] inline std::optional<std::uint64_t> HUDOpacityPostAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 2022;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2717507;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2717507;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (findings rumble-cache-v2)
	[[nodiscard]] inline std::optional<std::uint64_t> RumbleSetterAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 314453;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2268322;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2268322;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (findings rumble-gate-is-bgamepadenable)
	[[nodiscard]] inline std::optional<std::uint64_t> RumbleGateAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 1544449;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2666331;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2666331;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (findings difficulty-fix-v2)
	[[nodiscard]] inline std::optional<std::uint64_t> DifficultyTransitionAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 657344;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2233057;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2233057;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (findings difficulty-fix-v2 notes)
	[[nodiscard]] inline std::optional<std::uint64_t> DifficultyObjectAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 303410;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2698073;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2698073;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (G4.3 singleton; findings audio-singleton-ids)
	[[nodiscard]] inline std::optional<std::uint64_t> AudioManagerSingletonAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 1321158;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2703058;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2703058;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/live-settings-refresh-co77/findings.jsonl (audio-setter-og/ng/ae: Address Library ids)
	[[nodiscard]] inline std::optional<std::uint64_t> AudioMasterVolumeSetterAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 1255857;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2267162;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2267162;
		}
		return std::nullopt;
	}

	// source:E:/GardenEngine/re-answers/2026-10-20-og-master-volume-curve.md (curve Address Library ids OG 774791, NG and AE 2267410)
	[[nodiscard]] inline std::optional<std::uint64_t> AudioVolumeCurveAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 774791;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2267410;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2267410;
		}
		return std::nullopt;
	}

	// source:E:/GardenEngine/re-answers/2026-10-20-og-master-volume-curve.md (BSSoundCategoryHandle::SetVolume Address Library ids)
	[[nodiscard]] inline std::optional<std::uint64_t> SoundCategorySetVolumeAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 826593;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2267420;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 2267420;
		}
		return std::nullopt;
	}

	// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-imagespace/README.md source:CO-BETA/tools/engine-ids.lock.json
	[[nodiscard]] inline std::optional<std::uint64_t> PipBoyRedCellAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 1201665;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2678185;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 4784853;
		}
		return std::nullopt;
	}

	// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-imagespace/README.md source:CO-BETA/tools/engine-ids.lock.json
	[[nodiscard]] inline std::optional<std::uint64_t> PipBoyGreenCellAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 277404;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2678187;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 4784855;
		}
		return std::nullopt;
	}

	// TODO-verify(#407) source:RE-Work/subsystems/commonwealth-online-re/co385-imagespace/README.md source:CO-BETA/tools/engine-ids.lock.json
	[[nodiscard]] inline std::optional<std::uint64_t> PipBoyBlueCellAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 935488;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2678189;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 4784857;
		}
		return std::nullopt;
	}

	// source:RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md (G4.5 gamepad enable case 4; OG 1284221 and NG 2694300 from the case 4 listing)
	// source:CommonwealthOnlineUI/ThirdParty/CommonLibF4/include/RE/IDs.h (BSInputDeviceManager::Singleton, AE)
	[[nodiscard]] inline std::optional<std::uint64_t> InputDeviceManagerAddressLibraryId(const std::array<std::uint16_t, 4>& a_gameVersion)
	{
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 163, 0 }) {
			return 1284221;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 10, 984, 0 }) {
			return 2694300;
		}
		if (a_gameVersion == std::array<std::uint16_t, 4>{ 1, 11, 240, 0 }) {
			return 4807767;
		}
		return std::nullopt;
	}

	[[nodiscard]] inline float InvertIncreasingCurve(float (*a_curve)(float), float a_value)
	{
		const float target = std::clamp(a_value, a_curve(0.0f), a_curve(1.0f));
		float low = 0.0f;
		float high = 1.0f;
		for (int i = 0; i < 24; ++i) {
			const float mid = (low + high) * 0.5f;
			if (a_curve(mid) < target) {
				low = mid;
			} else {
				high = mid;
			}
		}
		return (low + high) * 0.5f;
	}
}
