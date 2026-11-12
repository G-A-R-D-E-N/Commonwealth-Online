from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def read_all(*relatives):
    return "".join(read(relative) for relative in relatives)


def test_inbound_budget_defaults_match_the_shared_limiter():
    limiter = read("co-core/include/F4TPacketRateLimit.h")
    assert "DefaultMaximumPacketsPerSecond = 120;" in limiter
    assert "DefaultMaximumBytesPerSecond = 256ULL * 1024ULL;" in limiter
    assert "DefaultMaximumViolationWindows = 3;" in limiter
    assert "MaximumInboundWirePacketsPerSecond = 512;" in limiter
    readme = read("CO-SERVER/server-cpp/README.md")
    assert "`inbound_packets_per_second` (10-512, default `120`)" in readme
    assert "`inbound_bytes_per_second` (65536-67108864, default `262144`)" in readme
    assert "`inbound_violation_windows` (1-60, default `3`)" in readme


def test_inbound_budget_keys_are_parsed_and_validated():
    config = read_all("co-core/src/config/F4TServerConfiguration.cpp")
    for key in ("inbound_packets_per_second", "inbound_bytes_per_second", "inbound_violation_windows"):
        assert f'"{key}"' in config, key
    assert "inbound_packets_per_second must be 10-512" in config
    assert "F4T::PacketRateLimit::MaximumInboundWirePacketsPerSecond" in config
    assert "inbound_bytes_per_second must be 65536-67108864" in config
    assert "inbound_violation_windows must be 1-60" in config
    assert "F4T::PacketRateLimit::Config inboundBudget{};" in read("co-core/include/F4TServerConfiguration.h")


def test_runtime_meters_decoded_gameplay_bytes_with_the_configured_budget():
    runtime = read_all("CO-SERVER/server-cpp/src/ServerRuntime.inc", "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc")
    assert "ObserveInboundDatagram(state.rate, state.voiceRate, monotonicNow, datagram, inboundBudget)" in runtime
    assert "InboundGameplayLimits(monotonicNow, state.joinedAtMonotonicSeconds, options.inboundBudget)" in runtime
    assert "const auto budgetAllows = rate.verdict == F4T::PacketRateLimit::Verdict::Allowed;" in runtime
    assert "auto packet = budgetAllows ? DecodeJson(message) : std::optional<nlohmann::json>{};" in runtime
    assert "const auto messageType = packet ? PacketTypeOf(*packet) : std::optional<std::string>{};" in runtime
    assert "const bool helloPacket = messageType.value_or(std::string{}) == \"hello\";" in runtime
    assert "helloPacket && CommonwealthOnline::Server::ServeModpackFilesIfRequested(" in runtime
    assert "NoteInboundWindowBytes(state.inboundWindowBytes, rate.windowReset, windowLabel, message.size())" in runtime
    assert runtime.count("DecodeJson(message)") == 1
    assert runtime.index("CommonwealthOnline::Server::ObserveInboundDatagram(state.rate") < runtime.index("DecodeJson(message)")
    assert "Window bytes by type:" in runtime
    meter = read("CO-SERVER/server-cpp/src/ServerVoiceMeter.h")
    assert "F4T::PacketRateLimit::Observe(a_globalState, a_nowMonotonicSeconds, a_budget, bytes)" in meter
    assert "kMaximumInboundWindowTypes = 16" in meter


def test_first_inbound_budget_strike_warns_the_player_and_nothing_else_does():
    enforcement = read("CO-SERVER/server-cpp/src/ServerEnforcement.cpp")
    assert "if (a_violation == Violation::InboundBudget && _notify) {" in enforcement
    runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
    assert '"fromName", "Server"' in runtime
    assert '{ "fromPlayerId", 0xFFFFFFFFU }' in runtime


def test_playerstate_sends_sticky_fields_only_on_change_or_resume():
    outbound = read("co-core/src/protocol/F4TGnsProtocolAdapterOutbound.cpp")
    assert "const bool durableChanged = durableDump != lastLocalDurableDump;" in outbound
    assert "const bool stickyDue = resumeStatePending || durableChanged || now - lastStickySendAt >= kStickyPlayerStateResendInterval;" in outbound
    assert "if (stickyDue || awaitingVisibilityResume) {" not in outbound
    sticky_block = outbound[outbound.index("if (stickyDue) {"):outbound.index("if (awaitingVisibilityResume) {", outbound.index("if (stickyDue) {"))]
    assert "MergeStickyPlayerState(playerState, durable);" in sticky_block
    assert outbound.count("lastLocalDurableDump = durableDump;") == 1
    assert "lastLocalDurableDump = durableDump;" in sticky_block


def test_protocol_docs_describe_the_budget_and_the_player_state_rule():
    docs = ROOT / "CO-BETA/docs"
    if not docs.is_dir():
        return
    text = (docs / "protocol/packets.md").read_text(encoding="utf-8")
    assert "### Player State Packet" in text
    assert "## Inbound Budget" in text
    assert "Sticky fields are sent when their state changed" in text
    assert '"Server"' in read("CO-BETA/docs/protocol/chat.md")


def main():
    for name, function in list(globals().items()):
        if name.startswith("test_") and callable(function):
            function()
    print("Inbound budget contract passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
