from pathlib import Path

root = Path(__file__).resolve().parents[3]

config_header = (root / "co-core/include/F4TServerConfiguration.h").read_text(encoding="utf-8")
config_source = (root / "co-core/src/config/F4TServerConfiguration.cpp").read_text(encoding="utf-8")
identity_header = (root / "co-core/include/F4TIdentityProtocol.h").read_text(encoding="utf-8")
identity_source = (root / "co-core/src/session/F4TIdentityProtocol.cpp").read_text(encoding="utf-8")
settings_source = (root / "co-core/src/config/F4TServerSettingsFile.cpp").read_text(encoding="utf-8")
server_config = (root / "CO-SERVER/server-cpp/src/ServerConfiguration.cpp").read_text(encoding="utf-8")
bootstrap = (root / "CO-SERVER/server-cpp/src/ServerBootstrap.cpp").read_text(encoding="utf-8")
bridge = (root / "CommonwealthOnlineUI/src/F4TServerSettingsBridge.cpp").read_text(encoding="utf-8")
settings_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/server-settings.js").read_text(encoding="utf-8")
markup = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/browser-markup.js").read_text(encoding="utf-8")
browser_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/browser.js").read_text(encoding="utf-8")
failure_header = (root / "CO-BETA/plugin/include/F4TConnectFailure.h").read_text(encoding="utf-8")
probe = (root / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp").read_text(encoding="utf-8")
ui_client = (root / "CO-BETA/plugin/src/F4TUIClient.cpp").read_text(encoding="utf-8")
alert = (root / "CO-BETA/plugin/src/F4THandshakeRejectionAlert.cpp").read_text(encoding="utf-8")

assert "requiredModpackUrl" in config_header, "the options must carry the optional modpack link"
for fragment in ("required_modpack_url", "RequiredModpackPath", "LoadRequiredModpack", "RequiredModpackError"):
    assert fragment in config_source, f"F4TServerConfiguration.cpp must handle {fragment}"
assert "required_modpack_url must be an https:// URL" in config_source, (
    "the modpack link must be validated with a clear error"
)
assert "LoadRequiredModpack(const std::filesystem::path&" not in server_config, (
    "the server must use the shared loader instead of keeping its own copy"
)
assert "F4T::ServerConfiguration::LoadRequiredModpack" in server_config
assert "F4T::ServerConfiguration::RequiredModpackPath" in server_config

assert "required_modpack_path is not set" in bootstrap, (
    "the server must log a clear startup warning when no required modpack is configured"
)

assert "ServerConfiguration::RequiredModpackError(plan.document" in settings_source, (
    "the host panel save path must check required_modpack_path like the server parser does"
)
assert "ServerConfiguration::RequiredModpackError(document" in bridge, (
    "the host panel load path must surface a bad required_modpack_path as an error"
)
assert "required_modpack_url" in settings_js and "not anti-cheat" in settings_js, (
    "the host panel help must document the modpack gate and that a faked fingerprint is accepted by design"
)

assert "kModpackPageMarker" in identity_header
assert "rejection.reason += kModpackPageMarker;" in identity_source, (
    "the modpack mismatch reason must carry the optional modpack page link"
)
assert "NormalizeModpackUrl" in identity_source, "the link must be validated before it reaches a client"
assert "bool IsAllowedModpackPageUrl(std::string_view a_url) noexcept;" in identity_header, (
    "co-core must publish the one modpack link validator the server config and the client probe both use"
)
assert "bool IsAllowedModpackPageUrl(std::string_view a_url) noexcept" in identity_source
assert "std::optional<std::string> NormalizeModpackUrl(const std::optional<std::string>& a_value);" in identity_header
assert "NormalizeModpackUrl(a_options.requiredModpackUrl)" in config_source, (
    "the server must validate required_modpack_url with the shared normalizer, so an over-long link fails "
    "validation instead of being silently dropped at handshake time"
)
assert "kMaximumModpackUrlBytes" in identity_header

assert "modpackUrl" in failure_header, "the client failure must carry the optional modpack link"
for fragment in ("F4T::Identity::ExtractModpackPageUrl", "failure.modpackUrl ="):
    assert fragment in probe, f"the client probe must extract the link ({fragment})"
for prefix_check in ('starts_with("https://")', 'starts_with("http://localhost")', "bool IsAllowedModpackPageUrl("):
    assert prefix_check not in probe, (
        "the client probe must call the shared co-core validator instead of testing URL prefixes "
        f"({prefix_check} would accept localhost.evil.com and 127.0.0.1@evil.com)"
    )
assert "connectionFailure->modpackUrl" in ui_client, "the join UI must receive the link"
assert "OpenModpackPage" in alert and '"modpack", "Get Modpack"' in alert, (
    "the rejection alert must offer a button that opens the server's modpack page"
)
assert "ShellExecuteW" in alert, "the alert must open the link with the shell, with no auto-download"
assert "F4T::Identity::ModpackUrlHost" in alert and 'presentation.message += std::format("\\nGet Modpack opens {}", host);' in alert, (
    "the rejection alert must show the destination host, so the player sees where Get Modpack goes"
)
assert "std::string_view ModpackUrlHost(std::string_view a_url) noexcept;" in identity_header, (
    "the host shown in the alert must come from the same parser that validates the link"
)

assert 'id="coCopyModpackButton"' in markup, "the join overlay must mount a Copy list button"
copy_js = (root / "CommonwealthOnlineUI/ui/views/CommonwealthOnline/browser/components/modpack-copy.js").read_text(encoding="utf-8")
assert "CO_ModpackCopy" in copy_js and "navigator.clipboard" in copy_js, (
    "the Copy list button must copy the mismatch lines"
)
assert ".then(function () { return clipboard.writeText(text); })" in copy_js and "execCommand" in copy_js, (
    "the copy button must await the clipboard write and fall back to execCommand when the write is refused"
)
assert 'notice.textContent = copied ? "Copied to clipboard." : "Copy failed.";' in copy_js, (
    "the notice must come from the real copy result"
)
assert "modpackCopy.show(modpack.lines, isError)" in browser_js, "the overlay must hand the lines to the copy component"

print("Modpack gate host and player UX contract tests passed")
