from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(relative):
    return (PLUGIN / relative).read_text(encoding="utf-8")


client_header = read("include/F4TTradeClient.h")
client = read("src/F4TTradeClient.cpp")
sync_header = read("include/F4TInventorySync.h")
sync = read("src/F4TInventorySync.cpp")
inbound = read("src/F4TNetworkingInbound.cpp")
networking = read("src/F4TNetworking.cpp")
wheel = read("src/F4TInteractWheel.cpp")
drop_gate = read("src/F4TInventoryDropGate.cpp")
pickup_gate = read("src/F4TInventoryPickupGate.cpp")
drop_refs_header = read("include/F4TDropMirrorRefs.h")

for packet_type in ("tradeRequest", "tradeRespond", "tradeOffer", "tradeConfirm", "tradeCancel"):
    check(f'{{ "type", "{packet_type}" }}' in client, f"the client must send {packet_type}")
for packet_type in ("tradeState", "tradeResult"):
    check(f'"{packet_type}"' in client, f"the client must handle {packet_type}")
check("void HandlePacket(const Json& a_packet);" in client_header, "the client must expose its packet handler")
check("CurrentTrade()" in client_header, "the client must expose its local trade state")
check('*packetType == "tradeState" || *packetType == "tradeResult"' in inbound, "the inbound dispatch must route trade packets")
check("F4T::TradeClient::HandlePacket(packet);" in inbound, "the inbound dispatch must hand trade packets to the client")

check('kTradeCapability = "player-trade-v1"' in sync_header, "hello must advertise player-trade-v1")
check("std::array<std::string_view, 4> kSessionCapabilities" in sync_header, "the hello capability list must hold four entries")
check("kSnapshotChannelCapability, kTradeCapability" in sync_header, "the trade capability must be in the hello list")
check("bool NoteTradeCommitted(std::uint32_t a_revision, const std::vector<F4T::Trade::Line>& a_give, const std::vector<F4T::Trade::Line>& a_receive);" in sync_header, "the mirror must expose the committed-trade helper")
check("SetExpectedRevision(a_revision)" in sync, "a committed trade must move the expected revision through the existing mirror")
check("a_revision != expected + 1" in sync, "a trade result that skips a revision must not be applied silently")
check("g_inboundTrades.front().revision <= *nextOwned->revision" in sync, "a trade commit must apply before a same-revision owned result")
check("AdvanceRevision" not in sync, "AdvanceRevision is private to PendingOwnedOps")

check(networking.count("F4T::Friends::Reset();") == networking.count("F4T::TradeClient::Reset();") > 0,
      "every disconnect that clears friends must clear the trade state too")
check('#include "F4TTradeClient.h"' in networking, "the networking module must include the trade client")

check('Slice("trade", "Trade")' in wheel and "F4T::TradeClient::RequestTrade(target)" in wheel,
      "the Trade slice must send a trade request")
check('Slice("trade", "Trade", true)' in wheel, "the Trade slice must be disabled while another player's trade is open")
check('Slice("tradeAccept", "Accept Trade")' in wheel and 'Slice("tradeCancel", "Cancel Trade")' in wheel,
      "the Trade slice must accept a request and cancel a trade")
check("F4T::TradeClient::RespondTrade(trade->tradeId, true)" in wheel and "F4T::TradeClient::CancelTrade(trade->tradeId)" in wheel,
      "the wheel must answer a pending trade through the client")
check('#include "F4TTradeClient.h"' in wheel, "the wheel must include the trade client")

for relative in ("include/F4TTradeClient.h", "src/F4TTradeClient.cpp"):
    raw = (PLUGIN / relative).read_bytes().replace(b"\r\n", b"\n")
    check(b"\r" not in raw, f"{relative} must use LF line endings")
    prose = b"\n".join(line for line in raw.splitlines() if not re.fullmatch(rb"[ \t]*// (?:Constraint: .*; )?source:\S+", line))
    check(b"//" not in prose and b"/*" not in prose, f"{relative} must not carry source comments other than single-line source citations")
    check(len(raw.splitlines()) <= 1000, f"{relative} must stay at or under 1000 lines")

check("[[nodiscard]] bool TradeSupported();" in sync_header, "the mirror must expose the trade capability gate")
check("bool TradeSupported()" in sync and "g_tradeSupported = WelcomeListsTrade(a_welcome);" in sync,
      "the trade capability must come from the welcome")
check("F4T::InventorySync::TradeSupported()" in wheel, "the Trade slices must be gated on the capability")
check("F4T::InventorySync::g_tradeApplying ||" in drop_gate and "F4T::InventorySync::g_tradeApplying ||" in pickup_gate,
      "both item gates must ignore the trade's own inventory changes")
check("AddLootToContainer(" in client and "RemoveLootFromContainer(" in client,
      "a committed trade must remove the give lines and add the receive lines")
check("ResolveItemFormId(" in client and "ResolveItemFormId(" in drop_refs_header, "item keys must resolve to local form ids")
check("HoldsLines(" in client, "a confirm must check that the game still holds every offered line")
check("UnequipObject(" in client and "F4T::EquipmentInstance::FindStack(" in client and "ActorEquipManager::GetSingleton()" in client,
      "a committed trade must unequip an offered item before it removes the item; source:CO-BETA/plugin/src/F4TNpcSyncLoot.cpp; source:CO-BETA/plugin/src/F4TProxyActorControllerEquipment.cpp")
check("RemoveOfferedLoot(*player, *formId, line.count)" in client, "a committed trade must remove each give line through the unequip-first helper")
check("!(trade->ownConfirmed || trade->confirmSent)" in client, "the lost-offer recheck must run once the local player has confirmed or has a confirm in flight")
check("if (held < a_count) {" in client and "equipped, static_cast<std::uint32_t>(equipped.stack->GetCount()));" in client,
      "an offered stack must be unequipped only when the unequipped copies held fall short of the offered count and the unequipped copies held; source:CO-BETA/plugin/src/F4TInventorySync.cpp")
check("return a_stack.IsEquipped() && !(a_stack.extra && a_stack.extra->GetByType<RE::BGSObjectInstanceExtra>());" in client,
      "the unequip must skip instanced stacks, so an instanced equipped copy is never unequipped; source:CO-BETA/plugin/src/F4TInventorySync.cpp")
check("bool confirmSent{};" in client_header and "g_trade->confirmSent = true;" in client and "trade->confirmSent = g_trade && g_trade->confirmSent" in client,
      "a sent confirm must be kept on the trade state and only for the same offer version")
check('"offerVersion"' in client and "offerVersion" in client_header, "the client must read and send the offer version")
check("bool ConfirmTrade(const std::string& a_tradeId, std::uint32_t a_offerVersion);" in client_header,
      "ConfirmTrade must take the offer version")
check('op == "tradeDecline"' in wheel and "F4T::TradeClient::RespondTrade(trade->tradeId, false)" in wheel,
      "the Decline Trade slice must answer with accept false")
check('op == "tradeConfirm"' in wheel, "the Confirm Trade slice must send the confirm")
check("ShowAlert(" in client and "DismissAlert(" in client, "the target must see a trade request notice")

trade_window = read("src/F4TTradeWindow.cpp")
check('state["partnerName"] = PartnerName(trade->partnerPlayerId);' in trade_window, "the trade state must carry the partner's display name")
check('#include "F4TRemotePlayerState.h"' in trade_window and '#include "F4TSocialNameplate.h"' in trade_window,
      "the trade window must resolve the partner name through the remote player state and the nameplate")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TTradeContract: all checks passed")
