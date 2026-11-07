#include "F4TFactionList.h"
#include "F4TFactionPayload.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

int main()
{
	using namespace F4T::Factions::Payload;
	using Json = nlohmann::json;

	assert(ToWholeSeconds(60.0) == 60U);
	assert(ToWholeSeconds(59.2) == 60U);
	assert(ToWholeSeconds(0.0) == 0U);
	assert(!ToWholeSeconds(-1.0).has_value());
	assert(!ToWholeSeconds(std::numeric_limits<double>::quiet_NaN()).has_value());
	assert(!ToWholeSeconds(std::numeric_limits<double>::infinity()).has_value());
	assert(ToWholeSeconds(5000.0) == kMaximumInviteSeconds);

	const Json whole = Json::parse(R"({"expiresInSeconds":60.0})");
	const Json integral = Json::parse(R"({"expiresInSeconds":60})");
	const Json fractional = Json::parse(R"({"expiresInSeconds":59.2})");
	assert(whole.at("expiresInSeconds").is_number());
	assert(ToWholeSeconds(whole.at("expiresInSeconds").get<double>()) == 60U);
	assert(ToWholeSeconds(integral.at("expiresInSeconds").get<double>()) == 60U);
	assert(ToWholeSeconds(fractional.at("expiresInSeconds").get<double>()) == 60U);

	assert(SanitizeText("a\"b\\c\x01" "d", 64) == "abcd");
	assert(SanitizeText("ab\x7F" "c", 64) == "abc");
	assert(SanitizeText("a\xC3\xA9", 2) == "a");
	assert(SanitizeText("a\xC3\xA9", 3) == "a\xC3\xA9");

	assert(IsSafeToken("abc", 8));
	assert(!IsSafeToken("", 8));
	assert(!IsSafeToken("a\"b", 8));
	assert(!IsSafeToken("abcdefghi", 8));

	assert(ToWholeSeconds(3600.0) == kMaximumInviteSeconds);
	assert(ToWholeSeconds(3600.5) == kMaximumInviteSeconds);
	assert(ToWholeSeconds(1e300) == kMaximumInviteSeconds);
	assert(ToWholeSeconds(0.5) == 1U);
	assert(!ToWholeSeconds(-0.5).has_value());

	assert(SanitizeText("ab\xE2\x82\xAC", 4) == "ab");
	assert(SanitizeText("ab\xE2\x82\xAC", 5) == "ab\xE2\x82\xAC");
	assert(SanitizeText("a\xF0\x9F\x98\x80", 4) == "a");
	assert(SanitizeText("a\xF0\x9F\x98\x80", 5) == "a\xF0\x9F\x98\x80");
	assert(SanitizeText("\x7F\x01", 64).empty());
	assert(SanitizeText("abc", 0).empty());

	assert(IsSafeToken("a b", 8));
	assert(IsSafeToken("abcdefgh", 8));
	assert(!IsSafeToken("a\\b", 8));
	assert(!IsSafeToken("a\x7F", 8));
	assert(!IsSafeToken("a\x1F", 8));

	const Json profile = Json::parse(R"({"description":"  Hi there  ","color":"#ff00aa"})");
	assert(ReadProfileDescription(profile) == "Hi there");
	assert(ReadProfileColor(profile) == "#FF00AA");
	assert(ReadProfileColor(Json::parse(R"({"color":""})")) == "");
	assert(!ReadProfileColor(Json::parse(R"({})")).has_value());
	assert(!ReadProfileColor(Json::parse(R"({"color":"#FFF"})")).has_value());
	assert(!ReadProfileColor(Json::parse(R"({"color":"#GG0000"})")).has_value());
	assert(!ReadProfileDescription(Json::parse(R"({})")).has_value());
	assert(!ReadProfileDescription(Json::parse("{\"description\":\"a\\\"b\"}")).has_value());
	assert(!ReadProfileDescription(Json::parse("{\"description\":\"a\\\\b\"}")).has_value());
	assert(!ReadProfileDescription(Json::parse("{\"description\":\"\\u00e9\"}")).has_value());
	assert(ReadProfileDescription(Json{ { "description", std::string(200, 'a') } }).has_value());
	assert(!ReadProfileDescription(Json{ { "description", std::string(201, 'a') } }).has_value());
	const auto motd = ReadProfileMotd(Json::parse(R"({"motd":"  Stay sharp  "})"));
	assert(motd && *motd == "Stay sharp");
	const auto absent = ReadProfileMotd(Json::parse(R"({})"));
	assert(absent && !absent->has_value());
	const auto blank = ReadProfileMotd(Json::parse(R"({"motd":"   "})"));
	assert(blank && blank->has_value() && blank->value().empty());
	assert(!ReadProfileMotd(Json::parse(R"({"motd":5})")).has_value());
	assert(!ReadProfileMotd(Json::parse("{\"motd\":\"a\\\"b\"}")).has_value());
	assert(!ReadProfileMotd(Json::parse("{\"motd\":\"a\\\\b\"}")).has_value());
	assert(!ReadProfileMotd(Json::parse("{\"motd\":\"\\u00e9\"}")).has_value());
	assert(ReadProfileMotd(Json{ { "motd", std::string(1000, 'a') } }).value().has_value());
	assert(!ReadProfileMotd(Json{ { "motd", std::string(1001, 'a') } }).has_value());

	namespace List = F4T::Factions::List;
	const Json listed = Json::parse(R"({"type":"factionList","factions":[{"id":"f-1","name":"Vault","tag":"VLT","color":"#FF0000","description":"Hi","leader":"Ann","memberCount":4,"onlineCount":2,"yours":true},{"id":"f-2","name":"Raiders","memberCount":1,"onlineCount":0,"yours":false}]})");
	const auto entries = List::ParseFactionList(listed);
	assert(entries.has_value() && entries->size() == 2);
	assert((*entries)[0].id == "f-1" && (*entries)[0].name == "Vault" && (*entries)[0].tag == "VLT");
	assert((*entries)[0].color == "#FF0000" && (*entries)[0].description == "Hi" && (*entries)[0].leader == "Ann");
	assert((*entries)[0].memberCount == 4 && (*entries)[0].onlineCount == 2 && (*entries)[0].yours);
	assert((*entries)[1].tag.empty() && (*entries)[1].leader.empty() && !(*entries)[1].yours);
	assert(!List::ParseFactionList(Json::parse(R"({"type":"factionList"})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":{}})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"name":"X","memberCount":1,"onlineCount":0,"yours":false}]})")).has_value());
	assert(!List::ParseFactionList(Json::parse("{\"factions\":[{\"id\":\"f\\\"1\",\"name\":\"X\",\"memberCount\":1,\"onlineCount\":0,\"yours\":false}]}")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"id":"f-1","name":"","memberCount":1,"onlineCount":0,"yours":false}]})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"id":"f-1","name":"X","memberCount":-1,"onlineCount":0,"yours":false}]})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"id":"f-1","name":"X","memberCount":1.5,"onlineCount":0,"yours":false}]})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"id":"f-1","name":"X","memberCount":4294967296,"onlineCount":0,"yours":false}]})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"id":"f-1","name":"X","memberCount":1,"onlineCount":0,"yours":"no"}]})")).has_value());
	assert(!List::ParseFactionList(Json::parse(R"({"factions":[{"id":"f-1","name":"X","memberCount":1,"onlineCount":0,"yours":false},{"name":"Y"}]})")).has_value());

	Json crowded = Json::array();
	for (int index = 0; index < 65; ++index) {
		crowded.push_back(Json{ { "id", "f-" + std::to_string(index) }, { "name", "F" }, { "memberCount", 1U }, { "onlineCount", 0U }, { "yours", false } });
	}
	assert(!List::ParseFactionList(Json{ { "factions", crowded } }).has_value());
	crowded.erase(crowded.begin());
	assert(List::ParseFactionList(Json{ { "factions", crowded } })->size() == List::kMaximumEntries);

	const List::Entry small{ "f-1", "Vault", "VLT", "#FF0000", "Hi", "Ann", 4U, 2U, true };
	assert(List::ToJson({ small }) == Json::array({ Json::parse(R"({"id":"f-1","name":"Vault","tag":"VLT","color":"#FF0000","description":"Hi","leader":"Ann","memberCount":4,"onlineCount":2,"yours":true})") }));
	assert(List::ToJson({ small, small }).size() == 2);
	assert(List::ToJson({}).empty());

	const List::Entry widest{ "f-1", std::string(64, 'n'), "TAGTAGTA", "#AABBCC", std::string(200, 'd'), std::string(128, 'l'), 4294967295U, 4294967295U, true };
	const auto capped = List::ToJson(std::vector<List::Entry>(64, widest));
	assert(capped.is_array() && !capped.empty() && capped.size() < List::kMaximumEntries);
	assert(capped.dump().size() <= List::kMaximumJsonBytes);

	const List::Entry wideOther{ "f-2", std::string(64, 'n'), "TAGTAGTA", "#AABBCC", std::string(200, 'd'), std::string(128, 'l'), 4294967295U, 4294967295U, false };
	std::vector<List::Entry> ownInside(64, wideOther);
	ownInside[60] = widest;
	const auto kept = List::ToJson(ownInside);
	assert(kept.dump().size() <= List::kMaximumJsonBytes && kept.size() < List::kMaximumEntries);
	assert(kept.back().at("yours") == true && kept.back().at("id").get<std::string>() == "f-1");

	return 0;
}
