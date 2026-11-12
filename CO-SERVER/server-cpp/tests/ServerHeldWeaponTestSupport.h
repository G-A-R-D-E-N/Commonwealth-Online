#pragma once

#include "F4TLoadOrder.h"
#include "F4TWorkshopRaids.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace HeldWeaponTestSupport
{
	using Json = nlohmann::json;

	inline constexpr std::uint32_t kHuntingRifleFormId{ 0x0004F46A };
	inline constexpr std::uint32_t kMinigunFormId{ 0x0001F669 };
	inline constexpr std::uint32_t kMacheteFormId{ 0x00033FE0 };
	inline constexpr std::uint32_t kNoneClassFormId{ 0x00000002 };

	inline Json PluginListPacket()
	{
		F4T::LoadOrder::PluginList list;
		list.full.push_back(F4T::LoadOrder::Plugin{ F4T::LoadOrder::PluginKind::full, 0, "Fallout4.esm" });
		const auto encoded = F4T::LoadOrder::EncodePluginList(list);
		assert(encoded.has_value());
		return Json{ { "type", std::string{ F4T::LoadOrder::kPacketType } }, { "data", *encoded } };
	}

	inline void WriteWeaponClasses(const std::filesystem::path& a_directory)
	{
		const Json classes{
			{ "format", 1 },
			{ "weapons", Json::array({
				Json{ { "plugin", "fallout4.esm" }, { "localFormId", F4T::WorkshopRaids::kSidearmWeaponFormId }, { "class", "Light" }, { "editorId", "10mm" } },
				Json{ { "plugin", "fallout4.esm" }, { "localFormId", kHuntingRifleFormId }, { "class", "Rifle" }, { "editorId", "HuntingRifle" } },
				Json{ { "plugin", "fallout4.esm" }, { "localFormId", kMinigunFormId }, { "class", "Heavy" }, { "editorId", "Minigun" } },
				Json{ { "plugin", "fallout4.esm" }, { "localFormId", kMacheteFormId }, { "class", "Melee" }, { "editorId", "Machete" } },
				Json{ { "plugin", "fallout4.esm" }, { "localFormId", kNoneClassFormId }, { "class", "None" }, { "editorId", "TestNoneClass" } }
			}) }
		};
		std::ofstream output(a_directory / "weapon-classes.json", std::ios::binary);
		output << classes.dump();
	}

	inline std::filesystem::path MakeWorkshopDirectory(const std::string& a_name)
	{
		const auto directory = std::filesystem::temp_directory_path() / (a_name + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		std::filesystem::create_directories(directory);
		{
			std::ofstream zones(directory / "workshop-zones.json", std::ios::binary);
			zones << R"({"version":1,"zones":[]})";
		}
		WriteWeaponClasses(directory);
		return directory;
	}
}
