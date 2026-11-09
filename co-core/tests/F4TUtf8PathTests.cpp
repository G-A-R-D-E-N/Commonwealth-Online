#include "F4TUtf8Path.h"

#include <cassert>
#include <filesystem>
#include <string>

int main()
{
	using F4T::Utf8Path::Decode;
	using F4T::Utf8Path::Encode;

	{
		const std::string ascii = "Data/Scripts/Quest.pex";
		assert(Encode(Decode(ascii)) == ascii);
		assert(Decode(ascii).filename() == std::filesystem::path{ "Quest.pex" });
	}

	{
		const std::string utf8 = "caf\xC3\xA9/\xE6\x97\xA5\xE6\x9C\xAC.esp";
		const auto path = Decode(utf8);
		assert(Encode(path) == utf8);
		assert(path.filename().u8string() == Decode("\xE6\x97\xA5\xE6\x9C\xAC.esp").u8string());
		assert(path.parent_path().filename().u8string() == Decode("caf\xC3\xA9").u8string());
	}

	{
		assert(Encode(Decode("")).empty());
		assert(Decode("").empty());
	}

	{
		const std::filesystem::path original{ "Textures/Armor.dds" };
		assert(Decode(Encode(original)) == original);
	}

	return 0;
}
