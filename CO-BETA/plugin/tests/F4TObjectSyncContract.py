from pathlib import Path
import re
import sys

PLUGIN = Path(__file__).resolve().parents[1]
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def read(path):
    return re.sub(r"//[^\n]*", "", (PLUGIN / path).read_text(encoding="utf-8"))


def lock_scope(text, index):
    depth = 0
    for position in range(index, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            if depth == 0:
                return text[index:position]
            depth -= 1
    return text[index:]


def function_body(text, signature):
    start = text.index("{", text.index(signature))
    depth = 0
    for position in range(start, len(text)):
        if text[position] == "{":
            depth += 1
        elif text[position] == "}":
            depth -= 1
            if depth == 0:
                return text[start:position + 1]
    return text[start:]


MUTATIONS = ("ActivateRef", "SetLocked", "RegisterSink", "GetEventSource", "SendObjectActivateRequest", "SendObjectLockRequest")


header = read("include/F4TObjectSync.h")
sync = read("src/F4TObjectSync.cpp")
network = read("src/F4TNetworkingObjects.cpp")
inbound = read("src/F4TNetworkingInbound.cpp")
check("kEnableObjectSync" not in header and "kCoTestFeatures" not in header, "object sync must be unconditional, not behind the test-feature switch")
check("kMaximumQueue = 64;" in header, "queue limit must be 64")
for queue in ("g_captures", "g_inbound"):
    check(re.search(rf"if \({queue}\.size\(\) >= kMaximumQueue\)\s*\{{\s*g_dropped \+= 1;\s*return[^;]*;\s*\}}\s*{queue}\.push_back", sync), f"{queue} must cap, count and drop before enqueue")
check("RE::" not in network, "network thread must not call engine")
for normalizer in ("NormalizeObjectState", "NormalizeObjectStateSnapshot", "NormalizeObjectStateRejected"):
    check(network.index(normalizer + "(") < network.index("EnqueuePacket("), f"{normalizer} before enqueue")
for normalizer in ("NormalizeActivateRequest", "NormalizeLockRequest"):
    check(re.search(normalizer + r"\(a_request\);\s*return normalized && Detail::SendPacketBytes", network), f"{normalizer} before send")
check("g_applyGuard.Consume(state, now)" in sync, "capture must consume replay guard")
check(re.search(r"auto\* extras = a_ref->extraList\.get\(\);", sync), "extra-data pointer must be read once")
check(re.search(r"a_state\.kind == Kind::Door && extras && extras->GetByType\(RE::EXTRA_DATA_TYPE::kTeleport\)\) \{\s*return false;\s*\}", sync), "teleport doors must be skipped through a guarded extra-data pointer")
check(re.search(r"if \(auto\* refLock = extras \? a_ref->GetLock\(\) : nullptr\)", sync), "lock read must not run without an extra-data list")
check("constexpr bool kEnableDoorOpenState = true;" in sync, "door open-state sync must be on")
check("kMaximumTrackedRefs = 32;" in header and "g_tracked.size() >= kMaximumTrackedRefs" in sync, "poll tracking must be bounded")
check("g_nextPoll = a_nowMs + 1000;" in sync, "poll at one Hz")
check("Kind::Runtime" in sync and "IsValidRecordIdentity" in sync, "stable identity required")

guard = "const std::scoped_lock lock(g_mutex);"
check("std::recursive_mutex" not in sync, "object sync lock must not be recursive")
scopes = list(re.finditer(re.escape(guard), sync))
check(len(scopes) == 6, "every shared-state entry point must take the lock")
for scope in scopes:
    body = lock_scope(sync, scope.start())
    for forbidden in MUTATIONS:
        check(forbidden not in body, f"lock must not be held across {forbidden}")
for signature in ("void Apply(", "bool Observe(", "void CanonicalizeState(", "Json Request(", "RE::BSEventNotifyControl ProcessEvent("):
    body = function_body(sync, signature)
    for forbidden in MUTATIONS:
        check(forbidden not in body, f"{signature.strip()} runs under the lock and must not call {forbidden}")
tick_lock = next((scope for scope in scopes if "g_applyGuard.Expire" in lock_scope(sync, scope.start())), None)
check(tick_lock is not None, "Tick must take the lock")
if tick_lock is not None:
    tick_end = tick_lock.start() + len(lock_scope(sync, tick_lock.start()))
    check(sync.index("g_applying.store(true)") > tick_end, "engine mutation must run after the lock is released")
    check(sync.index("SendObjectActivateRequest(") > tick_end, "sends must run after the lock is released")
check(re.search(r"if \(!mutations\.empty\(\)\) \{\s*g_applying\.store\(true\);([\s\S]{0,400}?)refLock->SetLocked\(mutation\.locked\);([\s\S]{0,400}?)ref->ActivateRef\(player, nullptr, 1, false, false, false\);([\s\S]{0,80}?)g_applying\.store\(false\);", sync), "deferred mutation loop must set and clear the apply guard around the queued writes")
tick = sync[sync.index("void Tick("):sync.index("void Reset()")]
for send in ("SendObjectActivateRequest", "SendObjectLockRequest"):
    check(send in tick and sync.count(send) == 1, f"{send} only from Tick")
apply_body = function_body(sync, "void Apply(")
mutation_body = function_body(sync, "struct Mutation")
pending_body = function_body(sync, "struct Pending")
check("a_mutations" not in apply_body and "mutations" not in apply_body, "Apply must not queue engine writes directly")
check("State expected;" in pending_body and "bool liveLocked{};" in pending_body and "std::uint32_t activations{};" in pending_body, "Pending must hold the last expected state, the live snapshot and the activation count")
check("std::map<std::uint32_t, Pending> pending;" in tick, "Tick must keep a per-ref pending map for the batch")
check("Apply(packet, now, player, pending, true)" in tick and "Apply(state, now, player, pending, false)" in tick, "both drains must record into the pending map with the tick's player, and only the live drain may carry an activation")
pending_loop = tick.index("for (const auto& [refId, entry] : pending)")
check(tick.index("while (!g_inbound.empty())") < pending_loop, "mutations must be emitted once per reference after the drain, not per packet")
check(pending_loop < tick.index("while (!g_captures.empty())"), "the pending writes must be decided before the capture loop")
check(tick.index("g_applyGuard.Record(expected, now)") < tick.index("g_applyGuard.Record(state, now)"), "a pending write must record its guard entry before a capture records its own, because Record replaces the entry per object")
check(tick.index("auto* player = RE::PlayerCharacter::GetSingleton();") < tick.index(guard), "Tick must resolve the player once before taking the lock")
refusal = re.search(r"const bool activation = expected\.kind == Kind::Activator[^;]*;\s*if \(!a_player && activation && \(a_live \|\| expected\.kind == Kind::Door\)\) \{\s*return;\s*\}", apply_body)
check(refusal is not None, "Apply must refuse a live activation-bearing packet, or any door activation, without a player, and must not refuse a snapshot activator entry: the snapshot is already popped, so its lock state has to be applied")
if refusal is not None:
    check(refusal.start() < apply_body.index("g_revisions[*key] = revision;"), "the refusal without a player must precede the revision write so the packet can be retried")
check(re.search(r"if \(!RE::TESForm::GetFormByID<RE::TESObjectREFR>\(refId\) \|\| \(activations != 0 && !player\)\) \{\s*if \(entry\.attempts >= kMaximumWriteRetries\) \{", tick), "a reference the write loop cannot resolve must stay pending for a bounded number of ticks instead of dropping the write")
check(re.search(r"queued->second\.expected = expected;\s*if \(a_live && expected\.kind == Kind::Activator\) \{\s*queued->second\.activations \+= 1;", apply_body), "a repeated reference in one batch must collapse onto the pending entry, and a snapshot replay must not add an activation")
check(re.search(r"const std::uint32_t activations = door \?\s*\(entry\.liveOpen != expected\.open \? 1u : 0u\) :\s*entry\.activations;", tick), "a door must collapse to one activation from the live snapshot to the last expected state while an activator keeps one per packet")
check("const bool lockWrite = expected.hasLock && entry.liveHasLock && entry.liveLocked != expected.locked;" in tick, "the lock decision must use the pending live snapshot, not engine state re-read per packet")
check(re.search(r"if \(\(lockWrite \|\| expected\.kind != Kind::Activator\) && !g_applyGuard\.Record\(expected, now\)\) \{\s*continue;\s*\}", tick), "a state-changing write must record the replay guard, and an activation-only write must not, because that entry would swallow a genuine local activation of the same reference")
check(re.search(r"if \(lockWrite && !expected\.locked\) \{\s*mutations\.push_back\(\{ refId, false, false \}\);\s*\}\s*for \(std::uint32_t index = 0; index < activations; index \+= 1\) \{\s*mutations\.push_back\(\{ refId, true, false \}\);\s*\}\s*if \(lockWrite && expected\.locked\) \{\s*mutations\.push_back\(\{ refId, false, true \}\);", tick), "an unlock must be written before the activation and a lock after it")
check(tick.index("CanonicalizeState(state);") < tick.index("g_applyGuard.Consume(state, now)"), "capture must canonicalize before consuming the replay guard")
check(re.search(r"CanonicalizeState\(state\);\s*if \(g_applyGuard\.Consume\(state, now\)\) \{\s*continue;\s*\}", tick), "the canonical state is what the replay guard is compared against")
check("std::uint32_t refId{};" in mutation_body, "Mutation must carry the reference id")
check("RE::TESObjectREFR*" not in mutation_body and "RE::REFR_LOCK*" not in mutation_body and "RE::PlayerCharacter*" not in mutation_body, "Mutation must not hold raw engine pointers")
check(re.search(r"auto\* ref = RE::TESForm::GetFormByID<RE::TESObjectREFR>\(mutation\.refId\);\s*if \(!ref\) \{\s*continue;\s*\}", tick), "the write loop must re-resolve the reference by id and skip an unresolved one")
check(re.search(r"if \(auto\* refLock = ref->GetLock\(\)\) \{\s*refLock->SetLocked\(mutation\.locked\);\s*\}", tick), "the write loop must re-resolve the lock, apply only the locked flag and skip a reference without one")
check("lockLevel" not in mutation_body, "a queued lock must carry no level: the mapping from the level read at capture to the stored level byte is unverified, so a remote lockLevel stays unapplied pending RE confirmation")
check("baseLevel" not in sync, "no remote apply path may write the stored lock level byte")
check(sync.count("kMaximumLockLevel") == 1 and "std::clamp(level, 0, static_cast<int>(F4T::ObjectWire::kMaximumLockLevel))" in sync, "the lock-level bound is only the capture read's clamp, with no remote write behind it")
check("kMaximumWriteRetries = 30;" in header, "an unresolved write must be retried for a bounded number of ticks")
check("std::uint32_t attempts{};" in pending_body and "std::uint32_t priorRevision{};" in pending_body, "Pending must carry the retry count and the revision to restore when a write is dropped")
check(re.search(r"if \(entry\.attempts >= kMaximumWriteRetries\) \{\s*if \(entry\.priorRevision == 0\) \{\s*g_revisions\.erase\(expected\.object\);\s*\} else \{\s*g_revisions\[expected\.object\] = entry\.priorRevision;\s*\}", tick), "a dropped write must restore the previous revision instead of erasing it, so an older stale packet is not accepted afterwards")
check(re.search(r"if \(const auto retry = g_retries\.find\(ref->GetFormID\(\)\); retry != g_retries\.end\(\)\) \{\s*priorRevision = retry->second\.priorRevision;\s*g_retries\.erase\(retry\);\s*\}", apply_body) and "entry.priorRevision = priorRevision;" in apply_body, "a fresh packet that replaces a pending retry must carry that retry's prior revision into the batch entry, or a revision that never reached the engine is restored when the write is dropped")
check(apply_body.find("g_retries.erase(retry);") != -1 and apply_body.index("g_revisions[*key] = revision;") < apply_body.index("g_retries.erase(retry);") < apply_body.index("g_applyGuard.Consume(expected, a_nowSeconds)"), "the pending retry must be dropped as soon as a higher revision is accepted, before the guard can swallow the packet, so the stale retry cannot re-lock the reference against the newer stored revision")
check("next.attempts += 1;" in tick and "retries.insert_or_assign(refId, next);" in tick, "an unresolved write must stay pending for the next tick")
check(re.search(r"for \(const auto& \[refId, retry\] : g_retries\) \{\s*pending\.emplace\(refId, retry\);\s*\}", tick), "a pending retry must rejoin the batch, and a fresh packet for the same reference must win")
check("g_retries = std::move(retries);" in tick, "the retry set must be rebuilt from the tick's decisions")
check("g_retries.clear();" in sync, "a session boundary must clear the pending retries")
check("LogThrottledWarning(" in tick and '"object_sync_write_unresolved"' in tick, "a dropped write must be reported, throttled")
check(re.search(r"void Apply\(const Json& a_packet, double a_nowSeconds, RE::PlayerCharacter\* a_player, std::map<std::uint32_t, Pending>& a_pending, bool a_live\)", sync), "Apply must be told whether a packet is live or a snapshot replay")
check(re.search(r"a_live && expected\.kind == Kind::Activator \? 1u : 0u", apply_body), "a snapshot replay must queue no activation for an activator")
for packet in ("objectState", "objectStateSnapshot", "objectStateRejected"):
    check(f'*packetType == "{packet}"' in inbound, f"dispatch {packet}")
ids = read("lib/commonlibf4/include/RE/IDs.h")
for name, variant in (
    ("ActivateRef", "753531, 2201147"),
    ("GetEventSource", "166230, 2201819"),
    ("GetLock", "930785, 2202648"),
    ("GetLockLevel", "782953, 2191018"),
    ("SetLocked", "157617, 2191020"),
):
    check(re.search(rf"VariantID {name}\{{ {variant} \}};", ids), f"OBJ-1 verified address for {name}")
check(read("src/F4TNetworking.cpp").count("F4T::ObjectSync::Reset();") == 2, "both disconnect paths reset")
check("F4T::ObjectSync::Tick(" in read("src/F4TReplicationRuntime.cpp"), "runtime drives Tick")
if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)
print("F4TObjectSyncContract: all checks passed.")
