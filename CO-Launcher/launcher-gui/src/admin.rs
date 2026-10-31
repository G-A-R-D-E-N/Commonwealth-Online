use std::fs;
use std::io::{BufRead, BufReader, Read, Write};
use std::net::{SocketAddr, TcpStream};
use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use serde_json::{Map, Value};

pub const DEFAULT_ADMIN_PORT: u16 = 7779;
pub const DEFAULT_GAME_PORT: u16 = 7777;
pub const NOT_RUNNING: &str = "The server is not running.";
const TOKEN: &str = ".admin-token";
const TIMEOUT: Duration = Duration::from_secs(3);
const MAX_REPLY: u64 = 1 << 20;
const UNAUTHORIZED: &str = "Unauthorized admin request.";

pub type Args = Map<String, Value>;
pub type Request = (String, Args);

pub struct Endpoint {
    port: u16,
    tokens: Vec<String>,
}

pub fn config_number(dirs: &[PathBuf], keys: &[&str]) -> Option<u16> {
    dirs.iter().find_map(|dir| {
        let map = crate::settings::read_object(dir)?;
        let value = crate::settings::pick(&map, keys)?;
        u16::try_from(value.as_u64()?).ok()
    })
}

pub fn admin_port(dirs: &[PathBuf]) -> u16 {
    config_number(dirs, &["admin_port", "adminPort"]).unwrap_or(DEFAULT_ADMIN_PORT)
}

pub fn game_port(dirs: &[PathBuf]) -> u16 {
    config_number(dirs, &["port"]).unwrap_or(DEFAULT_GAME_PORT)
}

pub fn endpoint(dirs: &[PathBuf]) -> Option<Endpoint> {
    let mut found: Vec<_> = dirs.iter().filter_map(|dir| token_in(dir)).collect();
    found.sort_by_key(|(at, _)| std::cmp::Reverse(*at));
    let mut tokens: Vec<String> = found.into_iter().map(|(_, token)| token).collect();
    tokens.dedup();
    if tokens.is_empty() {
        return None;
    }
    Some(Endpoint {
        port: admin_port(dirs),
        tokens,
    })
}

fn token_in(dir: &Path) -> Option<(SystemTime, String)> {
    let path = dir.join(TOKEN);
    let token = fs::read_to_string(&path).ok()?.trim().to_owned();
    let at = fs::metadata(&path).ok()?.modified().ok()?;
    (!token.is_empty()).then_some((at, token))
}

pub fn call(endpoint: &Endpoint, command: &str, args: &Args) -> Result<Value, String> {
    let mut last = Err(NOT_RUNNING.to_owned());
    for token in &endpoint.tokens {
        last = send(endpoint.port, token, command, args);
        if last.as_ref().err().map(String::as_str) != Some(UNAUTHORIZED) {
            break;
        }
    }
    last
}

fn send(port: u16, token: &str, command: &str, args: &Args) -> Result<Value, String> {
    let mut request = args.clone();
    request.insert("cmd".into(), command.into());
    request.insert("adminToken".into(), token.into());
    let address = SocketAddr::from(([127, 0, 0, 1], port));
    let mut stream =
        TcpStream::connect_timeout(&address, TIMEOUT).map_err(|_| NOT_RUNNING.to_owned())?;
    fn silent<E>(_: E) -> String {
        "The server did not answer.".to_owned()
    }
    stream.set_read_timeout(Some(TIMEOUT)).map_err(silent)?;
    stream.set_write_timeout(Some(TIMEOUT)).map_err(silent)?;
    writeln!(stream, "{}", Value::Object(request)).map_err(silent)?;
    let mut line = String::new();
    BufReader::new(stream.take(MAX_REPLY))
        .read_line(&mut line)
        .map_err(silent)?;
    let reply: Value = serde_json::from_str(&line).map_err(silent)?;
    if reply["ok"].as_bool() == Some(true) {
        Ok(reply)
    } else {
        Err(reply["error"]
            .as_str()
            .unwrap_or("The server refused the command.")
            .to_owned())
    }
}

pub const COMMANDS: [&str; 31] = [
    "status",
    "ping",
    "clients",
    "bans",
    "shutdown",
    "kick",
    "revive",
    "kill",
    "voice_mute",
    "voice_unmute",
    "ban",
    "unban",
    "steam_ban",
    "steam_unban",
    "profile_unban",
    "steam_lookup",
    "steam_flags",
    "steam_flag_clear",
    "identity_reset",
    "world_time",
    "world_weather",
    "world_mode",
    "world_policy",
    "world_policy_reload",
    "factions",
    "faction_disband",
    "faction_promote",
    "faction_kick",
    "quest_mode",
    "quest_gate",
    "quest_solo",
];

pub fn checked(command: &str, args: Args) -> Result<Request, String> {
    let word = |text: &str| {
        !text.is_empty() && text.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
    };
    if !word(command) {
        return Err(format!("'{command}' is not a command name"));
    }
    for (key, value) in &args {
        if !word(key) || !matches!(value, Value::String(_) | Value::Number(_) | Value::Bool(_)) {
            return Err(format!("'{key}' is not a valid argument"));
        }
    }
    let command = command.to_ascii_lowercase();
    if !COMMANDS.contains(&command.as_str()) {
        return Err(format!("'{command}' is not a server command"));
    }
    Ok((command, args))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::net::TcpListener;

    #[test]
    fn every_command_the_pages_send_is_on_the_list() {
        let quoted = |text: &str, after: &str, nth: usize| -> Vec<String> {
            text.lines()
                .filter_map(|line| line.trim_start().strip_prefix(after))
                .filter_map(|rest| rest.split('\'').nth(nth).map(str::to_owned))
                .collect()
        };
        let admin = include_str!("../ui/server-admin.js");
        let page = include_str!("../ui/server.js");
        let mut names = quoted(admin, "cmd('", 4);
        names.extend(quoted(page, "act('", 2));
        names.extend(quoted(page, "if (kind === 'steam') adminCall('", 0));
        assert!(names.len() > 25, "{names:?}");
        for name in names {
            assert!(COMMANDS.contains(&name.as_str()), "{name}");
        }
    }

    #[test]
    fn a_null_port_key_falls_through_to_the_next_spelling() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(
            dir.path().join(crate::settings::FILE),
            r#"{"admin_port": null, "adminPort": 8001}"#,
        )
        .unwrap();
        assert_eq!(admin_port(&[dir.path().to_path_buf()]), 8001);
    }

    #[test]
    fn only_plain_command_and_argument_names_are_sent() {
        let args = json!({"playerId": 3, "reason": "bad  manners", "ban": true});
        let (command, args) = checked("Kick", args.as_object().unwrap().clone()).unwrap();
        assert_eq!(command, "kick");
        assert_eq!(args.len(), 3);
        assert!(checked("ki!ck", Args::new()).is_err());
        assert!(checked("rm_everything", Args::new()).is_err());
        assert!(checked("", Args::new()).is_err());
        assert!(checked("kick", json!({"a": [1]}).as_object().unwrap().clone()).is_err());
        assert!(checked("kick", json!({"a b": 1}).as_object().unwrap().clone()).is_err());
    }

    #[test]
    fn a_command_carries_the_token_and_returns_the_reply() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut line = String::new();
            BufReader::new(stream.try_clone().unwrap())
                .read_line(&mut line)
                .unwrap();
            let sent: Value = serde_json::from_str(&line).unwrap();
            assert_eq!(
                (&sent["cmd"], &sent["adminToken"]),
                (&json!("ping"), &json!("t0k"))
            );
            writeln!(stream, r#"{{"ok":true,"data":{{"pong":true}}}}"#).unwrap();
            let (mut second, _) = listener.accept().unwrap();
            BufReader::new(second.try_clone().unwrap())
                .read_line(&mut line)
                .unwrap();
            writeln!(second, r#"{{"ok":false,"error":"nope"}}"#).unwrap();
        });
        let endpoint = Endpoint {
            port,
            tokens: vec!["t0k".into()],
        };
        let reply = call(&endpoint, "ping", &Args::new()).unwrap();
        assert_eq!(reply["data"]["pong"], json!(true));
        assert_eq!(call(&endpoint, "kick", &Args::new()), Err("nope".into()));
        server.join().unwrap();
    }

    #[test]
    fn an_unauthorized_token_falls_back_to_the_next_one() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = std::thread::spawn(move || {
            for _ in 0..2 {
                let (mut stream, _) = listener.accept().unwrap();
                let mut line = String::new();
                BufReader::new(stream.try_clone().unwrap())
                    .read_line(&mut line)
                    .unwrap();
                let sent: Value = serde_json::from_str(&line).unwrap();
                if sent["adminToken"] == json!("good") {
                    writeln!(stream, r#"{{"ok":true}}"#).unwrap();
                } else {
                    writeln!(stream, r#"{{"ok":false,"error":"{UNAUTHORIZED}"}}"#).unwrap();
                }
            }
        });
        let endpoint = Endpoint {
            port,
            tokens: vec!["stale".into(), "good".into()],
        };
        assert!(call(&endpoint, "status", &Args::new()).is_ok());
        server.join().unwrap();
    }

    #[test]
    fn the_newest_token_and_the_config_port_are_used() {
        let old = tempfile::tempdir().unwrap();
        let new = tempfile::tempdir().unwrap();
        fs::write(old.path().join(TOKEN), "old\n").unwrap();
        std::thread::sleep(Duration::from_millis(30));
        fs::write(new.path().join(TOKEN), "new\n").unwrap();
        fs::write(
            old.path().join(crate::settings::FILE),
            "\u{feff}{\"adminPort\": 8001, \"port\": 9000}",
        )
        .unwrap();
        let dirs = [old.path().to_path_buf(), new.path().to_path_buf()];
        let found = endpoint(&dirs).unwrap();
        assert_eq!(
            (found.tokens, found.port),
            (vec!["new".into(), "old".into()], 8001)
        );
        assert_eq!(config_number(&dirs, &["port"]), Some(9000));
        assert!(endpoint(&[tempfile::tempdir().unwrap().path().to_path_buf()]).is_none());
    }
}
