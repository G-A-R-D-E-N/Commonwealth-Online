from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def read_code(relative):
    return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


def function_body(code, signature):
    start = code.index(signature)
    brace = code.index("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


def in_order(text, *needles):
    positions = []
    for needle in needles:
        index = text.find(needle)
        if index < 0:
            return False
        positions.append(index)
    return positions == sorted(positions)


factions = read_code("src/F4TFactions.cpp")
server_faction = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerFactions.cpp").read_text(encoding="utf-8"))
server_invites = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerFactionInvites.cpp").read_text(encoding="utf-8"))
server_maintenance = strip_comments((ROOT / "CO-SERVER/server-cpp/src/ServerMaintenance.inc").read_text(encoding="utf-8"))
ci_workflow = (ROOT / ".github/workflows/co-beta-ci.yml").read_text(encoding="utf-8")
compile_ci = (ROOT / "scripts/windows-compile-ci.py").read_text(encoding="utf-8")

parse_invite = function_body(factions, "std::optional<Invite> ParseInvite(")
check(in_order(parse_invite, 'ReadToken(a_invite, "inviteId"', 'ReadName(a_invite, "factionName"',
               'ReadName(a_invite, "fromName"', "ReadExpiresInSeconds(a_invite)"),
      "ParseInvite must read inviteId, factionName, fromName and expiresInSeconds in that order")
check("!inviteId || !factionName || !fromName || !expiresInSeconds" in parse_invite,
      "ParseInvite must reject an invite that is missing any required field")
check('ReadString(a_invite, "factionTag").value_or("")' in parse_invite and "kMaximumTagBytes" in parse_invite,
      "ParseInvite must read factionTag as optional text capped at kMaximumTagBytes")

state_packet = function_body(factions, "void HandleStatePacket(")
check('a_packet.find("faction")' in state_packet and "!factionJson->is_null()" in state_packet,
      "factionState must treat an absent or null faction as no faction")
check(re.search(r"\n\s*g_faction = std::move\(faction\);", state_packet) is not None,
      "factionState must replace the stored faction, clearing it when the state has none")
check(re.search(r'malformed faction\."\);\s*return;', state_packet) is not None,
      "factionState with a malformed faction must be ignored as a whole")
check(re.search(r'malformed pending invite\."\);\s*return;', state_packet) is not None,
      "factionState with one malformed pending invite must be ignored as a whole")
check('"faction_state_invites"' in state_packet and "invitesJson->size() > kMaximumPendingInvites" in state_packet,
      "factionState must reject a pendingInvites array over kMaximumPendingInvites")
check(in_order(function_body(server_faction, "std::vector<OutboundFrame> FactionService::HandleFactionKick("),
               'FactionResultPacket("kick", false, "kicked"', "FactionStatePacket(a_peers, *target, a_unixTimeSeconds)"),
      "the server must send the kicked player a kick result and then the factionState, which carries no faction")
check(re.search(r"Json faction;\s*if \(auto\* membership = FactionOfProfile\(a_viewer\.profileId\)\)", server_faction) is not None,
      "the server must leave the faction null for a profile that is not in a faction")

result_ops_match = re.search(r"std::array<std::string_view, (\d+)> kResultOps\{([^}]*)\}", factions)
result_ops = re.findall(r'"(\w+)"', result_ops_match.group(2)) if result_ops_match else []
check({"kick", "invite"} <= set(result_ops), "kResultOps must accept the kick and invite factionResult ops")
check('FactionResultPacket("invite", false, "inviteExpired"' in server_invites,
      "the server must tell the inviter inviteExpired through an invite factionResult")
check("protocol.PollFactionInvites(" in server_maintenance,
      "the server maintenance tick must poll faction invites so an idle server still sends inviteExpired")
check("kMaximumReasonBytes = 64" in factions, "factionResult reasons must be capped at 64 bytes")
result = function_body(factions, "void HandleResultPacket(")
check(in_order(result, 'ReadString(a_packet, "op")', 'ReadBool(a_packet, "ok")', 'ReadString(a_packet, "reason")',
               "g_lastResult = LastResult{ *op, *ok, reason };"),
      "factionResult must read op, ok and reason and store them as the last result")
check('Payload::SanitizeText(ReadString(a_packet, "reason").value_or(""), kMaximumReasonBytes)' in result,
      "factionResult reason must be sanitized and capped at kMaximumReasonBytes")

parse_member = function_body(factions, "std::optional<Member> ParseMember(")
check('ReadPlayerId(a_member, "playerId").value_or(0)' in parse_member,
      "an offline member has no playerId and must parse as 0")
check("PlayerIdJson(member.playerId)" in factions, "a member playerId of 0 must be emitted as null")
check(in_order(parse_member, 'F4T::SocialNameplate::ResolveShownName(',
               'ReadName(a_member, "displayName", kMaximumPlayerNameBytes)',
               'ReadName(a_member, "name", kMaximumPlayerNameBytes)'),
      "ParseMember must resolve the shown name displayName-first with name as fallback")
check('if (!profileId || !name)' in parse_member and 'return std::nullopt;' in parse_member,
      "a member with no profileId or no usable name must be dropped")
check('"memberNames"' not in factions and '"officers"' not in factions,
      "the plugin must read per-member name and officer, not the server admin memberNames and officers maps")
check('{ "displayName", view.displayName }' in server_faction
      and 'member["profileId"] = profileId;' in server_faction
      and 'member["name"] = *view.disclosedName;' in server_faction
      and 'member["name"] = view.displayName;' in server_faction
      and '{ "officer", a_faction.officers.contains(profileId) }' in server_faction,
      "the server member row must always carry displayName, add profileId and name only when disclosed, fall back to display text in name for legacy viewers, and keep the officer flag ParseMember reads")
check('if (peer) {' in server_faction and 'member["playerId"] = peer->playerId;' in server_faction,
      "the server must omit playerId for offline members")
parse_faction = function_body(factions, "std::optional<Faction> ParseFaction(")
check("!members->is_array() || members->size() > kMaximumMembers" in parse_faction,
      "ParseFaction must reject a members value that is not an array or is over kMaximumMembers")
check(re.search(r"ParseMember\(memberJson\);\s*if \(!member\) \{\s*return std::nullopt;", parse_faction) is not None,
      "ParseFaction must drop the whole faction when one member is unusable")

roster = function_body(factions, "void HandleRosterPacket(")
check("players->size() > kMaximumRosterEntries" in roster and "Ignoring malformed playerRoster packet." in roster,
      "playerRoster over kMaximumRosterEntries must be ignored as a whole")
check(re.search(r'invalid entry\."\);\s*return;', roster) is not None and "*playerId == 0" in roster,
      "playerRoster with an entry that has no playerId must be ignored as a whole")
check(in_order(roster, "g_roster = std::move(roster);", "QueueStateChanged();"),
      "playerRoster must replace the roster only after every entry has parsed")
build_state = function_body(factions, "std::string BuildStateJson(")
check("entry.playerId != localPlayerId.value_or(0)" in build_state,
      "the roster in the state JSON must leave out the local player")

received = function_body(factions, "void HandleInviteReceivedPacket(")
check("*expiresInSeconds == 0" in received and "Ignoring malformed factionInviteReceived packet." in received,
      "factionInviteReceived with zero seconds must be dropped as malformed")
check('ReadToken(a_packet, "inviteId", kMaximumIdBytes)' in received,
      "factionInviteReceived must validate inviteId as a safe token")

action_source = function_body(factions, "void HandleAction(")
check("a_actionJson.size() > kMaximumActionJsonBytes" in action_source and "kMaximumActionJsonBytes = 4 * 1024" in factions,
      "a faction action over 4 KiB must be dropped before it is parsed; the limit must fit a full description and MOTD with JSON escapes")
check("Json::parse(std::string{ a_actionJson }, nullptr, false)" in action_source and "action.is_object()" in action_source,
      "a faction action must parse without throwing and must be a JSON object")
check("IsBoundedText(name, kMaximumFactionNameBytes)" in action_source and "tag->size() > kMaximumTagBytes" in action_source,
      "create must reject an over-long name or tag instead of truncating it")
check("Payload::IsSafeToken(*text, a_maximumBytes)" in function_body(factions, "std::optional<std::string> ReadToken("),
      "ids and tokens must be validated with Payload::IsSafeToken")

check("F4TFactionGapContract.py" in ci_workflow, "co-beta-ci.yml must run the player faction gap contract")
check("F4TFactionGapContract.py" in compile_ci, "windows-compile-ci.py must run the player faction gap contract")

list_header = (PLUGIN / "include/F4TFactionList.h").read_text(encoding="utf-8")
list_parse = function_body(list_header, "inline std::optional<std::vector<Entry>> ParseFactionList(")
list_entry = function_body(list_header, "inline std::optional<Entry> ParseEntry(")
list_json = function_body(list_header, "inline nlohmann::json ToJson(")
check(re.search(r"ParseEntry\(factionJson\);\s*if \(!entry\) \{\s*return std::nullopt;", list_parse) is not None,
      "ParseFactionList must drop the whole packet when one entry is unusable")
check(re.search(r"if \(bytes \+ cost > kMaximumJsonBytes\) \{\s*break;", list_json) is not None,
      "ToJson must drop entries past the budget from the tail")
check("for (const bool wantYours : { true, false })" in list_json and "keep[index] = true;" in list_json,
      "ToJson must reserve the own entry before filling the rest, then emit in server order")
check("bytes = 2;" in list_json and "(kept == 0 ? 0U : 1U)" in list_json and "dump().size()" in list_json,
      "ToJson must count the exact serialized bytes, brackets and separators included")
list_count = function_body(list_header, "inline std::optional<std::uint32_t> ReadCount(")
check("field->is_number_unsigned()" in list_count and "0xFFFFFFFFULL" in list_count,
      "member and online counts must be unsigned and no larger than 32 bits")
handle_list = function_body(factions, "void HandleFactionListPacket(")
check(re.search(r'malformed factionList packet\."\);\s*return;', handle_list) is not None and '"faction_list_malformed"' in handle_list,
      "a malformed factionList must be logged and ignored, leaving the stored list untouched")
check("QueueStateChanged()" in handle_list, "a stored factionList must notify the UI")
check("g_factionList = std::move(*factions);" in handle_list and handle_list.index("std::scoped_lock lock(g_mutex);") < handle_list.index("g_factionList = std::move(*factions);"),
      "the stored faction list must be written under the lock")
check(re.search(r'\{ "type", "factionList" \}', factions) is not None, "the factions module must send the factionList request")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TFactionGapContract: all checks passed.")
