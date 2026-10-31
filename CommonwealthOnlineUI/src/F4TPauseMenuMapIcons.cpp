#include "pch.h"

#include "F4TPauseMenuMapIcons.h"

#include <cstddef>
#include <cstdint>
#include <iterator>

namespace F4T::PauseMenuMap
{
	namespace
	{
		constexpr MarkerIcons kDefaultIcons{ "SettlementMarker", "icon_38.png", "icon_39.png" };
		constexpr MarkerIcons kQuestIcons{ "QuestMarker", "icon_73.png", "icon_73.png" };
		constexpr MarkerIcons kPlayerSetIcons{ "PlayerSetMarker", "icon_91.png", "icon_91.png" };
		constexpr MarkerIcons kPlayerLocIcons{ "PlayerLocMarker", "icon_94.png", "icon_94.png" };

		constexpr MarkerIcons kTypeTable[] = {
			{ "CaveMarker", "icon_194.png", "icon_195.png" },
			{ "CityMarker", "icon_188.png", "icon_189.png" },
			{ "DiamondCityMarker", "icon_176.png", "icon_177.png" },
			{ "EncampmentMarker", "icon_163.png", "icon_164.png" },
			{ "FactoryMarker", "icon_135.png", "icon_136.png" },
			{ "MonumentMarker", "icon_105.png", "icon_106.png" },
			{ "MetroMarker", "icon_114.png", "icon_115.png" },
			{ "MilitaryBaseMarker", "icon_111.png", "icon_112.png" },
			{ "LandmarkMarker", "icon_123.png", "icon_124.png" },
			{ "OfficeMarker", "icon_99.png", "icon_100.png" },
			{ "TownRuinsMarker", "icon_17.png", "icon_18.png" },
			{ "UrbanRuinsMarker", "icon_14.png", "icon_15.png" },
			{ "SancHillsMarker", "icon_50.png", "icon_51.png" },
			{ "SettlementMarker", "icon_38.png", "icon_39.png" },
			{ "SewerMarker", "icon_35.png", "icon_36.png" },
			{ "VaultMarker", "icon_8.png", "icon_9.png" },
			{ "AirfieldMarker", "icon_218.png", "icon_219.png" },
			{ "BunkerHillMarker", "icon_209.png", "icon_210.png" },
			{ "CamperMarker", "icon_203.png", "icon_204.png" },
			{ "CarMarker", "icon_200.png", "icon_201.png" },
			{ "ChurchMarker", "icon_191.png", "icon_192.png" },
			{ "CountryClubMarker", "icon_185.png", "icon_186.png" },
			{ "CustomHouseMarker", "icon_182.png", "icon_183.png" },
			{ "DriveInMarker", "icon_169.png", "icon_170.png" },
			{ "ElevatedHighwayMarker", "icon_166.png", "icon_167.png" },
			{ "FaneuilHallMarker", "icon_159.png", "icon_160.png" },
			{ "FarmMarker", "icon_156.png", "icon_157.png" },
			{ "FillingStationMarker", "icon_153.png", "icon_154.png" },
			{ "ForestedMarker", "icon_150.png", "icon_151.png" },
			{ "GoodneighborMarker", "icon_147.png", "icon_148.png" },
			{ "GraveyardMarker", "icon_144.png", "icon_145.png" },
			{ "HospitalMarker", "icon_141.png", "icon_142.png" },
			{ "IndustrialDomeMarker", "icon_138.png", "icon_139.png" },
			{ "IndustrialStacksMarker", "icon_179.png", "icon_180.png" },
			{ "InstituteMarker", "icon_132.png", "icon_133.png" },
			{ "IrishPrideMarker", "icon_129.png", "icon_130.png" },
			{ "JunkyardMarker", "icon_126.png", "icon_127.png" },
			{ "ObservatoryMarker", "icon_102.png", "icon_103.png" },
			{ "PierMarker", "icon_96.png", "icon_97.png" },
			{ "PondLakeMarker", "icon_85.png", "icon_86.png" },
			{ "QuarryMarker", "icon_76.png", "icon_77.png" },
			{ "RadioactiveAreaMarker", "icon_65.png", "icon_66.png" },
			{ "RadioTowerMarker", "icon_62.png", "icon_63.png" },
			{ "SalemMarker", "icon_53.png", "icon_54.png" },
			{ "SchoolMarker", "icon_44.png", "icon_45.png" },
			{ "ShipwreckMarker", "icon_32.png", "icon_33.png" },
			{ "SubmarineMarker", "icon_29.png", "icon_30.png" },
			{ "SwanPondMarker", "icon_26.png", "icon_27.png" },
			{ "SynthHeadMarker", "icon_23.png", "icon_24.png" },
			{ "TownMarker", "icon_20.png", "icon_21.png" },
		};
	}

	const MarkerIcons& IconsFor(std::uint8_t a_type)
	{
		using MT = RE::MARKER_TYPE;
		const auto typed = static_cast<MT>(a_type);
		const auto idx = static_cast<std::uint32_t>(a_type);
		if (idx < std::size(kTypeTable)) {
			return kTypeTable[idx];
		}

		switch (typed) {
		case MT::kQuest:
		case MT::kQuestDoor:
		case MT::kQuestMultiple:
			return kQuestIcons;
		case MT::kPlayerSet:
			return kPlayerSetIcons;
		case MT::kPlayerLocation:
			return kPlayerLocIcons;
		default:
			return kDefaultIcons;
		}
	}
}
