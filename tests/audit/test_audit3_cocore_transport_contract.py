#!/usr/bin/env python3
"""Source contract for the audit-3 co-core transport fixes (tunnel admission key, monotonic handshake clock)."""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(relative):
    return (ROOT / relative).read_text(encoding="utf-8")


def function_body(source, signature, closer="\n\t}\n"):
    start = source.index(signature)
    return source[start:source.index(closer, start)]


def main():
    support = read("CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp")
    admit = function_body(support, "int AdmitConnection(")
    assert "steamLink->Resolve(a_ipv4, a_port" in admit, "AdmitConnection must resolve the tunnel guest by loopback port"
    assert "SteamPeerKey(" in admit, "tunnel guests must be limited on their Steam id key"
    peer = function_body(support, "int AdmitPeer(")
    assert "GetSteamId(a_steamId)" in peer, "tunnel guests must be ban-checked by Steam id, not the shared loopback address"

    runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
    assert re.search(r"\.steamLink\s*=\s*std::addressof\(steamIdentityLink\)", runtime), "admission context must carry the Steam identity link"

    maintenance = read("CO-SERVER/server-cpp/src/ServerMaintenance.inc")
    handshake = re.search(r"HandshakeExpired\(([^)]*)\)", maintenance)
    assert handshake, "maintenance must evaluate the handshake timeout"
    arguments = re.sub(r"\s+", " ", handshake[1])
    assert "joinedAtMonotonicSeconds" in arguments and "loopMonotonic" in arguments, "handshake timeout must use the steady clock"
    assert "Unix" not in arguments, "handshake timeout must not use the wall clock"

    fragments = read("co-core/src/transport/F4TMessageFragments.cpp")
    assert "min_element" in fragments, "a full fragment table must evict its oldest partial instead of refusing"

    afk = re.search(r"Afk::Evaluate\(([^;]*)\);", maintenance)
    assert afk, "maintenance must evaluate the AFK verdict"
    afk_arguments = re.sub(r"\s+", " ", afk[1])
    assert "lastActivityAtMonotonicSeconds" in afk_arguments and "loopMonotonic" in afk_arguments, "AFK must use the steady clock"
    assert "Unix" not in afk_arguments, "AFK must not compare wall-clock times"
    assert "lastActivityAtMonotonicSeconds = MonotonicSeconds()" in runtime, "activation must stamp activity on the steady clock"
    assert "lastActivityAtMonotonicSeconds = a_monotonicSeconds" in runtime, "player activity must stamp the steady clock"
    protocol = read("CO-SERVER/server-cpp/src/ServerProtocolImpl.inc")
    assert "Afk::NextExemption(session.afkExemption, exempt != packet->end() && exempt->get<bool>(), a_monotonicSeconds)" in protocol, (
        "the keepAlive exemption must be stamped on the steady clock"
    )
    assert "Unix" not in read("CO-SERVER/server-cpp/include/ServerAfk.h"), "the AFK decision must not take wall-clock inputs"

    tunnel_header = read("co-core/include/F4TSteamTunnel.h")
    tunnel_host = read("co-core/src/session/F4TSteamTunnelHost.cpp")
    assert "std::vector<std::jthread> admissionWorkers_" in tunnel_header, "an unstored admission worker must join, not terminate"
    assert "std::jthread worker([this, a_connection" in tunnel_host, "the admission worker local must join when push_back throws"
    assert "std::thread" not in tunnel_header and "std::thread" not in tunnel_host, "no raw std::thread in the host tunnel"

    stop = function_body(tunnel_host, "void SteamHostTunnel::Stop() noexcept")
    assert "StopState state;" in stop and "make_unique" not in stop and "StopDetached" not in stop, (
        "the destructor path must not allocate"
    )
    assert "void Stop() noexcept;" in tunnel_header and "FinishStop(StopState& a_state) noexcept" in tunnel_header
    assert "catch (...)" in function_body(tunnel_host, "void SteamHostTunnel::FinishStop("), "FinishStop must swallow errors so Stop cannot throw"
    assert "FinishStop(*owned)" in tunnel_host, "the slow stop work (joins, unbind, socket closes) must run from the completion"
    finish = function_body(tunnel_host, "void SteamHostTunnel::FinishStop(")
    detached = function_body(tunnel_host, "SteamHostTunnel::StopCompletion SteamHostTunnel::StopDetached(")
    assert "join" not in detached and "ReleaseBinding" not in detached and "closeConnection" not in detached, (
        "StopDetached runs under the caller's lock and must not block"
    )
    assert "join()" in finish and "ReleaseBinding(" in finish
    hold = function_body(tunnel_host, "void SteamHostTunnel::HoldForHostApproval(")
    assert "PendingAdmission{ a_plan," in hold and "std::move(a_plan)" not in hold, (
        "a failed push_back must still refuse with an intact plan, log callback included"
    )

    service = read("CO-BETA/plugin/src/F4TSteamTunnelService.cpp")
    assert "->Stop()" not in service.replace("guest->Stop()", ""), "the plugin must not stop the host tunnel with service.mutex held"
    assert service.count("host->StopDetached()") == 2, "StopTunnels and StopHost must both use the detached stop"
    stop_tunnels = function_body(service, "void StopTunnels(Service& a_service)", "\n\t\t}\n")
    assert stop_tunnels.index("StopDetached()") < stop_tunnels.index("hostStop.reset();") < stop_tunnels.index("JoinStoppedPump"), (
        "the exit path finishes the host stop inline, after releasing service.mutex"
    )
    stop_host = service.split("void StopHost()", 1)[1].split("bool HostRunning()", 1)[0]
    assert "hostStop.reset();" not in stop_host and "FinishStopInBackground(service, std::move(hostStop))" in stop_host, (
        "StopHost must not make the game thread wait for bind/unbind"
    )
    assert stop_host.index("StopDetached()") < stop_host.index("FinishStopInBackground(") < stop_host.index("JoinStoppedPump(service)")
    background = function_body(service, "void FinishStopInBackground(", "\n\t\t}\n")
    assert "std::jthread" in background and "stop.reset();" in background and "LogPendingEvents(a_service)" in background, (
        "the finisher must complete the stop and then log the TunnelStopped event"
    )
    start_host = service.split("Transport::TunnelError StartHost(", 1)[1]
    assert start_host.index("JoinFinishers(service, true)") < start_host.index("std::lock_guard lock{ service.mutex }"), (
        "a new host must wait for the previous stop to finish before taking service.mutex"
    )
    shutdown = service.split("void Shutdown()", 1)[1]
    assert shutdown.index("JoinFinishers(service, true)") < shutdown.index("service.host.reset()"), (
        "Shutdown must join pending finishers before the host they point at is destroyed"
    )
    assert re.search(r"StopTunnels\(\*this\);\s*JoinFinishers\(\*this, true\);", service), "the service destructor must join pending finishers"
    assert service.index("std::unique_ptr<Transport::SteamHostTunnel> host;") < service.index("std::vector<Finisher> finishers;"), (
        "finishers must be destroyed before the host"
    )
    assert "std::stop_callback abandon{ a_stop" in read("CO-BETA/plugin/src/F4TSteamJoinPrompt.cpp"), (
        "the host approval prompt must wake when its stop token fires"
    )

    print("audit3 co-core transport contracts verified")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, ValueError, OSError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
