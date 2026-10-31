from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

VOICE_FILES = (
    "include/F4TVoiceControl.h",
    "src/F4TVoiceControl.cpp",
    "include/F4TGnsVoice.h",
    "src/F4TGnsVoice.cpp",
)

FORBIDDEN = (r"enable", r"feature", r"gate", r"flag", r"#if")

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def read_code(relative):
    return strip_comments((PLUGIN / relative).read_text(encoding="utf-8"))


def brace_body(text, start):
    brace = text.index("{", start)
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index + 1]
    raise AssertionError("unterminated body")


def function_body(code, signature):
    return brace_body(code, code.index(signature))


def regex_in_order(text, *patterns):
    position = 0
    for pattern in patterns:
        found = re.compile(pattern, re.S).search(text, position)
        if not found:
            return False
        position = found.end()
    return True


shim = read_code("src/F4TGnsWinsockShim.cpp")
identity = read_code("src/F4TClientIdentity.cpp")
replication = read_code("src/F4TReplicationRuntime.cpp")
glue = read_code("src/F4TGnsVoice.cpp")
control = read_code("src/F4TVoiceControl.cpp")

recv = function_body(shim, "int Recv(SOCKET a_socket")
check(regex_in_order(recv,
                     r"payload\.size\(\)\) \{\n\t+continue;\n\t+\}\n\n\t+if \(F4T::GnsVoice::TryHandleInbound\(state,[^\n]*\)\) \{ continue; \}\n",
                     r"F4T::ComponentReceive::ClassifyReceive\(\*state,"),
      "the shim Recv must hand each message to the voice inbound hook as a statement that continues, directly after the message filter and before ClassifyReceive")
check('#include "F4TGnsVoice.h"' in shim, "the shim must include the voice glue header")

check(regex_in_order(identity,
                     r'metadata\.capabilities\.emplace_back\("session-lifecycle-v1"\);',
                     r'\n\t\tmetadata\.capabilities\.emplace_back\("voice-v1"\);'),
      "the hello capabilities must add voice-v1 unconditionally next to session-lifecycle-v1")
check(identity.count('"voice-v1"') == 1, "voice-v1 must be advertised in exactly one place in the client identity")

tick = function_body(replication, "void Tick()")
check(regex_in_order(tick, r"F4T::ObjectSync::Tick\(nowMs\);", r"if \(connected\) \{\s*F4T::GnsVoice::PumpControl\("),
      "the replication tick must pump the voice control sync after the object sync tick, only while connected")
check('#include "F4TGnsVoice.h"' in replication, "the replication runtime must include the voice glue header")

inbound = function_body(glue, "bool TryHandleInbound(")
check(regex_in_order(inbound,
                     r"if \(!F4T::Voice::HasVoicePrefix\(bytes\)\) \{\s*return false;\s*\}",
                     r"receivable = g_control\.CanReceiveVoice\(\);",
                     r"if \(!receivable\) \{\s*\+\+g_dropped;\s*return true;\s*\}",
                     r"DecodeVoicePacket\(bytes, F4T::Voice::VoiceDirection::ServerToClient, packet\) != F4T::Voice::VoiceDecodeResult::Ok\) \{\s*\+\+g_dropped;\s*return true;\s*\}"),
      "the inbound hook must return false for non-COV1 datagrams, count and consume unnegotiated or undecodable COV1 datagrams, and decode in the server direction")
check("SendPacketBytes" not in inbound and "RE::" not in inbound,
      "the inbound voice hook runs on the receive thread and must not send JSON or touch the engine")
check(regex_in_order(inbound,
                     r"\{\s*const std::scoped_lock lock\(g_sinkMutex\);\s*sink = g_sink;\s*\}",
                     r"if \(!sink\) \{\s*\+\+g_dropped;\s*return true;\s*\}",
                     r"sink\(packet\);\s*\+\+g_sunk;"),
      "the inbound voice hook must copy the sink under g_sinkMutex and call it after the lock is released")
check(regex_in_order(inbound,
                     r"const std::scoped_lock controlLock\(g_controlMutex\);\s*const std::scoped_lock socketLock\(g_socketMutex\);\s*if \(g_socket\.lock\(\) != a_state\) \{\s*g_socket = a_state;\s*g_control\.Reset\(\);\s*\}"),
      "a datagram on a new socket must rebind and reset the control sync inside one control-mutex section, so a voice send cannot pass the gate with the previous session's ack on the new socket")

send = function_body(glue, "bool SendVoicePacket(")
check(regex_in_order(send,
                     r"const std::scoped_lock controlLock\(g_controlMutex\);\s*if \(!g_control\.CanSendVoice\(\)\) \{\s*\+\+g_sendFailures;\s*return false;\s*\}",
                     r"std::scoped_lock bridgeLock\(state->bridgeMutex\)",
                     r"api->send\(",
                     r"F4T::GnsProtocolAdapter::Delivery::UnreliableSequenced"),
      "the voice send must check the control gate under the control mutex, then send unreliable under the bridge mutex")
check("state->closed.load" in send and "state->connected.load" in send,
      "the voice send must check connected and closed before sending")
check("ReliableOrdered" not in glue, "voice must never be sent on the reliable lane")
adapter = strip_comments((ROOT / "co-core/include/F4TGnsProtocolAdapter.h").read_text(encoding="utf-8"))
check("UnreliableSequenced = 0" in adapter, "the unreliable delivery lane must stay wire value 0")
check("kSendDropped" not in send and "kSendBackpressure" not in send,
      "a voice packet the bridge did not send must not count as sent")
check(regex_in_order(send, r"result != F4T::GnsWinsockShimState::kSendSent", r"\+\+g_sent"),
      "only kSendSent may count a voice packet as sent, checked before the sent counter")

check('constexpr double kSendIntervalSeconds = 1.0;' in control,
      "the control sync must pace snapshots at one per second")
check(regex_in_order(function_body(control, "std::optional<std::string> ControlSync::Poll("),
                     r"m_ackedRevision == m_revision", r"kSendIntervalSeconds"),
      "the control sync poll must stop on acknowledged state before applying the pace")

pump = function_body(glue, "void PumpControl(")
check(regex_in_order(pump,
                     r"\A\{\s*const auto state = BoundSocket\(\);\s*if \(!state\) \{\s*return;\s*\}\s*bool protocolReady = false;\s*\{\s*const std::scoped_lock protocolLock\(state->protocolMutex\);\s*protocolReady = state->protocol\.IsReady\(\);\s*\}\s*if \(!protocolReady\) \{\s*return;\s*\}\s*std::optional<std::string> snapshot;\s*\{\s*const std::scoped_lock lock\(g_controlMutex\);\s*snapshot = g_control\.Poll\(a_now\);"),
      "PumpControl must read protocol.IsReady() under state->protocolMutex and return before g_control.Poll, so no snapshot is sent before sessionReady")

main_cpp = read_code("src/main.cpp")
check(regex_in_order(main_cpp,
                     r"case F4SE::MessagingInterface::kGameDataReady:",
                     r"F4T::UIClient::OnGameDataReady\(\);\s*F4T::VoiceRuntime::Start\(\);"),
      "main.cpp must start the voice runtime in the kGameDataReady case directly after UIClient::OnGameDataReady")
check(regex_in_order(main_cpp,
                     r"case F4SE::MessagingInterface::kGameDataReady:",
                     r"F4T::UIClient::OnGameDataReady\(\);(?:(?!break;).)*?F4T::RangedCombatCapture::Initialize\(\);(?:\s*F4T::FrameLimiter::Install\(\);)?\s*break;"),
      "main.cpp must initialize ranged combat capture in the kGameDataReady case after UIClient::OnGameDataReady")

inbound_cpp = read_code("src/F4TNetworkingInbound.cpp")
check('#include "F4TGnsVoice.h"' in inbound_cpp, "F4TNetworkingInbound.cpp must include the voice glue header")
check(regex_in_order(function_body(inbound_cpp, "void HandleWelcomePacket("),
                     r"F4T::ObjectSync::NoteWelcome\(a_packet\);",
                     r"F4T::GnsVoice::NoteWelcome\(a_packet\);"),
      "HandleWelcomePacket must hand the welcome to the voice glue directly after the object sync")
check(regex_in_order(inbound_cpp,
                     r"if \(F4T::Chat::TryHandleChatPacket\(\*packetType, packet\)\) \{\s*return;\s*\}",
                     r"if \(F4T::GnsVoice::TryHandleControlPacket\(\*packetType, packet\)\) \{\s*return;\s*\}"),
      "the voice control packets must be dispatched directly after the chat packets and return")

check(regex_in_order(function_body(glue, "bool TryHandleControlPacket("),
                     r'a_type == "voiceStateAck"\)\s*\{\s*OnStateAck\(a_packet\);\s*return true;',
                     r'a_type == "voiceTalk"\)\s*\{\s*OnTalk\(a_packet\);\s*return true;',
                     r'a_type == "voiceNotice"\)\s*\{\s*OnNotice\(a_packet\);\s*return true;',
                     r"return false;\s*\}\s*\Z"),
      "TryHandleControlPacket must map voiceStateAck, voiceTalk and voiceNotice to OnStateAck, OnTalk and OnNotice and return false otherwise")

runtime_files = [path.relative_to(PLUGIN).as_posix() for path in sorted(PLUGIN.glob("src/F4TVoiceRuntime*.cpp"))]
runtime_files += [path.relative_to(PLUGIN).as_posix() for path in sorted(PLUGIN.glob("include/F4TVoiceRuntime*.h"))]
runtime_files += ["tests/F4TVoiceRuntimeLogicTests.cpp"]
runtime_cpp = read_code("src/F4TVoiceRuntime.cpp")
runtime = "\n".join(read_code(relative) for relative in runtime_files if relative.endswith(".cpp"))
check(regex_in_order(runtime,
                     r"F4T::GnsVoice::RegisterVoiceSink\(",
                     r"F4T::GnsVoice::RegisterControlEventHandler\(",
                     r"F4T::VoiceUi::SetActionHandler\(",
                     r"F4T::F4SECapabilities::AddPermanentTask\(Tick\);"),
      "the voice runtime must register the voice sink, the control handler and the UI action handler, then add its permanent task")
check("SetCanSend(view.canSend);" in runtime, "the voice runtime must pass the control view canSend to the transmitter")
check(regex_in_order(runtime,
                     r"GetWindowThreadProcessId\(GetForegroundWindow\(\)",
                     r"const bool inputAllowed = [^;]*GetCurrentProcessId\(\)[^;]*IsCustomMenuInputCaptured\(\)[^;]*IsChatInputFocused\(\)[^;]*!F4T::SessionWorldState::IsMenuOpen\(RE::Console::MENU_NAME\.data\(\)\)[^;]*;",
                     r"SetInputAllowed\(inputAllowed\);"),
      "the voice input guard must combine the foreground process, the custom menu capture, the chat focus and the open console menu before SetInputAllowed")
check(re.search(r"SetDesiredControl\(\s*(F4T::Voice::)?DesiredControl\(", runtime) is not None,
      "the voice runtime must publish SetDesiredControl(DesiredControl(...)) every tick")
check("CoInitializeEx(" in function_body(runtime_cpp, "void WatchDevices(") and runtime.count("CoInitializeEx(") == 1,
      "CoInitializeEx must appear only inside the device watcher thread function")
check(re.search(r"bool IsChatInputFocused\(\)\s*\{", read_code("src/F4TTextEntryMode.cpp")) is not None,
      "F4TTextEntryMode.cpp must define IsChatInputFocused")
for relative in runtime_files:
    text = (PLUGIN / relative).read_text(encoding="utf-8")
    check("//" not in text and "/*" not in text, f"{relative} must not contain comments")
    for token in ("kEnable", "enable", "feature", "flag", "#if"):
        check(not re.search(token, text, re.IGNORECASE), f"{relative} must not contain the token {token}")
for relative in ("src/main.cpp", "src/F4TNetworkingInbound.cpp"):
    text = (PLUGIN / relative).read_text(encoding="utf-8")
    for line in text.splitlines():
        if "voice" not in line.lower():
            continue
        for token in FORBIDDEN:
            check(not re.search(token, line, re.IGNORECASE), f"{relative} must not gate voice: {line.strip()}")

for relative in VOICE_FILES:
    text = (PLUGIN / relative).read_text(encoding="utf-8")
    check("//" not in text and "/*" not in text, f"{relative} must not contain comments")
    for token in FORBIDDEN:
        check(not re.search(token, text, re.IGNORECASE), f"{relative} must not contain the token {token}")
for relative in ("src/F4TGnsWinsockShim.cpp", "src/F4TClientIdentity.cpp", "src/F4TReplicationRuntime.cpp", "src/main.cpp", "src/F4TNetworkingInbound.cpp"):
    text = (PLUGIN / relative).read_text(encoding="utf-8")
    for line in text.splitlines():
        if "voice" not in line.lower():
            continue
        for token in FORBIDDEN:
            check(not re.search(token, line, re.IGNORECASE), f"{relative} must not gate voice: {line.strip()}")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TGnsVoiceContract: all checks passed.")
