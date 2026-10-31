from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"
UI = ROOT / "CommonwealthOnlineUI"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    return (PLUGIN / relative).read_text(encoding="utf-8")


def read_ui(relative):
    return (UI / relative).read_text(encoding="utf-8")


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


friends_header = read("include/F4TFriends.h")
friends = read("src/F4TFriends.cpp")
wheel_header = read("include/F4TInteractWheel.h")
wheel = read("src/F4TInteractWheel.cpp")
runtime = read("src/F4TReplicationRuntime.cpp")
inbound = read("src/F4TNetworkingInbound.cpp")
ui_client = read("src/F4TUIClient.cpp")
wheel_ui = read("src/F4TInteractWheelUi.cpp")
ui_client_header = read("include/F4TUIClient.h")
factions = read("src/F4TFactions.cpp")
networking = read("src/F4TNetworking.cpp")
downed = read("src/F4TDownedEngine.cpp")
voice_runtime = read("src/F4TVoiceRuntime.cpp")
invite_offer = read("src/F4TFactionInviteOffer.cpp")

api_ui = read_ui("include/CommonwealthOnlineUI_API.h")
api_plugin = (PLUGIN / "include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
plugin_api = read_ui("src/PluginAPI.cpp")
wheel_view = read_ui("src/F4TInteractWheelView.cpp")
wheel_view_header = read_ui("include/F4TInteractWheelView.h")
browser = read_ui("src/F4TPrismaUI_Browser.cpp")
cursor = read_ui("src/F4TPrismaUI_Cursor.cpp")
lifecycle = read_ui("src/F4TPrismaUI_Lifecycle.cpp")
menu = read_ui("src/F4TPrismaUI_Menu.cpp")
chat = read_ui("src/F4TChatView.cpp")
faction_view = read_ui("src/F4TFactionView.cpp")
faction_html = read_ui("ui/views/CommonwealthOnline/faction/index.html")
friends_js = read_ui("ui/views/CommonwealthOnline/faction/faction.friends.js")
wheel_js = read_ui("ui/views/CommonwealthOnline/interact-wheel/wheel.js")

check("enum class Relationship" in friends_header, "the client must expose the relationship the wheel needs")
check("BuildStateJson" in friends_header, "friend state must reach the Social window")

for packet_type in ("friendState", "friendRequestReceived", "friendResult"):
    check(f'"{packet_type}"' in friends, f"the client must handle the {packet_type} packet")
check('{ "type", "friendRequest" }' in friends, "Add Friend must send friendRequest")
check('{ "type", "friendRequestResponse" }' in friends, "Accept and Decline must send friendRequestResponse")
check('{ "type", "friendRemove" }' in friends, "Remove must send friendRemove")
check('{ "type", "friendListRequest" }' in friends, "the client must ask for the list on welcome")
check("wants to add you as a friend" in friends, "the request alert must use the required wording")
check("request.accept" not in friends and '"accept"' in friends, "the alert actions must accept and decline")
check("durationMs" in friends, "the request alert must expire with the server lifetime")
check("QueueKnownSetChanged" in friends, "an accepted friendship must refresh nameplates and voice keys")
check("CommandLineToArgv" not in friends, "the friend client must not grow shell parsing")

check("ServerSupports" in wheel, "friend slices must be gated on the friends capability")
check("RelationshipWith" in wheel, "friend slices must follow the current relationship")
check("IncomingRequestId" in wheel, "Accept Friend Request must use the request the server sent")
check('Slice("requestSent", "Friend Request Sent", true)' in wheel, "a pending request must render disabled")
check("Trade" not in wheel and "trade" not in wheel, "no placeholder slices may exist")
check('Slice("invite", "Invite to Faction")' in wheel, "Invite to Faction must be a wheel slice")
check('op == "invite"' in wheel and '"invite"' in wheel, "the invite slice must send the existing invite op")
check("localCanInvite" in wheel and "TargetInFaction" in wheel, "invite must require leader or officer and a player outside the faction")
check('op == "mute" || op == "unmute"' in wheel, "the wheel must offer mute and unmute")
check('"mutePlayer"' in wheel and "VoiceRuntime::SubmitAction" in wheel, "mute must reuse the existing mutePlayer op")
check("IsPlayerMuted" in wheel, "the mute slice must read the current mute state")
check('send("close")' in wheel_js and '"Escape"' in wheel_js, "Esc must close the wheel through the view")
check('button === "b"' in wheel_js and 'send("close")' in wheel_js, "B must close the wheel through the view")
check('"op":"close"' in wheel_view, "a view-side close must tell the plugin the wheel is gone")
check("ViewGone" in wheel, "the wheel must close when the view closes itself")
check("IsInteractWheelVisible" in ui_client_header, "the plugin must be able to read whether the wheel view is still open")
check("TargetLost" in wheel, "the wheel must close when the target moves away, disconnects, or goes down")
check("kActivationRange" in wheel, "the wheel must use the existing activation range")
check("IsDowned" in wheel and "TryGetProxyActorForRemotePlayerId" in wheel, "a downed or missing target must close the wheel")
check("MenuBlocksWheel" in wheel, "the wheel must not open over another menu")
check("TakeCancelPressed" not in downed and "kCancelUserEvent" not in downed, "the Cancel latch must stay gone; the view owns Esc and B")
check("IsChatVisible" in wheel and "IsCharacterSelectVisible" in wheel, "the wheel must not open over chat or character select")
check("IsFactionWindowVisible" in wheel and "IsQuestWindowVisible" in wheel, "the wheel must not open over the Social or Quest window")
check("IsMainMenuVisible" in wheel and "IsBrowserVisible" in wheel, "the wheel must not open over the menu or browser")

check("F4T::FactionInviteOffer::Tick();" in runtime, "the Activate entry point must stay wired")
check("F4T::InteractWheel::Tick();" in runtime, "the wheel must be ticked every frame")
check(runtime.index("F4T::FactionInviteOffer::Tick();") < runtime.index("F4T::InteractWheel::Tick();"),
      "the wheel tick must run after the Activate edge")
check("ShowAlert" not in invite_offer, "Activate must open the wheel instead of the old invite alert")

check("F4T::Friends::NoteWelcome(a_welcome)" in factions, "the welcome must be read for friends-v1")
check("F4T::Friends::TryHandlePacket" in inbound, "friend packets must reach the friend client")
check("F4T::Friends::Reset()" in networking, "a disconnect must clear friend state")
check("F4T::Friends::HandleAction" in factions, "the Social window must route friend actions")
check("IVCommonwealthOnlineUI16" in wheel_ui, "the plugin must request the wheel interface")
check("InteractWheel::BuildStateJson" in wheel_ui, "the wheel view must pull its state through the plugin")
check("OpenInteractWheel" in ui_client_header and "CloseInteractWheel" in ui_client_header, "the plugin must open and close the wheel")
check("F4T::Friends::BuildStateJson" in factions, "the Social window state must carry friends")

check("V16 = 16" in api_ui, "the API must gain V16 without changing V1 to V15")
check("IVCommonwealthOnlineUI16" in api_ui and "IVCommonwealthOnlineUI16" in api_plugin, "both API headers must declare V16")
check(api_ui == api_plugin, "the two API headers must stay identical")
check("InterfaceVersionMap<IVCommonwealthOnlineUI16>" in api_ui, "V16 needs its own layout fingerprint")
check("0xD3B8F411" in api_ui, "the V16 layout must be unique")
check("SetInteractWheelCallbacks" in plugin_api and "OpenInteractWheel" in plugin_api, "the UI plugin must export the wheel interface")
check("case InterfaceVersion::V16:" in plugin_api, "the API dispatcher must serve V16")

check("CommonwealthOnline/interact-wheel/index.html" in wheel_view, "the wheel view must load its page")
check('BindUIEvent(a_view, "coInteractWheelAction"' in wheel_view, "the wheel view must bind its action event")
check("IsActionKnown" in wheel_view, "the wheel view must gate unknown ops")
check("InteractWheelView::TryDispatchControllerInput" in browser, "controller input must reach the wheel")
check("InteractWheelView::TryDispatchControllerAxes" in browser, "stick input must reach the wheel")
check("TryDispatchControllerInput" in wheel_view_header and "TryDispatchControllerAxes" in wheel_view_header, "the wheel view must expose the input take-over")
check("InteractWheelView::IsCurrentlyVisible()" in cursor, "the wheel must hide the native cursor")
check("InteractWheelView::Initialize()" in lifecycle, "the wheel view must initialize with the other views")
check("InteractWheelView::Close" in menu, "the wheel must close with the pause menu and lockout")
check("InteractWheelView::IsVisible()" in chat, "chat must not open over the wheel")
check("64 * 1024" in faction_view, "the Social window buffer must fit 200 friends")

check("faction.friends.js" in faction_html, "the Social window must load the Friends section")
check("friendAccept" in friends_js and "friendRemove" in friends_js, "the Friends section must accept and remove")
check("escape" in friends_js.lower() or "esc(" in friends_js, "the Friends section must escape names")
check("does not support friends" in friends_js, "the Friends section must explain a server without friends-v1")
check("sliceIndexForAxes" in wheel_js and "sliceIndexForAngle" in wheel_js, "slice selection must stay testable in wheel.js")
check("coInteractWheelController" in wheel_js, "the wheel must accept controller input")

gate = function_body(faction_view, "bool IsActionKnown(")
friend_ops = sorted(set(re.findall(r'"(friend[A-Z]\w*)"', friends_js)))
check(friend_ops, "the Friends section must send friend ops")
for op in friend_ops:
    check(f'"{op}"' in gate, f"the Social window action gate must pass {op}")
check("F4T::Friends::HandleAction" in factions, "the Social window must route friend ops to the friend client")

for relative in ("include/F4TFriends.h", "src/F4TFriends.cpp", "include/F4TInteractWheel.h", "src/F4TInteractWheel.cpp", "include/F4TInteractWheelUi.h", "src/F4TInteractWheelUi.cpp"):
    raw = (PLUGIN / relative).read_bytes()
    check(b"\r\n" not in raw or raw.count(b"\n") == raw.count(b"\r\n"), f"{relative} must not mix CRLF and LF line endings")
    check(len(raw.splitlines()) <= 750, f"{relative} must stay at or under 750 lines")

for relative in ("src/ServerFriends.cpp", "src/ServerFriends.h"):
    raw = (ROOT / "CO-SERVER/server-cpp" / relative).read_bytes()
    check(len(raw.splitlines()) <= 750, f"{relative} must stay at or under 750 lines")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TFriendsWheelContract: all checks passed")
