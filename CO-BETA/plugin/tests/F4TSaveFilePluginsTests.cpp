#include "F4TSaveFilePlugins.h"

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using F4T::SaveFilePlugins::PluginList;

	struct BuiltSave
	{
		std::vector<std::uint8_t> bytes;
		std::size_t               pluginInfoSizeOffset{};
	};

	void AppendU8(std::vector<std::uint8_t>& a_bytes, std::uint8_t a_value)
	{
		a_bytes.push_back(a_value);
	}

	void AppendU16(std::vector<std::uint8_t>& a_bytes, std::uint16_t a_value)
	{
		a_bytes.push_back(static_cast<std::uint8_t>(a_value & 0xFF));
		a_bytes.push_back(static_cast<std::uint8_t>((a_value >> 8) & 0xFF));
	}

	void AppendU32(std::vector<std::uint8_t>& a_bytes, std::uint32_t a_value)
	{
		for (std::size_t index = 0; index < 4; ++index) {
			a_bytes.push_back(static_cast<std::uint8_t>((a_value >> (index * 8)) & 0xFF));
		}
	}

	void AppendString(std::vector<std::uint8_t>& a_bytes, std::string_view a_text)
	{
		assert(a_text.size() <= 0xFFFF);
		AppendU16(a_bytes, static_cast<std::uint16_t>(a_text.size()));
		for (const char character : a_text) {
			a_bytes.push_back(static_cast<std::uint8_t>(character));
		}
	}

	void SetU32(std::vector<std::uint8_t>& a_bytes, std::size_t a_offset, std::uint32_t a_value)
	{
		assert(a_offset + 4 <= a_bytes.size());
		for (std::size_t index = 0; index < 4; ++index) {
			a_bytes[a_offset + index] = static_cast<std::uint8_t>((a_value >> (index * 8)) & 0xFF);
		}
	}

	BuiltSave BuildSave(
		const std::vector<std::string>& a_full,
		const std::vector<std::string>& a_light,
		bool a_hasLightList,
		std::uint32_t a_version = 15,
		std::uint8_t a_formVersion = 68,
		std::string_view a_gameVersion = "1.10.163.0",
		std::string_view a_playerName = "TestChar")
	{
		BuiltSave save;
		auto& bytes = save.bytes;

		constexpr std::uint32_t kWidth = 2;
		constexpr std::uint32_t kHeight = 2;

		const std::string_view magic = F4T::SaveFilePlugins::kSaveMagic;
		bytes.insert(bytes.end(), magic.begin(), magic.end());
		const auto headerSizeOffset = bytes.size();
		AppendU32(bytes, 0);
		AppendU32(bytes, a_version);
		AppendU32(bytes, 1);
		AppendString(bytes, a_playerName);
		AppendU32(bytes, 1);
		AppendString(bytes, "Commonwealth");
		AppendString(bytes, "0d.0h.0m.0 days.0 hours.0 minutes");
		AppendString(bytes, "HumanRace");
		AppendU16(bytes, 0);
		AppendU32(bytes, 0);
		AppendU32(bytes, 0);
		AppendU32(bytes, 0);
		AppendU32(bytes, 0);
		AppendU32(bytes, kWidth);
		AppendU32(bytes, kHeight);
		SetU32(bytes, headerSizeOffset, static_cast<std::uint32_t>(bytes.size() - 16));

		bytes.resize(bytes.size() + 4ull * kWidth * kHeight, 0);
		AppendU8(bytes, a_formVersion);
		AppendString(bytes, a_gameVersion);
		save.pluginInfoSizeOffset = bytes.size();
		AppendU32(bytes, 0);
		const auto blockStart = bytes.size();
		AppendU8(bytes, static_cast<std::uint8_t>(a_full.size()));
		for (const auto& name : a_full) {
			AppendString(bytes, name);
		}
		if (a_hasLightList) {
			AppendU16(bytes, static_cast<std::uint16_t>(a_light.size()));
			for (const auto& name : a_light) {
				AppendString(bytes, name);
			}
		}
		SetU32(bytes, save.pluginInfoSizeOffset, static_cast<std::uint32_t>(bytes.size() - blockStart));
		return save;
	}

	PluginList MakeLoaded(std::vector<std::string> a_full, std::vector<std::string> a_light)
	{
		return PluginList{ std::move(a_full), std::move(a_light) };
	}

	void TestParsesFullAndLight()
	{
		const auto save = BuildSave({ "Fallout4.esm", "DLCRobot.esm" }, { "ccBGSFO4115-X02.esl" }, true);
		const auto parsed = F4T::SaveFilePlugins::ParseSavePlugins(save.bytes);
		assert(parsed.has_value());
		assert(parsed->full.size() == 2);
		assert(parsed->full[0] == "Fallout4.esm");
		assert(parsed->full[1] == "DLCRobot.esm");
		assert(parsed->light.size() == 1);
		assert(parsed->light[0] == "ccBGSFO4115-X02.esl");
	}

	void TestParsesSaveWithoutLightList()
	{
		const auto save = BuildSave({ "Fallout4.esm" }, {}, false, 11, 60);
		const auto parsed = F4T::SaveFilePlugins::ParseSavePlugins(save.bytes);
		assert(parsed.has_value());
		assert(parsed->full.size() == 1);
		assert(parsed->light.empty());
	}

	void TestParsesAeSaveWithEmptyPlayerName()
	{
		const auto save = BuildSave({ "Fallout4.esm", "DLCNukaWorld.esm" }, { "ccOTMFO4001-Remnants.esl" }, true, 15, 69, "1.11.240.0", "");
		const auto parsed = F4T::SaveFilePlugins::ParseSavePlugins(save.bytes);
		assert(parsed.has_value());
		assert(parsed->full.size() == 2);
		assert(parsed->light.size() == 1);
	}

	void TestRejectsBadMagic()
	{
		auto save = BuildSave({ "Fallout4.esm" }, {}, false);
		save.bytes[0] = static_cast<std::uint8_t>('X');
		assert(!F4T::SaveFilePlugins::ParseSavePlugins(save.bytes).has_value());
	}

	void TestRejectsTruncatedHeader()
	{
		const auto save = BuildSave({ "Fallout4.esm" }, { "ccBGSFO4115-X02.esl" }, true);
		const std::vector<std::uint8_t> truncated(save.bytes.begin(), save.bytes.begin() + 64);
		assert(!F4T::SaveFilePlugins::ParseSavePlugins(truncated).has_value());
	}

	void TestRejectsWrongPluginInfoSize()
	{
		auto save = BuildSave({ "Fallout4.esm" }, { "ccBGSFO4115-X02.esl" }, true);
		SetU32(save.bytes, save.pluginInfoSizeOffset, 1);
		assert(!F4T::SaveFilePlugins::ParseSavePlugins(save.bytes).has_value());
	}

	void TestRejectsUnknownSaveVersion()
	{
		const auto save = BuildSave({ "Fallout4.esm" }, {}, false, 16);
		assert(!F4T::SaveFilePlugins::ParseSavePlugins(save.bytes).has_value());
	}

	void TestFindsMissingPlugins()
	{
		const PluginList saved{ { "Fallout4.esm", "Missing.esp", "Other.esl" }, { "Light.esp" } };
		const auto loaded = MakeLoaded({ "fallout4.ESM", "Light.esp" }, {});
		const auto missing = F4T::SaveFilePlugins::FindMissingPlugins(saved, loaded);
		assert(missing.size() == 2);
		assert(missing[0] == "Missing.esp");
		assert(missing[1] == "Other.esl");

		const auto allLoaded = MakeLoaded({ "Fallout4.esm", "Missing.esp" }, { "Other.esl", "Light.esp" });
		assert(F4T::SaveFilePlugins::FindMissingPlugins(saved, allLoaded).empty());
	}

	void TestFormatsMissingMessage()
	{
		const auto shortMessage = F4T::SaveFilePlugins::BuildMissingPluginsMessage({ "Missing.esp", "Other.esl" });
		assert(shortMessage ==
		       "This save needs plugins that are not loaded: Missing.esp, Other.esl. Install the server modpack or pick another save.");

		const auto longMessage = F4T::SaveFilePlugins::BuildMissingPluginsMessage(
			{ "A.esp", "B.esp", "C.esp", "D.esp", "E.esp", "F.esp", "G.esp", "H.esp" });
		assert(longMessage ==
		       "This save needs plugins that are not loaded: A.esp, B.esp, C.esp, D.esp, E.esp and 3 more. "
		       "Install the server modpack or pick another save.");
	}
}

int main()
{
	TestParsesFullAndLight();
	TestParsesSaveWithoutLightList();
	TestParsesAeSaveWithEmptyPlayerName();
	TestRejectsBadMagic();
	TestRejectsTruncatedHeader();
	TestRejectsWrongPluginInfoSize();
	TestRejectsUnknownSaveVersion();
	TestFindsMissingPlugins();
	TestFormatsMissingMessage();
	return EXIT_SUCCESS;
}
