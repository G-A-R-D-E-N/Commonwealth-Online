#!/usr/bin/env python3
"""test_server_auth_settings_contracts_part2: consolidated contract sections.

Consolidated contract sections. Every assertion below is the original file's, moved verbatim
into the labelled section it came from, so a failure still names the contract
that broke.

Run it directly, exactly like the scripts it replaces:

    python tests/audit/test_server_auth_settings_contracts_part2.py
"""
from __future__ import annotations
import os
import sys
import traceback
from pathlib import Path

_MERGE_ROOT = Path(__file__).resolve().parents[2]


class _SectionExit(Exception):
    """Raised where a moved contract used to call sys.exit()/raise SystemExit."""

    def __init__(self, code=None):
        super().__init__(code)
        self.code = code


def _merge_exit(code=None):
    raise _SectionExit(code)


def _run_section(name, section):
    cwd = os.getcwd()
    environ = dict(os.environ)
    ok = True
    try:
        section()
    except _SectionExit as exc:
        ok = exc.code in (0, None)
        if not ok:
            print("FAIL: " + name + " exited with " + repr(exc.code))
    except BaseException as exc:  # noqa: BLE001 - a bad contract must not stop the rest
        if isinstance(exc, KeyboardInterrupt):
            raise
        traceback.print_exc()
        ok = False
    finally:
        os.chdir(cwd)
        if dict(os.environ) != environ:
            os.environ.clear()
            os.environ.update(environ)
    print(("PASS " if ok else "FAIL ") + name)
    return ok




def _section_007_test_server_settings_bridge_contract():
    """Contract moved verbatim from tests/audit/test_server_settings_bridge_contract.py."""
    __file__ = str(_MERGE_ROOT / "tests/audit/test_server_settings_bridge_contract.py")
    import re
    from pathlib import Path

    ROOT = Path(__file__).resolve().parents[2]
    UI = ROOT / "CommonwealthOnlineUI"
    VIEWS = UI / "ui/views/CommonwealthOnline/browser"


    def read(path: Path) -> str:
        return path.read_text(encoding="utf-8")


    def body(text: str, signature: str) -> str:
        start = text.index(signature)
        open_brace = text.index("{", start)
        depth = 0
        for index in range(open_brace, len(text)):
            depth += {"{": 1, "}": -1}.get(text[index], 0)
            if depth == 0:
                return text[open_brace : index + 1]
        raise AssertionError(f"unterminated body for {signature}")


    def public_workflow(name: str) -> str:
        for directory in (".github/public-ci/workflows", ".github/workflows"):
            if (ROOT / directory / name).is_file():
                return read(ROOT / directory / name)
        raise AssertionError(f"workflow not found: {name}")


    def test_ui_build_links_the_shared_configuration_module() -> None:
        xmake = read(ROOT / "CO-BETA/plugin/xmake.lua")
        assert 'add_files(path.join(ui_dir, "src/**.cpp"))' in xmake, "the plugin DLL does not build the UI sources"
        assert 'add_deps("CommonwealthOnlineCore")' in xmake, "plugin build does not link CommonwealthOnlineCore"
        assert 'add_requires("nlohmann_json v3.12.0")' in xmake, "plugin package version differs from co-core"
        assert not (UI / "xmake.lua").exists(), "the UI no longer builds its own DLL"


    def test_the_schema_has_one_implementation() -> None:
        shared = read(ROOT / "co-core/src/config/F4TServerConfiguration.cpp") + read(ROOT / "co-core/src/config/F4TServerConfigModpack.cpp")
        assert "namespace F4T::ServerConfiguration" in shared
        assert "std::vector<std::string> Validate(" in shared
        assert "server_password must be 4-64 printable ASCII characters when set" in shared

        server_header = read(ROOT / "CO-SERVER/server-cpp/include/ServerConfiguration.h")
        assert "using F4T::ServerConfiguration::Validate;" in server_header, "server does not reuse the shared rules"
        assert "struct Options" not in server_header, "the server still owns a second Options definition"

        server_source = read(ROOT / "CO-SERVER/server-cpp/src/ServerConfiguration.cpp")
        assert "F4T::ServerConfiguration::Parse(a_root, a_baseDirectory)" in server_source
        assert "F4T::ServerConfiguration::LoadConfiguredRequiredModpack" in server_source, "the server lost its modpack manifest loading"
        assert "required_modpack_path" in shared, "the shared configuration module lost the modpack path field"
        for rule in ("must be <= 64 characters", "max_players must be 1-100", "downed_seconds must be 5-600"):
            assert rule not in server_source, "server wrapper duplicates a validation rule: " + rule


    def test_dedicated_servers_still_load_the_json_file() -> None:
        arguments = read(ROOT / "CO-SERVER/server-cpp/src/ServerArguments.cpp")
        assert "Configuration::Parse(root, a_path.parent_path())" in arguments
        assert "Could not open server configuration '" in arguments


    def test_bridge_uses_the_shared_save_path_and_reports_its_errors() -> None:
        bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
        assert "const auto outcome = SaveSettings(configPath, values" in bridge
        assert "ReadDocument(configPath)" in bridge
        assert "ServerConfiguration::Validate(options, configuredModpack)" in bridge, "the panel does not report the server's own errors"
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
        host = bridge[bridge.index("std::filesystem::path HostServerDirectory()") : bridge.index("void HandleAction(")]
        assert host.index("ReadStoredDirectory()") < host.index("F4T::UserPaths::PackagedServerDirectory()"), (
            "the folder the panel saved is the host folder, with Data/server as the default"
        )
        assert "kServerExecutableName" in host, "only a folder that can start the server may be the host folder"
        paths = read(UI / "src/F4TUserPaths.cpp")
        assert 'L"server"' in paths and "GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS" in paths
        assert "GetModuleFileNameW(nullptr" in paths, (
            "the panel must read the game's own Data folder, which Mod Organizer maps onto the server mod"
        )
        packaged = paths[paths.index("std::filesystem::path PackagedServerDirectory()"):]
        assert packaged.index("GameDataDirectory()") < packaged.index("ModuleDataDirectory()"), (
            "the game's Data folder must be resolved before the plugin folder fallback"
        )
        probe = read(ROOT / "CO-BETA/plugin/src/F4TClientModpackProbe.cpp")
        assert "F4T::UserPaths::GameDataDirectory()" in probe and "F4T::UserPaths::ModuleDataDirectory()" in probe, (
            "the modpack probe must use the one Data folder rule"
        )
        assert "std::optional<std::filesystem::path> GameExecutableDirectory()" not in probe, (
            "the probe must not keep its own Data folder helper"
        )
        launcher = read(ROOT / "CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
        assert "F4T::ServerSettings::HostServerDirectory()" in launcher, (
            "the host must start the server from the folder the panel shows"
        )
        assert "F4T::UserPaths::PackagedServerDirectory()" not in launcher, (
            "the launcher must not resolve a folder of its own"
        )
        assert "parent_path().parent_path().parent_path()" not in launcher, (
            "the launcher must not guess the server folder from its own module path"
        )
        get = bridge[bridge.index("void ProcessGet(") : bridge.index("void ProcessSave(")]
        assert "WriteStoredDirectory(directory);" in get, (
            "the folder the panel finds by itself must be remembered, so the player never types it"
        )
        assert "!requestedFolder && !running.directory" in get, (
            "the auto-remember must use the resolver's own answer, not a second read of the request"
        )
        assert 'a_request.value("serverDirectory"' not in get, (
            "the request must be read once, inside ChosenDirectory"
        )



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
        runtime = read(ROOT / "CO-SERVER/server-cpp/src/ServerRuntime.inc").replace('#include "ServerRuntimeMessage.inc"', read(ROOT / "CO-SERVER/server-cpp/src/ServerRuntimeMessage.inc"))
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
                           "co-ss-doors-shared", "co-ss-survival", "co-ss-survival-needs", "co-ss-friendly-fire", "co-ss-faction-friendly-fire",
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

        workflow = public_workflow("co-ui-ci.yml")
        assert "components/server-settings.js" in workflow
        assert "server-settings-controller.test.js" in workflow, "co-ui-ci.yml does not run the server settings controller test"

        regressions = public_workflow("co-audit-regressions.yml")
        assert "node --test *.test.js" in regressions


    def test_controller_input_reaches_the_new_controls() -> None:
        navigation = read(VIEWS / "components/controller-navigation.js")
        assert "CO_ServerSettings.ownsField(id)" in navigation
        assert "CO_ServerSettings.activate(root, id, state)" in navigation
        prompts = read(VIEWS / "components/controller-prompts.js")
        assert "CO_ServerSettings.promptLabel(steamId)" in prompts


    def test_hosting_starts_the_folder_the_panel_saved() -> None:
        launcher = read(ROOT / "CO-BETA/plugin/src/F4TLocalServerLauncher.cpp")
        assert "F4T::ServerSettings::HostServerDirectory()" in launcher, (
            "hosting must start the folder the panel saves, not one of its own"
        )
        assert "F4T::UserPaths::PackagedServerDirectory()" not in launcher, (
            "a second folder rule in the launcher is how hosting and the panel drift apart"
        )
        bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
        choose = bridge[bridge.index("std::filesystem::path ChosenDirectory(") : bridge.index("Json LimitsJson(")]
        assert "HostServerDirectory()" in choose, "the panel must resolve the same host folder hosting starts"
        save = bridge[bridge.index("void ProcessSave(") : bridge.index("void ProcessAction(")]
        assert save.index("SaveSettings(configPath, values") < save.index("WriteStoredDirectory(directory);"), (
            "the folder is remembered only after the settings were written into it"
        )
        assert save.index("WriteStoredDirectory(directory);") < save.index("Reply(a_request, std::move(payload), true, {})"), (
            "the host folder must be saved before the panel reports success"
        )


    def test_the_panel_says_when_a_deploy_moved_the_folder() -> None:
        bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
        stored = bridge[bridge.index("std::filesystem::path ReadStoredDirectory(") : bridge.index("void WriteStoredDirectory")]
        assert 'root.find("movedByDeploy")' in stored, (
            "the deploy marker must be read from the same store the panel writes"
        )
        get = bridge[bridge.index("void ProcessGet(") : bridge.index("void ProcessSave(")]
        assert 'payload["folderMoved"] = true;' in get, "the panel must be told the deploy moved the folder"
        assert get.index("WriteStoredDirectory(directory);") < get.index('payload["folderMoved"]'), (
            "the marker is cleared before the notice is sent, so the panel says it once"
        )
        assert "else if (moved && !stored.empty())" in get, (
            "a server that is already running must not keep the notice for every later read"
        )
        panel = read(VIEWS / "components/server-settings.js")
        assert "settings.folderMoved = event.data.folderMoved === true;" in panel, (
            "a settings reply carries the deploy notice"
        )
        section = panel[panel.index("function section(state, focusedId)") : panel.index("function isNumeric(")]
        assert 'id="co-ss-folder-moved"' in section, "the card must show the deploy notice"
        assert "A server deploy moved Server settings to this folder." in section


    def test_a_folder_without_the_server_executable_is_not_a_host_folder() -> None:
        bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
        looks = bridge[bridge.index("bool LooksLikeServerPackage(") : bridge.index("std::filesystem::path ReadStoredDirectory(")]
        assert "kServerExecutableName" in looks, "a folder the launcher cannot start must not be accepted"
        assert "kConfigFileName" not in looks, (
            "a config-only folder would be saved while hosting starts from Data\\server"
        )
        assert "no CommonwealthOnline.Server.exe" in bridge, (
            "the refusal must name the executable the folder is missing"
        )


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
        test_hosting_starts_the_folder_the_panel_saved()
        test_the_panel_says_when_a_deploy_moved_the_folder()
        test_a_folder_without_the_server_executable_is_not_a_host_folder()
        print("Server settings bridge contract passed")


    def test_the_p2p_listing_lock_is_read_shown_and_preserved() -> None:
        bridge = read(UI / "src/F4TServerSettingsBridge.cpp")
        assert '"publicListingLocked"' in bridge, "the bridge must read the running lock sibling"
        assert "running.publicListingLocked" in bridge, "the bridge must carry the running lock"
        assert "if (running.publicListingLocked)" in bridge, "the panel must react to the running lock"
        assert "if (!RawPublicListingRestorer(configPath, adjustDocument, restorerError))" in bridge, (
            "a locked P2P save must preserve the file's listing key, and a file that cannot be read must refuse the save"
        )
        assert "Reply(a_request, Json(), false, { restorerError });" in bridge, (
            "the restorer's error must reach the panel instead of an empty adjuster letting the form value through"
        )
        assert "SaveSettings(configPath, values, &ApplyLiveSettings, running.document, adjustDocument)" in bridge, (
            "the raw-key restorer must run inside the shared save path"
        )
        assert "ServerSettings::DocumentAdjuster RawPublicListingRestorer(" not in bridge, (
            "the restorer belongs to co-core, where its behaviour can be tested without the game"
        )

        header = read(ROOT / "co-core/include/F4TServerSettingsFile.h")
        assert "[[nodiscard]] bool RawPublicListingRestorer(" in header, "co-core must own the raw-listing restore"
        assert header.index("using DocumentAdjuster") < header.index("bool RawPublicListingRestorer("), (
            "the restorer is declared next to the adjuster it fills"
        )
        settings = read(ROOT / "co-core/src/config/F4TServerSettingsFile.cpp")
        restorer = body(settings, "bool RawPublicListingRestorer(")
        for spelling in ('a_document.erase("public_listing")', 'a_document.erase("publicListing")'):
            assert spelling in restorer, (
                "the restorer must clear both key spellings before it puts the saved one back"
            )
        assert "a_adjustDocument = {};" in restorer, "the restorer must start with no adjuster"
        absent = restorer.index("if (!present) {")
        assert 'a_document.erase("public_listing")' in restorer[absent:absent + 400], (
            "an absent file must leave the listing key out instead of writing the form value"
        )
        assert "ReadDocument(a_path)" in restorer, "the restorer must read the file it is preserving"
        assert "UnchangedMessage(" in restorer, "a refused restore must say nothing was written"

        settings_panel = read(VIEWS / "components/server-settings.js")
        assert "settings.listingLocked === true" in settings_panel, "the listing toggle must pin off while locked"
        assert "Steam P2P hosts are never listed." in settings_panel, "the locked panel must explain why"

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
        reparsed = sequence.index("ServerConfiguration::Parse(plan.document, a_path.parent_path())", adjusted)
        listed = sequence.index("ServerConfiguration::ListedTextSaveErrors(adjusted)")
        asked = sequence.index("a_liveSettings(plan.document")
        written = sequence.index("CommitSave(a_path, plan)")
        assert checked < adjusted < reparsed < listed < asked < written, (
            "the adjuster runs after the world-mode check, and the adjusted document is checked before the live change and the write"
        )
        guard = sequence[adjusted:asked]
        assert "return SaveOutcome{" in guard, "a listed name the restored key would make invalid must abandon the save"
        assert "ServerConfiguration::Validate(" not in guard, (
            "the post-adjuster check must not re-run the whole schema: the listing-only directory_url rule would fail every P2P save"
        )
        assert "ListedTextSaveErrors(" in guard and "Nothing was changed" not in guard, (
            "the refusal wording comes from UnchangedMessage, like the other refusals"
        )

        config = read(ROOT / "co-core/src/config/F4TServerConfiguration.cpp")
        messages = body(config, "std::string ListedTextSaveMessage(")
        for message in (
            "server_name is blank while the file's public_listing is on",
            "server_name must be 1-64 printable UTF-8 bytes while the file's public_listing is on",
            "server_description must be at most 256 printable UTF-8 bytes while the file's public_listing is on",
        ):
            assert message in messages, "the save-time listed-text error must name the field and the fix: " + message
        listed_errors = body(config, "std::vector<std::string> ListedTextSaveErrors(")
        assert "if (!a_options.publicListing)" in listed_errors, (
            "the save-time check must only run while the file lists the server"
        )
        assert "CollectListedTextSaveErrors(" in listed_errors
        for unrelated in ("directory_url", "IsAllowedDirectoryUrl", "host must be", "max_players"):
            assert unrelated not in listed_errors, (
                "the save-time check must stay narrow to the listed name and description: " + unrelated
            )


    if True:
        main()


_SECTIONS = [
    ("tests/audit/test_server_settings_bridge_contract.py", _section_007_test_server_settings_bridge_contract),
]


def main():
    failures = 0
    for name, section in _SECTIONS:
        if not _run_section(name, section):
            failures += 1
    print(
        "test_server_auth_settings_contracts_part2: "
        + str(len(_SECTIONS) - failures)
        + "/"
        + str(len(_SECTIONS))
        + " contract sections passed"
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
