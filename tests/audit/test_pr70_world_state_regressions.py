from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def require(condition: bool, message: str, failures: list[str]) -> None:
    if not condition:
        failures.append(message)


def main() -> int:
    failures: list[str] = []
    runtime = read("CO-SERVER/server-cpp/src/ServerRuntime.inc")
    runtime_support = read("CO-SERVER/server-cpp/src/ServerRuntimeSupport.cpp")
    persistence_gate = read("CO-SERVER/server-cpp/src/PersistenceGate.cpp")

    time_start = runtime.find(".setWorldTime =")
    time_end = runtime.find(".setWorldWeather =", time_start)
    time_block = runtime[time_start:time_end]
    weather_start = time_end
    weather_end = runtime.find(".worldMode =", weather_start)
    weather_block = runtime[weather_start:weather_end]
    helper_start = runtime.find("auto updateWorldState = ")
    helper_block = runtime[helper_start:runtime.find(chr(10) + chr(9) + "};", helper_start)] if helper_start >= 0 else ""

    def ordered(block: str) -> bool:
        if "updateWorldState(" in block:
            block = helper_block
        persist = block.find("UpdateServerWorldState")
        mutate = max(block.find("serverWorldState["), block.find("serverWorldState.update("))
        broadcast = block.find("BroadcastWorldPacket")
        return min(persist, mutate, broadcast) >= 0 and persist < mutate < broadcast
    require(
        0 <= time_start < time_end and ordered(time_block),
        "PR70-WORLD: time persistence must precede live mutation and broadcast",
        failures,
    )
    require(
        0 <= weather_start < weather_end and ordered(weather_block),
        "PR70-WORLD: weather persistence must precede live mutation and broadcast",
        failures,
    )

    send_start = runtime_support.find("void SendWorldStateToConnection(")
    send_end = runtime_support.find("\n\tvoid BroadcastWorldPacket", send_start)
    send_block = runtime_support[send_start:send_end]
    require("weatherConsoleArg" not in send_block, "PR70-WORLD: reconnect replay must not expose weatherConsoleArg", failures)
    require("weatherFormId" in send_block, "PR70-WORLD: reconnect replay must include weatherFormId", failures)
    require("weatherConsoleArg" not in persistence_gate[persistence_gate.find("SessionReadyDelivered"):], "PR70-WORLD: persistence replay must not expose weatherConsoleArg", failures)
    require("weatherFormId" in persistence_gate[persistence_gate.find("SessionReadyDelivered"):], "PR70-WORLD: persistence replay must include weatherFormId", failures)
    require("weatherConsoleArg" in weather_block, "PR70-WORLD: server persistence may retain weatherConsoleArg internally", failures)

    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        return 1

    print("PR 70 world-state regression contracts passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
