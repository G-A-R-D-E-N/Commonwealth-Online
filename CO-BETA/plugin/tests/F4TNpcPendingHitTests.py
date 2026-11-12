import argparse
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cmake", default=shutil.which("cmake"))
args = parser.parse_args()
if not args.cmake:
    parser.error("cmake is required")
source = (Path(__file__).resolve().parents[1] / "src/F4TNpcSyncHealth.cpp").read_text(encoding="utf-8")

def extract(anchor):
    start = source.index(anchor)
    begin = source.index("{", start)
    depth = 0
    for end in range(begin, len(source)):
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
            if depth == 0:
                return source[start:end + 1]
    raise ValueError(anchor)

with tempfile.TemporaryDirectory(prefix="co-npc-pending-hits-") as temporary:
    root = Path(temporary)
    constants = re.findall(r"constexpr (?:std::size_t|auto) kMaximum(?:PendingNpcHits|NpcHitsPerTick|NpcHitAge) = [^;]+;", source)
    if len(constants) != 3:
        raise ValueError("pending hit constants not found")
    (root / "pending-hit-constants.h").write_text("\n".join(constants) + "\n", encoding="utf-8")
    (root / "pending-hit-struct.h").write_text(extract("struct PendingNpcHit") + ";\n", encoding="utf-8")
    (root / "pending-hit-functions.h").write_text(
        extract("void ApplyPendingNpcHits()") + "\n" + extract("bool EnqueueNpcHit(") + "\n", encoding="utf-8")
    assert "std::atomic<std::uint64_t> g_capacityDroppedNpcHits{};" in source and "std::atomic<std::uint64_t> g_staleDroppedNpcHits{};" in source
    assert "++g_capacityDroppedNpcHits;" in source and "++g_staleDroppedNpcHits" in source
    assert "std::uint64_t CapacityDroppedNpcHitCount()" in source
    source = (Path(__file__).resolve().parents[1] / "src/F4TNpcSyncLootTake.cpp").read_text(encoding="utf-8")
    (root / "loot-request-loop.h").write_text(extract("for (const auto& queued : requests)") + "\n", encoding="utf-8")
    repository = Path(__file__).resolve().parents[3]
    loot_header = (repository / "co-core/include/F4TNpcLootSync.h").read_text(encoding="utf-8")
    margin = re.search(r"inline constexpr std::int64_t TakeHostRefusalMarginMilliseconds = [^;]+;", loot_header)
    if not margin:
        raise ValueError("loot refusal margin not found")
    source = (repository / "co-core/src/npc/F4TNpcLootSync.cpp").read_text(encoding="utf-8")
    (root / "loot-expiry.h").write_text(margin.group() + "\n" + extract("bool TakeRequestExpired(") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TReplicationRuntime.cpp").read_text(encoding="utf-8")
    (root / "runtime-health-call.h").write_text(extract("const auto runGameplay = ") + ";\nF4T::NpcSync::Update(runGameplay);\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TReplicationRuntimePublish.cpp").read_text(encoding="utf-8")
    (root / "publish-hits.h").write_text(extract("void SendPendingCombatHits(") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TPlayerAnimEventSync.cpp").read_text(encoding="utf-8")
    (root / "animation-event.h").write_text(extract("struct OutboundEvent") + ";\nstd::deque<OutboundEvent> g_outbound;\n" + extract("void FlushOutbound()") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TProjectileAim.cpp").read_text(encoding="utf-8")
    (root / "projectile-aim.h").write_text(extract("struct OutboundAim") + ";\nstd::deque<OutboundAim> g_aimOutbound;\n" + extract("void FlushOutbound()") + "\n" + extract("void DiscardOutbound()") + "\n" + extract("void QueueAimForSend(") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TFurnitureSeatSync.cpp").read_text(encoding="utf-8")
    (root / "furniture-publish.h").write_text(extract("void PublishLocal()") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TSessionLaunchSaves.cpp").read_text(encoding="utf-8")
    (root / "first-join-save.h").write_text(extract("std::optional<F4T::FirstJoinAttempt::Scope> FirstJoinScope()") + "\n" + extract("std::optional<bool> QueueProfileSave()") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TSessionLaunchOnboarding.cpp").read_text(encoding="utf-8")
    (root / "first-join-start.h").write_text(extract("F4T::SessionLaunch::LaunchOutcome StartFirstTimeJoin(") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TSessionSaveQueue.cpp").read_text(encoding="utf-8")
    (root / "save-queue-note.h").write_text(extract("void NoteEntered(") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8")
    (root / "stage-first-join.h").write_text(extract("bool StageApprovedFirstJoin(") + "\n", encoding="utf-8")
    (root / "save-attestation.h").write_text(
        extract("bool RecordActiveSave(const std::string& a_serverKey, const std::string& a_saveFile, const std::string& a_sha256Hex, bool a_announce = true, std::optional<std::uint64_t> a_generation = std::nullopt)") + "\n" +
        extract("bool RecordActiveSave(const std::string& a_serverKey, const std::string& a_saveFile)") + "\n" +
        extract("bool AttestFirstJoinSave(") + "\n", encoding="utf-8")
    source = (Path(__file__).resolve().parents[1] / "src/F4TSurvivalEnforcer.cpp").read_text(encoding="utf-8")
    assert "REL::ID objectId{ 303410, 2698073, 2698073" in source  # E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md
    assert "REL::ID transitionId{ 657344, 2233057, 2233057" in source  # E:/RE-work/F4RR-co/RE-Work/subsystems/commonwealth-online-re/co245-settings/README.md
    (root / "survival-notice.h").write_text(extract("std::string RequiredDifficultyName(") + "\n" + extract("void NotifyUnavailable(") + "\n" + extract("void ClearNotice()") + "\n", encoding="utf-8")
    (root / "survival-tick.h").write_text(extract("bool Tick(") + "\n", encoding="utf-8")
    shutil.copyfile(Path(__file__).with_suffix(".cpp"), root / "test.cpp")
    shutil.copyfile(Path(__file__).with_name("F4TSurvivalEnforcerTests.cpp"), root / "survival-test.cpp")
    shutil.copyfile(Path(__file__).with_name("F4TSaveQueueEntryTests.cpp"), root / "save-queue-test.cpp")
    shutil.copyfile(repository / "CO-BETA/plugin/include/F4TSessionSaveGuard.h", root / "F4TSessionSaveGuard.h")
    shutil.copyfile(repository / "CO-BETA/plugin/include/F4TSessionSaveGuardPolicy.h", root / "F4TSessionSaveGuardPolicy.h")
    shutil.copyfile(repository / "CO-BETA/plugin/include/F4TSurvivalPolicyClient.h", root / "F4TSurvivalPolicyClient.h")
    shutil.copyfile(repository / "CO-BETA/plugin/include/F4TFirstJoinAttempt.h", root / "F4TFirstJoinAttempt.h")
    shutil.copyfile(repository / "co-core/include/F4TSurvivalPolicyState.h", root / "F4TSurvivalPolicyState.h")
    (root / "CMakeLists.txt").write_text('''cmake_minimum_required(VERSION 3.20)
project(NpcPendingHits LANGUAGES CXX)
add_executable(npc-pending-hits test.cpp)
add_executable(survival-enforcer survival-test.cpp)
add_executable(save-queue-entry save-queue-test.cpp)
target_compile_features(npc-pending-hits PRIVATE cxx_std_20)
target_compile_features(survival-enforcer PRIVATE cxx_std_20)
target_compile_features(save-queue-entry PRIVATE cxx_std_20)
if(MSVC)
  target_compile_options(npc-pending-hits PRIVATE /W4 /WX /UNDEBUG)
  target_compile_options(survival-enforcer PRIVATE /W4 /WX /UNDEBUG)
  target_compile_options(save-queue-entry PRIVATE /W4 /WX /UNDEBUG)
else()
  target_compile_options(npc-pending-hits PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  target_compile_options(survival-enforcer PRIVATE -Wall -Wextra -Werror -UNDEBUG)
  target_compile_options(save-queue-entry PRIVATE -Wall -Wextra -Werror -UNDEBUG)
endif()
''', encoding="utf-8")
    for command in ([args.cmake, "-S", str(root), "-B", str(root / "build")],
                    [args.cmake, "--build", str(root / "build"), "--config", "Release"]):
        subprocess.run(command, check=True, timeout=120)
    executable = next(path for path in (root / "build").rglob("npc-pending-hits*")
                      if path.name in ("npc-pending-hits", "npc-pending-hits.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=15)
    executable = next(path for path in (root / "build").rglob("save-queue-entry*")
                      if path.name in ("save-queue-entry", "save-queue-entry.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=15)
    executable = next(path for path in (root / "build").rglob("survival-enforcer*")
                      if path.name in ("survival-enforcer", "survival-enforcer.exe") and path.is_file())
    subprocess.run([str(executable)], check=True, timeout=15)
print("NPC pending hits: session, reconnect, retry and disconnected cases PASS")
