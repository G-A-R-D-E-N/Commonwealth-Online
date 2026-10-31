#include "F4TModpackContentPolicy.h"

#include <cassert>
#include <string>
#include <string_view>

namespace
{
	using namespace F4T::ModpackManifest;

	void ExpectAllowed(std::string_view a_name, AutoInstallFileKind a_kind, std::string_view a_stem)
	{
		assert(CheckAutoInstallName(a_name) == AutoInstallRefusal::none);
		const auto classified = ClassifyAutoInstallName(a_name);
		assert(classified);
		assert(classified->kind == a_kind);
		assert(classified->pluginStem == a_stem);
	}

	void TestPluginsAndCompanionsAreAllowed()
	{
		ExpectAllowed("MyMod.esp", AutoInstallFileKind::plugin, "MyMod");
		ExpectAllowed("MyMod.esm", AutoInstallFileKind::plugin, "MyMod");
		ExpectAllowed("My Mod.esl", AutoInstallFileKind::plugin, "My Mod");
		ExpectAllowed("mymod.ESP", AutoInstallFileKind::plugin, "mymod");
		ExpectAllowed("MyMod - Main.ba2", AutoInstallFileKind::mainArchive, "MyMod");
		ExpectAllowed("mymod - main.ba2", AutoInstallFileKind::mainArchive, "mymod");
		ExpectAllowed("MyMod - Textures.ba2", AutoInstallFileKind::texturesArchive, "MyMod");
		ExpectAllowed("MyMod - Geometry.psg", AutoInstallFileKind::previsGeometry, "MyMod");
		ExpectAllowed("MyMod - Geometry.csg", AutoInstallFileKind::precombinedGeometry, "MyMod");
		ExpectAllowed("MyMod.cdx", AutoInstallFileKind::textureIndex, "MyMod");
		ExpectAllowed("MyMod - Voices_en.ba2", AutoInstallFileKind::voicesArchive, "MyMod");
		ExpectAllowed("MyMod - Voices_ptbr.ba2", AutoInstallFileKind::voicesArchive, "MyMod");
		assert(ClassifyAutoInstallName("MyMod - Voices_en.ba2")->voiceLanguage == "en");
		ExpectAllowed("COM10.esp", AutoInstallFileKind::plugin, "COM10");
		ExpectAllowed("Sim Settlements 2.esm", AutoInstallFileKind::plugin, "Sim Settlements 2");
		ExpectAllowed("MyMod (v2) [patch].esp", AutoInstallFileKind::plugin, "MyMod (v2) [patch]");
	}

	void TestPathsAndUnsafeNamesAreRefused()
	{
		assert(CheckAutoInstallName("") == AutoInstallRefusal::emptyName);
		assert(CheckAutoInstallName(std::string(kMaximumAutoInstallNameBytes - 4, 'a') + ".esp") == AutoInstallRefusal::none);
		assert(CheckAutoInstallName(std::string(kMaximumAutoInstallNameBytes - 3, 'a') + ".esp") == AutoInstallRefusal::nameTooLong);
		assert(CheckAutoInstallName("Data/MyMod.esp") == AutoInstallRefusal::pathSeparator);
		assert(CheckAutoInstallName("..\\MyMod.esp") == AutoInstallRefusal::pathSeparator);
		assert(CheckAutoInstallName("F4SE\\Plugins\\x.esp") == AutoInstallRefusal::pathSeparator);
		assert(CheckAutoInstallName("MyMod.esp:hidden") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("C:MyMod.esp") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("MYMOD~1.ESP") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("Mod<1>.esp") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("Mod?.esp") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("Mod*.esp") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("Mod|x.esp") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName("Mod\"x.esp") == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName(std::string("Mod\x01.esp")) == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName(std::string("Mod\x7F.esp")) == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName(std::string("Mod\xC3\xA9.esp")) == AutoInstallRefusal::forbiddenCharacter);
		assert(CheckAutoInstallName(" MyMod.esp") == AutoInstallRefusal::leadingOrTrailingDotOrSpace);
		assert(CheckAutoInstallName("MyMod.esp ") == AutoInstallRefusal::leadingOrTrailingDotOrSpace);
		assert(CheckAutoInstallName("MyMod.esp.") == AutoInstallRefusal::leadingOrTrailingDotOrSpace);
		assert(CheckAutoInstallName(".esp") == AutoInstallRefusal::leadingOrTrailingDotOrSpace);
	}

	void TestWindowsDeviceNamesAreRefused()
	{
		for (const auto name : { "CON.esp", "con.esp", "PRN.esm", "AUX.esl", "NUL.esp", "COM1.esp", "com9.esp", "COM0.esp", "LPT1.esp", "lpt9 - Main.ba2",
				 "CONIN$.esp", "CONOUT$.esp", "CLOCK$.esp", "NUL", "AUX .esp", "NUL.x.esp", "COM1 .esp", "con.cdx" }) {
			assert(CheckAutoInstallName(name) == AutoInstallRefusal::reservedDeviceName);
		}
	}

	void TestOtherTypesAreRefused()
	{
		for (const auto name : { "MyMod.dll", "MyMod.ini", "F4SE.exe", "MyMod.pex", "MyMod.ba2", "MyMod - Animations.ba2", "MyMod - Voices_.ba2",
				 "MyMod - Voices_e-n.ba2", "MyMod - Voices_abcdefghijklmnopq.ba2", "MyMod - Voices_e.ba2", "MyMod - Voices_abcdefghi.ba2",
				 "MyMod - Voices_en1.ba2", "MyMod.esp.txt", "MyMod.bsa", "MyMod - Main.bsa" }) {
			assert(CheckAutoInstallName(name) == AutoInstallRefusal::unsupportedType);
			assert(!ClassifyAutoInstallName(name));
		}
	}

	void TestOfficialAndCreationClubContentIsRefused()
	{
		for (const auto name : { "Fallout4.esm", "fallout4.esm", "Fallout4 - Main.ba2", "Fallout4 - Textures.ba2", "Fallout4.cdx",
				 "DLCRobot.esm", "DLCworkshop01.esm", "DLCworkshop02.esm", "DLCworkshop03.esm", "DLCCoast.esm", "DLCNukaWorld.esm",
				 "DLCNukaWorld - Voices_en.ba2", "DLCUltraHighResolution.esm", "dlccoast - geometry.csg", "Fallout4Patch.esp",
				 "DLCSomething.esp", "dlc - Main.ba2" }) {
			assert(CheckAutoInstallName(name) == AutoInstallRefusal::officialContent);
		}
		for (const auto name : { "ccBGSFO4001-PipBoy(Black).esl", "CCBGSFO4001-PipBoy(Black).esl", "ccbgsfo4001-pipboy(black) - main.ba2",
				 "ccOTMFO4001-Remnants.esl", "ccSBJFO4003-Grenade - Textures.ba2", "ccMyMod.esp", "ccABCFO4XYZ-thing.esp", "CC.esp" }) {
			assert(CheckAutoInstallName(name) == AutoInstallRefusal::creationClub);
		}
		for (const auto name : { "CommonwealthOnline.esp", "commonwealthonline - main.ba2", "CommonwealthOnline_Vault109.esp",
				 "CommonwealthOnlinePatch.esl" }) {
			assert(CheckAutoInstallName(name) == AutoInstallRefusal::commonwealthOnlineContent);
		}
	}

	void TestRefusalsAreDescribed()
	{
		assert(DescribeAutoInstallRefusal(AutoInstallRefusal::none) == "allowed");
		assert(DescribeAutoInstallRefusal(AutoInstallRefusal::officialContent) == "official game content is never downloaded");
		assert(DescribeAutoInstallFileKind(AutoInstallFileKind::textureIndex) == "texture index");
	}
}

int main()
{
	TestPluginsAndCompanionsAreAllowed();
	TestPathsAndUnsafeNamesAreRefused();
	TestWindowsDeviceNamesAreRefused();
	TestOtherTypesAreRefused();
	TestOfficialAndCreationClubContentIsRefused();
	TestRefusalsAreDescribed();
	return 0;
}
