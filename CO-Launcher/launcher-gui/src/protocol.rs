use launcher_core::Runtime;
use serde::{Deserialize, Serialize};
use serde_json::{Map, Value};

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
    },
    ServerRestart,
    Quit,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HostMode {
    P2p,
    Dedicated,
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
    Server,
    Network,
    Start,
    Admin,
    Save,
    Restart,
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
    Busy {
        on: bool,
        what: Task,
    },
    Server(Box<ServerView>),
    ServerNetwork {
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
    pub installed: bool,
    pub bundled: bool,
    pub note: String,
    pub problem: Option<String>,
    pub running: bool,
    pub status: Option<StatusView>,
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
    fn server_save_carries_the_whole_settings_form_in_camel_case() {
        let form = json!({
            "name": "N", "description": "", "maxPlayers": 8, "password": "", "port": 7777,
            "worldMode": "free-roam", "vats": "vanilla", "doorsShared": true,
            "friendlyFire": false, "factionFriendlyFire": false, "chat": true,
            "factions": true, "persistence": true, "steamFriendsOnly": true,
            "downedSeconds": 60, "reviveSeconds": 3
        });
        let text = json!({"cmd": "server_save", "settings": form}).to_string();
        assert!(matches!(
            command(&text),
            Some(Command::ServerSave { settings, .. }) if settings.max_players == 8 && !settings.friendly_fire
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
