use std::path::Path;

use launcher_core::discover::{find_game, Env};
use launcher_core::runtime::{detect, Detected};

use crate::locations;
use crate::mods::Pin;
use crate::protocol::{Event, RuntimeView, StateView};

pub const NO_MO2: &str = "Mod Organizer 2 was not found. Download the Commonwealth Online modlist with Wabbajack and run this launcher from inside that folder.";
pub const OS_NOTE: &str =
    "launching from the launcher is not supported on this OS yet; start Fallout 4 from Steam";

pub fn is_windows() -> bool {
    std::env::consts::OS == "windows"
}

pub fn snapshot(pin: Option<&Pin>) -> Event {
    let configured = match locations::user_file().map(|path| locations::load(&path)) {
        Some(Ok(configured)) => Some(configured),
        Some(Err(error)) => {
            let mut view = empty_view();
            view.error = Some(error);
            return Event::State(Box::new(view));
        }
        None => None,
    };
    let game = configured
        .as_ref()
        .map(|value| value.game.as_str())
        .filter(|value| !value.is_empty())
        .map(Path::new);
    Event::State(Box::new(inspect_with_game(pin, &Env::detect(), game)))
}

pub fn inspect(pin: Option<&Pin>, env: &Env) -> StateView {
    inspect_with_game(pin, env, None)
}

fn inspect_with_game(pin: Option<&Pin>, env: &Env, game_override: Option<&Path>) -> StateView {
    let mut view = empty_view();
    let game = match find_game(pin.map(|pin| pin.game.as_path()).or(game_override), env) {
        Ok(game) => game,
        Err(error) => {
            view.error = Some(error.to_string());
            return view;
        }
    };
    view.game_dir = Some(path_text(&game));
    let info = match detect(&game) {
        Ok(info) => info,
        Err(error) => {
            view.error = Some(error.to_string());
            return view;
        }
    };
    view.exe_version = Some(info.version);
    view.sha256 = Some(info.sha256);
    match info.detected {
        Detected::Unsupported(found) => {
            view.play_note = Some(format!("Fallout 4 {found} is not a supported runtime"));
            view.unsupported = Some(found);
        }
        Detected::Supported(runtime) => {
            view.runtime = Some(RuntimeView::of(runtime));
            view.play_note = play_note(pin).map(str::to_owned);
            view.can_play = view.play_note.is_none();
        }
    }
    view
}

fn empty_view() -> StateView {
    StateView {
        game_dir: None,
        error: None,
        exe_version: None,
        sha256: None,
        runtime: None,
        unsupported: None,
        can_play: false,
        play_note: None,
    }
}

fn play_note(pin: Option<&Pin>) -> Option<&'static str> {
    if !pin.is_some_and(|pin| pin.exe.is_some()) {
        Some(NO_MO2)
    } else if !is_windows() {
        Some(OS_NOTE)
    } else {
        None
    }
}

fn path_text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn empty_env() -> Env {
        Env {
            home: None,
            registry_steam_paths: Vec::new(),
            registry_gog_paths: Vec::new(),
        }
    }

    fn pin(game: PathBuf, exe: Option<PathBuf>) -> Pin {
        Pin {
            game,
            exe,
            instance: PathBuf::new(),
            profile: None,
        }
    }

    #[test]
    fn missing_game_folder_is_an_error_state() {
        let dir = tempfile::tempdir().unwrap();
        let pinned = pin(dir.path().join("nope"), None);
        let view = inspect(Some(&pinned), &empty_env());
        assert!(view.error.is_some());
        assert_eq!(view.game_dir, None);
        assert!(!view.can_play);
    }

    #[test]
    fn configured_game_is_shown_without_a_mod_manager() {
        let dir = tempfile::tempdir().unwrap();
        std::fs::write(dir.path().join("Fallout4.exe"), b"").unwrap();
        let view = inspect_with_game(None, &empty_env(), Some(dir.path()));
        assert_eq!(
            view.game_dir.as_deref(),
            Some(path_text(dir.path()).as_str())
        );
        assert!(!view.can_play);
    }

    #[test]
    fn play_needs_a_pinned_mod_organizer_exe() {
        let game = PathBuf::from("D:/Fallout 4");
        assert_eq!(play_note(None), Some(NO_MO2));
        assert_eq!(play_note(Some(&pin(game.clone(), None))), Some(NO_MO2));
        let launchable = pin(game, Some(PathBuf::from("D:/MO2/ModOrganizer.exe")));
        assert_eq!(
            play_note(Some(&launchable)),
            if is_windows() { None } else { Some(OS_NOTE) }
        );
    }
}
