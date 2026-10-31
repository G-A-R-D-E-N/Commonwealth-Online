#include "F4TFactionValidation.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <string>

int main()
{
	using Json = nlohmann::json;
	using namespace F4T::FactionValidation;

	const auto create = NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "  Iron Wolves  " }, { "tag", "iw" } });
	assert(create.has_value());
	assert(create->at("type") == "factionCreate");
	assert(create->at("name") == "  Iron Wolves  ");
	assert(create->at("tag") == "iw");
	assert(NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "abc" } })->contains("name"));
	assert(!NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "abc" } })->contains("tag"));
	assert(NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "ab" } }).has_value());
	assert(NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "" } }).has_value());
	assert(NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "abc" }, { "tag", "" } }).has_value());
	assert(!NormalizeFactionCreate(Json{ { "type", "factionCreate" } }));
	assert(!NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", 42 } }));
	assert(!NormalizeFactionCreate(Json{ { "type", "factionCreate" }, { "name", "Valid Name" }, { "tag", 7 } }));
	assert(!NormalizeFactionCreate(Json::array()));

	const auto name = [](const char* a_value) { return NormalizeFactionName(a_value); };
	assert(name("  Iron Wolves  ") == std::string{ "Iron Wolves" });
	assert(name("Dawn-Rider's 2") == std::string{ "Dawn-Rider's 2" });
	assert(name("abc") == std::string{ "abc" });
	assert(name("ABCD") == std::string{ "ABCD" });
	assert(name(std::string(24, 'x').c_str()) == std::string(24, 'x'));
	assert(!name("ab"));
	assert(!name("   ab   "));
	assert(!name(""));
	assert(!name("   "));
	assert(!name(std::string(25, 'x').c_str()));
	assert(!name("Two  Spaces"));
	assert(!name("Bad_Name"));
	assert(!name("Bad.Name"));
	assert(!name("Tab\tName"));
	assert(!name("Caf\xc3\xa9 Name"));

	const auto tag = [](const char* a_value) { return NormalizeFactionTag(a_value); };
	assert(tag("IW") == std::string{ "IW" });
	assert(tag("iw") == std::string{ "IW" });
	assert(tag("  a1b2 ") == std::string{ "A1B2" });
	assert(tag("  ab  ") == std::string{ "AB" });
	assert(tag("") == std::string{});
	assert(tag("   ") == std::string{});
	assert(!tag("A"));
	assert(!tag(" a "));
	assert(!tag("ABCDE"));
	assert(!tag("A-B"));
	assert(!tag("A B"));

	assert(IsFactionTextAllowed("Iron Wolves"));
	assert(IsFactionTextAllowed("Scunthorpe Grape Pickers"));
	assert(IsFactionTextAllowed(""));
	assert(!IsFactionTextAllowed("F u c k"));
	assert(!IsFactionTextAllowed("SH1T Crew"));
	assert(!IsFactionTextAllowed("the-n4zi's"));
	assert(!IsFactionTextAllowed("HITLER"));
	assert(!IsFactionTextAllowed("$hit happens"));
	assert(!IsFactionTextAllowed("n@zi pride"));
	assert(!IsFactionTextAllowed("wh0re"));
	assert(!IsFactionTextAllowed("hi+ler fans"));
	assert(!IsFactionTextAllowed("sh!t"));
	assert(!IsFactionTextAllowed("$h!+"));
	assert(!IsFactionTextAllowed("Smash! Team"));
	assert(!IsFactionTextAllowed("sh|t"));
	assert(!IsFactionTextAllowed("fu(k"));
	assert(!IsFactionTextAllowed("ni66er"));
	assert(!IsFactionTextAllowed("fa99ot"));
	assert(IsFactionTextAllowed("Area 69 (best) | Raiders"));
	assert(IsFactionTextAllowed("Wow! Cash $5 @ noon"));

	assert(NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", 7 } })->at("targetPlayerId") == 7);
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", 7.0 } }));
	assert(NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", 4294967295ULL } })->at("targetPlayerId") == 4294967295ULL);
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", 0 } }));
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" } }));
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", -3 } }));
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", 4294967296ULL } }));
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", "7" } }));
	assert(!NormalizeFactionInvite(Json{ { "type", "factionInvite" }, { "targetPlayerId", true } }));

	const auto response = NormalizeFactionInviteResponse(Json{ { "type", "factionInviteResponse" }, { "inviteId", "i-1" }, { "accept", true } });
	assert(response.has_value() && response->at("inviteId") == "i-1" && response->at("accept") == true);
	assert(NormalizeFactionInviteResponse(Json{ { "inviteId", std::string(64, 'i') }, { "accept", false } }).has_value());
	assert(!NormalizeFactionInviteResponse(Json{ { "inviteId", std::string(65, 'i') }, { "accept", false } }));
	assert(!NormalizeFactionInviteResponse(Json{ { "inviteId", "" }, { "accept", false } }));
	assert(!NormalizeFactionInviteResponse(Json{ { "inviteId", "i-1" } }));
	assert(!NormalizeFactionInviteResponse(Json{ { "inviteId", "i-1" }, { "accept", "yes" } }));
	assert(!NormalizeFactionInviteResponse(Json{ { "accept", true } }));

	assert(NormalizeFactionLeave(Json{ { "type", "factionLeave" } })->at("type") == "factionLeave");
	assert(!NormalizeFactionLeave(Json::array()));

	const auto kick = NormalizeFactionKick(Json{ { "type", "factionKick" }, { "targetProfileId", "profile-2" } });
	assert(kick.has_value() && kick->at("targetProfileId") == "profile-2");
	assert(NormalizeFactionKick(Json{ { "targetProfileId", std::string(64, 'p') } }).has_value());
	assert(NormalizeFactionKick(Json{ { "targetProfileId", std::string(128, 'p') } }).has_value());
	assert(!NormalizeFactionKick(Json{ { "targetProfileId", std::string(129, 'p') } }));
	assert(!NormalizeFactionKick(Json{ { "targetProfileId", "" } }));
	assert(!NormalizeFactionKick(Json{ { "type", "factionKick" } }));
	assert(!NormalizeFactionKick(Json{ { "targetProfileId", 5 } }));

	assert(NormalizeFactionDisband(Json{ { "type", "factionDisband" } })->at("type") == "factionDisband");
	assert(!NormalizeFactionDisband(Json::array()));

	const auto promote = NormalizeFactionPromote(Json{ { "type", "factionPromote" }, { "targetProfileId", "profile-2" } });
	assert(promote.has_value() && promote->at("type") == "factionPromote" && promote->at("targetProfileId") == "profile-2");
	assert(NormalizeFactionPromote(Json{ { "targetProfileId", std::string(64, 'p') } }).has_value());
	assert(NormalizeFactionPromote(Json{ { "targetProfileId", std::string(128, 'p') } }).has_value());
	assert(!NormalizeFactionPromote(Json{ { "targetProfileId", std::string(129, 'p') } }));
	assert(!NormalizeFactionPromote(Json{ { "targetProfileId", "" } }));
	assert(!NormalizeFactionPromote(Json{ { "type", "factionPromote" } }));
	assert(!NormalizeFactionPromote(Json{ { "targetProfileId", 5 } }));
	assert(!NormalizeFactionPromote(Json::array()));

	const auto setRank = NormalizeFactionSetRank(Json{ { "type", "factionSetRank" }, { "targetProfileId", "profile-2" }, { "rank", "officer" } });
	assert(setRank.has_value() && setRank->at("type") == "factionSetRank" && setRank->at("targetProfileId") == "profile-2" && setRank->at("rank") == "officer");
	assert(NormalizeFactionSetRank(Json{ { "targetProfileId", "profile-2" }, { "rank", "member" } }).has_value());
	assert(!NormalizeFactionSetRank(Json{ { "targetProfileId", "profile-2" }, { "rank", "leader" } }));
	assert(!NormalizeFactionSetRank(Json{ { "targetProfileId", "profile-2" }, { "rank", "Officer" } }));
	assert(!NormalizeFactionSetRank(Json{ { "targetProfileId", "profile-2" } }));
	assert(!NormalizeFactionSetRank(Json{ { "targetProfileId", "" }, { "rank", "officer" } }));
	assert(NormalizeFactionSetRank(Json{ { "targetProfileId", std::string(128, 'p') }, { "rank", "officer" } }).has_value());
	assert(!NormalizeFactionSetRank(Json{ { "targetProfileId", std::string(129, 'p') }, { "rank", "officer" } }));
	assert(!NormalizeFactionSetRank(Json{ { "targetProfileId", "profile-2" }, { "rank", 1 } }));
	assert(!NormalizeFactionSetRank(Json::array()));

	const auto setProfile = NormalizeFactionSetProfile(Json{ { "type", "factionSetProfile" }, { "description", "  Hold the line  " }, { "color", "#abcdef" } });
	assert(setProfile.has_value() && setProfile->at("type") == "factionSetProfile" && setProfile->at("description") == "Hold the line" && setProfile->at("color") == "#ABCDEF");
	assert(NormalizeFactionSetProfile(Json{ { "description", std::string(200, 'a') }, { "color", "" } }).has_value());
	assert(NormalizeFactionSetProfile(Json{ { "description", "   " + std::string(200, 'a') + "   " }, { "color", "" } }).has_value());
	assert(!NormalizeFactionSetProfile(Json{ { "description", std::string(201, 'a') }, { "color", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "a\tb" }, { "color", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", std::string("a\x01") }, { "color", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "caf\xC3\xA9" }, { "color", "" } }));
	assert(!NormalizeFactionDescription("Say \"hi\""));
	assert(!NormalizeFactionDescription("back\\slash"));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "Say \"hi\"" }, { "color", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "back\\slash" }, { "color", "" } }));
	assert(NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" } }).has_value());
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "#12345" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "123456" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "#GGGGGG" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "color", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", 5 }, { "color", "" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", 5 } }));
	assert(!NormalizeFactionSetProfile(Json::array()));
	assert(NormalizeFactionMotd("  Raid at dusk  ").value_or("") == "Raid at dusk");
	assert(NormalizeFactionMotd("").has_value());
	assert(NormalizeFactionMotd(std::string(1000, 'a')).has_value());
	assert(!NormalizeFactionMotd(std::string(1001, 'a')));
	assert(!NormalizeFactionMotd("Say \"hi\""));
	assert(!NormalizeFactionMotd("back\\slash"));
	assert(!NormalizeFactionMotd("a\tb"));
	assert(!NormalizeFactionMotd("caf\xC3\xA9"));
	assert(NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" } })->count("motd") == 0);
	assert(NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" }, { "motd", "  Raid at dusk  " } })->at("motd") == "Raid at dusk");
	assert(NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" }, { "motd", "" } })->at("motd") == "");
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" }, { "motd", std::string(1001, 'a') } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" }, { "motd", "Say \"hi\"" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" }, { "motd", "a\tb" } }));
	assert(!NormalizeFactionSetProfile(Json{ { "description", "" }, { "color", "" }, { "motd", 5 } }));

	assert(NormalizePlayerRosterRequest(Json{ { "type", "playerRosterRequest" } })->at("type") == "playerRosterRequest");
	assert(!NormalizePlayerRosterRequest(Json::array()));

	assert(NormalizeFactionListRequest(Json{ { "type", "factionList" } })->at("type") == "factionList");
	assert(!NormalizeFactionListRequest(Json::array()));

	return 0;
}
