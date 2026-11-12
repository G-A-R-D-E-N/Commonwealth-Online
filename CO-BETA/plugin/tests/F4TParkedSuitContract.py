from pathlib import Path
import re
# TODO-verify: concept-level parked suit wiring checks, engine facts unverified; source:CodeNotes-GARDEN power-armor-sync.


ROOT = Path(__file__).resolve().parents[3]
PLUGIN_DIR = ROOT / "CO-BETA/plugin"
SERVER_DIR = ROOT / "CO-SERVER/server-cpp/src"

publish_cpp = (PLUGIN_DIR / "src/F4TReplicationRuntimePublish.cpp").read_text(encoding="utf-8")
sync_cpp = (PLUGIN_DIR / "src/F4TParkedSuitSync.cpp").read_text(encoding="utf-8")
sync_h = (PLUGIN_DIR / "include/F4TParkedSuitSync.h").read_text(encoding="utf-8")
menu_cpp = (PLUGIN_DIR / "src/F4TParkedSuitMenu.cpp").read_text(encoding="utf-8")
internal_h = (PLUGIN_DIR / "include/F4TParkedSuitInternal.h").read_text(encoding="utf-8")
inbound_sync_cpp = (PLUGIN_DIR / "src/F4TParkedSuitSyncInbound.cpp").read_text(encoding="utf-8")
sync_internal_h = (PLUGIN_DIR / "include/F4TParkedSuitSyncInternal.h").read_text(encoding="utf-8")
queue_cpp = (PLUGIN_DIR / "src/F4TParkedSuitSyncQueue.cpp").read_text(encoding="utf-8")
sync_all = sync_cpp + queue_cpp + inbound_sync_cpp + sync_internal_h + menu_cpp + internal_h
outbound_cpp = (PLUGIN_DIR / "src/F4TNetworkingOutbound.cpp").read_text(encoding="utf-8")
networking_h = (PLUGIN_DIR / "include/F4TNetworking.h").read_text(encoding="utf-8")
identity_cpp = (PLUGIN_DIR / "src/F4TClientIdentity.cpp").read_text(encoding="utf-8")
power_armor_cpp = (PLUGIN_DIR / "src/F4TPowerArmorSync.cpp").read_text(encoding="utf-8")
script_cpp = (PLUGIN_DIR / "src/F4TPowerArmorScript.cpp").read_text(encoding="utf-8")
script_h = (PLUGIN_DIR / "include/F4TPowerArmorScript.h").read_text(encoding="utf-8")
inbound_cpp = (PLUGIN_DIR / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8")
networking_cpp = (PLUGIN_DIR / "src/F4TNetworking.cpp").read_text(encoding="utf-8")
lifecycle_cpp = (PLUGIN_DIR / "tests/F4TConnectionLifecycleTests.cpp").read_text(encoding="utf-8")

assert "F4T::ParkedSuitSync::PublishLocal(*player, capturedPowerArmorState);" in publish_cpp, "the owner must publish parks and removes from the local poll"
assert "SendParkedSuitPacket(" in sync_all, "the owner module must send through the networking helper"
assert "bool SendParkedSuitPacket(const nlohmann::json& a_packet);" in networking_h, "the send helper must be declared"
assert "bool SendParkedSuitPacket(const nlohmann::json& a_packet)" in outbound_cpp, "the send helper must be defined"
assert 'emplace_back("parked-suit-v1")' in identity_cpp, "the plugin must advertise parked-suit-v1 in hello"
assert "SwitchToPowerArmor" in script_cpp, "the enter must dispatch the Papyrus SwitchToPowerArmor native"
assert "kEnterPowerArmor" not in power_armor_cpp, "the raw enter engine id must be gone"
remote_state_cpp = (PLUGIN_DIR / "src/F4TRemotePlayerState.cpp").read_text(encoding="utf-8")
assert "if (a_state.powerArmorInFrame) {" in remote_state_cpp, "the posture guard must key off the merged suit state"
assert "a_state.isSneaking = false;" in remote_state_cpp, "the posture guard must clear sneak in a suit"
update_body = remote_state_cpp[remote_state_cpp.index("void UpdateRemotePlayer("):remote_state_cpp.index("bool HasRemotePlayer(")]
assert update_body.index("if (a_state.powerArmorInFrame) {") < update_body.index("g_remotePlayers[a_state.playerId] = std::move(a_state);"), "the posture guard must run before the state is stored, so every consumer reads the suppressed stance"
assert "result.isSneaking = a_state.isSneaking;" in (PLUGIN_DIR / "include/F4TRemotePlayerState.h").read_text(encoding="utf-8"), "the animation snapshot must carry the suppressed stance"

wire_path = ROOT / "co-core/src/world/F4TParkedSuit.cpp"
assert wire_path.exists(), "the parked suit wire module must exist"
assert "F4TParkedSuit.cpp" in (ROOT / "co-core/xmake.lua").read_text(encoding="utf-8"), "co-core must build the parked suit module"
assert "CoreParkedSuitTests" in (ROOT / "co-core/xmake-tests.lua").read_text(encoding="utf-8"), "the parked suit test must be registered"

parked_suits_h_path = SERVER_DIR / "ServerParkedSuits.h"
parked_suits_cpp_path = SERVER_DIR / "ServerParkedSuits.cpp"
assert parked_suits_h_path.exists() and parked_suits_cpp_path.exists(), "the server parked suit handler must exist as ServerParkedSuits.h and ServerParkedSuits.cpp"
assert not (SERVER_DIR / "ServerParkedSuits.inc").exists(), "ServerParkedSuits.inc must stay deleted; the handler lives in ServerParkedSuits.h and ServerParkedSuits.cpp"
parked_suits_h = parked_suits_h_path.read_text(encoding="utf-8")
inc = parked_suits_cpp_path.read_text(encoding="utf-8")
assert "std::string_view a_reason = {});" in parked_suits_h and "a_reason = {}" not in inc, "the ack reason default belongs in the declaration only"
assert "a_previous = std::nullopt, double a_previousMarginUnits = 0.0) const;" in parked_suits_h and "a_previous = std::nullopt" not in inc, "the scope refresh defaults belong in the declaration only"
assert "F4T::ParkedSuit::NormalizeRemoveRequest(a_packet)" in inc and "F4T::ParkedSuit::NormalizeParkRequest(a_packet)" in inc, "the handler must validate both actions"
assert "parkedSuits.Remove(ownerKey, *localId)" in inc, "only the owner's playerKey may remove a suit"
assert "const auto removedSuitId = parkedSuits.Remove(ownerKey, *localId);" in inc, "a remove must name the owner's local ref and return the server suit id"
assert inc.count("F4T::ParkedSuit::BuildAckPacket(a_packet, true)") == 4 and inc.count("F4T::ParkedSuit::BuildAckPacket(a_packet, false, a_reason)") == 1, "every park and remove gets an ack, ok only on success"
assert "if (parkedSuits.FindByLocal(ownerKey, *localId)) return ParkedSuitWriteRejected(a_connectionId, a_packet);" in inc, "a remove with no stored record must ack ok, and one the sender still holds must ack not ok"
assert "outcome == F4T::ParkedSuit::ParkOutcome::NotOwner) {" in inc, "a park onto a suit taken from its original parker must ack not ok"
assert "if (write) return ParkedSuitWriteRejected(a_connectionId, a_packet);" in inc, "a park or remove that fails the rate limit gets ok:false"
assert 'if (a_recipientKey == a_record.localOwnerKey) packet["localId"] = a_record.localId;' in inc and inc.count('packet["localId"]') == 1, "localId must be sent only to the player who parked the suit"
assert "ObjectStateVisible(InterestScopeFromMovement(recipient.lastMovement), park->position)" in inc, "a park must only be relayed in interest scope"
assert "kMaximumObjectReach" in inc and "kMaximumCombatPositionAgeSeconds" in inc, "a park needs a fresh transform within reach"
object_state = (SERVER_DIR / "ServerObjectState.inc").read_text(encoding="utf-8")
assert 'if (a_type == "parkedSuit") return HandleParkedSuitRequest(' in object_state, "parkedSuit must be dispatched"
assert "AppendFrames(frames, ParkedSuitFramesFor(a_connectionId, a_target));" in object_state, "a joining holder must get the stored suits"
assert "ParkPacketFor(const std::string& a_recipientKey, const F4T::ParkedSuit::Record& a_record)" in inc, "every park packet must be built per recipient"
assert 'packet["mine"] = a_recipientKey == a_record.ownerKey;' in inc, "mine must be true only for the recipient's own playerKey"
assert inc.count("F4T::ParkedSuit::BuildParkPacket(") == 1, "park packets must not be shared between recipients"
assert inc.count("ParkPacketFor(") == 6,"the join snapshot, the reassign relay and the park relay must all carry mine"
assert "AppendFrames(frames, ParkedSuitFramesFor(a_connectionId, a_session, previous));" in object_state, "a scope refresh must re-send the stored suits"
assert "ParkedSuitFramesFor(a_connectionId, a_session, scanPoint, kObjectScanStepUnits)" in object_state, "a movement step must send suits that came into range"
assert "ObjectStateCoveredByPrevious(*a_previous, record.position, a_previousMarginUnits)" in inc, "a refresh must skip suits the previous scope already covered"
assert "std::deque<PendingTransition> g_pending;" in sync_all, "failed transitions must be retained"
assert "!F4T::Networking::SendParkedSuitPacket(front.packet)" in sync_all, "a retry must stop at the first unsent packet so park and remove keep their order"
assert "\t\tFlushPending();" in sync_all, "every publish tick must flush the queue"
assert '%:include "ServerParkedSuits.h"' in (SERVER_DIR / "ServerOwnedInventory.inc").read_text(encoding="utf-8"), "the handler must be included in the session scope"
assert "F4T::ParkedSuit::kCapabilityName" in (SERVER_DIR / "ServerWorldPolicy.inc").read_text(encoding="utf-8"), "the server must advertise parked-suit-v1"
assert "parkedSuitRequests" in (SERVER_DIR / "ServerSessionTypes.h").read_text(encoding="utf-8"), "each session needs its own parked suit rate bucket"

wire = wire_path.read_text(encoding="utf-8")
assert "std::optional<HackRequest> NormalizeHackRequest" in wire, "the wire must validate a hack request"
assert "Json BuildHackResultPacket(std::string_view a_suitId, bool a_hacked)" in wire, "the wire must build a hack result"
assert "optional<HackRequest> NormalizeHackRequest" in (ROOT / "co-core/include/F4TParkedSuit.h").read_text(encoding="utf-8"), "the wire header must declare the hack request"
assert "HackOutcome Table::Reassign" in wire, "the table must hand a suit to its new owner"
assert "if (const auto hack = F4T::ParkedSuit::NormalizeHackRequest(a_packet)) {" in inc, "the handler must accept a hack"
assert "parkedSuits.Reassign(hack->suitId, ownerKey)" in inc, "a break-in must transfer ownership to the sender"
assert "F4T::ParkedSuit::BuildHackResultPacket(hack->suitId, true)" in inc, "the sender must get a hack result"
assert "ObjectStateVisible(InterestScopeFromMovement(a_session.lastMovement), a_position)" in inc, "a hack needs the suit in scope"
assert "std::isfinite(distance) && distance <= kMaximumObjectReach" in inc and inc.count("ParkedSuitInReach(a_session, stored->position, a_monotonicSeconds)") == 2, "a breach start and a hack need the sender within reach of the suit"
assert '"invalidParkRequest"' in inc and '"outOfReach"' in inc and '"parkRefused"' in inc, "a rejected park must name its reason in the ack"
assert "if (stored && stored->ownerKey == ownerKey) {" in inc and "BuildHackResultPacket(hack->suitId, false)" in inc, "a hack on your own suit answers hacked false, not a rejection"
assert "if (!ParkedSuitChallengeLive(challenge, hack->suitId, a_monotonicSeconds)) {" in inc,"a hack needs a live challenge for that suit"
assert "F4T::ParkedSuit::Breach::Generate(challenge->seed, challenge->length)" in inc and "F4T::ParkedSuit::Breach::Solves(grid, challenge->length, hack->picks)" in inc, "a hack must replay its picks against the grid regenerated from the challenge"
assert inc.count("a_session.parkedSuitChallenge.reset();") == 2, "a failed breach and a hack consume the challenge"
assert "ParkedSuitBreachLength(stored->ownerKey, stored->position)" in inc, "the server sets the lock length, never the client"
assert "BuildBreachChallengePacket(*breachSuitId, seed, length)" in inc, "a breach start answers with the server challenge"
assert "void HandleUiAction(std::string_view a_actionJson);" in sync_h, "the menu action entry point must be declared"
assert "RequestHack" not in sync_h and "SendBreachFailed" not in sync_h, "break-in and breach reports now leave through the menu action"
assert '{ "action", "hack" }' in sync_all, "the break-in request must be a parkedSuit hack"
assert "bool TryHandlePacket(std::string_view a_packetType, const nlohmann::json& a_packet)" in sync_all, "the plugin must consume relayed suits"
assert 'if (name == "hackResult") {' in sync_all, "the plugin must record a hack result"
assert re.search(r'name == "hackResult"\) \{\s*StoreHackResult\(\*suitId, a_packet\);\s*return true;', inbound_sync_cpp), "a hack result must return before the alert check"
assert "F4T::ParkedSuitSync::TryHandlePacket(*packetType, packet)" in inbound_cpp, "the inbound loop must dispatch parkedSuit"
api_h = (ROOT / "CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
assert "struct IVCommonwealthOnlineUI30" in api_h, "the UI must expose a parked suit interface"
assert "ParkedSuitCallbacks" in api_h, "the parked suit callbacks must be declared"
assert "InterfaceVersionMap<IVCommonwealthOnlineUI30>" in api_h, "the parked suit interface needs a layout guard"
ui_api_cpp = (ROOT / "CommonwealthOnlineUI/src/PluginAPI.cpp").read_text(encoding="utf-8")
assert "Api_SetParkedSuitCallbacks" in ui_api_cpp and "g_interfaceV30" in ui_api_cpp, "the UI must service the parked suit interface"
assert "case InterfaceVersion::V30:" in ui_api_cpp, "V30 must be routable"
view_cpp = (ROOT / "CommonwealthOnlineUI/src/F4TParkedSuitView.cpp").read_text(encoding="utf-8")
assert "coParkedSuitSetState" in view_cpp, "the view must push state into the page"
assert 'BindUIEvent(a_view, "coParkedSuitAction", OnAction)' in view_cpp, "the view must receive break-in actions"
assert 'op != "breachStart" && op != "hack" && op != "breachFailed"' in view_cpp, "the view must forward breach start, break-in and failed breach"
assert "GetStateJson(" in view_cpp, "the view must pull suits from the plugin"
assert "ParkedSuitView::Initialize();" in (ROOT / "CommonwealthOnlineUI/src/F4TPrismaUI_Lifecycle.cpp").read_text(encoding="utf-8"), "the view must be initialised"
assert (ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/parked-suit/index.html").exists(), "the menu page must exist"
page_js = (ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/parked-suit/parked-suit.js").read_text(encoding="utf-8")
assert "coParkedSuitAction" in page_js and 'send("hack", pending.suitId, picks)' in page_js, "the menu must send a break-in with its picks for the selected suit"
assert 'send("breachStart", suitId)' in page_js and '"ESTABLISHING LINK"' in page_js, "the menu must request a server challenge and show the link status"
assert "LINK_MS = 6000" in page_js, "an unanswered breach start must time out after 6 seconds"
assert "(Math.imul(seed, 1103515245) + 12345) >>> 0" in page_js and "seed * 1103515245" not in page_js and "reseed" not in page_js, "the grid must use the exact 32-bit LCG seeded by the server"
breach_cpp = (ROOT / "co-core/src/world/F4TParkedSuitBreach.cpp").read_text(encoding="utf-8")
breach_h = (ROOT / "co-core/include/F4TParkedSuitBreach.h").read_text(encoding="utf-8")
js_codes = re.findall(r'"([0-9A-F]{2})"', re.search(r"var CODES = \[([^\]]*)\];", page_js).group(1))
cpp_codes = re.findall(r'"([0-9A-F]{2})"', re.search(r"kCodes\{([^}]*)\}", breach_cpp).group(1))
js_grid = re.search(r"var GRID = (\d+);", page_js).group(1)
js_mul, js_add = re.search(r"Math\.imul\(seed, (\d+)\) \+ (\d+)", page_js).groups()
assert js_codes and js_codes == cpp_codes, "the breach code list must match the page's CODES list in order"
assert f"{js_mul}U" in breach_cpp and f"{js_add}U" in breach_cpp, "the breach LCG multiplier and increment must match the page"
assert re.search(rf"kGridSize = {js_grid};", breach_h) and "kGridSize" in breach_cpp, "the breach grid size must match the page's GRID"
ui_cpp = (ROOT / "CO-BETA/plugin/src/F4TParkedSuitUi.cpp").read_text(encoding="utf-8")
assert "BuildUiStateJson" in ui_cpp and "HandleUiAction" in ui_cpp, "the plugin must serve and receive menu traffic"
assert "F4T::ParkedSuitSync::HandleUiAction(a_actionJson)" in ui_cpp, "a menu action must reach the parked suit module"
assert "const auto picks = NormalizePicks(parsed);" in sync_all and '{ "picks", *picks }' in sync_all, "a break-in from the menu must carry validated picks"
assert "struct ParkedSuitCallbacks" in (ROOT / "CommonwealthOnlineUI/include/CommonwealthOnlineUI_API.h").read_text(encoding="utf-8")
assert "Json BuildHackAlertPacket(std::string_view a_suitId, bool a_failed)" in wire, "the wire must build an owner alert"
assert "Json BuildBreachFailedPacket(std::string_view a_suitId)" in wire, "the wire must build a failed breach report"
assert "std::optional<std::string> NormalizeHackAlert(const Json& a_packet)" in wire, "the wire must parse an owner alert"
assert "const auto previousOwnerKey = stored->ownerKey;" in inc, "the handler must remember who owned the suit"
assert "const auto alert = F4T::ParkedSuit::BuildHackAlertPacket(hack->suitId, false);" in inc, "a break-in must alert the previous owner"
assert "if (PlayerKeyOf(ownerSession) != previousOwnerKey) continue;" in inc, "the alert must go only to the previous owner"
assert 'if (name == "hackAlert") {' in sync_all, "the plugin must consume the owner alert"
assert "F4T::ParkedSuitUi::NotifyHackAttempt(*suitId, reported)" in sync_all, "the owner alert must carry the attempt outcome"
assert "NormalizeBreachFailedRequest(a_packet)" in inc, "the server must accept a failed breach report"
assert "BuildHackAlertPacket(*failedHack, true)" in inc, "a failed breach must alert the owner as a failure"
assert "failedRecord->ownerKey == ownerKey" in inc, "a failed breach on your own suit must stay silent"
assert "ParkedSuitInReach(a_session, failedRecord->position, a_monotonicSeconds)" in inc, "a failed report must be reach checked"
assert "if (!challenge || challenge->suitId != *failedHack) {" in inc, "a failed breach report needs a challenge for that suit"
assert '{ "action", "breachFailed" }' in sync_all, "the plugin must report a failed breach"
assert 'send("breachFailed", failedSuit)' in page_js, "the menu must report a failed breach"
ui_cpp_text = (PLUGIN_DIR / "src/F4TParkedSuitUi.cpp").read_text(encoding="utf-8")
assert "void NotifyHackAttempt(std::uint32_t a_refFormId, bool a_failed)" in ui_cpp_text, "the plugin must show a break-in notification"
assert "tried to break into your parked power armor and failed" in ui_cpp_text, "a failed attempt must read as a failure"
assert "ShowAlert" in ui_cpp_text and "request.tone = \"warning\";" in ui_cpp_text, "the notification must be a warning alert"
assert "F4T::RemotePlayerState::GetRemotePlayerSnapshot()" in sync_all, "the menu must resolve owner keys to names"
assert "F4T::SocialNameplate::ResolveDisplayName(remote.displayName, remote.characterName)" in sync_all, "owner names must come from the remote player registry"
assert "Owner unknown" in sync_all, "an unresolvable owner must not show a key"
assert "const auto position = a_packet.find(\"position\");" in sync_all, "the suit position must be kept"
assert '{ "location", LocationText(placed) }' in sync_all, "the menu state must carry the suit location"
assert "X {:.0f}  Y {:.0f}  Z {:.0f}" in sync_all, "the position must remain the location fallback"
assert "RE::TESForm::GetFormByID<RE::TESObjectCELL>" in sync_all, "the place name must come from the parked cell"
assert "RE::TESForm::GetFormByID<RE::TESWorldSpace>" in sync_all, "an exterior place name must fall back to the worldspace"
assert "RE::TESFullName::GetFullName(*cell)" in sync_all, "the cell name must be read from TESFullName"
assert "cell->GetLocation()" in sync_all, "an unnamed cell must fall back to its BGSLocation"
assert "PlaceName(a_placed.cellId, a_placed.worldspaceId)" in sync_all, "the place name must be resolved from the stored cell and worldspace ids"
assert "if (auto place = PlaceName(a_placed.cellId, a_placed.worldspaceId); !place.empty())" in sync_all, "a known place must be shown instead of raw coordinates"
assert "std::size_t LockLength(std::string_view a_ownerKey, const Placed& a_placed)" in sync_all, "lock strength must scale with the owner"
assert "return dx * dx + dy * dy + dz * dz <= kAttendedDistanceSquared ? 4 : 3;" in sync_all, "an attended suit must be the hardest lock"
assert '{ "targetLength", LockLength(placed.ownerKey, placed) }' in sync_all, "the menu state must carry the lock length as a display hint"
assert "breach = createBreach(linkSuit, linked.challenge.seed, linked.challenge.length);" in page_js, "the puzzle must use the server challenge seed and length"
assert "Math.max(MIN_TARGET, Math.min(MAX_TARGET, length))" in page_js, "the lock length must be clamped"
assert "return breach ? breach.length + 1 : MAX_TARGET + 1;" in page_js, "the buffer must scale with the lock"
assert "OrderedByDistance()" in sync_all, "the menu must order suits by distance"
assert "for (const auto& [refFormId, placed] : OrderedByDistance())" in sync_all or "for (const auto& [suitId, placed] : OrderedByDistance())" in sync_all, "the state must be built from the ordered list"
assert "RE::PlayerCharacter::GetSingleton()" in sync_all, "the ordering origin must be the local player"
assert "player->GetPosition()" in sync_all, "the ordering origin must be the player position"
assert "return a_left.first < a_right.first;" in sync_all, "equal distances must order deterministically"
assert "#include <algorithm>" in sync_all, "the ordering must use std::sort"
assert '#include "F4TPowerArmorScript.h"' in sync_all, "the sync module must reach the frame lock"
assert "bool SetFrameLocked(RE::TESObjectREFR& a_frame, bool a_locked);" in script_h, "the frame lock must be declared"
assert "bool SetFrameOwner(RE::TESObjectREFR& a_frame, RE::TESNPC* a_ownerBase);" in script_h, "the frame owner must be declared"
assert "kReferenceVmType{ 64 }" in script_cpp, "a frame must be dispatched as an object reference"
assert '"ObjectReference", "BlockActivation", a_locked, false' in script_cpp, "the frame lock must dispatch the vanilla BlockActivation native"
assert '"ObjectReference", "SetActorOwner", a_ownerBase, false' in script_cpp, "the frame owner must dispatch the vanilla SetActorOwner native"
assert "F4T::PowerArmorScript::SetFrameLocked(*frame, a_locked)" in sync_all, "a suit that is not ours must be locked"
assert "F4T::PowerArmorScript::SetFrameOwner(*frame, a_ownerBase)" in sync_all, "a broken-in suit must carry the breaker's ownership"
assert "ReconcileFrames(a_player.GetNPC());" in sync_all, "frame locks must be re-applied on every publish tick"
assert "bool mine{};" in sync_all and 'const auto mineField = a_packet.find("mine");' in sync_all, "the park packet's mine flag must be stored per suit"
assert "const auto mine = placed.mine;" in sync_all and "if (!placed.mine) {" in sync_all, "ownership must come from the server's mine flag"
assert "g_localKey" not in sync_all and "g_breakPending" not in sync_all and "IsMine(" not in sync_all, "the local key learning must be gone"
assert "owner.second.mine = false;" not in sync_all, "a publish tick must not clear the mine flags; a disconnect drops the suits instead"
assert re.search(r"g_hackResults\.erase\(\*suitId\);\s*\}\s*F4T::ParkedSuitUi::NotifyStateChanged\(\);", inbound_sync_cpp), "a new break-in must clear the old outcome and refresh the menu"
assert '{ "mine", mine }' in sync_all and '{ "locked", !mine }' in sync_all, "the menu state must carry ownership and lock"
assert 'mine ? std::string{ "You" } : OwnerName(placed.ownerKey)' in sync_all, "an owned suit must read as yours"
assert 'suit["outcome"] = found->second ? "broken" : "owned";' in sync_all, "the menu state must carry the hack outcome"
assert 'suit["challenge"] = nlohmann::json{ { "suitId", Hex(suitId) }, { "seed", challenge->seed }, { "length", challenge->length } };' in sync_all, "the server challenge must reach the menu on its own suit"
assert 'if (name == "breachChallenge") {' in sync_all, "the plugin must store the server challenge"
assert sync_all.count("F4T::ParkedSuitUi::NotifyStateChanged();") >= 4, "the menu must refresh on park, remove, challenge, hack result and a new break-in"
assert '{ "localId", Hex(a_occupied.frame.refFormId) }' in sync_all and '{ "suitId", Hex(a_occupied' not in sync_all, "a park must name the owner's local ref, never the server suit id"
assert "owner->second.mine && owner->second.localId == a_localId" in sync_all, "a remove must erase only the owner's suits with the matching local ref"


def function_body(code, signature):
    start = code.index(signature)
    indent = code[code.rindex("\n", 0, start) + 1:start]
    return code[start:code.index("\n" + indent + "}\n", start) + len(indent) + 3]


assert "#include <mutex>" in sync_all and "std::mutex g_mutex;" in sync_all, "the shared parked suit maps must sit behind one mutex"
assert "// Game thread only" not in sync_all, "the receive thread shares the maps, so the game thread only comment must go"
assert "Constraint: g_owners, g_hackResults, g_challenge and g_adopting are shared with the receive thread under g_mutex." in sync_all, "the shared state constraint must be stated"
assert "bool SendPark(const Occupied& a_occupied, bool a_consoleSpawned)" in sync_all, "a park must report whether the queue took it and whether a console spawn queued it"
assert '#include "F4TDebugLogging.h"' in sync_all, "the sync module must log a lost unlock task"
assert "void OnDisconnected();" in sync_h, "the disconnect hook must be declared"
assert "namespace F4T::ParkedSuitSync { void OnDisconnected() {} }" in lifecycle_cpp, "the connection lifecycle test must stub the disconnect hook it extracts"
assert '#include "F4TParkedSuitSync.h"' in networking_cpp, "the networking module must reach the disconnect hook"
disconnect_networking = function_body(networking_cpp, "void DisconnectFromLocalServer()")
assert "F4T::ParkedSuitSync::OnDisconnected();" in disconnect_networking, "a disconnect must drop parked suit state"
assert disconnect_networking.index("StopReceiveThread();") < disconnect_networking.index("F4T::ParkedSuitSync::OnDisconnected();"), "the parked suit reset must run after the receive thread stops"
connect_networking = function_body(networking_cpp, "bool ConnectToServerInternal(")
assert "F4T::ParkedSuitSync::OnDisconnected();" in connect_networking, "a new connection must not inherit the last session's parked suits"
disconnect = function_body(sync_cpp, "void OnDisconnected()")
assert "F4SE::GetTaskInterface()" in disconnect and "taskInterface->AddTask(" in disconnect, "the unlock must be queued as a game task"
assert re.search(r"g_challenge\.reset\(\);\s*g_adopting\.clear\(\);\s*g_resync\.store\(true\);\s*\}\s*const auto\* taskInterface = F4SE::GetTaskInterface\(\);", disconnect), "the unlock list must be collected under the lock and queued after it is released"
assert "g_owners.clear();" in disconnect and "g_hackResults.clear();" in disconnect and "g_appliedLocked.clear();" in disconnect, "a disconnect must drop every parked suit map"
assert re.search(r"if \(g_owners\.contains\(suitId\)\) \{\s*continue;\s*\}\s*\}\s*if \(auto\* frame = RE::TESForm::GetFormByID<RE::TESObjectREFR>\(localId\)\) \{\s*(?:\(void\))?F4T::PowerArmorScript::SetFrameLocked\(\*frame, false\);", disconnect), "the queued unlock must skip a suit that is owned again and must unlock by local ref outside the lock"
assert "applied.locked" in disconnect, "only suits that are currently locked need an unlock"
apply_body = function_body(sync_cpp, "bool ApplyLock(")
assert "std::optional<std::uint32_t> a_expectedBase" in apply_body, "an owner apply must carry the expected frame base"
assert "const auto* base = frame->GetObjectReference();" in apply_body and "base->GetFormID() != *a_expectedBase" in apply_body, "the frame base must match the parked base before anything is dispatched"
assert "RE::TESForm::GetFormByID<RE::TESObjectREFR>(a_localId)" in apply_body and "GetFormByID<RE::TESObjectREFR>(a_suitId)" not in apply_body, "the frame must be found through its local ref only"
assert apply_body.index("base->GetFormID() != *a_expectedBase") < apply_body.index("F4T::PowerArmorScript::SetFrameLocked(*frame, a_locked)"), "the base check must run before the lock dispatch"
assert apply_body.index("F4T::PowerArmorScript::SetFrameOwner(*frame, a_ownerBase)") < apply_body.index("g_appliedLocked[a_suitId] = AppliedLock{ a_localId, a_locked };"), "a suit is marked applied only after every dispatch succeeded"
reconcile_body = function_body(sync_cpp, "void ReconcileFrames(")
assert "} else if (a_playerBase) {" in reconcile_body, "a mine suit must not be marked applied-unlocked without a player base"
assert "ApplyLock(suitId, localId, false, nullptr, std::nullopt)" in reconcile_body, "stale locks must still be released"
assert "ApplyLock(suitId, *placed.localId, false, a_playerBase, placed.frameBaseFormId)" in reconcile_body and "ApplyLock(suitId, *placed.localId, true, nullptr, placed.frameBaseFormId)" in reconcile_body, "every owner apply must pass the parked frame base"
assert "if (!placed.localId) {" in reconcile_body and reconcile_body.index("if (!placed.localId) {") < reconcile_body.index("ApplyLock(suitId, *placed.localId, true"), "an entry without a local ref must get no frame call"
apply_calls = re.findall(r"ApplyLock\(suitId, ([^,]+),", sync_cpp)
assert apply_calls and all(arg in ("*placed.localId", "localId") for arg in apply_calls), "every frame lock call must be keyed by the local ref"
frame_lookups = re.findall(r"GetFormByID<RE::TESObjectREFR>\(([^)]*)\)", sync_cpp)
assert len(frame_lookups) >= 2 and all(arg in ("a_localId", "localId") for arg in frame_lookups), "a frame ref lookup must be gated by the local ref, never the server suit id"
assert "const auto owners = [] {" in reconcile_body, "reconcile must copy the owners under the lock before dispatching"
store_body = function_body(inbound_sync_cpp, "void StorePark(")
assert 'ParseHexId(*powerArmor, "frameBaseFormId")' in store_body and "*frameBaseFormId" in store_body, "a park must store the frame base it was parked in"
assert 'ParseHexId(a_packet, "localId")' in store_body, "a park must read the local ref when the owner is told it"
assert "existing->second.mine && !mine" in store_body, "a suit taken from you must drop its outcome; a break-in keeps the 'Broken in.' the server sends first"
assert store_body.index("g_hackResults.erase(a_suitId);") < store_body.index("g_owners[a_suitId] = Placed{"), "the outcome must be dropped before the new owner state is stored"
assert "std::vector<std::pair<std::uint32_t, Placed>> OrderedByDistance()" in sync_all and "const Placed*" not in sync_all, "the ordered list must hold copies, not pointers into the locked map"
assert "const auto hackResults = [] {" in sync_all and "hackResults.find(suitId)" in sync_all, "the menu state must read a locked snapshot of the outcomes"
flush = function_body(queue_cpp, "void FlushPendingLocked(")
assert "const std::scoped_lock lock(g_mutex);" in flush, "the retry queue must be guarded"
assert "front.inFlight = true;" in flush and "if (now < front.nextSend) {" in flush, "the front transition must wait for its ack before another send"
assert "kAckTimeout" in flush and "kRetryDelay" in flush and "kMaximumAttempts" in flush, "a missing ack must time out, retry after a delay and stop after the attempt cap"
assert re.search(r'\+\+front\.attempts;\s*if \(!F4T::Networking::SendParkedSuitPacket\(front\.packet\)\) \{\s*front\.nextSend = now \+ kRetryDelay;\s*return;', flush), "a local send failure must consume an attempt and wait before retrying, so repeated failures reach the removal warning"
assert "pop_front" not in flush[flush.index("SendParkedSuitPacket(front.packet)"):], "a sent transition must stay queued until its ack"
assert 'Log("ParkedSuitSync", "dropped {} for localId {:08X} after {} attempts"' in flush, "a dropped transition must be logged"
tryh = function_body(inbound_sync_cpp, "bool TryHandlePacket(")
assert 'name == "ack"' in tryh and tryh.index('name == "ack"') < tryh.index("ReadSuitId(a_packet)"), "the ack must be read before the suit id"
ack_body = function_body(inbound_sync_cpp, "void HandleAck(")
assert "g_pending.front().op != name" in ack_body and "g_pending.front().localId != *localId" in ack_body, "an ack must match the front transition by op and local ref"
assert ack_body.index("g_pending.pop_front();") < ack_body.index("inFlight = false;"), "a transition leaves the queue only on an ok ack"
assert 'name == "remove" && g_pending.front().attempts > 1' not in ack_body, "repeated server rejections must not silently discard a suit removal"
assert 'a_failedRemovals.push_back(front.localId)' in flush, "a removal still rejected after all retries must warn the player"
assert "a_transition.consoleSpawned && !a_transition.nacked" in sync_all and "g_pending.front().nacked = true;" in sync_all, "a console park the server rejected must not be retried by later spawn commands"
assert "if (!mine && target.first != suitId) {" in sync_all, "the menu lists only own suits plus the one foreign suit being hacked"
assert "RegisterActivateSink();" in sync_all and "TESActivateEvent" in sync_all and "F4T::ParkedSuitUi::Open();" in sync_all, "activating a foreign suit must open the menu on it"
assert "g_target.reset();" in sync_all, "the hack target is cleared on disconnect"
assert sync_all.count("std::optional<std::uint32_t> g_target;") == 1 and sync_all.count("kActivatedSuitReachSquared") == 0, "no duplicated target declarations, and the distance pick is gone"
assert 'op != "clearTarget"' in (ROOT / "CommonwealthOnlineUI/src/F4TParkedSuitView.cpp").read_text(encoding="utf-8"), "the view must forward close and clearTarget to the plugin"
assert "FlushPendingLocked(failedRemovals);" in sync_all and "NotifyRemovalFailed(front.localId)" not in sync_all, "the removal-failure alert is raised after the queue lock is released"
assert "droppedRemoval" in function_body(queue_cpp, "void Enqueue("), "evicting a queued removal must warn the player and prefer evicting a park"
assert "SuitForFrame(a_ref.GetFormID())" in function_body(sync_cpp, "void OnFrameActivated("), "an activated frame opens the menu only through the mirror ref of a suit"
assert "nearest" not in function_body(sync_cpp, "void OnFrameActivated("), "a nearby frame of a stranger's suit must never be picked by distance"
assert 'if (name == "park") {' not in ack_body and "IsDefinitiveRefusal(refusal)" in ack_body, "a nacked park is retried with backoff and leaves the queue only on a definitive refusal"
assert "g_consoleUnsent.erase(g_pending.front().localId);" in ack_body, "console unsent is cleared only on an ok ack"
assert "g_consoleParked.insert(g_pending.front().localId);" in ack_body, "a console spawn is recorded as parked only once the server accepts it"
for receive_body in (tryh, store_body, ack_body, function_body(inbound_sync_cpp, "void StoreChallenge("), function_body(inbound_sync_cpp, "void StoreHackResult(")):
    assert "PlaceName(" not in receive_body and not re.search(r"\bRE::", receive_body), "the receive thread must not read game objects"
assert "PlaceName(" in function_body(menu_cpp, "std::string LocationText("), "the place name is resolved for the menu only"
console_cpp = (PLUGIN_DIR / "src/F4TConsoleGuard.cpp").read_text(encoding="utf-8")
power_armor_h = (PLUGIN_DIR / "include/F4TPowerArmorSync.h").read_text(encoding="utf-8")
assert '#include "F4TParkedSuitSync.h"' in console_cpp, "the console guard must reach the spawned frame park"
console_detour = function_body(console_cpp, "void ExecuteCommandDetour(")
assert "const auto humanCommand = source != CommandSource::kInternal && source != CommandSource::kPluginCode;" in console_detour, "internal and plugin commands must never queue a park"
assert "const auto spawnsFrame = humanCommand && OnGameThread() && IsPlaceAtMeCommand(a_command);" in console_detour, "only a human command on the game thread that names placeatme may queue a park"
assert console_detour.index("original(a_command);") < console_detour.index("QueueParkSpawnedFrames(std::move(framesBefore));"), "the park must be queued after the original command returns"
assert "auto framesBefore = spawnsFrame ? F4T::ParkedSuitSync::SnapshotFrameRefs() : std::unordered_set<std::uint32_t>{};" in console_detour and console_detour.index("SnapshotFrameRefs()") < console_detour.index("original(a_command);"), "the frames present before the command must be snapshotted before it runs"
assert "if (spawnsFrame) {" in console_detour and "const InternalCallScope scope;" in console_detour, "the queue must sit behind the untouched internal scope"
place_at_me_body = function_body(console_cpp, "bool IsPlaceAtMeCommand(")
assert "std::tolower" in place_at_me_body and '"placeatme"' in place_at_me_body, "the command match must be case-insensitive on placeatme"
queue_body = function_body(console_cpp, "void QueueParkSpawnedFrames(")
assert "F4SE::GetTaskInterface()" in queue_body and "taskInterface->AddTask(" in queue_body and "F4T::ParkedSuitSync::ParkSpawnedFrames(before)" in queue_body and "std::move(a_before)" in queue_body, "the park must be queued as a game task carrying the pre-command snapshot"
assert "void ParkSpawnedFrames(const std::unordered_set<std::uint32_t>& a_before);" in sync_h and "SnapshotFrameRefs();" in sync_h, "the spawned frame park and its snapshot must be declared"
park_spawned = function_body(sync_cpp, "void ParkSpawnedFrames(")
cell_frames = function_body(sync_cpp, "std::vector<RE::TESObjectREFR*> PowerArmorFramesInCell(")
assert "F4T::Networking::IsConnectedToServer()" in park_spawned, "no park without a server connection"
assert "kFurnitureTypePowerArmorKeywordFormId" in cell_frames and "furniture->HasKeyword(keyword)" in cell_frames, "a spawned frame must carry the FurnitureTypePowerArmor keyword"
assert "g_consoleParked.contains(refId)" in park_spawned and "g_consoleParked.insert(" not in park_spawned and "g_consoleUnsent.insert(refId);" in park_spawned and "if (!sent) {" in park_spawned, "a frame is parked once per ref, and a park is recorded only by its ack so a dropped one is retried by a later command"
assert "std::unordered_set<std::uint32_t> g_consoleParked;" in sync_all and "#include <unordered_set>" in sync_all, "the dedupe set must exist"
assert "SendPark(Occupied{ F4T::PowerArmorSync::CaptureFrameFromReference(*ref)" in park_spawned, "a spawned frame must go through the owner park path"
assert "F4T::PowerArmorSync::CaptureOccupiedFrame(*player)" in park_spawned and "occupied->refFormId == refId" in park_spawned, "the frame the player occupies must never be parked from the console path"
assert "a_before.contains(refId)" in park_spawned, "only frames that appeared after the command may park"
assert "kMaximumSpawnedParks" in park_spawned and "kSpawnedFrameReachSquared" in park_spawned and "kSpawnedFrameReachSquared = 400.0F * 400.0F;" in sync_all, "one call must cap the parks and stay within a 400 unit reach"
assert "ForEachReference" in cell_frames and "GetParentCell()" in cell_frames, "the scan must walk the player's cell"
assert "g_consoleParked.clear();" in disconnect and "g_consoleUnsent.clear();" in disconnect, "a disconnect must clear the dedupe and retry sets"
assert "SnapshotFrameRefs()" in console_cpp, "the console guard must take the frame snapshot"
assert "OccupiedFrame CaptureFrameFromReference(RE::TESObjectREFR& a_frame);" in power_armor_h, "the frame capture helper must be declared"
assert "CaptureFrameFromReference(*frame.get())" in function_body(power_armor_cpp, "std::optional<OccupiedFrame> CaptureOccupiedFrame("), "the occupied capture must reuse the shared helper"
assert "seenSuitIds.insert(*suitValue).second" in wire_path.read_text(encoding="utf-8"), "a saved file with a repeated suitId must drop the repeat"
wire_cpp = wire_path.read_text(encoding="utf-8")
assert "if (same->ownerKey != same->localOwnerKey && a_record.ownerKey == a_record.localOwnerKey) {" in wire_cpp and "return ParkOutcome::NotOwner;" in wire_cpp, "a suit taken from its original parker must not be overwritten by that parker"
tests_cpp = (ROOT / "co-core/tests/F4TParkedSuitTests.cpp").read_text(encoding="utf-8")
assert "ParkOutcome::NotOwner" in tests_cpp, "the re-park of a hacked-away suit must be pinned by a test"
assert '{ "1C", "E9", "1C", "55", "FF", "C3" }' in tests_cpp and '{ "1C", "C3", "7A", "C3" }' in tests_cpp, "the breach grid must be pinned to the JS generator"
mirror_cpp = (PLUGIN_DIR / "src/F4TParkedSuitMirror.cpp").read_text(encoding="utf-8")
power_armor_mirror_cpp = (PLUGIN_DIR / "src/F4TPowerArmorMirror.cpp").read_text(encoding="utf-8")
reconcile_mirror = function_body(mirror_cpp, "void Reconcile(")
assert "EnsureMirrorFrame(" in reconcile_mirror and "ApplyMirrorPieces(" in reconcile_mirror, "a mirror must be placed before its pieces are applied"
assert "F4T::ProxySaveSafety::IsProxyTickBlocked()" in reconcile_mirror and reconcile_mirror.index("IsProxyTickBlocked()") < reconcile_mirror.index("DestroyMirrorFrame("), "a mirror must not be created or destroyed while the proxy tick is blocked"
assert reconcile_mirror.index("EnsureMirrorFrame(") < reconcile_mirror.index("ApplyMirrorPieces("), "pieces must follow a ready placement"
assert "DestroyMirrorFrame(mirror->second)" in reconcile_mirror, "an unwanted mirror must be destroyed"
assert "!request.mine && !locked" in reconcile_mirror, "a suit that is not ours must be locked once"
assert "Stale(mirror->second, *request)" in reconcile_mirror, "a mirror whose frame base or pieces changed must be rebuilt"
assert "ReleaseFrame" not in function_body(mirror_cpp, "void Graduate("), "a graduated mirror holds no reservation to release"
assert "g_mirrors.erase(a_suitId);" in function_body(mirror_cpp, "void Graduate("), "graduate must erase without destroying the frame"
assert "mirror = g_mirrors.erase(mirror);" in function_body(mirror_cpp, "void DestroyAllOnGameThread()"), "destroy-all must drop every mirror"
assert "DestroyMirrorFrame(mirror->second);" in function_body(mirror_cpp, "void DestroyAllOnGameThread()"), "destroy-all must destroy each mirror frame"
assert "return entry.first;" in function_body(mirror_cpp, "std::optional<std::uint32_t> SuitForFrame("), "a frame must map back to its suit"
apply_pieces = function_body(power_armor_mirror_cpp, "bool ApplyMirrorPieces(")
assert "BuildPieceExtra(piece, *object)" in apply_pieces and "piecesApplied" in apply_pieces, "pieces must be added once per frame with a built extra"
assert apply_pieces.index("AddObjectToContainer(") < apply_pieces.index("SyncFurnitureVisualsToInventory("), "frame visuals sync only after the pieces are added"
assert "ITEM_REMOVE_REASON::kNone" in apply_pieces, "pieces are added with the no-remove reason"
publish_body = function_body(sync_cpp, "void PublishLocal(")
assert publish_body.index("ReconcileFrames(a_player.GetNPC());") < publish_body.index("ReconcileMirrors(a_player);"), "mirror frames must follow the frame locks on every publish tick"
assert publish_body.index("AdoptMirror(frame->refFormId)") < publish_body.index("SendRemove(frame->refFormId, a_captured->frameBaseFormId)"), "entering your own parked suit must adopt it instead of removing it"
send_remove_body = function_body(sync_cpp, "void SendRemove(")
assert "frameBaseFormId != a_frameBaseFormId" in send_remove_body and send_remove_body.index("frameBaseFormId != a_frameBaseFormId") < send_remove_body.index("for (auto owner = g_owners.begin();"), "a remove must not touch a record whose stale localId names a frame of another base"
assert "a_frameBaseFormId != 0 &&" in send_remove_body and "a_owner.second.frameBaseFormId != 0 &&" in send_remove_body, "a base of 0 is unknown, so the remove must not be skipped for it"
enqueue_body = function_body(queue_cpp, "void Enqueue(")
assert "RememberDroppedPark(a_transition);" in enqueue_body and "RememberDroppedPark(*victim);" in enqueue_body, "a park that the full queue drops must be remembered for a retry"
assert "g_droppedParks.erase(a_transition.localId);" in enqueue_body, "a transition queued for a frame must clear that frame's retry entry"
retry_body = function_body(sync_cpp, "void RetryDroppedParks(")
assert "g_pending.size() >= kMaximumPendingTransitions" in retry_body and "Enqueue(PendingTransition{ std::move(packet), \"park\", refId, false });" in retry_body, "a retry must only send into a queue with room and must re-enqueue the stored park"
assert "a_owner.second.mine && a_owner.second.localId == refId" in retry_body and "std::erase_if(g_droppedParks" in retry_body and "settled.contains(a_entry.first)" in retry_body, "a retry entry must clear once the server knows the frame, the frame is out of reach or gone, or it was sent"
assert "occupied->refFormId == refId" in retry_body, "a retry must settle the frame the player is standing in"
assert "std::unordered_map<std::uint32_t, nlohmann::json> g_droppedParks;" in queue_cpp and "g_droppedParks;" in sync_internal_h, "the dropped parks must keep their original packet"
assert "auto packet = found->second;" in retry_body and 'packet["position"] = PositionJson(frame);' in retry_body and 'packet["angleZ"] = frame.angleZ;' in retry_body, "a retry must keep the stored packet, with its pieces, and refresh only the position and facing"
assert 'packet["powerArmor"]' not in retry_body and "PowerArmorState" not in retry_body and "SendPark(" not in retry_body, "a retry must not rebuild the power armor state and lose the pieces"
assert publish_body.index("ReconcileMirrors(a_player);") < publish_body.index("RetryDroppedParks();") < publish_body.index("if (!a_captured) {"), "dropped parks must be retried on every publish tick, before the in-frame check"
assert "g_droppedParks.clear();" in disconnect, "a disconnect must forget the dropped parks"
assert "g_adopting.clear();" not in publish_body, "leaving the frame must keep the adopt hold until the server confirms the adoption"
adopt_body = function_body(sync_cpp, "bool AdoptMirror(")
assert '{ "action", "adopt" }' in adopt_body and '{ "suitId", Hex(*suitId) }' in adopt_body and "Graduate(*suitId);" in adopt_body, "an adoption must send the adopt request and graduate the mirror"
assert "!owner->second.mine" in adopt_body, "only your own suit can be adopted"
assert re.search(r"scoped_lock lock\(g_mutex\);[\s\S]*?g_adopting\.emplace\(\*suitId, false\);\s*\}", adopt_body), "the adopt hold must be set under the lock, before the graduate"
release_body = function_body(inbound_sync_cpp, "void ReleaseSuit(")
assert "owner->second.localId = std::nullopt;" in release_body and "g_appliedLocked.erase(applied);" in release_body and "g_owners.erase(owner);" not in release_body, "a release must keep the suit record without its frame and drop the applied lock"
assert "SetFrameLocked(*frame, false)" in release_body and "DisableFrame(*frame)" in release_body, "a released frame must unlock and disable"
assert 'if (name == "release") {' in tryh and 'ReleaseSuit(*suitId, ParseHexId(a_packet, "localId"));' in tryh, "the plugin must handle a release by its local ref"
reconcile_mirrors = function_body(sync_cpp, "void ReconcileMirrors(")
assert "g_adopting.contains(suitId)" in reconcile_mirrors and "F4T::ParkedSuitMirror::Reconcile(a_player, requests);" in reconcile_mirrors, "mirror frames must follow the owner list except suits being adopted"
assert "F4T::PowerArmorSync::Decode(*powerArmor)" in store_body and "angleField->get<float>()" in store_body, "a park must keep the frame's pieces and facing"
assert "F4T::ParkedSuitMirror::DestroyAll();" in disconnect and re.search(r"scoped_lock lock\(g_mutex\);\s*g_adopting\.clear\(\);", disconnect) and "if (unlock.empty())" not in disconnect, "a disconnect must destroy the mirror frames on the game thread and clear the adopt hold under the lock"
assert '#include "F4TParkedSuitMirror.h"' in sync_cpp and "inline std::unordered_map<std::uint32_t, bool> g_adopting;" in internal_h and "std::unordered_set<std::uint32_t> g_adopting;" not in sync_cpp, "the sync module must reach the mirror and the adopt set must live in the internal header"
assert '#include "F4TParkedSuitInternal.h"' in sync_cpp and '#include "F4TParkedSuitInternal.h"' in menu_cpp, "the sync and menu modules must share the internal header"
save_guard_cpp = (PLUGIN_DIR / "src/F4TSessionSaveGuard.cpp").read_text(encoding="utf-8")
mirror_h = (PLUGIN_DIR / "include/F4TPowerArmorMirror.h").read_text(encoding="utf-8")
stale_body = function_body(mirror_cpp, "bool Stale(")
destroy_all_body = function_body(mirror_cpp, "void DestroyAllOnGameThread()")
apply_lock_body = function_body(sync_cpp, "bool ApplyLock(")
abandon_body = function_body(queue_cpp, "void AbandonPark(")
enqueue_body = function_body(queue_cpp, "void Enqueue(")
mirror_ack_body = function_body(inbound_sync_cpp, "void HandleAck(")
assert "struct MirrorFrame" not in power_armor_h and "enum class MirrorStatus" not in power_armor_h, "the host-test header must not carry the mirror types"
assert "struct MirrorFrame" in mirror_h, "the plugin-only mirror header must declare the mirror frame"
assert "#include <RE/Fallout.h>" in mirror_h and "std::uint32_t cellId{};" in mirror_h and "std::uint32_t worldspaceId{};" in mirror_h and "float angleZ{};" in mirror_h, "the mirror frame must carry its placement and the RE types it holds"
assert "ReserveFrame" not in mirror_cpp and "ReleaseFrame" not in mirror_cpp, "a mirror frame must not hold a frame save reservation"
assert "if (F4T::ParkedSuitMirror::DestroyAll()) {" in save_guard_cpp and save_guard_cpp.index("if (F4T::ParkedSuitMirror::DestroyAll()) {") < save_guard_cpp.index("const auto outerFrameSave = t_frameSaveInProgress;"), "a save must destroy the mirror frames before the world is written"
pending_block = save_guard_cpp[save_guard_cpp.index("if (F4T::ParkedSuitMirror::DestroyAll()) {"):save_guard_cpp.index("const auto outerFrameSave = t_frameSaveInProgress;")]
assert "NoteEntered(owner, false);" in pending_block and "return false;" in pending_block, "a teardown that is still pending must refuse the save so it retries"
assert '#include "F4TParkedSuitMirror.h"' in save_guard_cpp, "the save guard must reach the mirror teardown"
assert "ForgetAdopting" not in sync_cpp, "an adopt hold must not be cleared on a nack or an abandon; it lasts until the frame is left or the session drops"
assert 'a_pending.op != "adopt"' in enqueue_body, "an adopt must not be evicted for a park"
assert "victim = g_pending.begin()" not in enqueue_body and "AbandonPark(a_transition);" in enqueue_body, "a full queue with nothing evictable must drop the incoming transition, never an adopt"
assert "g_adopting.erase" not in mirror_ack_body and "g_adopting.clear" not in mirror_ack_body, "a nack must leave the adopt hold alone"
assert "a_mirror.x != a_request.x" in stale_body and "a_mirror.z != a_request.z" in stale_body, "a suit that moved must rebuild its mirror"
assert all(part in stale_body for part in ("a_mirror.cellId != a_request.cellId.value_or(0)", "a_mirror.worldspaceId != a_request.worldspaceId.value_or(0)", "a_mirror.angleZ != a_request.angleZ")), "a mirror whose cell, worldspace or facing changed must rebuild"
assert "applied->second.localId == a_localId" in apply_lock_body, "a lock applied to another frame must not count as applied"
assert re.search(r"if \(!frame\) \{\s*const std::scoped_lock lock\(g_mutex\);\s*g_appliedLocked\.erase\(a_suitId\);", apply_lock_body), "a lock for a vanished frame must drop its applied entry"
assert "RE::PlayerCharacter::GetSingleton()" in destroy_all_body and "mirror->second.refFormId == occupiedRef" in destroy_all_body, "destroy-all must keep the mirror the player is inside"
assert "\tnamespace\n\t{\n\t\tstd::optional<nlohmann::json> NormalizePicks(" in menu_cpp and "\t}\n\n\tstd::string BuildUiStateJson()" in menu_cpp, "the menu helpers must sit in an anonymous namespace"
assert "\n\n\n" not in menu_cpp, "the menu must not carry stray blank lines"
assert '\n\t\tif (name == "release") {\n\t\t\tReleaseSuit(*suitId, ParseHexId(a_packet, "localId"));' in inbound_sync_cpp, "the release branch must sit at the same depth as the remove branch"
ensure_mirror = function_body(power_armor_mirror_cpp, "MirrorStatus EnsureMirrorFrame(")  # TODO-verify: engine call names unverified; source:CodeNotes-GARDEN power-armor-sync.
destroy_mirror = function_body(power_armor_mirror_cpp, "void DestroyMirrorFrame(")
assert "a_mirror.z = a_z;" in ensure_mirror, "a new mirror must remember where it was placed"
assert "a_mirror.cellId = a_cellId;" in ensure_mirror and "a_mirror.worldspaceId = a_worldspaceId;" in ensure_mirror and "a_mirror.angleZ = a_angleZ;" in ensure_mirror, "a new mirror must remember its cell, worldspace and facing"
assert "ForgetFrameLock(a_mirror.refFormId);" in destroy_mirror, "a destroyed mirror must drop its lock record"  # TODO-verify: lock record drop unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert "if (!a_mirror.handle || a_mirror.handle.get()) {" in destroy_mirror and destroy_mirror.index("if (!a_mirror.handle") < destroy_mirror.index("DestroyPlacedFrame("), "a mirror whose frame is already gone must not be retired again"  # TODO-verify: handle resolution unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert "void ForgetFrameLock(std::uint32_t a_frameFormId);" in script_h and "g_lockedFrames.erase(a_frameFormId)" in script_cpp, "the lock record must be forgotten by frame"  # TODO-verify: lock record API unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert "SetFrameLocked(*mirror.reference, false)" in reconcile_mirror and "SetFrameOwner(*mirror.reference, a_player.GetNPC())" in reconcile_mirror, "a mirror of our own suit must be unlocked and owned by the player"  # TODO-verify: owner and lock calls unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert "SetFrameLocked(*mirror.reference, true)" in reconcile_mirror, "a mirror of another player's suit must be locked"  # TODO-verify: lock call unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert "bool FrameResolvesToBase(" in sync_cpp and "FrameResolvesToBase(*placed.localId, placed.frameBaseFormId)" in reconcile_mirrors, "a suit with a real frame of its base must not get a mirror"  # TODO-verify: base form check unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert reconcile_mirrors.index("FrameResolvesToBase(*placed.localId") < reconcile_mirrors.index("requests.push_back("), "the real-frame check must run before a mirror request is built"
assert "if (!IsPowerArmorPieceForm(" in function_body(power_armor_mirror_cpp, "bool ApplyMirrorPieces("), "a mirror must skip pieces that are not power armor"  # TODO-verify: piece filter unverified in game; source:CodeNotes-GARDEN power-armor-sync.
assert "!keyword || !furniture->HasKeyword(keyword)" in ensure_mirror, "a mirror must be a furniture form carrying the power armor keyword"  # TODO-verify: keyword check unverified in game; source:CodeNotes-GARDEN power-armor-sync.
public_destroy_all = function_body(mirror_cpp, "bool DestroyAll()")
assert "IsOnRecordedGameThread()" in public_destroy_all and "AddTask(" in public_destroy_all and "DestroyAllOnGameThread();" in public_destroy_all, "destroy-all must run inline on the game thread and queue from any other thread"
assert "g_mirrors.empty()" in destroy_all_body, "destroy-all must return early when no mirror exists"
assert '#include "F4TProxySaveSafety.h"' in mirror_cpp, "the mirror must reach the game-thread and tick-gate checks"
proxy_save_safety_cpp = (PLUGIN_DIR / "src/F4TProxySaveSafety.cpp").read_text(encoding="utf-8")
proxy_save_safety_h = (PLUGIN_DIR / "include/F4TProxySaveSafety.h").read_text(encoding="utf-8")
assert "bool IsOnRecordedGameThread() noexcept;" in proxy_save_safety_h and "bool IsOnRecordedGameThread() noexcept" in proxy_save_safety_cpp and "return g_gameThreadId.load(std::memory_order_acquire) == REX::W32::GetCurrentThreadId();" in proxy_save_safety_cpp, "the recorded game thread must be testable from any thread"
assert not re.search(r"^ {4}", mirror_cpp, re.M) and not re.search(r"^ {4}", power_armor_mirror_cpp, re.M), "the mirror sources must use tab indentation"
assert "\t\tconst std::scoped_lock lock(g_lockedFramesMutex);" in function_body(script_cpp, "void ForgetFrameLock("), "the lock record forget must use tab indentation"
parked_mirror_h = (PLUGIN_DIR / "include/F4TParkedSuitMirror.h").read_text(encoding="utf-8")
assert "bool DestroyAll();" in parked_mirror_h, "destroy-all must report whether a teardown is still pending"
assert "return true;" in public_destroy_all and "g_mirrorsPresent.load(" in public_destroy_all, "a queued teardown must report pending only when mirrors exist"
assert "SyncPresence(occupiedRef);" in reconcile_mirror and "SyncPresence(0);" in function_body(mirror_cpp, "void Graduate(") and "SyncPresence(occupiedRef);" in destroy_all_body, "every mirror change must refresh the presence flag"
presence_body = function_body(mirror_cpp, "void SyncPresence(")
assert "std::any_of(" in presence_body and "refFormId != a_occupiedRef" in presence_body, "the presence flag must ignore the mirror the player occupies"
assert disconnect.index("g_adopting.clear();") < disconnect.index("GetTaskInterface()"), "the adopt hold must clear before the task interface check so a missing interface cannot leave it"
store_park_body = function_body(inbound_sync_cpp, "void StorePark(")
assert "std::atomic<bool> g_resync{ true };" in sync_cpp and "g_resync.exchange(false)" in publish_body and "g_resync.store(true);" in disconnect, "a reconnect or a disconnect must resync the frame the player stands in"
assert publish_body.index("g_resync.exchange(false)") < publish_body.index("AdoptMirror(frame->refFormId)"), "the resync must be consumed before the enter edge is decided"
assert "!g_owners.empty() && g_resync.exchange(false)" in publish_body, "a resync must stay pending while no parked suits are known, so the welcome parks arrive before it is spent"
assert "a_transition.packet[\"nonce\"] = ++g_nonce;" in enqueue_body, "every queued request must carry a fresh nonce"
assert "nonce == 0" in mirror_ack_body and "NonceOf(g_pending.front().packet) != nonce" in mirror_ack_body, "an ack must match the nonce of the front transition"
assert "ForgetAdoption(a_transition);" in enqueue_body and "ForgetAdoption(front);" in flush, "a dropped adopt must release its adopt hold"
assert "g_adopting.emplace(*suitId, false);" in adopt_body and "if (g_adopting.contains(*suitId)) {" in adopt_body, "an adopt in flight must not be sent twice"
assert "adopting->second = true;" in mirror_ack_body and 'name == "adopt"' in mirror_ack_body, "an ok adopt ack must mark the hold as acknowledged, not clear it"
assert "mine && localId && adopting != g_adopting.end() && adopting->second" in store_park_body and "g_adopting.erase(adopting);" in store_park_body, "the adopt hold must clear only when the owner's park arrives"
assert "g_graduated[a_localId] = *suitId;" in adopt_body and "GraduatedSuit(a_localId)" in adopt_body, "an entry into a graduated frame must re-send the adopt"
assert "IsPowerArmorFrame(*frame)" in release_body and "FrameResolvesToBase(localId, *expectedBase)" in release_body, "a release by local ref must verify the frame is a power armor frame of the expected base"
assert "!frame || !expectedBase ||" in release_body, "a release with no matching owner entry must not unlock or disable the frame"
graduated_body = function_body(sync_cpp, "std::optional<std::uint32_t> GraduatedSuit(")
assert "const std::scoped_lock lock(g_mutex);" in graduated_body and "g_graduatedBeforeReconnect.find(a_localId)" in graduated_body, "graduations are shared with the receive thread, so both maps are read under the lock"
assert "if (mine && localId) {" in store_park_body and "g_graduated.erase(graduated);" in store_park_body and "g_graduatedBeforeReconnect.erase(carried);" in store_park_body and "graduated->second == a_suitId" in store_park_body and "carried->second == a_suitId" in store_park_body, "a park of the sender's own suit must end both graduations for that suit, with or without an acknowledged adopt, so a later entry sends remove"
assert "g_graduatedBeforeReconnect.insert_or_assign(localId, suitId);" in disconnect and "g_graduatedBeforeReconnect.merge(" not in disconnect and "g_graduated.clear();" in disconnect, "a disconnect must carry unconfirmed graduations over the reconnect, the newer value winning on a duplicate key, and clear the live map"
ui_cpp = (PLUGIN_DIR / "src/F4TParkedSuitUi.cpp").read_text(encoding="utf-8")
assert "Someone broke into your parked power armor and took it" in ui_cpp and "Someone is breaking into" not in ui_cpp, "a break-in that took the suit must say it was taken"
piece_keys_cpp = (PLUGIN_DIR / "src/F4TParkedSuitPieceKeys.cpp").read_text(encoding="utf-8")
assert "nlohmann::json WithPieceKeys(nlohmann::json a_packet, const std::vector<F4T::PowerArmorSync::Piece>& a_pieces);" in internal_h, "the piece key helper must be declared in the internal header"
assert "F4T::InventoryDropGate::TranslateItemKey(form)" in piece_keys_cpp, "park piece keys must come from the plugin's item key translation"
hook_h = (PLUGIN_DIR / "include/F4TPowerArmorExitHook.h").read_text(encoding="utf-8")
hook_cpp = (PLUGIN_DIR / "src/F4TPowerArmorExitHook.cpp").read_text(encoding="utf-8")
thread_table_h = (PLUGIN_DIR / "include/F4THookThreadTable.h").read_text(encoding="utf-8")
plugin_main_cpp = (PLUGIN_DIR / "src/main.cpp").read_text(encoding="utf-8")
assert "namespace F4T::PowerArmorExitHook" in hook_h and "void Install();" in hook_h, "the exit hook must declare its install entry point"
assert "PowerArmorExit = 29," in thread_table_h, "the exit hook must have its own hook-thread id"
assert '#include "F4TPowerArmorExitHook.h"' in plugin_main_cpp and "F4T::PowerArmorExitHook::Install();" in plugin_main_cpp, "main must install the exit hook"
assert "kSwitchFromPowerArmorStart{ 550299, 2219450, 2219450 }" in hook_cpp, "the exit hook must resolve its target by the OG, NG and AE ids"
assert "constexpr std::array<std::uint8_t, 22> kExpectedPrologueOg{" in hook_cpp and "constexpr std::array<std::uint8_t, 22> kExpectedPrologueNgAe{" in hook_cpp, "each runtime family must verify its own 22-byte exit prologue"
assert "version != F4T::RuntimeSelect::VerifiedVersion()" in hook_cpp and "F4T::RuntimeSelect::IsOG()" in hook_cpp, "the exit hook must refuse unverified runtimes and pick the prologue per runtime"
assert "F4T::HookThread::Note(F4T::HookThread::Id::PowerArmorExit);" in hook_cpp and "s_exitHook.hook->original<SwitchFromPowerArmorStartFn>()(a_actor);" in hook_cpp, "the exit detour must note the call and always forward to the original"
assert "using SwitchFromPowerArmorStartFn = void (*)(RE::Actor*);" in hook_cpp and "void SwitchFromPowerArmorStartDetour(RE::Actor* a_actor) noexcept" in hook_cpp, "the exit body returns void on OG, NG and AE (decompile-confirmed), so the detour must be void"
assert 'if (!keys.empty()) a_packet["pieceKeys"] = std::move(keys);' in piece_keys_cpp, "the pieceKeys field is omitted when no piece has a key, so old servers still accept those parks"
assert "WithPieceKeys(nlohmann::json{" in sync_cpp and '{ "angleZ", a_occupied.frame.angleZ } }, a_occupied.powerArmor.pieces);' in sync_cpp, "a park must send the piece keys the server audit checks, through WithPieceKeys"
server_protocol_h = (ROOT / "CO-SERVER/server-cpp/include/ServerProtocol.h").read_text(encoding="utf-8")
session_types_h = (SERVER_DIR / "ServerSessionTypes.h").read_text(encoding="utf-8")
snapshot_cpp = (SERVER_DIR / "ServerInventorySnapshot.cpp").read_text(encoding="utf-8")
parked_suit_h = (ROOT / "co-core/include/F4TParkedSuit.h").read_text(encoding="utf-8")
assert "std::optional<ParkedSuitPieceAudit> parkedSuitPieceAudit;" in session_types_h, "the pending piece audit is per session"
assert "std::uint64_t parkedSuitPieceAuditMisses{};" in server_protocol_h, "audit misses are counted in the protocol stats"
assert "static constexpr double kParkedSuitPieceAuditSeconds = 15.0;" in parked_suits_h, "the audit deadline is 15 seconds"
audit_arm = function_body(inc, "void ProtocolHost::Impl::ArmParkedSuitPieceAudit(")
audit_body = function_body(inc, "void ProtocolHost::Impl::AuditParkedSuitPiecesOnSnapshot(")
for audit_code in (audit_arm, audit_body):
    for banned in ("Reject(", "ParkedSuitWriteRejected", "BuildAckPacket", "ReliableFrame", "rejected(", "parkedSuits."):
        assert banned not in audit_code, f"the piece audit detects and logs only, it must not contain {banned}"
assert "ArmParkedSuitPieceAudit(a_connectionId, a_session, *removed, a_monotonicSeconds)" in inc and "ArmParkedSuitPieceAudit(a_connectionId, a_session, *stored, a_monotonicSeconds)" in inc, "a matched remove and an adopt both arm the audit"
assert inc.index("parkedSuits.Remove(ownerKey, *localId)") < inc.index("ArmParkedSuitPieceAudit(a_connectionId, a_session, *removed") < inc.index("F4T::ParkedSuit::BuildRemovePacket(ownerKey, *removedSuitId)"), "the remove audit arms from the pre-removal record after the removal commits"
assert "park->pieceKeys }" in inc, "a park record keeps the piece keys for the audit"
assert snapshot_cpp.index("ReconcileInventory(a_connectionId, *assembled)") < snapshot_cpp.index("inventoryPersistenceUnavailable") < snapshot_cpp.index("AuditParkedSuitPiecesOnSnapshot(a_connectionId, a_session, a_monotonicSeconds);") < snapshot_cpp.index("return reply;"), "the audit checks only after the committed ledger reconcile and outside the try that can nack, so a throw from it cannot reject a snapshot"
assert "PieceAuditResult EvaluatePieceAudit(const std::vector<std::string>& a_keys, const nlohmann::json& a_inventory, double a_now, double a_deadline);" in parked_suit_h, "the piece audit decision is a co-core function"
assert "F4T::ParkedSuit::EvaluatePieceAudit(audit->pieceKeys, *inventory, a_monotonicSeconds, audit->deadlineMonotonic)" in audit_body, "the server audit decides through EvaluatePieceAudit"
assert "const auto profile = inventoryAuthority->ProfileForConnection(a_connectionId);" in audit_body and "*profile != audit->profileId" in audit_body, "the audit reads only the sender's session and clears it when the profile changed"
retain_body = function_body(queue_cpp, "void RetainParksLocked(")
assert "RetainParksLocked();" in disconnect and "g_pending.clear();" not in disconnect, "a disconnect must keep queued parks for the reconnect"
assert 'transition->op == "park"' in retain_body and "transition->inFlight = false;" in retain_body and "g_pending.erase(transition)" in retain_body, "queued parks survive a disconnect while the other transitions are dropped"
assert "if (g_inFrame && g_occupied) {" in publish_body and "g_inFrame && g_occupied && connected" not in publish_body, "an exit while offline must still park the suit"
nack_body = function_body(queue_cpp, "std::chrono::seconds NackDelay(")
assert 'a_transition.op != "park"' in nack_body and "kParkBackoffCap" in nack_body, "a park backs off to a cap while other transitions keep the short delay"
assert 'front.op != "park"' in flush and "yields < g_pending.size()" in flush and "g_pending.push_back(std::move(front));" in flush, "a backed-off park yields the queue and is never dropped by attempts"
assert "NackDelay(front)" in flush and "NackDelay(g_pending.front())" in ack_body, "a timeout or nack waits by the nack delay"
assert "IsDefinitiveRefusal(refusal)" in ack_body and 'a_packet.find("reason")' in ack_body, "only a definitive refusal drops a park; a transient nack keeps it queued"
assert '"invalidParkRequest"' in sync_internal_h and '"parkRefused"' in sync_internal_h and '"notInFrame"' in sync_internal_h and '"consoleNotAllowed"' not in sync_internal_h, "the definitive refusal reasons are named in one predicate, and consoleNotAllowed is not one (the server never sends it)"
assert '"parkRefused"' in inc and '"invalidParkRequest"' in inc, "the server must send the refusal reasons the plugin treats as definitive"
supersede_body = function_body(queue_cpp, "void SupersedeQueuedParks(")
assert "SupersedeQueuedParks(a_transition.localId);" in function_body(queue_cpp, "void Enqueue(") and "!a_pending.inFlight" in supersede_body, "a newer park or remove replaces a queued park for the same frame, never one in flight"
assert "!IsDefinitiveRefusal(a_transition.refusal)" in function_body(queue_cpp, "void RememberDroppedPark("), "a park that is only transiently refused is remembered for a retry"
assert 'packet["console"] = true;' in sync_cpp and "if (a_consoleSpawned) {" in function_body(sync_cpp, "bool SendPark("), "a console-spawned park says so on the wire"
reset_body = function_body(sync_cpp, "void OnWorldReset(")
assert "g_occupied.reset();" in reset_body and "g_inFrame = false;" in reset_body and "g_missingOwnFrames.clear();" in reset_body, "a world reset forgets the frame the player stood in, without a park"
assert "OnWorldReset" not in disconnect, "a disconnect keeps the frame state so an offline exit still parks"
assert plugin_main_cpp.count("F4T::ParkedSuitSync::OnWorldReset();") == 2 and '#include "F4TParkedSuitSync.h"' in plugin_main_cpp, "the load and new-game messages both reset the frame state"
assert "const std::scoped_lock lock(g_mutex);" in reset_body and "DiscardParksLocked();" in reset_body and "g_droppedParks.clear();" in reset_body, "a world reset must drop the queued and retry parks under the queue lock"
assert reset_body.index("const std::scoped_lock lock(g_mutex);") < reset_body.index("DiscardParksLocked();") < reset_body.index("g_droppedParks.clear();"), "the queue is discarded under the lock, then the retry parks"
discard_body = function_body(queue_cpp, "void DiscardParksLocked(")
assert 'transition->op != "park"' in discard_body and "AbandonPark(*transition);" in discard_body and "g_pending.erase(transition)" in discard_body, "a world reset erases every queued park that is not in flight"
assert "transition->inFlight" in discard_body and "transition->staleWorld = true;" in discard_body and discard_body.index("transition->staleWorld = true;") < discard_body.index("AbandonPark(*transition);"), "an in-flight park survives the reset only as stale"
assert re.search(r"if \(now < front\.nextSend\) \{\s*return;\s*\}\s*if \(front\.staleWorld\) \{\s*g_pending\.pop_front\(\);", flush), "a stale in-flight park is dropped at its ack timeout, never resent"
assert ack_body.index("if (g_pending.front().staleWorld) {") < ack_body.index("g_pending.front().nacked = true;") and "g_pending.pop_front();" in ack_body[ack_body.index("if (g_pending.front().staleWorld) {"):ack_body.index("g_pending.front().nacked = true;")], "a stale park's nack is dropped, never requeued"
assert "!transition->staleWorld" in retain_body, "a stale park is dropped on disconnect, never resent after the reconnect"
gone_body = function_body(sync_cpp, "void RemoveGoneOwnParks(")
assert "kGoneFrameConfirmation" in gone_body and "SendRemove(localId, 0);" in gone_body and "placed.cellId == cell->GetFormID()" in gone_body, "an own frame is removed only after the confirmation window in its own cell"
assert "OwnFrameLive(localId, frameBaseFormId)" in gone_body and "return;" in gone_body, "the live check is per base and one removal goes out per tick"
assert publish_body.index("ReconcileMirrors(a_player);") < publish_body.index("RemoveGoneOwnParks(a_player);") < publish_body.index("RetryDroppedParks();"), "the gone check runs after the mirror pass and before the retry"
assert gone_body.index("candidates.emplace_back(") < gone_body.index("std::erase_if(g_missingOwnFrames") < gone_body.index("if (!cell) {") and "a_candidate.first == a_entry.first" in gone_body, "a missing-frame timer survives only while its frame is a candidate in the current cell, so a return restarts the confirmation"
assert "if (placed.mine) {" in reconcile_mirrors and reconcile_mirrors.index("if (placed.mine) {") < reconcile_mirrors.index("requests.push_back("), "an own suit without its frame gets no mirror"
assert "HasLocalParkRecord(frame->refFormId)" in publish_body and publish_body.index("AdoptMirror(frame->refFormId)") < publish_body.index("HasLocalParkRecord(frame->refFormId)") < publish_body.index("SendRemove(frame->refFormId, a_captured->frameBaseFormId)"), "a remove on entry needs a local park record and runs after the adopt check"
assert 'if (op == "remove") {' in menu_cpp and "RemoveOwnSuit(*suitId);" in menu_cpp, "the menu's remove op reaches the owner-only removal"
remove_own = function_body(sync_cpp, "void RemoveOwnSuit(")
assert "owner->second.mine" in remove_own and "SendRemove(*localId, 0);" in remove_own, "a menu remove is owner-only and uses the existing remove message"
assert "std::vector<std::uint32_t> addedPieceFormIds;" in mirror_h and "std::uint32_t pieceApplyPasses{};" in mirror_h, "the mirror frame records added pieces and its re-apply passes"
assert "std::find(a_mirror.addedPieceFormIds.begin()" in apply_pieces and "a_mirror.addedPieceFormIds.push_back(" in apply_pieces, "a piece is added once"
assert "constexpr std::uint32_t kMaximumPieceApplyPasses = 30;" in power_armor_mirror_cpp and "++a_mirror.pieceApplyPasses;" in apply_pieces, "unresolved pieces get a bounded re-apply"
assert apply_pieces.index("if (added) {") < apply_pieces.index("SyncFurnitureVisualsToInventory("), "the visuals sync only after a piece was added"
assert "IsFurnitureForm(state.frameBaseFormId)" in power_armor_cpp and "power_armor_default_frame_unresolved" in power_armor_cpp, "a furniture frame without a default frame form is not parkable"
assert power_armor_cpp.index("power_armor_default_frame_unresolved") < power_armor_cpp.index("auto candidates = a_equippedItems;"), "the not-parkable gate runs before any piece is captured"

assert '&& op != "remove"' in view_cpp and 'op != "breachStart" && op != "hack" && op != "breachFailed"' in view_cpp, "the view must forward the owner's remove to the plugin"
parked_suit_html = (ROOT / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/parked-suit/index.html").read_text(encoding="utf-8")
assert 'id="coParkedSuitRemove" hidden' in parked_suit_html, "the remove button must exist and start hidden"
assert 'removeSuit = document.getElementById("coParkedSuitRemove")' in page_js and 'removeSuit.addEventListener("click"' in page_js, "the remove button must be wired"
assert 'send("remove", suit.suitId);' in page_js and "if (!suit || !suit.mine) return;" in page_js, "remove is sent only for the selected own suit"
assert "removeSuit.hidden = inBreach || inTransfer || !(suits[selected] && suits[selected].mine);" in page_js, "the remove button shows only for an own suit outside breach"

print("F4TParkedSuitContract: all checks passed.")
