#include "F4TWindowsCaseFold.h"

#include <cassert>
#include <string>

int main()
{
	using F4T::WindowsCaseFold::Key;
	assert(Key("Textures/FILE.DDS") == std::string{ "textures/file.dds" });
	assert(Key("\xc3\x84.DDS") == std::string{ "\xc3\xa4.dds" });
	assert(Key("\xd0\x90.ESP") == std::string{ "\xd0\xb0.esp" });
	assert(Key("\xf0\x90\x90\x80") == std::string{ "\xf0\x90\x90\xa8" });
	assert(Key("\xc4\xb0") == std::string{ "\xc4\xb0" });
	assert(!Key("\xc0\xaf"));
	assert(!Key("\xed\xa0\x80"));
	assert(!Key("\xf4\x90\x80\x80"));
	assert(!Key("\xe2\x82"));
}
