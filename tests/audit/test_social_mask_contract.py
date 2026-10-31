from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


handlers = read("CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp")
reconcile = read("CO-SERVER/server-cpp/src/ServerProtocolReconcile.cpp")
chat = read("CO-SERVER/server-cpp/src/ServerChat.cpp")
chat_header = read("CO-SERVER/server-cpp/src/ServerChat.h")
factions = read("CO-SERVER/server-cpp/src/ServerFactions.cpp")
invites = read("CO-SERVER/server-cpp/src/ServerFactionInvites.cpp")
graph = read("CO-SERVER/server-cpp/src/ServerSocialGraph.cpp")
graph_header = read("CO-SERVER/server-cpp/src/ServerSocialGraph.h")
store = read("CO-SERVER/server-cpp/src/DurableStateKnownPeople.cpp")
store_header = read("CO-SERVER/server-cpp/include/DurableStateFileStore.h")
gate = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")
gate_header = read("CO-SERVER/server-cpp/include/PersistenceGate.h")
impl = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
setup = read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
server_xmake = read("CO-SERVER/server-cpp/xmake.lua")
client_chat = read("CO-BETA/plugin/src/F4TChat.cpp")
client_factions = read("CO-BETA/plugin/src/F4TFactions.cpp")

assert "socialGraph.NameViewFor(" in handlers, "live playerState fanout must split display and disclosed names per recipient"
assert 'relay.erase("characterName")' in handlers, "live playerState fanout must omit characterName for strangers"
assert "socialGraph.NameViewFor(" in reconcile, "catch-up playerState replay must split display and disclosed names per viewer"
assert 'packet.erase("characterName")' in reconcile, "catch-up playerState replay must omit characterName for strangers"
assert "SenderNameView(" in chat, "chat send and history must split display and disclosed names per viewer"
assert 'masked.erase("fromName")' in chat, "chat send and history must omit fromName for strangers"
assert '"fromProfileId"' in chat and 'erase("fromProfileId")' in chat, "history must carry the sender profile for masking and strip it on the wire"
assert "const SocialGraph& social" in chat_header, "chat service must hold the social graph"
assert "RosterPacketFor(" in factions, "roster must be built per viewer"
assert "PeerNameView(" in factions and "PeerNameView(" in invites, "roster and invite names must split display and disclosed names per viewer"
assert 'if (view.disclosedName) player["name"]' in factions, "roster must omit name for strangers and keep it for known viewers"
assert '{ "displayName", view.displayName }' in factions, "faction member rows must always carry displayName"
assert 'member["profileId"]' in factions and 'member["name"]' in factions, "faction member rows must gate profileId and name on disclosure"
assert 'received["fromName"]' in invites or 'pending["fromName"]' in invites, "invites must keep fromName only for disclosed viewers"
assert "SameFaction(" in factions, "faction membership must imply known for roster display"
assert '"Stranger"' in graph or "kStrangerName" in graph, "unknown viewers must see the Stranger placeholder"
assert "|| Knows(" in graph, "masks must consult the known-people set"
assert "kMaximumKnownPerProfile" in graph_header, "the known set must be capped"
assert '"knownPeople"' in store, "known people must persist under the knownPeople root key"
assert "GetKnownPeople" in store_header and "SaveKnownPeople" in store_header, "the store must expose known-people accessors"
assert "ReadStoredKnownPeople" in gate_header and "SaveKnownPeople" in gate_header, "the gate must expose known-people accessors"
assert "socialGraph{" in impl, "the protocol host must own the social graph"
assert "EnableSocial" in setup, "server setup must enable the social graph"
assert 'add_files("src/ServerSocialGraph.cpp")' in server_xmake, "the server build must compile the social graph"
assert 'server_test("CommonwealthOnlineServerSocialMaskTests", "tests/ServerSocialMaskTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the server mask tests must be registered"
assert "ResolveShownName(" in client_chat, "client chat must resolve displayName first so stranger messages are not dropped"
assert client_factions.count("ResolveShownName(") >= 4, "client factions must resolve displayName first in invites, members, and roster"
assert '{ "leader", leaderView.displayName }' in factions, "faction list must show the leader as display text, never the raw name"

identity = read("co-core/include/F4TIdentityProtocol.h")
compat = read("co-core/src/session/F4TClientCompatibility.cpp")
welcome = read("CO-SERVER/server-cpp/src/ServerWelcome.cpp")
protocol_impl_h = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.h")
factions_h = read("CO-SERVER/server-cpp/src/ServerFactions.h")

assert 'SocialNamesCapability = "social-names-v1"' in identity, "the social-names capability must be named in one shared constant"
assert "F4T::Identity::SocialNamesCapability" in compat, "client hellos must claim the social-names capability"
assert '"social-names-v1"' in welcome, "the welcome must advertise the social-names capability"
assert "SessionHasSocialNames" in protocol_impl_h, "the protocol host must gate legacy display text on the hello capability"
assert "socialNames" in chat_header and "socialNames" in factions_h, "chat and faction peers must carry the viewer capability"
assert 'masked["fromName"] = view.displayName' in chat, "legacy chat and history must carry display text in fromName"
assert 'relay["characterName"] = view.displayName' in handlers, "legacy live relay must carry display text in characterName"
assert 'packet["characterName"] = view.displayName' in reconcile, "legacy catch-up replay must carry display text in characterName"
assert 'a_packet["characterName"] = view.displayName' in graph, "legacy join refresh must carry display text in characterName"
assert 'member["name"] = view.displayName' in factions, "legacy faction rows must carry display text in name"
assert 'player["name"] = view.displayName' in factions, "legacy roster rows must carry display text in name"
assert 'pending["fromName"] = view.displayName' in invites, "legacy pending invites must carry display text in fromName"
assert 'received["fromName"] = inviteView.displayName' in invites, "legacy invite frames must carry display text in fromName"

friends = read("CO-SERVER/server-cpp/src/ServerFriends.cpp")
client_friends = read("CO-BETA/plugin/src/F4TFriends.cpp")
wheel = read("CO-BETA/plugin/src/F4TInteractWheel.cpp")

assert "social.NameViewFor(" in friends, "a friend request must run the inviter name through the viewer mask"
assert '{ "displayName", a_view.displayName }' in friends, "a friend request must carry display text for the recipient"
assert 'if (view.disclosedName) packet["fromName"]' in friends, "a friend request must keep fromName only for disclosed viewers"
assert "AreFriends(" in graph and "if (AreFriends(" in graph, "a friend name must survive the known-people cap"
assert "return !AreFriends(" in graph, "the known-people eviction must never drop a friend"
assert '{ "friends", std::move(friendStore) }' in graph, "friendships must persist next to knownPeople"
assert "LoadFriends(a_stored)" in graph, "a store without a friends key must still load"
assert "ForgetProfile" in graph, "slot deletion must clean friendships up on both sides"
assert "F4T::Identity::FriendsCapability" in friends, "the friend service must gate on the shared friends capability"
assert 'FriendsCapability = "friends-v1"' in identity, "the friends capability must be named in one shared constant"
assert "F4T::Identity::FriendsCapability" in compat, "client hellos must claim the friends capability"
assert '"friends-v1"' in welcome, "the welcome must advertise the friends capability"
assert "friendService" in impl, "the protocol host must own the friend service"
assert "DispatchFriendPacket(" in impl, "protocol dispatch must route friend packets"
assert "PollFriendRequests" in impl, "the host must poll friend request expiry"
assert "PollExpiry(" in friends and "friendService.PollExpiry(" in friends, "friend requests must expire without a friend packet arriving"
assert "HasFriends(*peer->session)" in friends, "a client without friends-v1 must never receive a friend packet"
assert "KnownListCopy(" in friends and "RestoreKnownList(" in friends, "a failed save must also unlearn the names"
assert 'add_files("src/ServerFriends.cpp")' in server_xmake, "the server build must compile the friend service"
assert 'server_test("CommonwealthOnlineServerFriendTests", "tests/ServerFriendTests.cpp", "CommonwealthOnlineServerProtocol")' in server_xmake, "the friend tests must be registered"
assert '"requestExpired"' in friends and '"declined"' in friends, "requests that end without a friendship must disclose no name"
assert "Friends::ServerSupports()" in wheel, "the wheel must leave the friend slice out when the server has no friends"
assert '"friends-v1"' in client_friends, "the client must read the friends capability from the welcome"
assert '"friendRequestReceived"' in client_friends, "the client must handle incoming friend requests"
