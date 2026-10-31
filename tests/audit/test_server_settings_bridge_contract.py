import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
UI = ROOT / "CommonwealthOnlineUI"
VIEWS = UI / "ui/views/CommonwealthOnline/browser"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def test_ui_build_links_the_shared_configuration_module() -> None:
    xmake = read(UI / "xmake.lua")
    assert 'includes(path.join(co_core_dir, "xmake.lua"))' in xmake, "UI build does not include co-core"
    assert 'add_deps("CommonwealthOnlineCore")' in xmake, "UI build does not link CommonwealthOnlineCore"
    assert 'add_requires("nlohmann_json v3.12.0")' in xmake, "UI package version differs from co-core"


def test_the_schema_has_one_implementation() -> None:
    shared = read(ROOT / "co-core/src/config/F4TServerConfiguration.cpp")
    assert "namespace F4T::ServerConfiguration" in shared
    assert "std::vector<std::string> Validate(" in shared
    assert "server_password must be 4-64 printable ASCII characters when set" in shared

    server_header = read(ROOT / "CO-SERVER/server-cpp/include/ServerConfiguration.h")
    assert "using F4T::ServerConfiguration::Validate;" in server_header, "server does not reuse the shared rules"
    assert "struct Options" not in server_header, "the server still owns a second Options definition"

    server_source = read(ROOT / "CO-SERVER/server-cpp/src/ServerConfiguration.cpp")
    assert "F4T::ServerConfiguration::Parse(a_root, a_baseDirectory)" in server_source
    assert "F4T::ServerConfiguration::LoadRequiredModpack" in server_source, "the server lost its modpack manifest loading"
    assert "required_modpack_path" in shared, "the shared configuration module lost the modpack path field"
    for rule in ("must be <= 64 characters", "max_players must be 1-16", "downed_seconds must be 5-600"):
        assert rule not in server_source, "server wrapper duplicates a validation rule: " + rule


def test_dedicated_servers_still_load_the_json_file() -> None:
    arguments = read(ROOT / "CO-SERVER/server-cpp/src/ServerArguments.cpp")
    assert "Configuration::Parse(root, a_path.parent_path())" in arguments
    assert "Could not open server configuration '" in arguments


def test_bridge_uses_the_shared_save_path_and_reports_its_errors() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    assert "const auto outcome = SaveSettings(configPath, values" in bridge
    assert "ReadDocument(configPath)" in bridge
    assert "ServerConfiguration::Validate(options)" in bridge, "the panel does not report the server's own errors"
    for action in ('"serverSettingsGet"', '"serverSettingsSave"'):
        assert action in bridge, "missing action " + action
    assert 'kConfigFileName = "commonwealth-server.json"' in bridge
    for rule in ("must be 5-600", "must be 1-16", "4-64 printable"):
        assert rule not in bridge, "bridge duplicates a validation rule: " + rule
    for field_list in ("serverName", "downedSeconds", "ValuesJson", "ReadValues"):
        assert field_list not in bridge, "the bridge keeps a second field list: " + field_list


def test_the_save_sequence_runs_the_start_rule_then_the_server_then_the_write() -> None:
    settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
    sequence = settings[settings.index("SaveOutcome SaveSettings("):]
    prepared = sequence.index("PrepareSave(a_path, a_values)")
    checked = sequence.index("if (!plan.worldModeRefusal.empty())")
    asked = sequence.index("a_liveSettings(plan.document")
    written = sequence.index("CommitSave(a_path, plan)")
    assert prepared < checked < asked < written, "the settings are decided before the file is written"
    assert "return SaveOutcome{" in sequence[checked:asked], (
        "the world mode verdict must abandon the save before the live change can be asked"
    )
    refused = sequence.index("LiveApplyResult::Refused")
    assert checked < refused < written
    refused_path = sequence[refused:written]
    assert "return SaveOutcome{" in refused_path, "a refusal must abandon the save"
    assert "RevertLiveSettings(a_liveSettings" in refused_path, (
        "a refusal that already applied some fields must put the previous settings back"
    )
    assert "a_runningDocument" in refused_path, (
        "the put-back must target the running server's own settings, not only the file"
    )
    assert "Nothing was changed" in settings, "a refusal must tell the host nothing was written"
    assert sequence.count("RevertLiveSettings(") >= 2, (
        "both the refusal and the failed write must put the previous settings back"
    )
    assert written < sequence.rindex("RevertLiveSettings("), (
        "a write that fails after the running server switched must put it back"
    )

    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    assert "SaveSettings(configPath, values" in bridge
    for owned in ("CommitSave(", "PrepareSave(", "UnservableModeError", "Nothing was changed"):
        assert owned not in bridge, "the bridge keeps save ordering of its own: " + owned
    assert "LiveApply::Unavailable" in bridge, "a server that is not there must reach the sequence as unavailable"


def test_a_config_that_cannot_be_read_is_an_error_not_blank_settings() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    load = bridge[bridge.index("Json LoadPayload(") : bridge.index("void ProcessGet(")]
    assert "bool readable = true;" in load
    assert "readable = false;" in load, "a parse failure must mark the snapshot unreadable"
    assert load.index("if (readable) {") < load.index('data["values"] = ToPayload'), (
        "an unreadable file must not produce default values the panel would show as the file's"
    )
    assert 'data["readable"] = false;' in load
    panel = read(VIEWS / "components/server-settings.js")
    assert "settings.readable === false" in panel, "the panel must react to an unreadable snapshot"
    assert "could not be read" in panel


def test_the_panel_offers_the_folder_the_game_starts() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    remembered = bridge[bridge.index("std::filesystem::path RememberedDirectory()") : bridge.index("std::filesystem::path ChosenDirectory(")]
    assert "F4T::UserPaths::PackagedServerDirectory()" in remembered, (
        "in-game hosting starts Data/server, so the panel must offer that folder"
    )
    paths = read(UI / "src/F4TUserPaths.cpp")
    assert 'L"server"' in paths and "GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS" in paths


def test_the_hosting_refusal_comes_from_the_settings_state() -> None:
    panel = read(VIEWS / "components/server-settings.js")
    host = read(VIEWS / "components/steam-friends.js")
    assert "function hostBlockReason(" in panel
    assert "CO_ServerSettings.hostBlockReason(state)" in host, "the host card must ask the settings why hosting is blocked"
    reason = panel[panel.index("function hostBlockReason(") : panel.index("function valueOf(")]
    assert "settings.readable === false" in reason, "an unreadable config must be named as the reason"
    assert "advertised(state).name" in reason, "a readable config without a name must still block hosting"
    assert "could not be read" not in host, "the host card must not keep its own copy of the refusal text"


def test_a_stored_folder_that_is_not_text_does_not_disable_the_panel() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    stored = bridge[bridge.index("std::filesystem::path ReadStoredDirectory()") : bridge.index("void WriteStoredDirectory")]
    assert 'root.value("serverDirectory"' not in stored, (
        "json::value throws on a non-string, and that throw escapes every settings action"
    )
    assert "found->is_string()" in stored


def test_the_world_mode_rule_and_its_wording_have_one_home() -> None:
    policy = read(ROOT / "co-core/src/world/F4TWorldPolicy.cpp")
    assert "std::string UnservableModeError(" in policy
    assert "std::string UnservableModeMessage(" in policy
    assert "CanServeMode(LoadData(a_dataPath).value_or(Data{}), a_mode)" in policy, (
        "the offline check must be the same predicate the server start uses"
    )

    bootstrap = read(ROOT / "CO-SERVER/server-cpp/src/ServerBootstrap.cpp")
    assert "UnservableModeMessage(F4T::Utf8Path::Encode(a_out.worldPolicyPath), a_out.worldMode)" in bootstrap
    runtime = read(ROOT / "CO-SERVER/server-cpp/src/ServerRuntime.inc")
    live_settings = read(ROOT / "CO-SERVER/server-cpp/src/ServerLiveSettings.cpp")
    assert "UnservableModeMessage(F4T::Utf8Path::Encode(*a_context.worldPolicyPath), *mode)" in live_settings
    settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
    assert "UnservableModeError(verdict.policyPath, *mode)" in settings, (
        "the panel and the save must ask the same co-core question the server start answers"
    )
    assert "F4T::WorldPolicy::ParseMode(verdict.mode)" in settings
    assert settings.count("UnservableModeError(") == 1, "the world mode verdict must be built in exactly one place"

    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    for name, source in (
        ("ServerBootstrap.cpp", bootstrap),
        ("ServerRuntime.inc", runtime),
        ("F4TServerSettingsBridge.cpp", bridge),
        ("F4TServerSettingsFile.cpp", settings),
    ):
        assert "needs a generated world policy file" not in source, "a second copy of the world mode refusal text in " + name


def test_the_plan_carries_the_policy_path_the_server_would_read() -> None:
    settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
    assert "verdict.policyPath = F4T::Utf8Path::Decode(a_options.worldPolicyPath);" in settings
    assert "verdict.policyPath = a_baseDirectory / verdict.policyPath;" in settings, (
        "a relative world_policy_path must resolve the way ServerBootstrap::Prepare resolves it"
    )
    assert "plan.worldPolicyPath = verdict.policyPath;" in settings
    header = read(ROOT / "co-core/include/F4TServerSettingsFile.h")
    assert "std::filesystem::path worldPolicyPath;" in header
    assert "struct WorldModeVerdict" in header and "[[nodiscard]] WorldModeVerdict JudgeWorldMode(" in header


def test_the_host_panel_refuses_an_unservable_saved_mode_without_a_server() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    load = bridge[bridge.index("Json LoadPayload(") : bridge.index("void ProcessGet(")]
    assert "worldModeRefusal = JudgeWorldMode(options, a_directory).refusal;" in load, (
        "the panel must get the verdict from co-core's own check"
    )
    assert 'data["worldModeRefusal"] = std::move(worldModeRefusal);' in load, (
        "the verdict must reach the panel on every settings reply"
    )
    for owned in ("CanServeMode", "LoadData(", "ParseMode(", "is_relative()", "needs a generated world policy file"):
        assert owned not in load, "the bridge rebuilds the world mode rule: " + owned

    panel = read(VIEWS / "components/server-settings.js")
    reason = panel[panel.index("function hostBlockReason(") : panel.index("function valueOf(")]
    assert "settings.worldModeRefusal" in reason, "hosting must be refused for the saved file's mode"
    assert reason.index("settings.worldModeRefusal") < reason.index("advertised(state).name"), (
        "an unservable mode is refused before the name is even asked for"
    )
    assert "needs a generated world policy file" not in panel, "the panel keeps no copy of the server's wording"
    card = panel[panel.index("function section(state, focusedId)") : panel.index("function isNumeric(")]
    assert 'id="co-ss-world-mode-refusal"' in card, (
        "the settings card must show the refusal itself, not only when Host is pressed"
    )
    assert card.index("settings.worldModeRefusal") < card.index("FIELDS.forEach("), (
        "the refusal belongs above the controls it is about"
    )
    assert 'escapeHtml(settings.worldModeRefusal)' in card, "the server's text still renders escaped"
    apply = panel[panel.index("function applyResult(") : panel.index("function capturePassword(")]
    assert "settings.worldModeRefusal = event.data.worldModeRefusal || \"\";" in apply, (
        "a settings reply must replace the verdict, not keep a stale one"
    )

    settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
    save = settings[settings.index("SaveOutcome SaveSettings(") :]
    assert "plan.worldModeRefusal" in save, "the save and the host panel must share one verdict"


def test_the_panel_reads_a_changed_folder_before_it_writes_and_quotes_real_errors() -> None:
    panel = read(VIEWS / "components/server-settings.js")
    save = panel[panel.index("function save()") : panel.index("function applyResultAndSave(")]
    assert 'if (!sameFolder(payload.serverDirectory, settings.folder)) {' in save, (
        "Save must notice a folder the panel has not read"
    )
    assert "function sameFolder(a, b) {" in panel, (
        "a trailing separator or a different case must not count as another folder"
    )
    read_at = save.index('sendAction("serverSettingsGet", { serverDirectory: payload.serverDirectory });')
    write_at = save.index('sendAction("serverSettingsSave", payload);')
    assert read_at < write_at and "return;" in save[read_at:write_at], (
        "nothing may be written before the typed folder has been read"
    )
    assert "settings.pendingSave = {" in save and "draft: Object.assign({}, settings.draft)" in save, (
        "the edits made before the folder changed must survive the read"
    )

    resume = panel[panel.index("function applyResultAndSave(") : panel.index("function clearStatus(")]
    assert 'event.action !== "serverSettingsGet"' in resume, "the save must resume on the read's own reply"
    assert 'sendAction("serverSettingsSave", {' in resume and "valuesFromState(state)" in resume, (
        "the values written must be the folder that was just read plus the kept edits"
    )
    assert "settings.pendingBefore" in resume, (
        "a folder the bridge rejects must leave the panel on the config it had read"
    )
    assert resume.count("settings.folderError = firstReplyError(settings,") == 2, (
        "a rejected folder and an unparsable config in it are both the folder's own error"
    )
    fresh = panel[panel.index("function applyResult(") : panel.index("function capturePassword(")]
    assert 'settings.folderError = "";' in fresh, "a fresh reply must clear the folder's last error"
    row = panel[panel.index("function textMarkup(") : panel.index("function toggleMarkup(")]
    assert "field.folder && settings.folderError" in row, (
        "the read error renders only in the folder's row"
    )
    assert 'id="co-ss-folder-error"' in row, "the folder's read error must show beside the folder field"
    assert "function clearFolderError(" in panel and "clearFolderError(state);" in panel, (
        "typing another folder must drop the error instead of leaving it over a new path"
    )
    section = panel[panel.index("function section(state, focusedId)") : panel.index("function isNumeric(")]
    assert "error !== settings.folderError" in section, (
        "the folder's error must not be printed a second time in the card's list"
    )

    reason = panel[panel.index("function hostBlockReason(") : panel.index("function valueOf(")]
    assert "var errors = settings.errors || [];" in reason, "the refusal must read the settings errors"
    assert "String(errors[0])" in reason, "the refusal must quote the error instead of summarising it"
    assert '(detail ? " " + detail : "")' in reason, "an unreadable config must quote the parse error"
    assert '"The saved configuration would not start the server: " + detail' in reason, (
        "a readable config with validation errors must be refused with the server's own text"
    )


def test_the_panel_keeps_a_typed_password_and_says_the_file_still_holds_the_old_one() -> None:
    panel = read(VIEWS / "components/server-settings.js")
    save = panel[panel.index("function save()") : panel.index("function applyResultAndSave(")]
    assert 'settings.lastPasswordIntent = payload.passwordMode === "keep" ? "" : payload.passwordMode;' in save, (
        "the panel must remember which password intent the save carries"
    )
    assert 'settings.lastPasswordIntent = pending.passwordMode === "keep" ? "" : pending.passwordMode;' in panel, (
        "an intent must survive the automatic folder re-read"
    )
    refusal = panel[panel.index("var replyErrors = event && event.errors") :]
    assert 'event.action === "serverSettingsSave" && settings.lastPasswordIntent' in refusal
    assert '"the file still has the password it had"' in refusal, (
        "a refused save must say the file still holds the old password"
    )
    assert '"the file still has no password"' in refusal, (
        "a config without one must not claim to keep a password"
    )
    assert '". The new password is still in the field, and Save will send it again."' in refusal
    assert '"Nothing was saved, so the file still has the saved password. "' in refusal, (
        "a refused remove must say the saved password is still there"
    )
    assert "capturePassword" in panel and "passwordSaved" in panel, (
        "only a save that landed may empty the field"
    )

    tests = read(VIEWS / "tests/server-settings.test.js")
    assert "a refused save keeps the typed password and says the file still has the old one" in tests, (
        "the two-save path through a refusal must stay covered"
    )


def test_one_table_describes_the_document_and_the_payload() -> None:
    settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
    assert "constexpr Field kFields[]{" in settings, "the settings need one field table"
    assert "kSpellings" not in settings, "the spelling table must be part of the field table"
    for function in ("Json ApplyEditable(", "Json ToPayload(", "bool FromPayload("):
        assert function in settings, "missing " + function
    assert settings.count("for (const auto& field : kFields)") == 1, (
        "the document write, the payload write and the payload read must share one walk over the table"
    )
    for helper in ("void WriteField(", "bool ReadField(", "bool WalkFields("):
        assert helper in settings, "missing " + helper

    table_keys = set(re.findall(r'\.key = "(\w+)"', settings))
    panel = read(VIEWS / "components/server-settings.js")
    panel_keys = set(re.findall(r'\{ id: "[^"]+", key: "(\w+)"', panel))
    panel_keys.discard("passwordClear")
    assert table_keys == panel_keys, "the panel controls and the field table disagree: " + str(sorted(table_keys ^ panel_keys))


def test_bridge_never_touches_secrets_or_logs_the_password() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    for secret in (".admin-token", ".save-attestation-key", "server_password", "password" + "="):
        if secret == "password=":
            continue
        assert secret not in bridge, "bridge reads or writes a protected value: " + secret
    for line in bridge.splitlines():
        if "REX::" in line:
            assert "password" not in line.lower(), "bridge logs password material: " + line


def test_the_main_menu_routes_the_server_settings_actions() -> None:
    menu = read(UI / "src/F4TPrismaUI_MainMenu.cpp")
    assert 'actionName.starts_with("serverSettings")' in menu
    assert "F4T::ServerSettings::HandleAction(action)" in menu
    assert '#include "F4TServerSettingsBridge.h"' in menu


def test_the_host_panel_renders_the_section_without_duplicating_rules() -> None:
    friends = read(VIEWS / "components/steam-friends.js")
    assert "CO_ServerSettings.section(state, focusedId)" in friends
    assert "ids = ids.concat(CO_ServerSettings.fieldIds(state))" in friends

    settings = read(VIEWS / "components/server-settings.js")
    for identifier in ("co-ss-name", "co-ss-description", "co-ss-password", "co-ss-max-players", "co-ss-world-mode",
                       "co-ss-doors-shared", "co-ss-friendly-fire", "co-ss-faction-friendly-fire",
                       "co-ss-downed-seconds", "co-ss-chat", "co-ss-factions", "co-ss-persistence",
                       "co-ss-object-state", "co-ss-lan", "co-ss-listing", "co-ss-resume-hours"):
        assert identifier in settings, "missing control " + identifier
    for hidden in ("directory_url", "admin_port", "admin_profiles", "required_client_version"):
        assert hidden not in settings, "panel exposes an internal field: " + hidden
    assert 'key: "required_modpack' not in settings, "panel exposes an internal modpack field"
    assert 'id: "co-ss-modpack' not in settings, "panel exposes an internal modpack field"
    assert "must be 5-600" not in settings and "must be 1-16" not in settings, "panel duplicates validation rules"
    assert "applied to the running server" in settings, "panel does not say changes are live"
    assert "Restart the server" not in settings, "panel still asks the host to restart"
    assert "starting_inventory" in settings, "panel does not explain the unexposed inventory setting"
    assert "capturePassword" in settings and "restorePassword" in settings


def test_the_panel_is_shipped_and_tested() -> None:
    index = read(VIEWS / "index.html")
    assert "./components/server-settings.js" in index
    assert "./components/server-settings.css" in index
    assert (VIEWS / "tests/server-settings.test.js").is_file()

    menu = read(VIEWS.parent / "main-menu/index.html")
    assert "../browser/components/server-settings.js" in menu, "the in-game menu does not load the settings panel"
    assert "../browser/components/server-settings.css" in menu, "the in-game menu does not load the settings stylesheet"

    workflow = read(ROOT / ".github/workflows/co-ui-ci.yml")
    assert "components/server-settings.js" in workflow
    assert "server-settings.test.js" in workflow

    windows = read(ROOT / "scripts/windows-compile-ci.py")
    assert "browser/components/server-settings.js" in windows
    assert "server-settings.test.js" in windows


def test_controller_input_reaches_the_new_controls() -> None:
    navigation = read(VIEWS / "components/controller-navigation.js")
    assert "CO_ServerSettings.ownsField(id)" in navigation
    assert "CO_ServerSettings.activate(root, id, state)" in navigation
    prompts = read(VIEWS / "components/controller-prompts.js")
    assert "CO_ServerSettings.promptLabel(steamId)" in prompts


def main() -> None:
    test_ui_build_links_the_shared_configuration_module()
    test_the_schema_has_one_implementation()
    test_dedicated_servers_still_load_the_json_file()
    test_bridge_uses_the_shared_save_path_and_reports_its_errors()
    test_the_save_sequence_runs_the_start_rule_then_the_server_then_the_write()
    test_the_world_mode_rule_and_its_wording_have_one_home()
    test_one_table_describes_the_document_and_the_payload()
    test_the_plan_carries_the_policy_path_the_server_would_read()
    test_the_host_panel_refuses_an_unservable_saved_mode_without_a_server()
    test_the_panel_reads_a_changed_folder_before_it_writes_and_quotes_real_errors()
    test_the_panel_keeps_a_typed_password_and_says_the_file_still_holds_the_old_one()
    test_bridge_never_touches_secrets_or_logs_the_password()
    test_the_main_menu_routes_the_server_settings_actions()
    test_the_host_panel_renders_the_section_without_duplicating_rules()
    test_the_panel_is_shipped_and_tested()
    test_controller_input_reaches_the_new_controls()
    test_the_p2p_listing_lock_is_read_shown_and_preserved()
    test_the_save_path_can_restore_the_raw_listing_key()
    print("Server settings bridge contract passed")


def test_the_p2p_listing_lock_is_read_shown_and_preserved() -> None:
    bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
    assert '"publicListingLocked"' in bridge, "the bridge must read the running lock sibling"
    assert "running.publicListingLocked" in bridge, "the bridge must carry the running lock"
    assert "if (running.publicListingLocked)" in bridge, "the panel must react to the running lock"
    assert "RawPublicListingRestorer(configPath)" in bridge, (
        "a save on a locked P2P host must preserve the file's listing key"
    )
    assert "SaveSettings(configPath, values, &ApplyLiveSettings, running.document, adjustDocument)" in bridge, (
        "the raw-key restorer must run inside the shared save path"
    )
    assert 'a_document.erase("public_listing")' in bridge and 'a_document.erase("publicListing")' in bridge, (
        "the restorer must clear both key spellings before it puts the saved one back"
    )

    settings = read(VIEWS / "components/server-settings.js")
    assert "settings.listingLocked === true" in settings, "the listing toggle must pin off while locked"
    assert "Steam P2P hosts are never listed." in settings, "the locked panel must explain why"

    panel_tests = read(VIEWS / "tests/server-settings.test.js")
    assert "publicListingLocked = true" in panel_tests
    assert "a locked Steam P2P panel" in panel_tests


def test_the_save_path_can_restore_the_raw_listing_key() -> None:
    header = read(ROOT / "co-core/include/F4TServerSettingsFile.h")
    assert "using DocumentAdjuster = std::function<void(nlohmann::json& a_document)>;" in header
    assert "const DocumentAdjuster& a_adjustDocument" in header, (
        "SaveSettings must offer a document adjuster for the locked P2P save"
    )
    settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
    sequence = settings[settings.index("SaveOutcome SaveSettings("):]
    checked = sequence.index("if (!plan.worldModeRefusal.empty())")
    adjusted = sequence.index("a_adjustDocument(plan.document)")
    asked = sequence.index("a_liveSettings(plan.document")
    written = sequence.index("CommitSave(a_path, plan)")
    assert checked < adjusted < asked < written, (
        "the adjuster must run after the world-mode check and before the live change and the write"
    )


if __name__ == "__main__":
    main()
