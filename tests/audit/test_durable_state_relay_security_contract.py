from __future__ import annotations

from pathlib import Path

# Constraint: relayed field names mirror the wire contract (source:CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp:HandleWorldState).

ROOT = Path(__file__).resolve().parents[2]

DURABLE_FIELDS = ("equippedItems", "appearance", "characterName", "powerArmor", "progression")
RELAYED_FIELDS = ("equippedItems", "appearance", "characterName", "powerArmor")


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def method_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    end = text.find("\n\t}\n", start)
    return text[start:] if end < 0 else text[start:end]


def struct_body(text: str, signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        return ""
    end = text.find("\n\t};", start)
    return text[start:] if end < 0 else text[start:end]


def array_body(text: str, name: str) -> str:
    start = text.find(name)
    if start < 0:
        return ""
    end = text.find("}", start)
    return text[start:] if end < 0 else text[start:end]


def main() -> int:
    failures: list[str] = []

    handlers = read("CO-SERVER/server-cpp/src/ServerProtocolHandlers.cpp")
    session_types = read("CO-SERVER/server-cpp/src/ServerSessionTypes.h")
    protocol_header = read("CO-SERVER/server-cpp/include/ServerProtocol.h")
    maintenance = read("CO-SERVER/server-cpp/src/ServerMaintenance.inc")
    validation = read("co-core/src/validation/F4TAuthoritativePacketValidation.cpp")

    for constant in (
        "kDurablePlayerStateRelayIntervalSeconds = 0.5",
        "kMaximumWorldGameHourStepPerPacket = 1.0",
        "kMinimumWorldWeatherChangeIntervalSeconds = 3.0",
        "kWorldGameTimeResyncSeconds = 5.0",
    ):
        require(constant in session_types, f"M2/L1: ServerSessionTypes.h must define {constant}", failures)

    session_struct = struct_body(session_types, "struct Session")
    for field in (
        "lastDurableDigest",
        "lastDurableRelayMonotonic",
        "durableRelayPending",
        "lastWorldGameHour",
        "lastWorldWeatherFormId",
        "lastWorldTimeAcceptedUnix",
        "lastWorldWeatherChangeUnix",
    ):
        require(field in session_struct, f"M2/L1: Session must carry per-session state '{field}'", failures)

    player_state = method_body(handlers, "ProtocolHost::Impl::HandlePlayerState(")
    require(player_state != "", "M2: HandlePlayerState must exist in ServerProtocolHandlers.cpp", failures)
    shared_field_list = read("co-core/include/F4TPlayerStateValidation.h")
    durable_list = array_body(shared_field_list, "kDurablePlayerStateFields[]")
    relayed_list = array_body(shared_field_list, "kRelayedPlayerStateFields[]")
    for field in DURABLE_FIELDS:
        require(f'"{field}"' in durable_list, f"M2: the durable field list must cover {field}", failures)
    for field in RELAYED_FIELDS:
        require(f'"{field}"' in relayed_list, f"M2: the relayed field list must cover {field}", failures)
    require('"progression"' not in relayed_list, "M2: progression is persisted but must never be relayed", failures)
    require(
        "kRelayedPlayerStateFields" in player_state and "HasDurablePlayerState(*normalized)" in player_state,
        "M2: hasDurableState detection must use the shared relayed field list",
        failures,
    )

    cache_update = player_state.find("a_session.lastDurablePlayerState = std::move(cached);")
    digest_gate = player_state.find("DurablePlayerStateDigest(")
    require(cache_update >= 0, "M2: durable cache must still be updated on every durable packet", failures)
    require(digest_gate >= 0, "M2: durable relay must be gated on a digest", failures)
    require(
        0 <= cache_update < digest_gate,
        "M2: the durable cache merge must run before the digest relay gate so late joiners get current data",
        failures,
    )
    require(
        "a_session.lastDurableDigest" in player_state and "*a_session.lastDurableDigest != digest" in player_state,
        "M2: relay must only happen when the durable digest changed",
        failures,
    )
    require(
        "a_session.lastDurableRelayMonotonic" in player_state
        and "SessionTypes::kDurablePlayerStateRelayIntervalSeconds" in player_state,
        "M2: relay must be limited by the monotonic durable relay interval",
        failures,
    )
    require(
        "!a_session.lastDurableRelayMonotonic" in player_state,
        "M2: a session that has never relayed durable state must not be suppressed by the interval",
        failures,
    )
    require(
        "HasDurablePlayerState(relay) && !relayDurableState" in player_state,
        "M2: unchanged or rate-limited packets must strip the durable fields from the relay",
        failures,
    )
    strip_region = player_state[player_state.find("HasDurablePlayerState(relay) && !relayDurableState"):]
    strip_region = strip_region[: strip_region.find("frames.push_back")] if "frames.push_back" in strip_region else strip_region
    require(
        strip_region.count("relay.erase(field)") >= 1,
        "M2: the durable strip must erase the shared durable field list",
        failures,
    )
    require(
        'relay.erase("actionEvents")' in player_state,
        "M2: actionEvents must keep its existing peer-visibility relay behaviour",
        failures,
    )
    require(
        'relay.erase("progression")' in player_state,
        "M2: progression must be stripped from the relay fan-out",
        failures,
    )
    require(
        "peerVisibility.IsVisible(recipient.playerId, a_session.playerId)" in player_state,
        "M2: actionEvents filtering must stay interest-gated",
        failures,
    )

    digest_helper = method_body(handlers, "std::uint64_t DurablePlayerStateDigest(")
    require(digest_helper != "", "M2: DurablePlayerStateDigest helper must exist", failures)
    require(
        "Json durable = Json::object();" in digest_helper and "durable.dump()" in digest_helper
        and "std::hash<std::string>" in digest_helper,
        "M2: the digest must hash the ordered nlohmann dump of the durable subset (order-stable for identical JSON)",
        failures,
    )
    require(
        "kRelayedPlayerStateFields" in digest_helper,
        "M2: the durable digest must hash the shared relayed field list, so progression never enters the digest",
        failures,
    )

    world_state = method_body(handlers, "ProtocolHost::Impl::HandleWorldState(")
    require(world_state != "", "L1: HandleWorldState must exist in ServerProtocolHandlers.cpp", failures)
    require(
        "worldStateHostPlayerId != a_session.playerId" in world_state,
        "L1: the world-state host gate must stay in front of the relay",
        failures,
    )
    require(
        world_state.count("stats.worldStateRejected++;") >= 3,
        "L1: host, normaliser and transition rejections must all count into stats.worldStateRejected",
        failures,
    )
    require(
        world_state.find("WorldStateTransitionAllowed(") >= 0
        and world_state.find("WorldStateTransitionAllowed(") < world_state.find("stats.worldStatePacketsReceived++;"),
        "L1: transition validation must run before the accepted worldState is relayed and counted",
        failures,
    )

    transition = method_body(handlers, "bool WorldStateTransitionAllowed(")
    require(transition != "", "L1: WorldStateTransitionAllowed helper must exist", failures)
    require(
        "SessionTypes::kMaximumWorldGameHourStepPerPacket" in transition
        and "SessionTypes::kWorldGameTimeResyncSeconds" in transition
        and "std::isfinite(gameHour)" in transition,
        "L1: oversized non-finite/out-of-range gameHour steps must be rejected (resync allowance included)",
        failures,
    )
    require(
        "SessionTypes::kMinimumWorldWeatherChangeIntervalSeconds" in transition
        and "lastWorldWeatherFormId" in transition,
        "L1: weather formId changes must be rate-limited against the last accepted value",
        failures,
    )
    require(
        transition.find("a_session.lastWorldGameHour = gameHour;") > transition.find("if (!std::isfinite(gameHour)"),
        "L1: anchors must only advance after every check passes (a rejected packet must not move them)",
        failures,
    )

    wrap_helper = method_body(handlers, "double ShortGameHourDelta(")
    require(wrap_helper != "", "L1: ShortGameHourDelta wrap helper must exist", failures)
    require(
        "std::fmod(" in wrap_helper and "kHoursPerDay" in wrap_helper and "kHalfDayHours" in wrap_helper,
        "L1: the game hour step must be computed circularly so midnight does not read as a 24 h jump",
        failures,
    )

    require(
        "ReadDouble(*found, 0.0, 24.0)" in validation and "std::isfinite(value)" in validation,
        "L1: NormalizeWorldState must keep rejecting non-finite/out-of-range gameHour values",
        failures,
    )
    require(
        "IsValidHexFormId(weather, true, true)" in validation,
        "L1: NormalizeWorldState must keep validating the weather form id",
        failures,
    )

    require(
        "durableStateRelays" in protocol_header and "durableStateSuppressed" in protocol_header,
        "M2: ProtocolStats must expose the durable relay/suppression counters",
        failures,
    )
    require(
        "stats.durableStateRelays++" in player_state and "stats.durableStateSuppressed++" in player_state,
        "M2: the durable relay and suppression paths must count into the new stats",
        failures,
    )

    require(
        "ProtocolHost::Impl::PollDurablePlayerState(" in handlers,
        "M2: the trailing durable flush PollDurablePlayerState must exist in ServerProtocolHandlers.cpp",
        failures,
    )
    require(
        "protocol.PollDurablePlayerState(loopMonotonic, nowUnix)" in maintenance,
        "M2: the maintenance tick must drive the durable trailing flush",
        failures,
    )

    for path, symbol in (
        ("co-core/src/protocol/F4TGnsProtocolAdapter.cpp", "pendingRemoteProgression"),
        ("CO-BETA/plugin/include/F4TRemotePlayerState.h", "SetRemotePlayerProgression"),
        ("CO-BETA/plugin/src/F4TRemotePlayerState.cpp", "SetRemotePlayerProgression"),
        ("CO-BETA/plugin/include/F4TPlayerProgression.h", "ApplyRemoteProgressionFromPacket"),
        ("CO-BETA/plugin/src/F4TPlayerProgression.cpp", "ApplyRemoteProgressionFromPacket"),
        ("CO-BETA/plugin/src/F4TNetworkingInbound.cpp", "ApplyRemoteProgressionFromPacket"),
    ):
        require(symbol not in read(path), f"M2: {path} must not reference {symbol}; progression is never relayed", failures)

    durable_state = read("co-core/src/persistence/F4TDurableState.cpp")
    require(
        "kDurablePlayerStateFields" in durable_state
        and "DropRejectedProgression(" in durable_state
        and "IsProgressionAccepted(" in durable_state,
        "M2: the persisted playerState path must keep the durable field list and the stored-profile progression gate",
        failures,
    )

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("Durable state relay and world-state host contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
