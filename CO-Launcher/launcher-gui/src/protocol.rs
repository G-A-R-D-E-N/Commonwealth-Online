use launcher_core::Runtime;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

use crate::listing::ListingView;
use crate::locations::Locations;
use crate::settings::Settings;

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
pub enum Command {
    Init,
    Play,
    Mods,
    OpenMo2,
    Crashes,
    ShareLogs,
    RevealLogs,
    EndServers,
    ClearLinks,
    RepairServer,
    ResetFolder,
    CheckServerFiles,
    CheckSetup,
    Server,
    ServerNetwork {
        mode: HostMode,
    },
    ServerStart,
    ServerAdmin {
        command: String,
        #[serde(default)]
        args: Map<String, Value>,
    },
    ServerSave {
        settings: Settings,
        #[serde(default)]
        restart: bool,
        #[serde(default = "default_host_mode")]
        mode: HostMode,
    },
    ServerRestart,
    UpdateCheck,
    UpdateInstallLauncher,
    UpdateSkip {
        version: String,
    },
    UpdateInstallMod {
        #[serde(default)]
        adopt_unknown: bool,
    },
    UpdateRollbackMod,
    LocationsGet,
    LocationsRevealVortex,
    LocationsSave {
        locations: Locations,
    },
    UpdateSettings {
        include_prereleases: bool,
        auto_install: bool,
        #[serde(default)]
        notify_only: bool,
    },
    Quit,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HostMode {
    P2p,
    Dedicated,
}

fn default_host_mode() -> HostMode {
    HostMode::P2p
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum Task {
    Check,
    Play,
    Mods,
    OpenMo2,
    Crashes,
    Share,
    EndServers,
    ClearLinks,
    RepairServer,
    ResetFolder,
    CheckServerFiles,
    CheckSetup,
    Server,
    Network,
    Start,
    Admin,
    Save,
    Restart,
    UpdateCheck,
    UpdateInstall,
    UpdateInstallMod,
    UpdateRollbackMod,
    UpdateSkip,
    UpdateSettings,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(
    tag = "type",
    rename_all = "snake_case",
    rename_all_fields = "camelCase"
)]
pub enum Event {
    Mods {
        managers: Vec<ManagerView>,
        warning: &'static str,
    },
    Crashes {
        list: Vec<CrashView>,
    },
    LogsShared {
        path: String,
        files: usize,
    },
    FixResult {
        what: Task,
        ok: bool,
        message: String,
        list: Vec<ServerFileView>,
    },
    Busy {
        on: bool,
        what: Task,
    },
    Server(Box<ServerView>),
    ServerNetwork {
        checks: Vec<CheckView>,
    },
    SetupChecks {
        checks: Vec<CheckView>,
    },
    ServerReply {
        ok: bool,
        text: String,
        data: Option<Value>,
    },
    State(Box<StateView>),
    Error {
        message: String,
    },
    Locations {
        locations: Locations,
        message: String,
    },
    LocationsNotice {
        message: String,
    },
    UpdatePrefs {
        include_prereleases: bool,
        auto_install: bool,
        notify_only: bool,
        launcher_version: String,
        signing_configured: bool,
        #[serde(flatten)]
        channel: ModChannel,
    },
    UpdateAvailable {
        version: String,
        prerelease: bool,
        launcher_update: bool,
        mod_update: bool,
        notes: Option<String>,
        signing_configured: bool,
        #[serde(flatten)]
        channel: ModChannel,
    },
    UpdateProgress {
        what: &'static str,
        done: u64,
        total: u64,
    },
    UpdateResult {
        ok: bool,
        message: String,
        #[serde(flatten)]
        channel: ModChannel,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ModChannel {
    pub channel: &'static str,
    pub channel_reasons: Vec<String>,
    pub installed_version: Option<String>,
    pub can_install_mod: bool,
    pub needs_adopt_confirm: bool,
}

impl ModChannel {
    pub fn none() -> Self {
        ModChannel {
            channel: "unknown",
            channel_reasons: Vec::new(),
            installed_version: None,
            can_install_mod: false,
            needs_adopt_confirm: false,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ProfileView {
    pub name: String,
    pub enabled_mods: Option<usize>,
    pub active: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct CrashView {
    pub file: String,
    pub time: String,
    pub exception: String,
    pub fault: String,
    pub culprit: &'static str,
    pub verdict: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ManagerView {
    pub name: String,
    pub dir: String,
    pub exe: Option<String>,
    pub manages_this_game: bool,
    pub ours: bool,
    pub profiles: Vec<ProfileView>,
    pub profiles_listed: bool,
    pub detail: Option<DetailView>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct DetailView {
    pub profile: String,
    pub dir: String,
    pub f4se_note: String,
    pub files: Vec<String>,
    pub mods: Vec<ModView>,
    pub plugins: Vec<PluginView>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ModView {
    pub name: String,
    pub enabled: bool,
    pub separator: bool,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct PluginView {
    pub name: String,
    pub enabled: bool,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ServerView {
    pub folder: Option<String>,
    pub mo2_instance: Option<String>,
    pub mo2_profile: Option<String>,
    pub installed: bool,
    pub bundled: bool,
    pub note: String,
    pub problem: Option<String>,
    pub running: bool,
    pub starting: bool,
    pub stopping: bool,
    pub status: Option<StatusView>,
    pub listing: Option<ListingView>,
    pub settings: Option<Settings>,
    pub players: Vec<PlayerView>,
    pub bans: Vec<BanView>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StatusView {
    pub name: String,
    pub players: u64,
    pub max_players: u64,
    pub world_mode: String,
    pub port: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct PlayerView {
    pub id: u64,
    pub address: String,
    pub steam_id: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct BanView {
    pub kind: &'static str,
    pub id: String,
    pub reason: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct CheckView {
    pub level: &'static str,
    pub text: String,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct ServerFileView {
    pub path: String,
    pub size: u64,
    pub modified: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct StateView {
    pub game_dir: Option<String>,
    pub error: Option<String>,
    pub exe_version: Option<String>,
    pub sha256: Option<String>,
    pub runtime: Option<RuntimeView>,
    pub unsupported: Option<String>,
    pub can_play: bool,
    pub play_note: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct RuntimeView {
    pub label: &'static str,
    pub game_version: &'static str,
}

impl RuntimeView {
    pub fn of(runtime: Runtime) -> Self {
        Self {
            label: runtime.label(),
            game_version: runtime.game_version(),
        }
    }
}

pub fn script(event: &Event) -> String {
    let json = serde_json::to_string(event).unwrap_or_else(|_| String::from("null"));
    format!("window.coLauncher&&window.coLauncher.onEvent({json})")
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn command(text: &str) -> Option<Command> {
        serde_json::from_str(text).ok()
    }

    #[test]
    fn parses_every_contract_command() {
        assert_eq!(command(r#"{"cmd":"init"}"#), Some(Command::Init));
        assert_eq!(command(r#"{"cmd":"check"}"#), None);
        assert_eq!(command(r#"{"cmd":"play"}"#), Some(Command::Play));
        assert_eq!(command(r#"{"cmd":"quit"}"#), Some(Command::Quit));
        assert_eq!(command(r#"{"cmd":"mods"}"#), Some(Command::Mods));
        assert_eq!(command(r#"{"cmd":"open_mo2"}"#), Some(Command::OpenMo2));
        assert_eq!(command(r#"{"cmd":"crashes"}"#), Some(Command::Crashes));
        assert_eq!(command(r#"{"cmd":"share_logs"}"#), Some(Command::ShareLogs));
        assert_eq!(
            command(r#"{"cmd":"reveal_logs"}"#),
            Some(Command::RevealLogs)
        );
        assert_eq!(command(r#"{"cmd":"server"}"#), Some(Command::Server));
        assert_eq!(
            command(r#"{"cmd":"server_network","mode":"p2p"}"#),
            Some(Command::ServerNetwork {
                mode: HostMode::P2p
            })
        );
        assert_eq!(command(r#"{"cmd":"server_network","mode":"x"}"#), None);
        assert!(matches!(
            command(r#"{"cmd":"server_admin","command":"kick","args":{"playerId":3}}"#),
            Some(Command::ServerAdmin { command, args }) if command == "kick" && args.len() == 1
        ));
        assert_eq!(
            command(r#"{"cmd":"server_start"}"#),
            Some(Command::ServerStart)
        );
        assert_eq!(
            command(r#"{"cmd":"server_restart"}"#),
            Some(Command::ServerRestart)
        );
    }

    #[test]
    fn parses_the_update_commands() {
        assert_eq!(
            command(r#"{"cmd":"update_check"}"#),
            Some(Command::UpdateCheck)
        );
        assert_eq!(
            command(r#"{"cmd":"update_install_launcher"}"#),
            Some(Command::UpdateInstallLauncher)
        );
        assert_eq!(
            command(r#"{"cmd":"update_skip","version":"0.0.0.8"}"#),
            Some(Command::UpdateSkip {
                version: "0.0.0.8".to_owned()
            })
        );
        assert_eq!(command(r#"{"cmd":"update_skip"}"#), None);
        assert_eq!(
            command(r#"{"cmd":"update_settings","include_prereleases":false,"auto_install":true}"#),
            Some(Command::UpdateSettings {
                include_prereleases: false,
                auto_install: true,
                notify_only: false
            })
        );
        assert_eq!(
            command(
                r#"{"cmd":"update_settings","include_prereleases":true,"auto_install":false,"notify_only":true}"#
            ),
            Some(Command::UpdateSettings {
                include_prereleases: true,
                auto_install: false,
                notify_only: true
            })
        );
        assert_eq!(
            command(r#"{"cmd":"update_settings","auto_install":true}"#),
            None
        );
        assert_eq!(
            command(r#"{"cmd":"update_install_mod"}"#),
            Some(Command::UpdateInstallMod {
                adopt_unknown: false
            })
        );
        assert_eq!(
            command(r#"{"cmd":"update_install_mod","adopt_unknown":true}"#),
            Some(Command::UpdateInstallMod {
                adopt_unknown: true
            })
        );
        assert_eq!(
            command(r#"{"cmd":"update_rollback_mod"}"#),
            Some(Command::UpdateRollbackMod)
        );
        assert_eq!(
            command(r#"{"cmd":"update_install_mod","adopt_unknown":"yes"}"#),
            None
        );
    }

    #[test]
    fn parses_the_troubleshoot_commands() {
        assert_eq!(
            command(r#"{"cmd":"end_servers"}"#),
            Some(Command::EndServers)
        );
        assert_eq!(
            command(r#"{"cmd":"clear_links"}"#),
            Some(Command::ClearLinks)
        );
        assert_eq!(
            command(r#"{"cmd":"repair_server"}"#),
            Some(Command::RepairServer)
        );
        assert_eq!(
            command(r#"{"cmd":"reset_folder"}"#),
            Some(Command::ResetFolder)
        );
        assert_eq!(
            command(r#"{"cmd":"check_server_files"}"#),
            Some(Command::CheckServerFiles)
        );
        assert_eq!(
            command(r#"{"cmd":"check_setup"}"#),
            Some(Command::CheckSetup)
        );
    }

    #[test]
    fn server_save_carries_the_whole_settings_form_in_camel_case() {
        let form = json!({
            "name": "N", "description": "", "maxPlayers": 8, "password": "", "port": 7777,
            "worldMode": "free-roam", "vats": "vanilla", "doorsShared": true,
            "friendlyFire": false, "factionFriendlyFire": false, "chat": true, "profanityFilter": false,
            "factions": true, "persistence": true, "steamFriendsOnly": true,
            "downedSeconds": 60, "reviveSeconds": 3
        });
        let text = json!({"cmd": "server_save", "settings": form.clone()}).to_string();
        assert!(matches!(
            command(&text),
            Some(Command::ServerSave { settings, mode: HostMode::P2p, .. }) if settings.max_players == 8 && !settings.friendly_fire
        ));
        let dedicated =
            json!({"cmd": "server_save", "settings": form, "mode": "dedicated"}).to_string();
        assert!(matches!(
            command(&dedicated),
            Some(Command::ServerSave {
                mode: HostMode::Dedicated,
                ..
            })
        ));
        let missing = json!({"cmd": "server_save", "settings": {"name": "N"}}).to_string();
        assert_eq!(command(&missing), None);
    }

    #[test]
    fn rejects_unknown_and_removed_commands() {
        for removed in [
            "launch",
            "install",
            "rollback",
            "scan",
            "licence",
            "watch",
            "select_game",
            "save_settings",
            "finish_setup",
            "reset_setup",
            "open_url",
            "server_console",
            "update",
            "install_update",
            "install_mod",
            "update_rollback",
        ] {
            assert_eq!(command(&format!(r#"{{"cmd":"{removed}"}}"#)), None);
        }
        assert_eq!(command("not json"), None);
    }

    #[test]
    fn busy_event_names_the_task_in_snake_case() {
        assert_eq!(
            serde_json::to_value(Event::Busy {
                on: true,
                what: Task::OpenMo2,
            })
            .unwrap(),
            json!({"type":"busy","on":true,"what":"open_mo2"})
        );
        assert_eq!(
            serde_json::to_value(Event::Busy {
                on: false,
                what: Task::Share,
            })
            .unwrap(),
            json!({"type":"busy","on":false,"what":"share"})
        );
    }

    #[test]
    fn state_event_matches_contract() {
        let view = StateView {
            game_dir: Some("C:/g".to_owned()),
            error: None,
            exe_version: Some("1.10.163".to_owned()),
            sha256: Some("ab".to_owned()),
            runtime: Some(RuntimeView::of(Runtime::Og)),
            unsupported: None,
            can_play: false,
            play_note: Some("x".to_owned()),
        };
        assert_eq!(
            serde_json::to_value(Event::State(Box::new(view))).unwrap(),
            json!({
                "type": "state",
                "gameDir": "C:/g",
                "error": null,
                "exeVersion": "1.10.163",
                "sha256": "ab",
                "runtime": {"label":"OG","gameVersion":"1.10.163"},
                "unsupported": null,
                "canPlay": false,
                "playNote": "x"
            })
        );
    }

    #[test]
    fn update_events_flatten_the_mod_channel_in_camel_case() {
        let channel = ModChannel {
            channel: "standalone",
            channel_reasons: vec!["why".to_owned()],
            installed_version: Some("0.0.0.7".to_owned()),
            can_install_mod: true,
            needs_adopt_confirm: false,
        };
        assert_eq!(
            serde_json::to_value(Event::UpdateAvailable {
                version: "0.0.0.8".to_owned(),
                prerelease: false,
                launcher_update: true,
                mod_update: true,
                notes: None,
                signing_configured: false,
                channel: channel.clone(),
            })
            .unwrap(),
            json!({
                "type": "update_available",
                "version": "0.0.0.8",
                "prerelease": false,
                "launcherUpdate": true,
                "modUpdate": true,
                "notes": null,
                "signingConfigured": false,
                "channel": "standalone",
                "channelReasons": ["why"],
                "installedVersion": "0.0.0.7",
                "canInstallMod": true,
                "needsAdoptConfirm": false
            })
        );
        assert_eq!(
            serde_json::to_value(Event::UpdateResult {
                ok: false,
                message: "m".to_owned(),
                channel: ModChannel::none(),
            })
            .unwrap(),
            json!({
                "type": "update_result",
                "ok": false,
                "message": "m",
                "channel": "unknown",
                "channelReasons": [],
                "installedVersion": null,
                "canInstallMod": false,
                "needsAdoptConfirm": false
            })
        );
    }

    #[test]
    fn update_prefs_carries_the_channel_so_the_page_can_show_rollback_first() {
        assert_eq!(
            serde_json::to_value(Event::UpdatePrefs {
                include_prereleases: true,
                auto_install: false,
                notify_only: true,
                launcher_version: "0.0.0.7".to_owned(),
                signing_configured: false,
                channel: ModChannel {
                    channel: "standalone",
                    channel_reasons: Vec::new(),
                    installed_version: Some("0.0.0.7".to_owned()),
                    can_install_mod: false,
                    needs_adopt_confirm: false,
                },
            })
            .unwrap(),
            json!({
                "type": "update_prefs",
                "includePrereleases": true,
                "autoInstall": false,
                "notifyOnly": true,
                "launcherVersion": "0.0.0.7",
                "signingConfigured": false,
                "channel": "standalone",
                "channelReasons": [],
                "installedVersion": "0.0.0.7",
                "canInstallMod": false,
                "needsAdoptConfirm": false
            })
        );
    }

    #[test]
    fn error_event_matches_contract() {
        let error = Event::Error {
            message: "m".to_owned(),
        };
        assert_eq!(
            serde_json::to_value(error).unwrap(),
            json!({"type":"error","message":"m"})
        );
    }

    #[test]
    fn script_calls_the_page_hook_with_event_json() {
        let script = script(&Event::Error {
            message: "m".to_owned(),
        });
        assert_eq!(
            script,
            r#"window.coLauncher&&window.coLauncher.onEvent({"type":"error","message":"m"})"#
        );
    }
}
