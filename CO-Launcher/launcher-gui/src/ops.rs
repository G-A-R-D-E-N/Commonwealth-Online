use std::path::Path;
use std::time::{Duration, Instant};

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

fn failure(message: impl Into<String>) -> Option<Event> {
    Some(Event::Error {
        message: message.into(),
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
    if let Some(pick) = mods::sync_versions(Some(&pin.game)).filter(|pick| pick.pending) {
        return failure(pick.note);
    }
    if let Some(problem) = crate::server::ensure(pin) {
        return failure(problem);
    }
    if !mods::process_running("steam.exe") && open::that(STEAM_OPEN).is_err() {
        return failure(NO_STEAM_APP);
    }
    if !wait_for(steam_signed_in, POLL, STEAM_WAIT) {
        return failure(NO_STEAM);
    }
    if let Some(problem) = start(exe, &[PLAY_SHORTCUT]) {
        return Some(problem);
    }
    if wait_for(game_started, POLL, GAME_WAIT) {
        None
    } else {
        failure(NO_GAME)
    }
}

pub fn open_mo2(pin: Option<&Pin>) -> Option<Event> {
    let Some(exe) = pin.and_then(|pin| pin.exe.as_deref()) else {
        return failure(NO_MO2);
    };
    if let Some(problem) = start(exe, &[]) {
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
}
