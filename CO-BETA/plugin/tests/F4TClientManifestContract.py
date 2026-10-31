from pathlib import Path

root = Path(__file__).resolve().parents[3]
identity = (root / "CO-BETA/plugin/src/F4TClientIdentity.cpp").read_text(encoding="utf-8")

start = identity.index("std::optional<ClientCompatibility::Metadata> LoadPackagedMetadata()")
end = identity.index("\n\tstd::optional<ConnectionIdentity> PrepareConnectionIdentity", start)
load_func = identity[start:end]

assert "ReadPackagedManifest()" in load_func
assert "kManifestClientVersionField" in load_func
assert "ClientCompatibility::kClientVersion" in load_func
assert "kManifestFingerprintField" in load_func
assert "F4T::ContentManifest::IsValidFingerprint" in load_func
assert "ClientCompatibility::ResolveCompileTimeTarget()" in load_func

prep_start = identity.index("std::optional<ConnectionIdentity> PrepareConnectionIdentity(")
prep_end = identity.index("\n\tvoid HoldConnectionIdentity", prep_start)
prep_func = identity[prep_start:prep_end]

assert "const auto metadata = LoadPackagedMetadata();" in prep_func, (
    "the packaged manifest must be loaded exactly once per connection attempt and reused, "
    "not re-read a second time when the ConnectionIdentity is constructed."
)
assert ".metadata = metadata" in prep_func

metadata_index = prep_func.index("const auto metadata = LoadPackagedMetadata();")
fail_closed_index = prep_func.index("if (!metadata) {")
profile_index = prep_func.index("ServerProfileStore::ProfileServerKey(")
assert metadata_index < fail_closed_index < profile_index, (
    "the fail-closed manifest check must run before any server profile identity is created or "
    "persisted, so a connection that cannot satisfy the admission contract never starts or "
    "durably touches profile storage."
)

print("Client manifest contract tests passed")
