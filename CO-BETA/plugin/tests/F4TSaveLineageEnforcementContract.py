from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[3]
PLUGIN = ROOT / "CO-BETA/plugin"
SERVER = ROOT / "CO-SERVER/server-cpp"
CORE = ROOT / "co-core"

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def strip_comments(text):
    return re.sub(r"//[^\n]*", "", text)


def function_body(code, signature):
    start = code.index(signature)
    brace = code.index("{", start)
    depth = 0
    for index in range(brace, len(code)):
        if code[index] == "{":
            depth += 1
        elif code[index] == "}":
            depth -= 1
            if depth == 0:
                return code[brace:index + 1]
    raise AssertionError(f"unterminated body for {signature}")


lineage = strip_comments((PLUGIN / "src/F4TSaveLineageSession.cpp").read_text(encoding="utf-8"))
launch = strip_comments((PLUGIN / "src/F4TSessionLaunch.cpp").read_text(encoding="utf-8"))
ui = strip_comments((PLUGIN / "src/F4TUIClient.cpp").read_text(encoding="utf-8"))
identity = strip_comments((PLUGIN / "src/F4TClientIdentity.cpp").read_text(encoding="utf-8"))
logic = strip_comments((CORE / "src/domain/F4TSaveProfileLogic.cpp").read_text(encoding="utf-8"))
gate = strip_comments((SERVER / "src/IdentityGate.cpp").read_text(encoding="utf-8"))

admit_join = function_body(logic, "SaveAdmission AdmitJoinSave(")
check("a_isAdmin || a_resolution.saveFile.has_value()" in admit_join,
      "AdmitJoinSave must admit only an admin or a resolved attested save")
check("std::string{ SaveNotMultiplayerCode }" in admit_join,
      "AdmitJoinSave must deny with SaveNotMultiplayerCode")
check(re.search(r"SaveAdmission\{ true, \{\}, \{\} \};\s*\}\s*return SaveAdmission\{", admit_join) is not None,
      "AdmitJoinSave must not fall through to an allow")

admit_claim = function_body(logic, "SaveAdmission AdmitAttestedSaveClaim(")
check("if (a_standing.isAdmin) {" in admit_claim,
      "AdmitAttestedSaveClaim must admit an admin unconditionally")
check("a_claim && (verified || (a_serverHistory.empty() && !a_standing.profileKnown))" in admit_claim,
      "AdmitAttestedSaveClaim must record a claim for a verified profile, or against an empty history for a never-seen profile")
check("SameAttestedSave(a_entry, *a_claim)" in admit_claim,
      "AdmitAttestedSaveClaim must match the claim against the recorded history")
check("recorded && (verified || a_standing.claimReadmission)" in admit_claim,
      "AdmitAttestedSaveClaim must readmit a recorded claim without the secret only through the readmission standing")
check("std::string{ SaveNotMultiplayerCode }" in admit_claim,
      "AdmitAttestedSaveClaim must deny with SaveNotMultiplayerCode")

join_save = function_body(lineage, "JoinSaveDecision ResolveJoinSave(")
check("F4T::SaveProfileLogic::AdmitJoinSave(resolution, a_isAdmin)" in join_save,
      "SaveLineageSession::ResolveJoinSave must go through AdmitJoinSave")
check("if (!admission.allowed)" in join_save and "decision.allowed = false;" in join_save,
      "SaveLineageSession::ResolveJoinSave must refuse the join when admission denies")
check("decision.code = admission.code;" in join_save and "decision.reason = admission.reason;" in join_save,
      "SaveLineageSession::ResolveJoinSave must carry the refusal code and reason to the caller")
check(join_save.index("decision.allowed = false;") < join_save.index("decision.saveFile = resolution.saveFile;"),
      "SaveLineageSession::ResolveJoinSave must refuse before it selects the attested save")
check(re.search(r"allowed\s*=\s*true", join_save) is None,
      "SaveLineageSession::ResolveJoinSave must not re-allow a refused join")
check("catch (...) {" in join_save and "RefuseJoin" in join_save.split("catch (...) {")[1],
      "SaveLineageSession::ResolveJoinSave must refuse the join when the lineage check throws")
check(re.search(r"if \(!characterId\) \{\s*return RefuseJoin\(", join_save) is not None,
      "SaveLineageSession::ResolveJoinSave must refuse a save whose name carries no character id")
check("RecordActiveSave(a_serverKey, *boundSave)" in join_save and join_save.index("RecordActiveSave(a_serverKey, *boundSave)") < join_save.index("AnnounceRecordedAttestations(a_serverKey, attested)"),
      "SaveLineageSession::ResolveJoinSave must record a bound profile's save with no recorded attestation before it announces")
check("AnnounceRecordedAttestations(a_serverKey, attested)" in join_save,
      "SaveLineageSession::ResolveJoinSave must resend the recorded attestations on join")
check("kEnableSaveLineage" not in lineage and "kEnableSaveLineage" not in (PLUGIN / "include/F4TSaveLineageSession.h").read_text(encoding="utf-8"),
      "the save lineage path must not be gated")

announce = function_body(lineage, "void AnnounceSaveAttestation(")
check("F4T::SaveLineageStore::LookupServerSecret(a_serverKey)" in announce and '"profileSecret"' in announce,
      "every saveAttested announcement must carry the profile secret")

launch_body = function_body(launch, "LaunchOutcome LaunchValidatedSession(")
check("const bool isAdmin = F4T::ClientIdentity::IsConnectedServerAdmin();" in launch_body,
      "LaunchValidatedSession must take the admin exemption from the server's sessionReady flag")
check(launch_body.index("IsConnectedServerAdmin()") < launch_body.index("ResolveJoinSave(profileKey"),
      "LaunchValidatedSession must resolve the admin flag before it resolves the join save")
check(re.search(r"if \(!joinSave\.allowed\) \{\s*a_error = joinSave\.reason;\s*return LaunchOutcome::Failed;", launch_body) is not None,
      "LaunchValidatedSession must fail the launch with the refusal reason")
check(re.search(r"if \(!latestSave\.allowed\) \{\s*a_error = latestSave\.reason;\s*return LaunchOutcome::Failed;", launch_body) is not None,
      "the missing-save retry must also fail the launch with the refusal reason")

ui_body = function_body(ui, "bool Callback_JoinSession(")
check(re.search(r'if \(!ok\) \{\s*F4T::Networking::DisconnectFromLocalServer\(\);', ui_body) is not None,
      "UIClient must drop the connection when the launch is refused")
check('a_onComplete(ok, ok ? "" : launchError.c_str(), a_userData);' in ui_body,
      "UIClient must hand the refusal reason to the UI callback")

resolve_claim = function_body(identity, "std::optional<F4T::SaveProfileLogic::AttestedSave> ResolveSaveAttestation(")
check("F4T::SaveLineageStore::LookupAttestedSaves(a_serverKey)" in resolve_claim,
      "ClientIdentity::ResolveSaveAttestation must read the recorded attestations")
check(resolve_claim.count("return std::nullopt;") >= 2,
      "ClientIdentity::ResolveSaveAttestation must claim nothing without a bound save or a record")
check("Sha256HexFile" not in resolve_claim,
      "ClientIdentity::ResolveSaveAttestation must not attest a save at join time")

check("F4T::SaveProfileLogic::SaveClaimStanding{ isAdmin, secretVerified, profileKnown, claimReadmission }" in gate,
      "IdentityGate must admit the hello against the recorded attestations with the verified secret, known-profile and readmission standing")
check("claimReadmission = !_claimReadmitted.contains(profileId) && _identities.AwaitingConfirmation(profileId, a_nowUnixSeconds);" in gate,
      "IdentityGate must readmit a recorded claim without the secret only while the profile's secret delivery is unconfirmed and not yet readmitted")
check("_claimReadmitted.insert(profileId);" in gate and "_claimReadmitted.erase(profile->second);" in gate,
      "IdentityGate must mark a readmission and clear the mark only when the readmitted session confirms its secret")
check(gate.index("AdmitAttestedSaveClaim(") < gate.index("_identities.ValidateAndReserve(key"),
      "IdentityGate must admit the save claim before it reserves the identity, so a denied hello writes no pending state")
check("const bool isAdmin = _profileAdminLookup && _profileAdminLookup(profileId) && secretVerified;" in gate,
      "IdentityGate must take the admin exemption from the server's own profile lookup")
check("_saveAuthority->EnsureProfileSecret(profileId)" in gate and '(*packet)["profileSecret"] = secret->second;' in gate,
      "IdentityGate must issue the profile secret in sessionReady")
check("_saveAuthority->ProfileSecret(a_profileId)" in gate,
      "IdentityGate must verify an admin against the profile secret it issued")
check("save_attestation_unauthenticated" in gate and "save_attestation_rate_limited" in gate,
      "IdentityGate must reject an unauthenticated or rate-limited attestation")
check("F4T::SaveProfileLogic::SameAttestedSave(recorded, attested)" in gate,
      "IdentityGate must skip an attestation it has already recorded")
check("F4T::Identity::SaveAttestationCapability" in gate,
      "IdentityGate must refuse a client that does not report save attestation support")
check(gate.count("multiplayerSaveAdmin") == 1 and "multiplayerSaveAdmin" in function_body(gate, "bool IdentityGate::EnrichSessionReady("),
      "multiplayerSaveAdmin must only be set on the outbound sessionReady frame, never read from a client packet")
check(re.search(r"admission\.allowed[\s\S]{0,200}?\.forward = false", gate) is not None,
      "IdentityGate must not forward a hello whose save admission was denied")

shim = strip_comments((PLUGIN / "src/F4TGnsWinsockShim.cpp").read_text(encoding="utf-8"))
store = strip_comments((PLUGIN / "src/F4TSaveLineageStore.cpp").read_text(encoding="utf-8"))
inbound = strip_comments((PLUGIN / "src/F4TNetworkingInbound.cpp").read_text(encoding="utf-8"))

commit = function_body(identity, "bool CommitReadyIdentity(")
check("SaveLineageStore::StoreServerSecret(a_serverKey, *a_identity.profileSecret)" in commit,
      "ClientIdentity::CommitReadyIdentity must persist the profile secret the server issued in sessionReady")
check("ServerProfileStore::StoreResumeCredential(a_serverKey, a_identity.resumeToken, a_identity.resumeExpiresAt)" in commit,
      "ClientIdentity::CommitReadyIdentity must keep storing the rotated resume credential")
check("F4T::ClientIdentity::CommitReadyIdentity(serverKey, a_identity, a_error)" in shim,
      "the GNS connect committer must commit the ready identity through ClientIdentity")

shim_identity = function_body(shim, "F4T::GnsProtocolAdapter::ClientIdentity{")
check(".profileSecret = identity->profileSecret" in shim_identity,
      "the GNS shim must send the stored profile secret in the hello's ClientIdentity")

store_secret = function_body(store, "bool StoreServerSecret(")
check("!IsServerSecret(a_serverSecret)" in store_secret,
      "the sidecar must reject a profile secret that is not 32 lowercase hex characters")

check('*packetType == "saveAttestedAck"' in inbound and '*packetType == "saveAttestedRejected"' in inbound,
      "the client must handle the server's save attestation acknowledgment and rejection")

save_path = function_body(lineage, "std::optional<std::filesystem::path> SaveFilePath(")
engine_dir = function_body(lineage, "std::filesystem::path EngineSaveDirectory(")
check('RE::GetINISetting("sLocalSavePath:General")' in engine_dir and '"Saves"' not in lineage,
      "the save folder must come from the engine's sLocalSavePath, never a hard-coded Saves folder")
check("FindSaveDirectory(saveFolder, fileName)" in save_path and "g_searchedSaveFiles.emplace(a_saveFile).second" in save_path,
      "SaveFilePath must search for a save it cannot find, once per save name")
check(save_path.index("engineDirectory / fileName, ec") < save_path.index("g_saveDirectory / fileName, ec"),
      "the engine save folder must win over the last folder a search found")
check("SaveFilePath(a_saveFile, true)" in lineage and "SaveFilePath(g_pendingRecord->saveName, false)" in lineage,
      "only hashing may search; the per-frame pending poll must not")

if failures:
    for failure in failures:
        print(f"FAIL: {failure}")
    sys.exit(1)

print("F4TSaveLineageEnforcementContract: all checks passed.")
