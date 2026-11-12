#!/usr/bin/env python3
"""test_character_slots_contracts: consolidated contract sections.

Consolidated from 3 single-contract scripts so the group runs one process
instead of 3. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_character_slots_contracts.py
"""
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_001_test_character_slots_client_contract():
    """Contract moved verbatim from tests/audit/test_character_slots_client_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_character_slots_client_contract.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    def lines(text):
        return text.count("\n") + (0 if text.endswith("\n") else 1)


    ADAPTER_FAMILY = ("co-core/src/protocol/F4TGnsProtocolAdapter.cpp", "co-core/src/protocol/F4TGnsProtocolAdapterInternal.h", "co-core/src/protocol/F4TGnsProtocolAdapterImpl.h", "co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp", "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp")
    adapter = "".join(read(name) for name in ADAPTER_FAMILY)
    adapter_slots =read("co-core/src/protocol/F4TGnsProtocolAdapterSlots.cpp")
    inbound = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
    networking = read("CO-BETA/plugin/src/F4TNetworking.cpp")
    ui_client = read("CO-BETA/plugin/src/F4TUIClient.cpp")
    session_launch = read("CO-BETA/plugin/src/F4TSessionLaunch.cpp")
    client_h = read("CO-BETA/plugin/include/F4TCharacterSlotsClient.h")
    client = read("CO-BETA/plugin/src/F4TCharacterSlotsClient.cpp")
    ui_api = read("CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h")
    plugin_api = read("CO-BETA/plugin/include/CommonwealthOnlineUI_API.h")
    plugin_api_cpp = read("CommonwealthOnlineUI/src/PluginAPI.cpp")
    lifecycle = read("CommonwealthOnlineUI/src/F4TPrismaUI_Lifecycle.cpp")
    internal_h = read("CommonwealthOnlineUI/src/F4TPrismaUIInternal.h")
    view_cpp = read("CommonwealthOnlineUI/src/F4TCharacterSelectView.cpp")
    view_h = read("CommonwealthOnlineUI/include/F4TCharacterSelectView.h")
    panel_js = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/character-select/character-select.js")
    panel_test = read("CommonwealthOnlineUI/ui/views/CommonwealthOnline/character-select/character-select.test.js")
    join_flow = read("CO-BETA/plugin/src/F4TJoinFlow.cpp")
    chat_view = read("CommonwealthOnlineUI/src/F4TChatView.cpp")
    lifecycle_test = read("CO-BETA/plugin/tests/F4TConnectionLifecycleTests.cpp")

    assert "Detail::AddSlotProtocolToHello(hello);" in adapter, "hello must call the slot module"
    assert 'hello["slotProtocol"] = 1;' in adapter_slots, "hello must carry slotProtocol: 1"
    assert "Detail::ForwardCharacterSlotsToWelcome(packet, readyWelcome," in adapter,"sessionReady must call the slot module"
    assert 'a_welcome["characterSlots"] = *slots;' in adapter_slots, "sessionReady characterSlots must ride the merged welcome"

    assert '#include "F4TCharacterSlotsClient.h"' in inbound
    assert "F4T::CharacterSlotsClient::TryHandlePacket(*packetType, packet)" in inbound
    assert inbound.index("CharacterSlotsClient::TryHandlePacket") < inbound.index("HandleUnknownPacket(packet)"), \
        "slot packets must be offered before the unknown-packet path"
    assert "F4T::CharacterSlotsClient::NoteWelcome(a_packet);" in inbound, "the welcome slot block must seed the store"
    assert inbound.index("F4T::CharacterSlotsClient::NoteWelcome") < inbound.index("F4T::RemotePlayerState::SetAssignedPlayerId"), "the slot welcome must seed before the player id is assigned"

    assert "F4T::CharacterSlotsClient::HandleAction(a_actionJson)" in ui_client
    assert "F4T::CharacterSlotsClient::WithCharacterSlots(" in ui_client
    assert "F4T::CharacterSlotsClient::Reset();" in ui_client
    assert "F4T::CharacterSlotsClient::NeedsSelection()" in session_launch, "launch must gate on selection"
    assert "F4T::CharacterSlotsClient::DeferLaunch(" in session_launch, "gated joins must defer the session launch"
    assert session_launch.index("F4T::CharacterSlotsClient::NeedsSelection()") < session_launch.index("F4T::FirstJoinAbandon::BeginForSaveLessHold("), "the first-join discard must wait for character selection, or the server answers notBound and the picker loops"
    launch_body = session_launch.split("LaunchOutcome LaunchValidatedSession(")[1].split("\n\t}\n")[0]
    assert launch_body.index("F4T::CharacterSlotsClient::NeedsSelection()") < launch_body.index("return LaunchOutcome::Pending;") < launch_body.index("ServerProfileStore::HasFirstJoinHold(profileKey)") < launch_body.index("F4T::FirstJoinAbandon::BeginForSaveLessHold("), \
        "LaunchValidatedSession must return Pending at the selection gate before the first-join hold check, so a save-less hold cannot discard a slot the player has not chosen yet"
    assert "F4T::CharacterSlotsClient::CancelDeferredLaunch()" in ui_client, "disconnect must drop the deferred launch"
    assert "IsConnectedToServer()" in session_launch.split("DeferLaunch(")[1][:700], "a deferred resume must bail when the connection is gone"
    assert "g_deferredLaunch" not in client.split("void Reset()")[1].split("bool HandleAction")[0], "Reset must leave a deferred launch for CancelDeferredLaunch to complete"
    assert "characterList" in client.split("void NoteWelcome")[1].split("bool TryHandlePacket")[0], "the welcome must request the slot list when selection is pending"
    assert "Select a character to continue." in session_launch
    assert "Press Play" not in session_launch and "Press Play" not in client, "the bound-false Press Play completion is replaced by the selection reconnect"
    assert "a_bound" not in session_launch, "the launch resume must not carry a bound flag"
    assert "void StartSelectionReconnect()" in client and "void FinishSelectionReconnect(" in client, "the select reconnect must be owned by the slot client"
    assert "StartSelectionReconnect();" in client and "if (!g_deferredLaunch || g_reconnecting) {" in client, "a select reconnects once per selection, never while a reconnect is in flight"
    assert "g_deferredLaunch->sequence == a_sequence" in client, "a finished reconnect must resume only the join that started it"
    assert "request.host.c_str(), request.port, error, nullptr, request.serverPassword, request.id, true, &generation);" in client, "the reconnect must reuse the join's host, port, password and id"
    assert "g_reconnectWorker" in client and ".detach()" not in client, "the reconnect thread must be owned and joined, never detached"
    assert 'block["loading"] = true;' in client and 'block["needsSelection"] = g_supported && !g_legacy && !g_selectionDone && !g_reconnecting;' in client, "the panel must show loading, not the picker, across the reconnect"
    assert "Character selection did not take effect. Select a character again." in client, "an unbound reconnect must fail the join with a clear message"
    assert "std::optional<std::string> serverPassword;" in read("CO-BETA/plugin/include/F4TSessionLaunch.h"), "the join password must reach the deferred record"
    assert "F4T::SessionLaunch::JoinRequest launchRequest{ id, name, host, port, serverPassword };" in ui_client, "the join must hand its password to the launch request"
    assert 'slots->value("loading", false) == true' in view_cpp, "the select view must stay open across the reconnect"
    assert "block.supported !== true && block.loading !== true" in panel_js and "Loading character..." in panel_js, "the reconnect must show Loading character..."
    for op in ('"slotList"', '"slotCreate"', '"slotSelect"', '"slotDelete"', '"slotDeleteCancel"'):
        assert op in client, "the bridge must accept op " + op
    assert '"bound"' in client and '"needsReconnect"' in client, "the welcome bound flag and the select needsReconnect flag must be read"
    for packet in ('"characterList"', '"characterSelect"', '"characterCreate"', '"characterDelete"'):
        assert packet in client, "the bridge must send packet " + packet
    assert '"characterRestore"' not in client, "the player client must not expose a deleted-character restore action"
    assert '"characterListState"' in client and '"characterResult"' in client and '"characterDeleteChallenge"' in client, \
        "the client must handle all three server slot packets"
    assert "NeedsSelection" in client_h and "DeferLaunch" in client_h and "CancelDeferredLaunch" in client_h
    assert "kSendTokensPerSecond = 3.0;" in client and "kSendBurstTokens = 4.0;" in client, "slot packets must stay under the server token bucket (4/s, burst 4) at a 3/s client pace"
    assert "NormalizeName" in client, "create names must be validated against the shared server rule before sending"

    assert 'if (!succeeded && *op == "hello")' in client, "a refused hello must cancel the deferred launch"
    assert "CancelDeferredLaunch(message)" in client and "F4T::UIClient::ClearJoinInProgress();" in client, "the refused hello must clear the join flag through the UI owner"
    assert "void ClearJoinInProgress()" in ui_client, "the join flag clear must be a UI-owned, generation-free function"
    assert "void ClearJoinInProgress();" in read("CO-BETA/plugin/include/F4TUIClient.h"), "the join flag clear must be declared for the launch resume"
    null_branch = ui_client.split("void NotifyTransportDisconnected")[1].split("const std::string reasonCopy")[0]
    assert "CancelDeferredLaunch()" in null_branch and "g_joinInProgress.store(false)" in null_branch, "a null task interface must cancel the deferred launch inline"
    assert "F4T::UIClient::ClearJoinInProgress();" in session_launch.split("DeferLaunch(")[1][:700], "the resume bail must clear the join flag without the generation gate"
    assert '"deleted"' not in client and '"deletedAt"' not in client and '"restorableUntil"' not in client, "the player client must not carry deleted-slot state"
    assert 'bool CancelDeferredLaunch(std::string_view a_reason = "Connection cancelled.");' in client_h, "the cancel must carry its reason"
    assert "MAX_REFRESH_ATTEMPTS = 3" in panel_js and "refreshAttempts < MAX_REFRESH_ATTEMPTS" in panel_js, "the refresh retry must be capped at three sends"
    assert "Undo delete" not in panel_js and "Restore before" not in panel_js, "the select view must not offer deleted-character recovery"

    run_slot = ui_api.split("struct IVCommonwealthOnlineUI22", 1)[1].split("\n\t};", 1)[0] if "struct IVCommonwealthOnlineUI22" in ui_api else ""
    assert "V22" not in ui_api or ("GetViewHandle" in run_slot and "Slot" not in run_slot), "V22 may only be the view lookup, never character slots"
    map_key_slot = ui_api.split("struct IVCommonwealthOnlineUI19", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI19" in ui_api else ""
    assert "V19" not in ui_api or ("HandlePauseMenuMapKey" in map_key_slot and "Slot" not in map_key_slot), "V19 may only be the map key, never character slots"
    trade_slot = ui_api.split("struct IVCommonwealthOnlineUI20", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI20" in ui_api else ""
    assert "V20" not in ui_api or ("SetTradeActionCallback" in trade_slot and "Slot" not in trade_slot), "V20 may only be the trade interface, never character slots"
    report_slot = ui_api.split("struct IVCommonwealthOnlineUI21", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI21" in ui_api else ""
    assert "V21" not in ui_api or ("SetReportCallbacks" in report_slot and "Slot" not in report_slot), "V21 may only be the report interface, never character slots"
    state_bag_slot = ui_api.split("struct IVCommonwealthOnlineUI16", 1)[1].split("};", 1)[0] if "struct IVCommonwealthOnlineUI16" in ui_api else ""
    assert "V16" not in ui_api or ("PushStateBag" in state_bag_slot and "Slot" not in state_bag_slot), "V16 may only be the state bag bridge, never character slots"
    assert ui_api == plugin_api, "the UI API header copies must stay byte-identical"
    assert "CharacterSelect" not in ui_api and "CharacterSlots" not in ui_api, "no new plugin-to-UI API surface for slots"

    assert "F4T::CharacterSelectView::SetCallbacks(a_callbacks);" in plugin_api_cpp
    assert "F4T::CharacterSelectView::NotifyStateChanged();" in plugin_api_cpp
    assert "F4T::CharacterSelectView::Initialize();" in lifecycle
    assert "F4T::CharacterSelectView::Tick();" in lifecycle
    assert '#include "F4TCharacterSelectView.h"' in internal_h
    assert "coCharacterSelectSetState" in view_cpp and "coCharacterSelectAction" in view_cpp
    assert '"slotList"' in view_cpp or "slotList" in view_cpp
    assert "COUI::Session::Disconnect()" in view_cpp, "abandoning a gated join must disconnect"
    for op in ("slotList", "slotCreate", "slotSelect", "slotDelete", "slotDeleteCancel"):
        assert op in view_cpp, "the view must allow-list bridge op " + op

    for reason in ("invalidRequest", "badSlot", "slotEmpty", "slotOccupied", "slotsFull",
                   "nameTaken", "alreadyActive", "slotActive", "badToken", "tokenExpired",
                   "rateLimited", "persistenceUnavailable", "slotsUnavailable"):
        assert reason in panel_js, "reason " + reason + " must map to clear text"
    assert "coCharacterSelectHide" in panel_js and "coCharacterSelectModel" in panel_js
    assert "F4T::CharacterSelectView::IsVisible()" in chat_view.split("bool ScreenWantsChat()")[1].split("bool WindowWanted()")[0], \
        "Enter in a name field must not open chat over the select window"
    assert "maxlength=\"48\"" in panel_test or 'maxlength="48"' in panel_js
    assert ".replace(/&/g" in panel_js, "server names and notices must be HTML-escaped"
    assert "coCharacterSelectModel" in panel_test, "the panel test must drive the model surface"

    assert networking.count("F4T::CharacterSlotsClient::Reset();") == 2, "both local teardown paths must reset slots"
    assert "F4T::CharacterSlotsClient::Reset();" in networking.split("bool ConnectToServerInternal")[1][:3000], \
        "the connect teardown must reset the previous server's slot state"

    assert lines(client) < 1000, "the client slot module must stay under 1000 lines"
    assert lines(view_cpp) < 1000, "the select view module must stay under 1000 lines"
    assert lines(panel_js) < 1000, "the select panel script must stay under 1000 lines"
    assert lines(ui_client) < 1000, "F4TUIClient.cpp must stay under 1000 lines"
    assert lines(inbound) < 1000, "F4TNetworkingInbound.cpp must stay under 1000 lines"

    assert "src/F4TCharacterSlotsClient.cpp" not in read("CO-BETA/plugin/xmake.lua"), \
        "the client glob already covers src/**.cpp; do not list the file explicitly"

    assert "namespace F4T::CharacterSlotsClient { void Reset() {} }" in lifecycle_test, "the lifecycle fixture must stub the slot reset its extracted teardown calls"
    assert "bool IsReconnecting();" in client_h, "the reconnect accessor must be declared for the join flow"
    assert "!F4T::Networking::IsConnectedToServer() && !F4T::CharacterSlotsClient::IsReconnecting()" in join_flow, "an in-progress slot reconnect counts as connected while the world loads"
    assert "if (F4T::CharacterSlotsClient::NeedsSelection() || F4T::CharacterSlotsClient::IsReconnecting()) {" in join_flow and "g_loadingStartSeconds = NowSeconds();\n\t\t\t\t\treturn;" in join_flow.replace("\r\n", "\n"), "the loading clock must restart while the picker or the reconnect is pending"
    assert "reconnectJoin = needsReconnect && g_deferredLaunch.has_value();" in client and "g_selectionDone = !reconnectJoin;" in client, "a select that needs a reconnect with no deferred join must close the picker"
    assert "Character chosen. Reconnect to the server to play as this character." in client and "PushConnectionStatus(" in client, "closing the picker without a join must tell the player to reconnect"
    assert "const bool superseded = F4T::Networking::GetConnectionGeneration() != a_generation;" in client and "if (a_connected && !superseded) {" in client, "an unowned reconnect disconnects only while the connection generation is unchanged"
    reconnect_finish = client[client.index("void FinishSelectionReconnect("):client.index("void StartSelectionReconnect(")]
    assert reconnect_finish.index("if (!a_connected || superseded)") < reconnect_finish.index("AdoptJoinConnectionGeneration(a_generation)") < reconnect_finish.index("if (NeedsSelection())"), "only a validated owned reconnect may transfer its generation before launch can fail"
    assert "true, &generation);" in client and "QueueGameTask([sequence, generation, connected" in client, "the reconnect carries the generation returned by its connect operation"
    assert '#include "F4TSecretWipe.h"' in client and "F4T::Secret::Wipe(*a_password);" in client, "the join password is wiped through the shared helper"
    assert client.count("WipeServerPassword(deferred->request.serverPassword);") == 2, "resume and cancel must wipe the stored join password"
    assert "WipeServerPassword(request.serverPassword);" in client and "sequence]() mutable {" in client, "the reconnect worker must wipe its own password copy after connect"
    assert 'op != "slotList" && op != "slotDeleteCancel" && IsReconnecting()' in client, "mutating slot actions are dropped while a reconnect runs"

    print("test_character_slots_client_contract: all checks passed.")

    store = read("CO-BETA/plugin/src/F4TServerProfileStore.cpp")
    identity = read("CO-BETA/plugin/src/F4TClientIdentity.cpp")
    launch = read("CO-BETA/plugin/src/F4TSessionLaunch.cpp")
    send = client.split("void QueuePacket(")[1].split("void SendSlotPacket(")[0]
    assert send.index("BeginFreshCharacter") < send.index("SendPacketBytesForGeneration"), "fresh binding must be durable before creating the server character"
    assert "g_createAttemptSlots.push_back(*slot)" in send and "std::find(g_createAttemptSlots.begin()" in send, "a duplicate response cannot match another create of this slot on the same connection"
    assert "Reconnect before retrying character creation" in send
    assert "ConfirmFreshCharacter(serverKey, *slot, worldId)" in client
    assert "HasCharacterSlotBindings(a_serverKey)) return std::nullopt" in identity, "pre-selection hello must not attest another slot's legacy save"
    assert launch.index("HasUnconfirmedCharacterBinding(profileKey)") < launch.index("LoadSaveFromMainMenu"), "uncertain creation must not load or onboard"
    assert "binding->contains(\"awaitingCreate\")" in store

    create_result = client.split('if (*op == "create") {', 1)[1].split('StoreResult(', 1)[0]
    assert "DisconnectFromLocalServer" not in create_result, "failed creation must delegate cleanup to the join completion, not block the game task queue"
    assert "if (!CancelDeferredLaunch(reason))" in create_result, "a non-join failure only reports the error and preserves active save work"

def _section_002_test_character_slots_core_contract():
    """Contract moved verbatim from tests/audit/test_character_slots_core_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_character_slots_contracts.py")
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]


    def read(relative):
        return (ROOT / relative).read_text(encoding="utf-8")


    HEADER = "co-core/include/F4TCharacterSlots.h"
    IMPL = "co-core/src/session/F4TCharacterSlots.cpp"
    CONFIG_HEADER = "co-core/include/F4TServerConfiguration.h"
    CONFIG_CPP = "co-core/src/config/F4TServerConfiguration.cpp"
    CORE_XMAKE = "co-core/xmake.lua"

    header = read(HEADER)
    impl = read(IMPL)
    config_header = read(CONFIG_HEADER)
    config_cpp = read(CONFIG_CPP) + read("co-core/src/config/F4TServerConfigModpack.cpp")

    assert "DefaultCharacterSlots = 3" in header, "default slot count must be 3"
    assert "MinimumCharacterSlots = 1" in header, "slot count lower bound must be 1"
    assert "MaximumCharacterSlots = 8" in header, "slot count upper bound must be 8"
    assert "SlotKeySeparator = '#'" in header, "account/slot separator must be '#'"
    assert "AccountKey(std::string_view a_profileId)" in header, "account key must be one isolated function"
    assert "IsValidSlotCount" in header and "IsValidSlot" in header, "slot range checks must be declared"
    assert "SlotProfileKey" in header and "ParseSlotKey" in header, "slot key build/parse must be declared"
    assert "MigrateLegacyProfiles" in header and "MigrationResult" in header, "migration must be declared"
    assert "moved" in header and "alreadySlotted" in header and "conflict" in header, "migration must report moved/slotted/conflict"

    assert "Identity::IsValidProfileId(a_profileId)" in impl, "AccountKey must validate through the identity rule"
    assert "return std::nullopt" in impl, "AccountKey must fail closed on invalid input"
    assert "Identity::IsValidProfileId(a_account)" in impl, "SlotProfileKey must reject an invalid account half"
    assert "std::to_chars" in impl and "std::from_chars" in impl, "slot numbers must format/parse without locale or exceptions"
    assert "HasCharacterData" in impl, "migration must distinguish character data from identity data"
    assert "existing != profiles.end()" in impl, "migration must detect the half-run collision before mutating"
    assert "return result;" in impl, "migration conflict must return with zero mutations"
    assert "RemapDropOwners" in impl and "ownerProfileId" in impl, "migration must carry drop ownership to slot 0"
    assert "slotNode[key] = std::move(*value);" in impl, "migration must move character subkeys, never copy the node"
    assert "source->erase(key);" in impl, "migration must not leave character data on the account node"
    assert 'slotNode["updatedAt"] = a_updatedAt;' in impl, "migrated slot-0 entries must be timestamped"
    assert "std::isfinite(a_updatedAt)" in impl, "migration must require a finite timestamp"
    assert "FirstSlot" in impl, "legacy profiles must land on slot 0"

    assert '#include "F4TCharacterSlots.h"' in config_header, "server config must see the slot constants"
    assert "characterSlots{ F4T::CharacterSlots::DefaultCharacterSlots }" in config_header, "config default must be the slot constant"
    assert '"character_slots", "characterSlots"' in config_cpp, "config must parse both key spellings"
    assert "options.characterSlots = characterSlots < 0 ? 0" in config_cpp, "negative config values must clamp like max_players"
    assert "IsValidSlotCount(a_options.characterSlots)" in config_cpp, "config validation must reuse the slot bound"
    assert '"character_slots must be 1-8"' in config_cpp, "out-of-range slot counts must fail loudly"

    core_xmake = read(CORE_XMAKE)
    assert 'add_files(path.join(co_core_dir, "src/session/F4TCharacterSlots.cpp"))' in core_xmake, "the core build must compile the slot unit"
    assert 'add_headerfiles(path.join(co_core_dir, "include/F4TCharacterSlots.h"))' in core_xmake, "the core build must ship the slot header"

    assert read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc").count("DispatchCharacterSlotPacket") <= 1, "the .inc keeps at most the one slot routing line"
    for networking in ["F4TNetworkingGns.cpp", "F4TNetworkingOutbound.cpp", "F4TNetworkingInventory.cpp"]:
        assert "CharacterSlots" not in read("CO-BETA/plugin/src/" + networking), "no slot edits outside the registered call sites: %s" % networking
    teardown = read("CO-BETA/plugin/src/F4TNetworking.cpp")
    assert teardown.count("F4T::CharacterSlotsClient::Reset();") == 2, "slot reset runs on both local teardown paths and nowhere else"
    assert "CharacterSlotsClient::" not in teardown.replace("F4T::CharacterSlotsClient::Reset();", "").replace('#include "F4TCharacterSlotsClient.h"', ""), \
        "F4TNetworking.cpp keeps only the slot reset registration"
    inbound = read("CO-BETA/plugin/src/F4TNetworkingInbound.cpp")
    assert inbound.count("CharacterSlotsClient::") == 2, "inbound keeps only dispatch and welcome registrations"
    ADAPTER_FAMILY = ("co-core/src/protocol/F4TGnsProtocolAdapter.cpp", "co-core/src/protocol/F4TGnsProtocolAdapterInternal.h", "co-core/src/protocol/F4TGnsProtocolAdapterImpl.h", "co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp", "co-core/src/protocol/F4TGnsProtocolAdapterInbound.cpp")
    adapter = "".join(read(name) for name in ADAPTER_FAMILY)
    assert "duplicate or late welcome packet" in adapter, "the adapter must reject a late welcome; the reconnect select contract depends on it"
    assert "!helloSent || ready ||" in adapter, "the adapter must reject a second sessionReady; the reconnect select contract depends on it"
    assert not list((ROOT / "co-core/src").glob("*Slot*.inc")), "no NEW .inc files"
    assert not list((ROOT / "CO-SERVER/server-cpp/src").glob("*Slot*.inc")), "no NEW .inc files"

    for name in [HEADER, IMPL, CONFIG_HEADER, CONFIG_CPP]:
        assert len(read(name).splitlines()) < 1000, name
    assert len(Path(__file__).read_text(encoding="utf-8").splitlines()) < 1000, "audit test"

    print("test_character_slots_core_contract: all checks passed.")

def _section_003_test_character_slots_server_contract():
    """Contract moved verbatim from tests/audit/test_character_slots_server_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_character_slots_server_contract.py")
    """Pin the character-slot server wiring without compiling C++.

    Covers STEP 2 (server handlers): packet protocol files, the slot service, the
    one-line Impl registration, hello wiring, store/gate/config plumbing, and the
    negative guards (no new .inc files, no F4TNetworking edits, line limits).
    """
    import glob
    import os
    import re

    ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


    def read(relative):
        with open(os.path.join(ROOT, relative), "r", encoding="utf-8-sig", newline="") as handle:
            return handle.read()


    def check(condition, message):
        assert condition, message


    IMPL_H = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.h")
    IMPL_INC = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
    HANDLERS = read("CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp")
    API_INC = read("CO-SERVER/server-cpp/src/ServerProtocolApi.inc")
    PROTO_H = read("CO-SERVER/server-cpp/include/ServerProtocol.h")
    SETUP = read("CO-SERVER/server-cpp/src/ServerPersistenceSetup.inc")
    SESSION_TYPES = read("CO-SERVER/server-cpp/src/ServerSessionTypes.h")
    IDENTITY_GATE = read("CO-SERVER/server-cpp/src/IdentityGate.cpp")
    SERVICE_H = read("CO-SERVER/server-cpp/src/ServerCharacterSlots.h")
    SERVICE_CPP = read("CO-SERVER/server-cpp/src/ServerCharacterSlots.cpp")
    PROTO_CORE_H = read("co-core/include/F4TCharacterSlotProtocol.h")
    PROTO_CORE_CPP = read("co-core/src/session/F4TCharacterSlotProtocol.cpp")
    CONFIG_PARSE = read("co-core/src/config/F4TServerConfiguration.cpp")
    CO_XMAKE = read("co-core/xmake.lua")
    SERVER_XMAKE = read("CO-SERVER/server-cpp/xmake.lua")

    inc_files = glob.glob(os.path.join(ROOT, "CO-SERVER", "server-cpp", "src", "*.inc"))
    check(len(inc_files) > 0, "expected the existing split .inc units")
    for path in inc_files:
        name = os.path.basename(path)
        check(
            name.startswith("Server") or name.startswith("F4T"),
            "unexpected new .inc file: %s" % name,
        )

    check(IMPL_INC.count("DispatchCharacterSlotPacket") == 1, "the .inc must hold exactly one slot routing line")
    check("characterSlots.Dispatch(" in SERVICE_CPP, "service must own the dispatch")

    check("CharacterSlotService characterSlots;" in IMPL_H, "slot member must be declared in ServerProtocolImpl.h")
    check("DispatchCharacterSlotPacket(" in IMPL_H, "dispatch must be declared in ServerProtocolImpl.h")

    check("characterSlots.OnHello(" in HANDLERS, "hello must route through the slot service")
    check("ready[\"characterSlots\"]" in HANDLERS, "sessionReady must carry the slot block")
    check(SERVICE_CPP.count("RebindConnectionProfile(") == 1, "only the reconnect hello may rebind the connection; select must not")
    check("session_superseded" in HANDLERS, "a same-account hello must end the older session with a clear code")
    check("characterSlots.ReleaseConnection(connectionId)" in HANDLERS, "superseded connections must release their persistence binding")
    check("slotHello.proceed" not in HANDLERS and "outcome.refusal" not in SERVICE_CPP, "hello must never refuse; the newer hello supersedes")
    check("if (a_slotAware) {" in SERVICE_CPP, "a slot-aware hello must bind only through the pending selection")
    check("but no character is loaded for this client" in SERVICE_CPP, "the unbound legacy notice must not claim the client is playing")

    check("(*packet)[\"profileId\"] = pending->profileId;" in IDENTITY_GATE, "enrichment must stamp the account profile unconditionally")
    check("if (!(*packet).contains(\"profileId\"))" not in IDENTITY_GATE, "enrichment must not trust a frame-supplied profileId")

    check("EnableCharacterSlots" in PROTO_H, "ProtocolHost must expose EnableCharacterSlots")
    check("EnableCharacterSlots" in API_INC, "Api.inc must forward EnableCharacterSlots")
    check("EnableCharacterSlots(persistence, options.characterSlots)" in SETUP, "server setup must enable slots from Options")

    check("GetCharacterSlotRecords" in read("CO-SERVER/server-cpp/include/DurableStateFileStore.h"), "store must expose slot records")
    check("MigrateCharacterSlotsToSlotZero" in read("CO-SERVER/server-cpp/include/DurableStateFileStore.h"), "store must expose slot migration")
    check("RebindConnectionProfile" in read("CO-SERVER/server-cpp/include/PersistenceGate.h"), "gate must expose rebind")

    check("character_slot_retention_days" not in CONFIG_PARSE, "the retention setting must not return")

    check("characterSlotRequests" in SESSION_TYPES, "Session must carry the slot token bucket")
    check("TakeToken(a_bucket" in SERVICE_CPP, "slot packets must be rate limited")

    check("pendingDeletes.erase(found)" in SERVICE_CPP, "delete token must be consumed before use")
    check("pending.connectionId != a_connectionId" in SERVICE_CPP, "delete token must bind the issuing connection")
    check("tokenExpired" not in SERVICE_CPP and '"badToken"' in SERVICE_CPP, "expired tokens must answer badToken")
    check("entry.second.expiresMonotonic <= a_monotonicSeconds" in SERVICE_CPP, "expired delete tokens must be pruned on every dispatch")
    check("slotActive" in SERVICE_CPP, "the active slot must refuse delete")
    check("ReconcileRecords(" in SERVICE_CPP, "reconcile must run on slot entry points")
    check("orphan-record" in SERVICE_CPP, "crashed creates must reconcile to an empty slot")
    check("persistenceProfile" in SERVICE_CPP and "persistenceProfile" in HANDLERS, "hello must stamp the slot persistence key on sessionReady")

    PERSISTENCE_GATE = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")
    check("ReadyPersistenceProfile" in PERSISTENCE_GATE, "the ready path must persist under the slot key")
    check("(void)persistence->FlushPendingPlayerStates(true);" in SERVICE_CPP, "select must flush pending states before recording the choice")
    check('selected["needsReconnect"] = true;' in SERVICE_CPP, "select must report needsReconnect as literal true; the bind happens on the reconnect hello")
    check("std::map<std::string, PendingSelection, std::less<>> pendingSelections;" in SERVICE_H, "pending selections must be keyed per account")
    check("pendingSelections.erase(pending);" in SERVICE_CPP, "a pending selection must be consumed by one slot-aware hello")
    check("a_monotonicSeconds + kPendingSelectionSeconds" in SERVICE_CPP, "select must expire its pending selection after the fixed window")
    check("std::erase_if(pendingSelections, [&](const auto& entry) { return entry.second.expiresMonotonic <= a_monotonicSeconds; });" in SERVICE_CPP, "expired pending selections must be pruned on every hello")
    check(0 < HANDLERS.find("AppendFrames(frames, HandleDisconnected(connectionId, a_unixTimeSeconds, false, false));") < HANDLERS.find("a_session.active = true;"), "the superseding session must activate only after the supersede loop")
    check(0 < HANDLERS.find("AppendFrames(frames, FactionDisconnectFrames(a_session.profileId, a_unixTimeSeconds));") < HANDLERS.find("a_session.active = true;"), "the superseded profile's faction roster must be sent before the new session activates")
    check('"deletedAt"' in SERVICE_CPP and "EraseSlot(a_account, slot)" in SERVICE_CPP, "legacy soft-deleted records must be erased on reconcile")
    check("bool EraseSlot(std::string_view a_account, std::uint32_t a_slot, bool a_keepConfirmation = false);" in SERVICE_H, "delete must erase through one shared path")
    erase_start = SERVICE_CPP.index("bool CharacterSlotService::EraseSlot(")
    erase_body = SERVICE_CPP[erase_start:SERVICE_CPP.index("CharacterSlotService::ActiveSelection(", erase_start)]
    check("EraseCharacterSlotState(a_account, a_slot, a_keepConfirmation, !keepParent)" in erase_body, "EraseSlot must erase through the store call that queues the quest profile durably")
    STORE_CPP = read("CO-SERVER/server-cpp/src/DurableStateCharacterSlots.cpp")
    store_start = STORE_CPP.index("bool DurableStateFileStore::EraseCharacterSlotRecord(")
    check("QueuePendingQuestErase(a_root" in STORE_CPP[store_start:store_start + 700], "the slot record erase must queue the quest profile in the same durable mutation")
    state_start = STORE_CPP.index("bool DurableStateFileStore::EraseCharacterSlotState(")
    check("QueuePendingQuestErase(a_root" in STORE_CPP[state_start:state_start + 800], "the slot state erase must queue the quest profile in the same durable mutation")
    check("restorableUntil" not in SERVICE_CPP and "slotRestore" not in SERVICE_CPP, "no restore path may remain")
    check("IsLiveSlot(*account, i)" in SERVICE_CPP, "slotsFull must count slots with player data but no record")
    check("store.CreateCharacterSlot(*account, slot, newRecord" in SERVICE_CPP, "create must write the record and first player state in one durable mutation")
    check("HasProfileState(" in SERVICE_CPP, "occupancy must count inventory and grants, not only player state")
    check("ReleaseConnection(std::uint32_t a_connectionId)" in SERVICE_CPP, "the service must release a superseded connection's binding")

    QUEST_ADMIN = read("CO-SERVER/server-cpp/include/ServerQuestAdminAction.h")
    check("ParseSlotKey(*profile)" in QUEST_ADMIN, "quest admin must resolve through the session account")

    check('IsNameChar' in PROTO_CORE_CPP and "!= '#'" in PROTO_CORE_CPP, "slot names must reject '#'")
    check("NormalizeCreate" in PROTO_CORE_H and "NormalizeDelete" in PROTO_CORE_H, "protocol must normalize create/delete")
    check("MakeDeleteChallenge" in PROTO_CORE_H, "protocol must build the delete challenge")

    check(SERVICE_CPP.count("AccountOf(") >= 3, "every slot entry must derive the account server-side")

    check("F4TCharacterSlotProtocol.cpp" in CO_XMAKE, "co-core xmake must build the slot protocol")
    check('include/F4TCharacterSlotProtocol.h' in CO_XMAKE, "co-core xmake must ship the slot protocol header")
    check("ServerCharacterSlots.cpp" in SERVER_XMAKE, "server xmake must build the slot service")
    check("ServerCharacterSlotsTests.cpp" in SERVER_XMAKE, "server xmake must register the slot tests")
    check("ServerCharacterSlotStateTests.cpp" in SERVER_XMAKE, "server xmake must register the slot state tests")

    for relative in [
        "CO-SERVER/server-cpp/src/ServerCharacterSlots.h",
        "CO-SERVER/server-cpp/src/ServerCharacterSlots.cpp",
        "CO-SERVER/server-cpp/tests/ServerCharacterSlotsTests.cpp",
        "CO-SERVER/server-cpp/tests/ServerCharacterSlotStateTests.cpp",
        "co-core/include/F4TCharacterSlotProtocol.h",
        "co-core/src/session/F4TCharacterSlotProtocol.cpp",
        "tests/audit/test_character_slots_contracts.py",
    ]:
        lines = read(relative).count("\n") + 1
        check(lines < 1000, "%s has %d lines, limit is 1000" % (relative, lines))

    print("character slot server contract: all checks passed")

_SECTIONS = [
    ("tests/audit/test_character_slots_client_contract.py", _section_001_test_character_slots_client_contract),
    ("tests/audit/test_character_slots_core_contract.py", _section_002_test_character_slots_core_contract),
    ("tests/audit/test_character_slots_server_contract.py", _section_003_test_character_slots_server_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_character_slots_contracts: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
