#include "F4TChatFactionTag.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

namespace
{
	using Json = nlohmann::json;
	using F4T::ChatFactionTag::FromMessage;
	using F4T::ChatFactionTag::Sanitize;
}

int main()
{
	assert(Sanitize("AT") == "AT");
	assert(Sanitize("ABCDEFGH") == "ABCDEFGH");
	assert(Sanitize("ABCDEFGHI").empty());
	assert(Sanitize("  AT  ") == "AT");
	assert(Sanitize("").empty());
	assert(Sanitize("   ").empty());
	assert(Sanitize("A\x01T\x7f") == "AT");
	assert(Sanitize("A\nT") == "AT");
	assert(Sanitize("<b>X</b>") == "<b>X</b>");

	{
		std::string twoByteEight;
		for (int index = 0; index < 8; ++index) {
			twoByteEight += "\xC3\xA9";
		}
		assert(Sanitize(twoByteEight) == twoByteEight);
		assert(Sanitize(twoByteEight + "\xC3\xA9").empty());

		std::string threeByteEight;
		for (int index = 0; index < 8; ++index) {
			threeByteEight += "\xE2\x82\xAC";
		}
		assert(Sanitize(threeByteEight) == threeByteEight);
		assert(Sanitize(threeByteEight + "A").empty());
	}

	assert(FromMessage(Json{ { "factionTag", "AT" } }) == "AT");
	assert(FromMessage(Json{ { "factionTag", "ABCDEFGHI" } }).empty());
	assert(FromMessage(Json{ { "factionTag", 42 } }).empty());
	assert(FromMessage(Json{ { "factionTag", nullptr } }).empty());
	assert(FromMessage(Json{ { "factionTag", Json::object() } }).empty());
	assert(FromMessage(Json{ { "factionTag", Json::array({ "AT" }) } }).empty());
	assert(FromMessage(Json{ { "text", "hi" } }).empty());
	assert(FromMessage(Json{ { "factionTag", "" } }).empty());

	return 0;
}
