use std::fs::{self, File};
use std::net::{IpAddr, SocketAddr, TcpStream, UdpSocket};
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::time::Duration;

use launcher_core::modlist::{self, ModSwitch};
use launcher_core::server as files;
use launcher_core::Error;
use serde_json::Value;

use crate::admin::{self, Args, Endpoint, Request};
use crate::mods::{self, Pin};
use crate::ops;
use crate::protocol::{BanView, CheckView, Event, HostMode, PlayerView, ServerView, StatusView};
use crate::settings::{self, Settings};
use crate::state::NO_MO2;

const BUNDLE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/server-bundle.zip"));
const LOG: &str = "dedicated-console.log";
const START_WAIT: Duration = Duration::from_secs(20);
const STOP_WAIT: Duration = Duration::from_secs(15);
const POLL: Duration = Duration::from_secs(1);
const PROBE: Duration = Duration::from_millis(500);
const SHUTDOWN_GRACE: Duration = Duration::from_millis(800);
const MAX_REPLY_TEXT: usize = 4000;
const NEEDS_MO2_CLOSED: &str = "Close Mod Organizer 2, then try again. The launcher then turns on the Commonwealth Online Server mod, which the game needs to find the server.";
const NEEDS_MOD_ON: &str = "Turn on the Commonwealth Online Server mod in Mod Organizer 2, which the game needs to find the server.";
const IN_USE: &str =
    "The server files are in use. Press STOP SERVER (or close the game), then REFRESH.";

fn folders(pin: &Pin) -> Vec<PathBuf> {
    vec![
        files::dir(&pin.instance),
        pin.instance.join("overwrite").join("server"),
    ]
}

fn switch_on(pin: &Pin, apply: bool) -> ModSwitch {
    let current = mods::pin().filter(|now| now.instance == pin.instance);
    current
        .as_ref()
        .unwrap_or(pin)
        .profile
        .as_deref()
        .map_or(ModSwitch::Unreadable, |profile| {
            modlist::enable_mod(&pin.instance, profile, files::MOD, apply)
        })
}

pub fn ensure(pin: &Pin) -> Option<&'static str> {
    let dir = files::dir(&pin.instance);
    let _ = files::install(&dir, BUNDLE);
    if !files::installed(&dir) {
        return None;
    }
    (switch_on(pin, !mods::mo2_running()) == ModSwitch::Pending).then_some(NEEDS_MO2_CLOSED)
}

fn setup(pin: &Pin, apply: bool, view: &mut ServerView) {
    let dir = files::dir(&pin.instance);
    match files::install(&dir, BUNDLE) {
        Ok(written) if !written.is_empty() => {
            view.note = format!("Installed {}.", written.join(", "));
        }
        Ok(_) => {}
        Err(error) => {
            view.problem = Some(install_problem(&error));
            return;
        }
    }
    if !files::installed(&dir) {
        return;
    }
    let note = |view: &mut ServerView, text: &str| {
        if !view.note.is_empty() {
            view.note.push(' ');
        }
        view.note.push_str(text);
    };
    match switch_on(pin, apply) {
        ModSwitch::Already => {}
        ModSwitch::Done => note(
            view,
            "Turned on the Commonwealth Online Server mod in Mod Organizer 2.",
        ),
        ModSwitch::Pending => note(view, NEEDS_MO2_CLOSED),
        ModSwitch::Unreadable => note(view, NEEDS_MOD_ON),
    }
}

fn install_problem(error: &Error) -> String {
    match error {
        Error::Io { source, .. } if source.kind() == std::io::ErrorKind::PermissionDenied => {
            IN_USE.to_owned()
        }
        other => other.to_string(),
    }
}

fn check(level: &'static str, text: impl Into<String>) -> CheckView {
    CheckView {
        level,
        text: text.into(),
    }
}

fn ping(dirs: &[PathBuf]) -> bool {
    admin::endpoint(dirs).is_some_and(|e| admin::call(&e, "ping", &Args::new()).is_ok())
}

fn udp_in_use(port: u16) -> bool {
    UdpSocket::bind(("0.0.0.0", port)).is_err()
}

fn lan_address() -> Option<IpAddr> {
    let socket = UdpSocket::bind("0.0.0.0:0").ok()?;
    socket.connect("192.0.2.1:9").ok()?;
    Some(socket.local_addr().ok()?.ip())
}

pub fn overview(pin: Option<&Pin>, install: bool) -> Event {
    let mut view = ServerView {
        bundled: !BUNDLE.is_empty(),
        ..ServerView::default()
    };
    let Some(pin) = pin else {
        view.problem = Some(NO_MO2.to_owned());
        return Event::Server(Box::new(view));
    };
    let dirs = folders(pin);
    view.folder = Some(dirs[0].display().to_string());
    if install {
        setup(pin, !mods::mo2_running(), &mut view);
    }
    view.installed = files::installed(&dirs[0]);
    view.settings = view.installed.then(|| settings::load(&dirs[0]));
    if view.installed
        && dirs[0].join(settings::FILE).exists()
        && settings::read_object(&dirs[0]).is_none()
    {
        view.problem.get_or_insert(settings::UNREADABLE.to_owned());
    }
    if !view.installed && !view.bundled {
        view.problem = Some("This launcher build does not include the server files.".to_owned());
    }
    if let Some(endpoint) = admin::endpoint(&dirs) {
        live(&mut view, &endpoint);
    }
    Event::Server(Box::new(view))
}

fn live(view: &mut ServerView, endpoint: &Endpoint) {
    let none = Args::new();
    match admin::call(endpoint, "status", &none) {
        Ok(status) => view.status = Some(describe(&status["data"])),
        Err(message) => {
            if message != admin::NOT_RUNNING {
                view.problem.get_or_insert(message);
            }
            return;
        }
    }
    view.running = true;
    let data = |command| admin::call(endpoint, command, &none).unwrap_or_default();
    view.players = players(&data("clients")["data"]);
    view.bans = bans(&data("bans")["data"]);
}

fn describe(data: &Value) -> StatusView {
    let number = |key: &str| data[key].as_u64().unwrap_or(0);
    let text = |key: &str, other: &str| data[key].as_str().unwrap_or(other).to_owned();
    StatusView {
        name: text("server_name", "Commonwealth Online Server"),
        players: number("connected_clients"),
        max_players: number("max_players"),
        world_mode: text("world_mode", "unknown"),
        port: number("port"),
    }
}

fn players(data: &Value) -> Vec<PlayerView> {
    let text = |client: &Value, key: &str| client[key].as_str().map(str::to_owned);
    let list = data["clients"].as_array().map(Vec::as_slice);
    list.unwrap_or_default()
        .iter()
        .filter_map(|client| {
            Some(PlayerView {
                id: client["player_id"].as_u64()?,
                address: text(client, "address").unwrap_or_default(),
                steam_id: text(client, "steam_id"),
            })
        })
        .collect()
}

fn bans(data: &Value) -> Vec<BanView> {
    let text = |ban: &Value, key: &str| ban[key].as_str().unwrap_or_default().to_owned();
    let rows = |key: &str| data[key].as_array().cloned().unwrap_or_default();
    let mut out: Vec<BanView> = rows("bans")
        .iter()
        .map(|ban| match text(ban, "profileId") {
            id if !id.is_empty() => BanView {
                kind: "profile",
                id,
                reason: text(ban, "reason"),
            },
            _ => BanView {
                kind: "ip",
                id: text(ban, "ip"),
                reason: text(ban, "reason"),
            },
        })
        .collect();
    out.extend(rows("steamBans").iter().map(|ban| BanView {
        kind: "steam",
        id: text(ban, "steamId"),
        reason: text(ban, "reason"),
    }));
    out
}

fn port_check(running: bool, in_use: bool, port: u16) -> CheckView {
    match (running, in_use) {
        (true, true) => check("ok", format!("The server is listening on port {port}.")),
        (true, false) => check(
            "warn",
            format!("The server is running but is not listening on port {port} yet. Wait a few seconds, then check again."),
        ),
        (false, true) => check(
            "fail",
            format!("Another program is using port {port}, so the server cannot start. Close that program, or pick a different port in SETTINGS."),
        ),
        (false, false) => check("ok", format!("Port {port} is free, so the server can start.")),
    }
}

fn admin_exposure(running: bool, port: u16) -> CheckView {
    if !running {
        return check(
            "info",
            "The server's admin controls are checked while the server is running.",
        );
    }
    let Some(ip) = lan_address() else {
        return check(
            "info",
            "This PC has no network address, so the admin controls could not be tested from the network.",
        );
    };
    if TcpStream::connect_timeout(&SocketAddr::new(ip, port), PROBE).is_ok() {
        check(
            "fail",
            format!("DANGER: the server's admin controls (port {port}) answer on your network address {ip}, so other devices could reach them. Stop the server and do not run it until this is fixed."),
        )
    } else {
        check(
            "ok",
            format!("The admin controls only listen on this PC. A test connection to {ip} port {port} got no answer, as it should."),
        )
    }
}

pub fn network(pin: Option<&Pin>, mode: HostMode) -> Event {
    let dirs = pin.map(folders).unwrap_or_default();
    let status = admin::endpoint(&dirs).and_then(|e| admin::call(&e, "status", &Args::new()).ok());
    let running = status.is_some();
    let port = status
        .as_ref()
        .and_then(|s| u16::try_from(s["data"]["port"].as_u64()?).ok())
        .unwrap_or_else(|| admin::game_port(&dirs));
    let admin_port = admin::admin_port(&dirs);
    let installed = dirs.first().filter(|dir| files::installed(dir));
    let config = installed.map(|dir| settings::load(dir));
    let mut checks = vec![
        if installed.is_some() {
            check("ok", "The server is installed.")
        } else {
            check("fail", "The server is not installed yet. Press REFRESH.")
        },
        if running {
            check("ok", "The server is running.")
        } else {
            check("info", "The server is switched off right now.")
        },
        admin_exposure(running, admin_port),
    ];
    if running || mode == HostMode::Dedicated {
        checks.insert(2, port_check(running, udp_in_use(port), port));
    }
    match mode {
        HostMode::P2p => checks.extend(steam_checks(config.as_ref())),
        HostMode::Dedicated => checks.extend(lan_checks(port, config.as_ref())),
    }
    let listed = installed.and_then(|dir| settings::raw(dir, &["public_listing", "publicListing"]));
    if listed.and_then(|listing| listing.as_bool()) == Some(true) {
        checks.push(check(
            "warn",
            "This server is registered in the public server list, so strangers can find it. Only keep that on if you want that.",
        ));
    }
    Event::ServerNetwork { checks }
}

fn steam_checks(config: Option<&Settings>) -> Vec<CheckView> {
    let up = mods::process_running("steam.exe");
    let mut checks = vec![
        if up {
            check("ok", "Steam is running.")
        } else {
            check(
                "fail",
                "Steam is not running. Start Steam and sign in, or friends cannot join you.",
            )
        },
        if up && ops::steam_signed_in() {
            check(
                "ok",
                "You are signed in to Steam, so friends can join you through Steam.",
            )
        } else {
            check(
                "fail",
                "You are not signed in to Steam. Sign in, or friends cannot join you.",
            )
        },
        check(
            "info",
            "Friends join through Steam. You do not need to change any router or firewall settings.",
        ),
        check(
            "info",
            "While friends are connected they can see your internet address (IP). Only host for people you trust.",
        ),
    ];
    if config.is_some_and(|c| !c.steam_friends_only) {
        checks.push(check(
            "warn",
            "Anyone on Steam can join, not just your friends. Turn on 'Friends only' in SETTINGS to keep it to your friends.",
        ));
    }
    checks
}

fn lan_checks(port: u16, config: Option<&Settings>) -> Vec<CheckView> {
    let mut checks = vec![
        match lan_address() {
            Some(ip) => check(
                "ok",
                format!("People in your home join with this address: {ip} port {port}."),
            ),
            None => check(
                "warn",
                "This PC is not connected to a network, so nobody can join.",
            ),
        },
        check(
            "warn",
            format!("To let people from the internet in, your router must send port {port} (UDP) to this PC. This is called port forwarding. The launcher never changes your router, because forwarding opens a door from the whole internet to this PC. The launcher cannot see your router, so if you are not sure whether you set this up, check it."),
        ),
        check(
            "info",
            format!("To check, open your router's settings, find 'Port forwarding', and look for port {port}. Remove it if you do not want strangers to reach this PC."),
        ),
        check(
            "info",
            "Windows may ask to let the server through its firewall. Choose 'Private networks' only, never 'Public'.",
        ),
    ];
    if config.is_some_and(|c| c.password.is_empty()) {
        checks.push(check(
            "warn",
            "No password is set. Anyone who can reach the server can join. Set one in SETTINGS.",
        ));
    }
    checks
}

pub fn start(pin: Option<&Pin>) -> Option<Event> {
    let failure = |message: String| Some(Event::Error { message });
    let Some(pin) = pin else {
        return failure(NO_MO2.to_owned());
    };
    let dirs = folders(pin);
    if ping(&dirs) {
        return None;
    }
    if let Err(error) = files::install(&dirs[0], BUNDLE) {
        return failure(install_problem(&error));
    }
    if !files::installed(&dirs[0]) {
        return failure("The server files are missing.".to_owned());
    }
    let port = admin::game_port(&dirs);
    if udp_in_use(port) {
        return failure(format!("Another program is using port {port}."));
    }
    if let Err(error) = spawn(&dirs[0]) {
        return failure(format!("Could not start the server: {error}"));
    }
    if ops::wait_for(|| ping(&dirs), POLL, START_WAIT) {
        None
    } else {
        failure(format!(
            "The server did not start. See {LOG} in {}.",
            dirs[0].display()
        ))
    }
}

fn spawn(dir: &Path) -> std::io::Result<()> {
    let log = dir.join(LOG);
    let _ = fs::rename(&log, dir.join("dedicated-console.prev.log"));
    let out = File::create(&log)?;
    let mut server = std::process::Command::new(dir.join(files::EXE));
    server
        .current_dir(dir)
        .stdin(Stdio::null())
        .stderr(out.try_clone()?)
        .stdout(out);
    let config = dir.join(settings::FILE);
    if config.is_file() {
        server.arg("--config").arg(config);
    }
    #[cfg(windows)]
    std::os::windows::process::CommandExt::creation_flags(&mut server, 0x0000_0208);
    server.spawn().map(drop)
}

pub fn save(pin: Option<&Pin>, settings: &Settings, restart: bool, emit: &dyn Fn(Event)) -> Event {
    let reply = |ok: bool, text: &str| {
        emit(Event::ServerReply {
            ok,
            text: text.to_owned(),
            data: None,
        });
    };
    let Some(dirs) = pin.map(folders) else {
        reply(false, NO_MO2);
        return overview(pin, false);
    };
    if !files::installed(&dirs[0]) {
        reply(
            false,
            "The server is not installed yet. Press REFRESH first.",
        );
        return overview(pin, false);
    }
    if restart && admin::endpoint(&dirs).is_some_and(|endpoint| started_elsewhere(&dirs, &endpoint))
    {
        reply(
            false,
            "Nothing was saved. This server was not started with START SERVER, so the launcher cannot restart it. Restart it from the game, or press STOP SERVER and then START SERVER.",
        );
        return overview(pin, false);
    }
    let admin_port = admin::admin_port(&dirs);
    match settings::save(&dirs[0], settings, admin_port) {
        Ok(()) if ping(&dirs) => reply(
            true,
            "Settings saved. They apply the next time the server starts.",
        ),
        Ok(()) => reply(
            true,
            "Settings saved. The server uses them the next time it starts.",
        ),
        Err(text) => reply(false, &text),
    }
    overview(pin, false)
}

fn started_elsewhere(dirs: &[PathBuf], endpoint: &Endpoint) -> bool {
    admin::call(endpoint, "status", &Args::new())
        .is_ok_and(|status| differs_from_saved(&status["data"], &settings::load(&dirs[0])))
}

fn differs_from_saved(data: &Value, saved: &Settings) -> bool {
    data["port"].as_i64().is_some_and(|port| port != saved.port)
        || data["server_name"]
            .as_str()
            .is_some_and(|name| name != saved.name)
}

pub fn restart(pin: Option<&Pin>, emit: &dyn Fn(Event)) -> Event {
    let Some(dirs) = pin.map(folders) else {
        emit(Event::Error {
            message: NO_MO2.to_owned(),
        });
        return overview(pin, false);
    };
    let port = admin::game_port(&dirs);
    if let Some(endpoint) = admin::endpoint(&dirs).filter(|_| ping(&dirs)) {
        let _ = admin::call(&endpoint, "shutdown", &Args::new());
        if !ops::wait_for(|| !ping(&dirs) && !udp_in_use(port), POLL, STOP_WAIT) {
            emit(Event::Error {
                message: "The server did not stop. Press STOP SERVER, then START SERVER."
                    .to_owned(),
            });
            return overview(pin, false);
        }
    }
    match start(pin) {
        Some(error) => emit(error),
        None => emit(Event::ServerReply {
            ok: true,
            text: "The server restarted with your saved settings.".to_owned(),
            data: None,
        }),
    }
    overview(pin, false)
}

pub fn run(pin: Option<&Pin>, (command, args): &Request, emit: &dyn Fn(Event)) -> Event {
    let reply = pin
        .map(folders)
        .and_then(|dirs| admin::endpoint(&dirs))
        .ok_or_else(|| admin::NOT_RUNNING.to_owned())
        .and_then(|endpoint| admin::call(&endpoint, command, args));
    emit(reply_event(reply));
    if command == "shutdown" {
        std::thread::sleep(SHUTDOWN_GRACE);
    }
    overview(pin, false)
}

fn reply_event(reply: Result<Value, String>) -> Event {
    match reply {
        Ok(reply) => Event::ServerReply {
            ok: true,
            text: reply_text(&reply),
            data: reply.get("data").filter(|data| !data.is_null()).cloned(),
        },
        Err(text) => Event::ServerReply {
            ok: false,
            text,
            data: None,
        },
    }
}

fn reply_text(reply: &Value) -> String {
    let text = reply["message"].as_str().unwrap_or("Done.");
    text.chars().take(MAX_REPLY_TEXT).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn a_running_server_that_differs_from_the_saved_file_was_started_elsewhere() {
        let saved = settings::load(std::path::Path::new("no-such-folder"));
        let same = json!({"port": saved.port, "server_name": saved.name});
        assert!(!differs_from_saved(&same, &saved));
        assert!(differs_from_saved(
            &json!({"port": 9000, "server_name": saved.name}),
            &saved
        ));
        assert!(differs_from_saved(
            &json!({"port": saved.port, "server_name": "Mine"}),
            &saved
        ));
        assert!(!differs_from_saved(&json!({}), &saved));
    }

    #[test]
    fn replies_become_player_ban_and_status_rows() {
        let clients = json!({"clients": [
            {"player_id": 7, "address": "10.0.0.2", "steam_id": "7656"},
            {"address": "no id"}
        ]});
        assert_eq!(
            players(&clients),
            [PlayerView {
                id: 7,
                address: "10.0.0.2".into(),
                steam_id: Some("7656".into())
            }]
        );
        let listed = bans(&json!({
            "bans": [{"ip": "1.2.3.4", "profileId": "", "reason": "x"}, {"ip": "", "profileId": "p1", "reason": "y"}],
            "steamBans": [{"steamId": "7656", "reason": "z"}]
        }));
        let kinds: Vec<_> = listed.iter().map(|b| (b.kind, b.id.as_str())).collect();
        assert_eq!(
            kinds,
            [("ip", "1.2.3.4"), ("profile", "p1"), ("steam", "7656")]
        );
        let status = describe(
            &json!({"server_name": "S", "connected_clients": 2, "max_players": 16, "world_mode": "coop-story", "port": 7777}),
        );
        assert_eq!(
            (
                status.name.as_str(),
                status.players,
                status.max_players,
                status.port
            ),
            ("S", 2, 16, 7777)
        );
        let Event::ServerReply { data, .. } =
            reply_event(Ok(json!({"ok": true, "data": {"pong": true}})))
        else {
            panic!("not a reply");
        };
        assert_eq!(data, Some(json!({"pong": true})));
        assert_eq!(reply_text(&json!({"ok": true})), "Done.");
        assert_eq!(
            reply_text(&json!({"message": "Kicked player 7."})),
            "Kicked player 7."
        );
    }

    #[test]
    fn the_port_check_tells_the_four_cases_apart() {
        let levels: Vec<_> = [(true, true), (true, false), (false, true), (false, false)]
            .map(|(running, used)| port_check(running, used, 7777).level)
            .into();
        assert_eq!(levels, ["ok", "warn", "fail", "ok"]);
    }

    #[test]
    fn the_server_lives_in_a_mod_folder_that_the_profile_turns_on() {
        let instance = tempfile::tempdir().unwrap();
        let modlist = instance.path().join("profiles").join("Default");
        fs::create_dir_all(&modlist).unwrap();
        let modlist = modlist.join("modlist.txt");
        fs::write(&modlist, "+Main\n").unwrap();
        let dir = instance.path().join("mods").join(files::MOD).join("server");
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join(files::EXE), b"x").unwrap();
        let pin = Pin {
            game: "G".into(),
            exe: None,
            instance: instance.path().into(),
            profile: Some("Default".into()),
        };
        assert_eq!(folders(&pin)[0], dir);
        let mut view = ServerView::default();
        setup(&pin, false, &mut view);
        assert!(view.problem.is_none() && view.note.contains(NEEDS_MO2_CLOSED));
        assert_eq!(fs::read_to_string(&modlist).unwrap(), "+Main\n");
        let mut view = ServerView::default();
        setup(&pin, true, &mut view);
        assert!(view.problem.is_none() && view.note.contains("Turned on"));
        assert_eq!(
            fs::read_to_string(&modlist).unwrap(),
            "+Main\n+Commonwealth Online Server\n"
        );
    }

    #[test]
    fn saving_needs_an_installed_server_and_reports_bad_values_in_words() {
        let instance = tempfile::tempdir().unwrap();
        let pin = Pin {
            game: "G".into(),
            exe: None,
            instance: instance.path().into(),
            profile: None,
        };
        let dir = files::dir(&pin.instance);
        let replies = std::cell::RefCell::new(Vec::new());
        let record = |event: Event| replies.borrow_mut().push(event);
        let mut form = settings::load(&dir);
        save(Some(&pin), &form, false, &record);
        fs::create_dir_all(&dir).unwrap();
        fs::write(dir.join(files::EXE), b"x").unwrap();
        form.max_players = 40;
        save(Some(&pin), &form, false, &record);
        form.max_players = 4;
        form.name = "Crew".into();
        let Event::Server(view) = save(Some(&pin), &form, false, &record) else {
            panic!("not a server event");
        };
        let texts: Vec<_> = replies
            .borrow()
            .iter()
            .filter_map(|event| match event {
                Event::ServerReply { ok, text, .. } => Some((*ok, text.clone())),
                _ => None,
            })
            .collect();
        let oks: Vec<_> = texts.iter().map(|reply| reply.0).collect();
        assert_eq!(oks, [false, false, true]);
        assert!(texts[0].1.contains("not installed") && texts[1].1.contains("between 1 and 16"));
        let saved = view.settings.map(|s| (s.name, s.max_players));
        assert_eq!(saved, Some(("Crew".to_owned(), 4)));
    }

    #[test]
    fn the_dedicated_checks_warn_about_the_router_and_a_missing_password() {
        let mut form = settings::load(Path::new("no-such-folder"));
        let open = lan_checks(7777, Some(&form));
        assert!(open
            .iter()
            .any(|c| c.level == "warn" && c.text.contains("No password")));
        assert!(open.iter().any(|c| c.text.contains("port forwarding")));
        assert!(open.iter().all(|c| !c.text.contains("netsh")));
        form.password = "hunter22".into();
        let closed = lan_checks(7777, Some(&form));
        assert!(closed.iter().all(|c| !c.text.contains("No password")));
        form.steam_friends_only = false;
        let anyone = steam_checks(Some(&form));
        assert!(anyone.iter().any(|c| c.text.contains("Anyone on Steam")));
    }

    #[test]
    fn without_a_pinned_instance_the_page_says_so_and_commands_fail() {
        let Event::Server(view) = overview(None, true) else {
            panic!("not a server event");
        };
        assert_eq!(view.problem.as_deref(), Some(NO_MO2));
        assert!(matches!(
            start(None),
            Some(Event::Error { message }) if message == NO_MO2
        ));
    }
}
