from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


probe_h = (PLUGIN / "include/F4TIntroQuestProbe.h").read_text(encoding="utf-8")
probe_cpp = (PLUGIN / "src/F4TIntroQuestProbe.cpp").read_text(encoding="utf-8")
SESSION_SOURCES = [
    PLUGIN / "src/F4TSessionLaunchSaves.cpp",
    PLUGIN / "src/F4TSessionLaunchState.cpp",
    PLUGIN / "src/F4TSessionLaunchWorld.cpp",
    PLUGIN / "src/F4TSessionLaunchOnboarding.cpp",
    PLUGIN / "src/F4TSessionLaunch.cpp",
]
launch = "".join(path.read_text(encoding="utf-8") for path in SESSION_SOURCES)
setup = (PLUGIN / "src/F4TSessionCharacterSetupFlow.cpp").read_text(encoding="utf-8")
setup_h = (PLUGIN / "include/F4TSessionCharacterSetupFlow.h").read_text(encoding="utf-8")
suppress = (PLUGIN / "src/F4TNpcSyncSuppress.cpp").read_text(encoding="utf-8")
puppets = (PLUGIN / "src/F4TNpcSyncPuppets.cpp").read_text(encoding="utf-8")
gns = (PLUGIN / "src/F4TNetworkingGns.cpp").read_text(encoding="utf-8")

check("BeginFirstJoinTrace" in probe_h, "probe header must expose BeginFirstJoinTrace")
check("EndFirstJoinTrace" in probe_h, "probe header must expose EndFirstJoinTrace")
check("Tick" in probe_h, "probe header must expose Tick")

for form_id in ("0x0001ED86", "0x0001CC2A", "0x0001ED62", "0x000179F3", "0x000229E5"):
    check(form_id in probe_cpp, f"probe must watch quest form id {form_id}")
for quest in ("Tutorial", "MQ03", "MQ102", "DialogueConcordArea", "MQ103"):
    check(f'"{quest}"' in probe_cpp, f"probe must watch quest editor id {quest}")

check("GetFormByID<RE::TESQuest>" in probe_cpp, "probe must look quests up by form id")
check("GetEventSource" not in probe_cpp, "probe must not register event sources; polling is the only probe mechanism")
check("TESQuestStageEvent" not in probe_cpp, "probe must not sink TESQuestStageEvent")
check("TESQuestStartStopEvent" not in probe_cpp, "probe must not sink TESQuestStartStopEvent")
check("RegisterSink" not in probe_cpp, "probe must not register event sinks")
check("IntroQuestProbe" in probe_cpp, "probe logs must use IntroQuestProbe component")

check('#include "F4TIntroQuestProbe.h"' in launch, "SessionLaunch must include IntroQuestProbe")
check("IntroQuestProbe::BeginFirstJoinTrace" in launch, "first-join queue must begin the quest trace")
check("IntroQuestProbe::EndFirstJoinTrace" in launch, "first-join clear must end the quest trace")
check("IntroQuestProbe::Tick" in launch, "first-join poll must tick the quest probe")

for phase in ("StartingNewGame", "VanillaIntro", "WaitingForContinue", "WaitingForPodExit",
              "WaitingForGameplayWorldReady", "WaitingForProfileBind"):
    check(f"CharGenPhase::{phase}" in launch, f"first-join must use the {phase} phase")
check("WaitingForIntroPrime" not in launch and "WaitingForQuestComplete" not in launch,
      "legacy IntroPrime / LooksMenu phases must be gone")
check("case F4SE::MessagingInterface::kNewGame:" in launch, "OnF4SEMessage must observe kNewGame")
check("coc " not in launch and "coc Vault111Cryo" not in launch, "the coc Vault111Cryo path must be removed")
check("SessionIntroPrime" not in launch, "SessionLaunch must not reference the deleted IntroPrime module")
check("CharacterSetupController" not in launch, "SessionLaunch must not reference the deleted controller")

check("BGSCodeObj" in setup and "StartNewGame" in setup, "new game must start through BGSCodeObj.StartNewGame")
check('"cqf CO_CharacterSetupQuest BeginVanillaOnboarding"' in setup, "onboarding must begin via BeginVanillaOnboarding")
check('"cqf CO_CharacterSetupQuest ContinueToVault"' in setup, "Continue must run ContinueToVault")
check('"cqf CO_CharacterSetupQuest AbortOnboarding"' in setup, "failed joins must run AbortOnboarding")
check("ShowAlert(" in setup and "durationMs = 0" in setup, "Continue prompt must be a persistent CO alert")
check('"Continue to Vault 111 to join the server."' in setup, "Continue prompt message must match the brief")
check("kMq102FormId = 0x0001CC2A" in setup, "pod-exit signal must look up MQ102 by 0x0001CC2A")
check("kMq102PodExitStage = 2" in setup, "pod-exit signal must be MQ102 stage 2")
check("ShowContinueAlert" in setup_h and "ContinueToVault" in setup_h and "IsPodExitSignal" in setup_h,
      "setup flow header must expose the vanilla-start entry points")
check("coc " not in setup, "setup flow must not use coc")
check(re.search(r"SetStage\s*\(\s*5000\s*\)", launch + setup) is None, "must never call SetStage(5000)")
check("setstage MQ101 5000" not in (launch + setup).lower(), "must never console-setstage MQ101 5000")
check('"Alias_PrestonGarveyAlias"' in probe_cpp, "probe must resolve Preston through the DialogueConcordArea alias name")
check("GetAliasedRef" in probe_cpp, "probe must resolve Preston through GetAliasedRef")
check("RE::TESNPC" not in probe_cpp, "probe must not look Preston up by a guessed NPC form id")
check("%H:%M:%S" in probe_cpp, "watched quest stage changes must be logged with a timestamp")
check('"stage-change"' in probe_cpp and '"cell-change"' in probe_cpp,
      "probe must log Preston on stage change and on cell change")
check("server positionCorrection received" in probe_cpp, "probe must log every received server positionCorrection")
check("LogServerPositionCorrection(text)" in gns, "server positionCorrection receipt must log through the probe")
check("SanitizeLogText(a_payload, 512)" in probe_cpp, "server positionCorrection payload must not truncate at the 160-character default")
check('if (!g_pendingSpawn && BeginSpawn(state, a_player)) {' in puppets,
      "puppet-spawn decision must be guarded by BeginSpawn returning true")
check(puppets.index('LogNpcDecision("puppet-spawn"') > puppets.index("BeginSpawn(state, a_player)"),
      "puppet-spawn decision must log after BeginSpawn, not before it")
check('LogNpcDecision("suppress"' in suppress and 'LogNpcDecision("unsuppress"' in suppress,
      "suppress and unsuppress decisions must log through the probe")
check('LogNpcDecision("puppet-spawn"' in puppets and "puppet-death-applied" in puppets,
      "puppet spawn and death decisions must log through the probe")
for name, text in (("F4TNpcSyncSuppress.cpp", suppress), ("F4TNpcSyncPuppets.cpp", puppets), ("F4TNetworkingGns.cpp", gns)):
    check('#include "F4TIntroQuestProbe.h"' in text, f"{name} must include IntroQuestProbe")
for name, text in (("F4TIntroQuestProbe.cpp", probe_cpp), ("F4TNpcSyncSuppress.cpp", suppress),
                   ("F4TNpcSyncPuppets.cpp", puppets), ("F4TNetworkingGns.cpp", gns)):
    check(len(text.splitlines()) < 750, f"{name} must stay under 750 lines")


if failures:
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    sys.exit(1)

print("F4TIntroQuestProbeContract.py: ok")
