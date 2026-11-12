#include "F4TStateBagProtocol.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::StateBagProtocol;

	assert(kCapability == "state-bag-v1");
	assert(kPacketType == "stateBag");

	assert(IsValidKey("a"));
	assert(IsValidKey("quest.stage_1-x"));
	assert(IsValidKey(std::string(kMaximumKeyChars, 'k')));
	assert(!IsValidKey(""));
	assert(!IsValidKey(std::string(kMaximumKeyChars + 1, 'k')));
	assert(!IsValidKey("has space"));
	assert(!IsValidKey("tab\tkey"));
	assert(!IsValidKey("del\x7F"));
	assert(!IsValidKey("\x80key"));

	assert(FormatBag(BagId{}) == "global");
	assert(ParseBag("global") == BagId{});
	const auto player = ParseBag("player:7");
	assert(player.has_value());
	assert(player->kind == BagKind::Player);
	assert(player->playerId == 7);
	assert(*player == PlayerBag(7));
	assert(FormatBag(*player) == "player:7");
	assert(ParseBag("player:4294967295") == PlayerBag(4294967295u));
	assert(!ParseBag("player:4294967296"));
	assert(!ParseBag("player:07"));
	assert(!ParseBag("player:"));
	assert(!ParseBag("player:-1"));
	assert(!ParseBag("player:x"));
	assert(!ParseBag("Global"));
	assert(!ParseBag(""));

	assert(EncodedSize(Json("ab")) == 4);
	assert(EncodedSize(Json{ { "a", 1 } }) == 7);
	assert(EncodedSize(Json(nullptr)) == 4);
	assert(IsValidValue(Json("ab")));
	assert(IsValidValue(Json{ { "a", Json::array({ 1, "x", nullptr }) } }));
	const Json loneSurrogate = std::string("\xED\xA0\x80", 3);
	assert(!IsValidValue(loneSurrogate));
	assert(!IsValidValue(Json{ { "text", loneSurrogate } }));
	assert(EncodedSize(loneSurrogate) >= 5);
	assert(EncodedSize(Json{ { "text", loneSurrogate } }) > 5);

	assert(kResourceViewPacketType == "resourceView");
	assert(kResourceMessagePacketType == "resourceMessage");
	assert(IsValidResourceName("a"));
	assert(IsValidResourceName("quest-hub_2"));
	assert(IsValidResourceName(std::string(kMaximumResourceNameChars, 'n')));
	assert(!IsValidResourceName(""));
	assert(!IsValidResourceName(std::string(kMaximumResourceNameChars + 1, 'n')));
	assert(!IsValidResourceName("Upper"));
	assert(!IsValidResourceName("has.dot"));
	assert(!IsValidResourceName("sp ace"));
	assert(!IsValidResourceName("a/b"));

	assert(IsValidViewPathSegment("menu.html"));
	assert(IsValidViewPathSegment("a-b_c.d"));
	assert(IsValidViewPathSegment(std::string(kMaximumViewPathChars, 'v')));
	assert(!IsValidViewPathSegment(""));
	assert(!IsValidViewPathSegment("UPPER"));
	assert(!IsValidViewPathSegment(".hidden"));
	assert(!IsValidViewPathSegment("trailing."));
	assert(!IsValidViewPathSegment("with space"));
	assert(!IsValidViewPathSegment("with/slash"));
	assert(!IsValidViewPathSegment("con"));
	assert(!IsValidViewPathSegment("nul.txt"));
	assert(!IsValidViewPathSegment("com1"));
	assert(!IsValidViewPathSegment("lpt9"));
	assert(IsValidViewPathSegment("console"));
	assert(IsValidViewPathSegment("com10"));

	assert(IsValidViewPath("menu.html"));
	assert(IsValidViewPath("web/menu.html"));
	assert(IsValidViewPath("a-b_c/d.e/index.html"));
	assert(IsValidViewPath(std::string(kMaximumViewPathChars - 5, 'v') + ".html"));
	assert(!IsValidViewPath(""));
	assert(!IsValidViewPath("/menu.html"));
	assert(!IsValidViewPath("../menu.html"));
	assert(!IsValidViewPath("web/../menu.html"));
	assert(!IsValidViewPath("web/./menu.html"));
	assert(!IsValidViewPath("web//menu.html"));
	assert(!IsValidViewPath("menu.htm"));
	assert(!IsValidViewPath("menu.js"));
	assert(!IsValidViewPath("sp ace.html"));
	assert(!IsValidViewPath("a\\b.html"));
	assert(!IsValidViewPath("a-b_c/d.e/Index.html"));
	assert(!IsValidViewPath("web/.menu.html"));
	assert(!IsValidViewPath("web/menu.html."));
	assert(!IsValidViewPath("con.html"));
	assert(!IsValidViewPath("web/con.html"));
	assert(!IsValidViewPath(std::string(kMaximumViewPathChars - 4, 'v') + ".html"));

	assert(IsValidResourceData(Json{ { "k", 1 } }));
	assert(IsValidResourceData(Json(std::string(kMaximumResourceDataBytes - 2, 'x'))));
	assert(!IsValidResourceData(Json(std::string(kMaximumResourceDataBytes - 1, 'x'))));

	return 0;
}
