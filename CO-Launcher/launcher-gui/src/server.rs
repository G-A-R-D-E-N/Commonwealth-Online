#[cfg(test)]
use std::fs;
use std::fs::File;
use std::net::{IpAddr, SocketAddr, TcpStream, UdpSocket};
use std::path::{Path, PathBuf};
use std::process::{Child, Stdio};
use std::sync::Mutex;
use std::time::{Duration, Instant};

use launcher_core::modlist::{self, ModSwitch};
use launcher_core::server as files;
use launcher_core::setup::{self as flow, ManagerKind, MANAGER_NONE};
use launcher_core::Error;
use serde_json::Value;

use crate::admin::{self, Args, Endpoint, Request};
use crate::listing;
use crate::mods::{self, Pin};
use crate::ops;
use crate::protocol::{BanView, CheckView, Event, HostMode, PlayerView, ServerView, StatusView};
use crate::settings::{self, Settings};
use crate::state::no_site;

const BUNDLE: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/server-bundle.zip"));
const BUNDLE_VERSION: &str = include_str!(concat!(env!("OUT_DIR"), "/server-bundle.version"));
const LOG: &str = "dedicated-console.log";
const STOP_WAIT: Duration = Duration::from_secs(15);
const POLL: Duration = Duration::from_secs(1);
const PROBE: Duration = Duration::from_millis(500);
const SHUTDOWN_GRACE: Duration = Duration::from_millis(800);
const STARTUP_STOP_GRACE: Duration = Duration::from_secs(20);
const MAX_REPLY_TEXT: usize = 4000;
const NEEDS_MO2_CLOSED: &str = "Close Mod Organizer 2, then try again. The launcher then turns on the Commonwealth Online Server mod, which the game needs to find the server.";
const NEEDS_MOD_ON: &str = "Turn on the Commonwealth Online Server mod in Mod Organizer 2, which the game needs to find the server.";
const UNREADABLE_PROFILE: &str = "The launcher cannot read the Mod Organizer 2 profile, so it cannot check that the Commonwealth Online Server mod is turned on. Open Mod Organizer 2, pick a profile, then press PLAY again.";
const IN_USE: &str =
    "The server files are in use. Press STOP SERVER (or close the game), then REFRESH.";
const STILL_RUNNING: &str =
    "The server this launcher started did not stop. End it in Task Manager, then try again.";

struct StartingServer {
    dir: PathBuf,
    child: Child,
    stop_requested_at: Option<Instant>,
}

static STARTING: Mutex<Option<StartingServer>> = Mutex::new(None);

pub fn startup_in_progress() -> bool {
    let mut state = STARTING.lock().unwrap_or_else(|error| error.into_inner());
    if let Some(server) = state.as_mut() {
        if matches!(server.child.try_wait(), Ok(Some(_))) {
            *state = None;
        }
    }
    state.is_some()
}

fn starting(dir: &Path, running: bool) -> (bool, bool, Option<String>) {
    let mut state = STARTING.lock().unwrap_or_else(|error| error.into_inner());
    let Some(server) = state.as_mut() else {
        return (false, false, None);
    };
    if server.dir != dir {
        return (false, false, None);
    }
    if running && server.stop_requested_at.is_none() {
        *state = None;
        return (false, false, None);
    }
    if running {
        server.stop_requested_at = server.stop_requested_at.map(|_| Instant::now());
    }
    match server.child.try_wait() {
        Ok(None) => {
            let overdue = server
                .stop_requested_at
                .is_some_and(|at| at.elapsed() >= STARTUP_STOP_GRACE);
            if !running && overdue {
                let _ = server.child.kill();
                let _ = server.child.wait();
                *state = None;
                return (false, false, None);
            }
            (true, server.stop_requested_at.is_some(), None)
        }
        Ok(Some(status)) => {
            let stopped = server.stop_requested_at.is_some();
            *state = None;
            (
                false,
                false,
                (!stopped).then(|| {
                    format!(
                        "The server stopped while starting ({status}). See {LOG} in {}.",
                        dir.display()
                    )
                }),
            )
        }
        Err(error) => (
            true,
            server.stop_requested_at.is_some(),
            Some(format!("Could not check the starting server: {error}")),
        ),
    }
}

fn stop_pending(dir: &Path) -> bool {
    let state = STARTING.lock().unwrap_or_else(|error| error.into_inner());
    state
        .as_ref()
        .is_some_and(|server| server.dir == dir && server.stop_requested_at.is_some())
}

fn request_startup_stop(dir: &Path) -> Result<bool, String> {
    let mut state = STARTING.lock().unwrap_or_else(|error| error.into_inner());
    let Some(server) = state.as_mut() else {
        return Ok(false);
    };
    if server.dir != dir {
        return Ok(false);
    }
    if server
        .child
        .try_wait()
        .map_err(|error| error.to_string())?
        .is_none()
    {
        server.stop_requested_at = server.stop_requested_at.or(Some(Instant::now()));
    } else {
        *state = None;
        return Ok(false);
    }
    Ok(true)
}

pub(crate) fn folders(pin: &Pin) -> Vec<PathBuf> {
    vec![
        files::dir(&pin.instance),
        pin.instance.join("overwrite").join("server"),
    ]
}

#[derive(Clone)]
pub enum Site {
    Mo2(Pin),
    Standalone(PathBuf),
}

impl Site {
    pub fn dirs(&self) -> Vec<PathBuf> {
        match self {
            Site::Mo2(pin) => folders(pin),
            Site::Standalone(root) => vec![root.clone()],
        }
    }
}

pub fn is_standalone(manager: &str) -> bool {
    manager == ManagerKind::Vortex.label() || manager == MANAGER_NONE
}

pub fn site_for(pin: Option<&Pin>, manager: &str) -> Option<Site> {
    if is_standalone(manager) {
        return flow::default_server_root().map(Site::Standalone);
    }
    pin.cloned().map(Site::Mo2)
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

pub(crate) fn end_strays(dir: &Path) -> Result<bool, String> {
    let Some(pid) = files::recorded_host(dir) else {
        return Ok(false);
    };
    if mods::game_running() {
        return Ok(false);
    }
    if !owned_host(pid, dir) {
        files::forget_host(dir);
        return Ok(false);
    }
    let gone = || !owned_host(pid, dir);
    let graceful = admin::endpoint(&[dir.to_path_buf()])
        .is_some_and(|endpoint| admin::call(&endpoint, "shutdown", &Args::new()).is_ok());
    if !(graceful && ops::wait_for(gone, POLL, STOP_WAIT)) {
        let _ = mods::system_tool("taskkill.exe")
            .args(["/F", "/PID", &pid.to_string()])
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .status();
        if !ops::wait_for(gone, POLL, STOP_WAIT) {
            return Err(STILL_RUNNING.to_owned());
        }
    }
    files::forget_host(dir);
    Ok(true)
}

fn owned_host(pid: u32, dir: &Path) -> bool {
    let mut query = mods::system_tool("WindowsPowerShell/v1.0/powershell.exe");
    query.args([
        "-NoProfile",
        "-NonInteractive",
        "-Command",
        &format!("(Get-CimInstance Win32_Process -Filter 'ProcessId={pid}').ExecutablePath"),
    ]);
    query
        .output()
        .ok()
        .filter(|out| out.status.success())
        .is_some_and(|out| same_program(&String::from_utf8_lossy(&out.stdout), dir))
}

fn same_program(reported: &str, dir: &Path) -> bool {
    let running = Path::new(reported.trim()).canonicalize();
    let ours = dir.join(files::EXE).canonicalize();
    matches!((running, ours), (Ok(running), Ok(ours)) if running == ours)
}

fn switch_problem(switch: ModSwitch) -> Option<String> {
    match switch {
        ModSwitch::Pending => Some(NEEDS_MO2_CLOSED.to_owned()),
        ModSwitch::Unreadable => Some(UNREADABLE_PROFILE.to_owned()),
        ModSwitch::Already | ModSwitch::Done => None,
    }
}

pub fn ensure(site: &Site) -> Option<String> {
    let dirs = site.dirs();
    if let Err(error) = files::install(&dirs[0], BUNDLE, BUNDLE_VERSION) {
        return Some(install_problem(&error));
    }
    if !files::installed(&dirs[0]) {
        return None;
    }
    match site {
        Site::Mo2(pin) => switch_problem(switch_on(pin, !mods::mo2_running())),
        Site::Standalone(_) => None,
    }
}

fn setup(site: &Site, apply: bool, view: &mut ServerView) {
    let dir = site.dirs().remove(0);
    match files::install(&dir, BUNDLE, BUNDLE_VERSION) {
        Ok(written) if !written.is_empty() => {
            view.note = install_note(&written);
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
    let Site::Mo2(pin) = site else {
        return;
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

fn install_note(written: &[String]) -> String {
    let mut parts = Vec::new();
    let installed: Vec<&str> = written
        .iter()
        .map(String::as_str)
        .filter(|name| !name.ends_with(".new"))
        .collect();
    if !installed.is_empty() {
        parts.push(format!("Installed {}.", installed.join(", ")));
    }
    for name in written.iter().filter(|name| name.ends_with(".new")) {
        let base = name.strip_suffix(".new").unwrap_or(name.as_str());
        parts.push(format!(
            "Wrote {name}. A newer default for {base} came with this update. Your copy of {base} was kept, so compare the two files and copy {name} over {base} to use the new default."
        ));
    }
    parts.join(" ")
}

fn install_problem(error: &Error) -> String {
    match error {
        Error::InUse(_) => IN_USE.to_owned(),
        Error::Io { source, .. } if source.kind() == std::io::ErrorKind::PermissionDenied => {
            IN_USE.to_owned()
        }
        other => other.user_message(),
    }
}

pub(crate) fn check(level: &'static str, text: impl Into<String>) -> CheckView {
    CheckView {
        level,
        text: text.into(),
    }
}

fn ping(dirs: &[PathBuf]) -> bool {
    admin::endpoint(dirs).is_some_and(|e| admin::call(&e, "ping", &Args::new()).is_ok())
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum PortState {
    Free,
    InUse,
    Unknown,
}

fn classify_bind(bind: std::io::Result<()>) -> PortState {
    match bind {
        Ok(()) => PortState::Free,
        Err(error) if error.kind() == std::io::ErrorKind::AddrInUse => PortState::InUse,
        Err(_) => PortState::Unknown,
    }
}

pub(crate) fn port_state(port: u16) -> PortState {
    classify_bind(UdpSocket::bind(("0.0.0.0", port)).map(drop))
}

fn lan_address() -> Option<IpAddr> {
    let socket = UdpSocket::bind("0.0.0.0:0").ok()?;
    socket.connect("192.0.2.1:9").ok()?;
    Some(socket.local_addr().ok()?.ip())
}

pub fn overview(site: Option<&Site>, install: bool) -> Event {
    let mut view = ServerView {
        bundled: !BUNDLE.is_empty(),
        bundled_version: Some(BUNDLE_VERSION)
            .filter(|v| !v.is_empty())
            .map(str::to_owned),
        ..ServerView::default()
    };
    let Some(site) = site else {
        view.problem = Some(no_site().to_owned());
        return Event::Server(Box::new(view));
    };
    let dirs = site.dirs();
    view.folder = Some(dirs[0].display().to_string());
    if let Site::Mo2(pin) = site {
        view.mo2_instance = Some(pin.instance.to_string_lossy().into_owned());
        view.mo2_profile = pin.profile.clone();
    }
    let (starting_now, _, startup_problem) =
        starting(&dirs[0], stop_pending(&dirs[0]) && ping(&dirs));
    if let Some(problem) = startup_problem {
        view.problem.get_or_insert(problem);
    }
    if install && !starting_now {
        setup(site, !mods::mo2_running(), &mut view);
    }
    view.installed = files::installed(&dirs[0]);
    view.installed_version = files::installed_version(&dirs[0]);
    if view.installed {
        let (form, problem) = settings::load_and_problem(&dirs[0]);
        view.settings = form;
        if let Some(problem) = problem {
            view.problem.get_or_insert(problem);
        }
    }
    if !view.installed && !view.bundled {
        view.problem = Some("This launcher build does not include the server files.".to_owned());
    }
    if let Some(endpoint) = admin::endpoint(&dirs) {
        live(&mut view, &endpoint);
    }
    let (starting_now, stopping, startup_problem) = starting(&dirs[0], view.running);
    view.starting = starting_now;
    view.stopping = stopping;
    if let Some(problem) = startup_problem {
        view.problem.get_or_insert(problem);
    }
    if view.stopping {
        if view.running {
            if let Some(endpoint) = admin::endpoint(&dirs) {
                if let Err(error) = admin::call(&endpoint, "shutdown", &Args::new()) {
                    view.problem.get_or_insert(error);
                }
            }
        }
        view.running = false;
        view.note = "Stop requested. The server will shut down when startup finishes; if it is still starting after 20 seconds, the launcher ends it.".to_owned();
    } else if view.starting {
        view.note = "The server is starting and checking its modlist. You can stop it here or close the launcher.".to_owned();
    }
    Event::Server(Box::new(view))
}

fn live(view: &mut ServerView, endpoint: &Endpoint) {
    let none = Args::new();
    match admin::call(endpoint, "status", &none) {
        Ok(status) => {
            view.status = Some(describe(&status["data"]));
            view.listing = listing::ListingView::from_status(&status["data"]);
        }
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

pub(crate) fn port_check(running: bool, state: PortState, port: u16) -> CheckView {
    match (running, state) {
        (true, PortState::Free) => check(
            "warn",
            format!("The server is running but is not listening on port {port} yet. Wait a few seconds, then check again."),
        ),
        (true, PortState::InUse) => check(
            "info",
            format!("The server is running and port {port} is in use. This is expected, but the launcher has not confirmed which program holds it."),
        ),
        (true, PortState::Unknown) => check(
            "info",
            format!("The launcher could not check port {port}, so it cannot confirm that the server is listening."),
        ),
        (false, PortState::InUse) => check(
            "fail",
            format!("Another program is using port {port}, so the server cannot start. Close that program, or pick a different port in SETTINGS."),
        ),
        (false, PortState::Unknown) => check(
            "warn",
            format!("The launcher could not check port {port}, so it is not known whether the server can start."),
        ),
        (false, PortState::Free) => {
            check("ok", format!("Port {port} is free, so the server can start."))
        }
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
    let answered = TcpStream::connect_timeout(&SocketAddr::new(ip, port), PROBE).is_ok();
    exposure_check(ip, port, answered)
}

fn exposure_check(ip: IpAddr, port: u16, answered: bool) -> CheckView {
    if answered {
        check(
            "fail",
            format!("DANGER: port {port} accepts connections at your network address {ip}. The server's admin controls should listen only on this PC, so other devices may be able to reach them. Stop the server and do not run it until this is fixed."),
        )
    } else {
        check(
            "info",
            format!("A test connection to {ip} port {port} got no answer. That is one test on one address from this PC. It does not prove the admin controls are private, because it does not check other addresses, IPv6 or firewall rules."),
        )
    }
}

pub fn network(site: Option<&Site>, mode: HostMode) -> Event {
    let dirs = site.map(Site::dirs).unwrap_or_default();
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
        checks.insert(2, port_check(running, port_state(port), port));
    }
    let listing = status
        .as_ref()
        .and_then(|status| listing::ListingView::from_status(&status["data"]));
    match mode {
        HostMode::P2p => checks.extend(steam_checks(config.as_ref())),
        HostMode::Dedicated => {
            checks.extend(lan_checks(port, config.as_ref()));
            checks.extend(listing::checks(
                installed.map(PathBuf::as_path),
                listing.as_ref(),
                port,
            ));
        }
    }
    Event::ServerNetwork { checks }
}

pub(crate) fn steam_running(up: bool) -> CheckView {
    if up {
        check("ok", "Steam is running.")
    } else {
        check(
            "fail",
            "Steam is not running. Start Steam and sign in, or friends cannot join you.",
        )
    }
}

fn steam_checks(config: Option<&Settings>) -> Vec<CheckView> {
    let up = mods::process_running("steam.exe");
    let mut checks = vec![
        steam_running(up),
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

pub fn start(site: Option<&Site>) -> Option<Event> {
    let failure = |message: String| Some(Event::Error { message });
    let Some(site) = site else {
        return failure(no_site().to_owned());
    };
    let dirs = site.dirs();
    if ping(&dirs) {
        let _ = starting(&dirs[0], true);
        return None;
    }
    {
        let mut state = STARTING.lock().unwrap_or_else(|error| error.into_inner());
        if let Some(server) = state.as_mut() {
            match server.child.try_wait() {
                Ok(Some(_)) => *state = None,
                Ok(None) => {
                    return failure(
                        "A server is already starting. Wait for it or stop it first.".to_owned(),
                    )
                }
                Err(error) => {
                    return failure(format!("Could not check the starting server: {error}"))
                }
            }
        }
    }
    if let Err(error) = files::install(&dirs[0], BUNDLE, BUNDLE_VERSION) {
        return failure(install_problem(&error));
    }
    if !files::installed(&dirs[0]) {
        return failure("The server files are missing.".to_owned());
    }
    let port = admin::game_port(&dirs);
    match port_state(port) {
        PortState::Free => {}
        PortState::InUse => return failure(format!("Another program is using port {port}.")),
        PortState::Unknown => {
            let text = format!("Could not check port {port}, so the server was not started.");
            return failure(text);
        }
    }
    let mut child = match spawn(&dirs[0]) {
        Ok(child) => child,
        Err(error) => return failure(format!("Could not start the server: {error}")),
    };
    if let Err(error) = files::record_host(&dirs[0], child.id()) {
        let _ = child.kill();
        return failure(format!("Could not record the server process: {error}"));
    }
    *STARTING.lock().unwrap_or_else(|error| error.into_inner()) = Some(StartingServer {
        dir: dirs[0].clone(),
        child,
        stop_requested_at: None,
    });
    None
}

fn spawn(dir: &Path) -> std::io::Result<std::process::Child> {
    let log = dir.join(LOG);
    let _ = std::fs::rename(&log, dir.join("dedicated-console.prev.log"));
    let out = File::options().create(true).append(true).open(&log)?;
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
    server.spawn()
}

pub fn save(
    site: Option<&Site>,
    settings: &Settings,
    restart: bool,
    mode: HostMode,
    emit: &dyn Fn(Event),
) -> Event {
    let reply = |ok: bool, text: &str| {
        emit(Event::ServerReply {
            ok,
            text: text.to_owned(),
            data: None,
        });
    };
    let Some(dirs) = site.map(Site::dirs) else {
        reply(false, no_site());
        return overview(site, false);
    };
    if starting(&dirs[0], ping(&dirs)).0 {
        reply(
            false,
            "Wait for the server to finish starting or stop it before changing settings.",
        );
        return overview(site, false);
    }
    if !files::installed(&dirs[0]) {
        reply(
            false,
            "The server is not installed yet. Press REFRESH first.",
        );
        return overview(site, false);
    }
    if restart && admin::endpoint(&dirs).is_some_and(|endpoint| started_elsewhere(&dirs, &endpoint))
    {
        reply(
            false,
            "Nothing was saved. This server was not started with START SERVER, so the launcher cannot restart it. Restart it from the game, or press STOP SERVER and then START SERVER.",
        );
        return overview(site, false);
    }
    let admin_port = admin::admin_port(&dirs);
    let listing = (mode == HostMode::Dedicated).then_some(settings.public_listing);
    match settings::save(&dirs[0], settings, admin_port, listing) {
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
    overview(site, false)
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

pub fn restart(site: Option<&Site>, emit: &dyn Fn(Event)) -> Event {
    let Some(dirs) = site.map(Site::dirs) else {
        emit(Event::Error {
            message: no_site().to_owned(),
        });
        return overview(site, false);
    };
    if ping(&dirs) {
        let _ = starting(&dirs[0], true);
    } else if starting(&dirs[0], false).0 {
        emit(Event::Error {
            message: "Wait for startup to finish or stop the server before restarting.".to_owned(),
        });
        return overview(site, false);
    }
    let port = admin::game_port(&dirs);
    if let Some(endpoint) = admin::endpoint(&dirs).filter(|_| ping(&dirs)) {
        let _ = admin::call(&endpoint, "shutdown", &Args::new());
        if !ops::wait_for(
            || !ping(&dirs) && port_state(port) != PortState::InUse,
            POLL,
            STOP_WAIT,
        ) {
            emit(Event::Error {
                message: "The server did not stop. Press STOP SERVER, then START SERVER."
                    .to_owned(),
            });
            return overview(site, false);
        }
    }
    match start(site) {
        Some(error) => emit(error),
        None => emit(Event::ServerReply {
            ok: true,
            text: "The server is starting with your saved settings.".to_owned(),
            data: None,
        }),
    }
    overview(site, false)
}

pub fn run(site: Option<&Site>, (command, args): &Request, emit: &dyn Fn(Event)) -> Event {
    if command == "shutdown" {
        if let Some(dirs) = site.map(Site::dirs) {
            if ping(&dirs) {
                let _ = starting(&dirs[0], true);
            } else {
                match request_startup_stop(&dirs[0]) {
                    Ok(true) => {
                        emit(Event::ServerReply {
                            ok: true,
                            text: "Stop requested. The server will shut down when startup finishes; if it is still starting after 20 seconds, the launcher ends it.".to_owned(),
                            data: None,
                        });
                        return overview(site, false);
                    }
                    Err(error) => {
                        emit(Event::Error {
                            message: format!("Could not stop the starting server: {error}"),
                        });
                        return overview(site, false);
                    }
                    Ok(false) => {}
                }
            }
        }
    }
    let reply = site
        .map(Site::dirs)
        .and_then(|dirs| admin::endpoint(&dirs))
        .ok_or_else(|| admin::NOT_RUNNING.to_owned())
        .and_then(|endpoint| admin::call(&endpoint, command, args));
    emit(reply_event(reply));
    if command == "shutdown" {
        std::thread::sleep(SHUTDOWN_GRACE);
    }
    overview(site, false)
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
mod tests;
