use std::path::Path;
use std::time::{Duration, Instant};

use launcher_core::discover::is_gog_install;

use crate::mods::{self, Pin};
use crate::protocol::Event;
use crate::state::NO_MO2;

const PLAY_SHORTCUT: &str = "moshortcut://:F4SE";
const POLL: Duration = Duration::from_secs(1);
const NEVER_STARTED: Duration = Duration::from_secs(45);
const NO_WINDOW: Duration = Duration::from_secs(20);
const NOT_STARTED: &str = "Mod Organizer 2 did not start";
const STEAM_KEY: &str = r"HKCU\Software\Valve\Steam\ActiveProcess";
const STEAM_OPEN: &str = "steam://open/main";
const STEAM_WAIT: Duration = Duration::from_secs(90);
const GAME_WAIT: Duration = Duration::from_secs(60);
const NO_STEAM: &str = "Steam is not signed in. Sign in to Steam, then press PLAY again";
const NO_STEAM_APP: &str =
    "Steam could not be started. Start Steam and sign in, then press PLAY again";
const NO_GAME: &str =
    "Fallout 4 did not start through F4SE. Check that Steam is signed in, then press PLAY again";
const NO_GAME_OTHER: &str =
    "Fallout 4 did not start through F4SE. Open Mod Organizer 2 to check, then press PLAY again";

fn failure(message: impl Into<String>) -> Option<Event> {
    Some(Event::Error {
        message: message.into(),
    })
}

fn game_failure(game: &Path) -> Option<Event> {
    failure(if is_gog_install(game) {
        NO_GAME_OTHER
    } else {
        NO_GAME
    })
}

fn start(exe: &Path, args: &[&str]) -> Option<Event> {
    std::process::Command::new(exe)
        .args(args)
        .current_dir(exe.parent().unwrap_or(Path::new(".")))
        .spawn()
        .err()
        .and_then(|error| failure(format!("Could not start Mod Organizer 2: {error}")))
}

fn launch_args(pin: &Pin, play: bool) -> Option<Vec<String>> {
    launch_args_for(
        pin,
        play,
        launcher_core::modmgr::global_instance_name(&pin.instance),
    )
}

fn launch_args_for(pin: &Pin, play: bool, global_name: Option<String>) -> Option<Vec<String>> {
    let exe = pin.exe.as_ref()?;
    let portable = exe.parent()?.canonicalize().ok()? == pin.instance.canonicalize().ok()?;
    if portable {
        return Some(if play {
            vec![PLAY_SHORTCUT.to_owned()]
        } else {
            Vec::new()
        });
    }
    let name = global_name?;
    if play {
        Some(vec![format!("moshortcut://{name}:F4SE")])
    } else {
        Some(vec!["-i".to_owned(), name])
    }
}

pub fn steam_signed_in() -> bool {
    let mut reg = mods::system_tool("reg.exe");
    reg.args(["query", STEAM_KEY, "/v", "ActiveUser"]);
    reg.output().ok().is_some_and(|out| {
        out.status.success()
            && String::from_utf8_lossy(&out.stdout)
                .split_whitespace()
                .last()
                .is_some_and(|user| user != "0x0")
    })
}

fn game_started() -> bool {
    mods::process_running("f4se_loader.exe") || mods::process_running("Fallout4.exe")
}

pub fn wait_for(done: impl Fn() -> bool, poll: Duration, limit: Duration) -> bool {
    let begun = Instant::now();
    while !done() {
        if begun.elapsed() >= limit {
            return false;
        }
        std::thread::sleep(poll);
    }
    true
}

pub fn play(pin: Option<&Pin>) -> Option<Event> {
    let Some((pin, exe)) = pin.and_then(|pin| Some((pin, pin.exe.as_deref()?))) else {
        return failure(NO_MO2);
    };
    if let Some(pick) = mods::sync_versions(Some(pin)).filter(|pick| pick.pending) {
        return failure(pick.note);
    }
    if let Some(problem) = crate::server::ensure(pin) {
        return failure(problem);
    }
    if !is_gog_install(&pin.game) {
        if !mods::process_running("steam.exe") && open::that(STEAM_OPEN).is_err() {
            return failure(NO_STEAM_APP);
        }
        if !wait_for(steam_signed_in, POLL, STEAM_WAIT) {
            return failure(NO_STEAM);
        }
    }
    let Some(args) = launch_args(pin, true) else {
        return failure(NO_MO2);
    };
    if let Some(problem) = start(exe, &args.iter().map(String::as_str).collect::<Vec<_>>()) {
        return Some(problem);
    }
    if wait_for(game_started, POLL, GAME_WAIT) {
        None
    } else {
        game_failure(&pin.game)
    }
}

pub fn open_mo2(pin: Option<&Pin>) -> Option<Event> {
    let Some((pin, exe)) = pin.and_then(|pin| Some((pin, pin.exe.as_deref()?))) else {
        return failure(NO_MO2);
    };
    let Some(args) = launch_args(pin, false) else {
        return failure(NO_MO2);
    };
    if let Some(problem) = start(exe, &args.iter().map(String::as_str).collect::<Vec<_>>()) {
        return Some(problem);
    }
    let seen = wait_until_open(
        mods::mo2_running,
        mods::mo2_window_open,
        POLL,
        NEVER_STARTED,
        NO_WINDOW,
    );
    if seen {
        None
    } else {
        failure(NOT_STARTED)
    }
}

fn wait_until_open(
    running: impl Fn() -> bool,
    window: impl Fn() -> bool,
    poll: Duration,
    never_started: Duration,
    no_window: Duration,
) -> bool {
    let begun = Instant::now();
    let mut seen: Option<Instant> = None;
    loop {
        if window() {
            return true;
        }
        if seen.is_none() && running() {
            seen = Some(Instant::now());
        }
        match seen {
            Some(at) if at.elapsed() >= no_window => return true,
            Some(_) if !running() => return false,
            None if begun.elapsed() >= never_started => return false,
            _ => std::thread::sleep(poll),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn launch_arguments_target_the_selected_instance() {
        let root = tempfile::tempdir().unwrap();
        let portable = root.path().join("portable");
        let global = root.path().join("global");
        let program = root.path().join("program");
        for dir in [&portable, &global, &program] {
            std::fs::create_dir(dir).unwrap();
        }
        let mut pin = Pin {
            game: root.path().into(),
            instance: portable.clone(),
            exe: Some(portable.join("ModOrganizer.exe")),
            profile: None,
        };
        assert_eq!(
            launch_args_for(&pin, true, None),
            Some(vec!["moshortcut://:F4SE".into()])
        );
        pin.instance = global;
        pin.exe = Some(program.join("ModOrganizer.exe"));
        assert_eq!(
            launch_args_for(&pin, true, Some("My Instance".into())),
            Some(vec!["moshortcut://My Instance:F4SE".into()])
        );
        assert_eq!(
            launch_args_for(&pin, false, Some("My Instance".into())),
            Some(vec!["-i".into(), "My Instance".into()])
        );
        assert_eq!(launch_args_for(&pin, true, None), None);
    }
    use std::cell::Cell;

    #[test]
    fn the_wheel_closes_as_soon_as_the_window_shows() {
        let calls = Cell::new(0);
        let window = || {
            calls.set(calls.get() + 1);
            calls.get() == 3
        };
        assert!(wait_until_open(
            || true,
            window,
            Duration::ZERO,
            Duration::from_secs(5),
            Duration::from_secs(5)
        ));
        assert_eq!(calls.get(), 3);
    }

    #[test]
    fn a_running_process_with_no_window_title_closes_after_the_grace() {
        assert!(wait_until_open(
            || true,
            || false,
            Duration::ZERO,
            Duration::from_secs(5),
            Duration::ZERO
        ));
    }

    #[test]
    fn a_process_that_exits_before_showing_a_window_is_reported() {
        let polls = Cell::new(0);
        let running = || {
            polls.set(polls.get() + 1);
            polls.get() == 1
        };
        assert!(!wait_until_open(
            running,
            || false,
            Duration::ZERO,
            Duration::from_secs(5),
            Duration::from_secs(5)
        ));
    }

    #[test]
    fn a_process_that_never_appears_is_reported() {
        assert!(!wait_until_open(
            || false,
            || false,
            Duration::ZERO,
            Duration::ZERO,
            Duration::from_secs(5)
        ));
        assert!(!wait_for(|| false, Duration::ZERO, Duration::ZERO));
        assert!(wait_for(|| true, Duration::ZERO, Duration::ZERO));
    }

    #[test]
    fn without_a_pinned_exe_play_and_open_report_it() {
        for event in [play(None), open_mo2(None)] {
            assert_eq!(
                event,
                Some(Event::Error {
                    message: NO_MO2.to_owned()
                })
            );
        }
        let no_exe = Pin {
            game: "D:/Fallout 4".into(),
            exe: None,
            instance: "D:/MO2".into(),
            profile: None,
        };
        assert!(play(Some(&no_exe)).is_some());
    }

    #[test]
    fn a_game_that_does_not_start_names_steam_only_for_a_steam_install() {
        assert_eq!(
            game_failure(Path::new("D:/GOG Games/Fallout 4")),
            Some(Event::Error {
                message: NO_GAME_OTHER.to_owned()
            })
        );
        assert_eq!(
            game_failure(Path::new("D:/SteamLibrary/steamapps/common/Fallout 4")),
            Some(Event::Error {
                message: NO_GAME.to_owned()
            })
        );
        assert!(!NO_GAME_OTHER.contains("Steam"));
    }
}
