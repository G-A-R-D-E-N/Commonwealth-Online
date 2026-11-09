#include "F4TSettingsRefreshRoute.h"

#include <cassert>
#include <cmath>
#include <cstdint>

int main()
{
	using F4T::Settings::AudioManagerSingletonAddressLibraryId;
	using F4T::Settings::AudioMasterVolumeSetterAddressLibraryId;
	using F4T::Settings::AudioVolumeCurveAddressLibraryId;
	using F4T::Settings::DifficultyObjectAddressLibraryId;
	using F4T::Settings::DifficultyTransitionAddressLibraryId;
	using F4T::Settings::HUDColourSourceAddressLibraryId;
	using F4T::Settings::HUDOpacityPostAddressLibraryId;
	using F4T::Settings::HUDRefreshRoute;
	using F4T::Settings::InputDeviceManagerAddressLibraryId;
	using F4T::Settings::InvertIncreasingCurve;
	using F4T::Settings::PipBoyBlueCellAddressLibraryId;
	using F4T::Settings::PipBoyGreenCellAddressLibraryId;
	using F4T::Settings::PipBoyRedCellAddressLibraryId;
	using F4T::Settings::RouteHUDRefresh;
	using F4T::Settings::RumbleGateAddressLibraryId;
	using F4T::Settings::RumbleSetterAddressLibraryId;
	using F4T::Settings::SoundCategorySetVolumeAddressLibraryId;

	assert(RouteHUDRefresh("interface.hudColour") == HUDRefreshRoute::ColourBroadcast);
	assert(RouteHUDRefresh("interface.hudOpacity") == HUDRefreshRoute::OpacityPost);
	assert(RouteHUDRefresh("interface.showCompass") == HUDRefreshRoute::NoCallNeeded);
	assert(RouteHUDRefresh("interface.crosshair") == HUDRefreshRoute::NoCallNeeded);
	assert(RouteHUDRefresh("interface.activeEffectWidget") == HUDRefreshRoute::Gated);
	assert(RouteHUDRefresh("") == HUDRefreshRoute::Gated);

	assert(HUDColourSourceAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 1226590 });
	assert(HUDColourSourceAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2695540 });
	assert(HUDColourSourceAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 4802831 });
	assert(!HUDColourSourceAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!HUDColourSourceAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!HUDColourSourceAddressLibraryId({ 0, 0, 0, 0 }));

	assert(HUDOpacityPostAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 2022 });
	assert(HUDOpacityPostAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2717507 });
	assert(HUDOpacityPostAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2717507 });
	assert(!HUDOpacityPostAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!HUDOpacityPostAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!HUDOpacityPostAddressLibraryId({ 0, 0, 0, 0 }));

	assert(RumbleSetterAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 314453 });
	assert(RumbleSetterAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2268322 });
	assert(RumbleSetterAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2268322 });
	assert(!RumbleSetterAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!RumbleSetterAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!RumbleSetterAddressLibraryId({ 0, 0, 0, 0 }));

	assert(RumbleGateAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 1544449 });
	assert(RumbleGateAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2666331 });
	assert(RumbleGateAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2666331 });
	assert(!RumbleGateAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!RumbleGateAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!RumbleGateAddressLibraryId({ 0, 0, 0, 0 }));

	assert(DifficultyTransitionAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 657344 });
	assert(DifficultyTransitionAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2233057 });
	assert(DifficultyTransitionAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2233057 });
	assert(!DifficultyTransitionAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!DifficultyTransitionAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!DifficultyTransitionAddressLibraryId({ 0, 0, 0, 0 }));

	assert(DifficultyObjectAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 303410 });
	assert(DifficultyObjectAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2698073 });
	assert(DifficultyObjectAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2698073 });
	assert(!DifficultyObjectAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!DifficultyObjectAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!DifficultyObjectAddressLibraryId({ 0, 0, 0, 0 }));

	assert(AudioManagerSingletonAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 1321158 });
	assert(AudioManagerSingletonAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2703058 });
	assert(AudioManagerSingletonAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2703058 });
	assert(!AudioManagerSingletonAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!AudioManagerSingletonAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!AudioManagerSingletonAddressLibraryId({ 0, 0, 0, 0 }));

	// constraint: these Address Library ids must match the RE answer (source:E:/GardenEngine/re-answers/2026-10-20-og-master-volume-curve.md).
	assert(AudioMasterVolumeSetterAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 1255857 });
	assert(AudioMasterVolumeSetterAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2267162 });
	assert(AudioMasterVolumeSetterAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2267162 });
	assert(!AudioMasterVolumeSetterAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!AudioMasterVolumeSetterAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!AudioMasterVolumeSetterAddressLibraryId({ 0, 0, 0, 0 }));

	assert(AudioVolumeCurveAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 774791 });
	assert(AudioVolumeCurveAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2267410 });
	assert(AudioVolumeCurveAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2267410 });
	assert(!AudioVolumeCurveAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!AudioVolumeCurveAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!AudioVolumeCurveAddressLibraryId({ 0, 0, 0, 0 }));

	assert(SoundCategorySetVolumeAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 826593 });
	assert(SoundCategorySetVolumeAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2267420 });
	assert(SoundCategorySetVolumeAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 2267420 });
	assert(!SoundCategorySetVolumeAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!SoundCategorySetVolumeAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!SoundCategorySetVolumeAddressLibraryId({ 0, 0, 0, 0 }));

	assert(InputDeviceManagerAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 1284221 });
	assert(InputDeviceManagerAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2694300 });
	assert(InputDeviceManagerAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 4807767 });
	assert(!InputDeviceManagerAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!InputDeviceManagerAddressLibraryId({ 1, 10, 163, 1 }));
	assert(!InputDeviceManagerAddressLibraryId({ 0, 0, 0, 0 }));

	assert(PipBoyRedCellAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 1201665 });
	assert(PipBoyRedCellAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2678185 });
	assert(PipBoyRedCellAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 4784853 });
	assert(!PipBoyRedCellAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!PipBoyRedCellAddressLibraryId({ 0, 0, 0, 0 }));

	assert(PipBoyGreenCellAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 277404 });
	assert(PipBoyGreenCellAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2678187 });
	assert(PipBoyGreenCellAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 4784855 });
	assert(!PipBoyGreenCellAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!PipBoyGreenCellAddressLibraryId({ 0, 0, 0, 0 }));

	assert(PipBoyBlueCellAddressLibraryId({ 1, 10, 163, 0 }) == std::uint64_t{ 935488 });
	assert(PipBoyBlueCellAddressLibraryId({ 1, 10, 984, 0 }) == std::uint64_t{ 2678189 });
	assert(PipBoyBlueCellAddressLibraryId({ 1, 11, 240, 0 }) == std::uint64_t{ 4784857 });
	assert(!PipBoyBlueCellAddressLibraryId({ 1, 11, 241, 0 }));
	assert(!PipBoyBlueCellAddressLibraryId({ 0, 0, 0, 0 }));

	const auto aeCurve = [](float a_x) { return (std::pow(10.0f, a_x) - 1.0f) / 9.0f; };
	for (const float x : { 0.0f, 0.25f, 0.5f, 1.0f }) {
		assert(std::fabs(InvertIncreasingCurve(aeCurve, aeCurve(x)) - x) < 1e-4f);
	}
	assert(std::fabs(InvertIncreasingCurve(aeCurve, -1.0f)) < 1e-4f);
	assert(std::fabs(InvertIncreasingCurve(aeCurve, 2.0f) - 1.0f) < 1e-4f);

	return 0;
}
