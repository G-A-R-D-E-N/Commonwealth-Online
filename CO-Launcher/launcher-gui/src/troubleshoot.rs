use std::fs;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use launcher_core::server as files;

use crate::logs::NO_DOCUMENTS;
use crate::mods::{self, Pin};
use crate::protocol::{Event, ServerFileView, Task};
use crate::server;
use crate::state::NO_MO2;

const SAVED_FOLDER: &str = "host-server-folder.json";
const SAVED_FOLDER_DIR: &str = "Documents/My Games/Fallout4/CommonwealthOnline";
const GAME_OPEN: &str = "Close Fallout 4 first, then try again.";
const SERVER_RUNNING: &str =
    "Stop the server on the SERVER page, or use END LEFTOVER SERVERS first.";
const STILL_RUNNING: &str =
    "server process(es) are still running. They are dedicated servers, so stop them on the SERVER page.";
const NO_PACKAGE: &str = "This launcher has no bundled server package. Reinstall the launcher.";
const NO_APPDATA: &str = "Cannot find your AppData folder.";
const NO_LIST: &str = "Could not read the list of running programs.";

pub fn fix(what: Task, outcome: Result<String, String>) -> Event {
    let (ok, message) = match outcome {
        Ok(message) => (true, message),
        Err(message) => (false, message),
    };
    Event::FixResult {
        what,
        ok,
        message,
        list: Vec::new(),
    }
}

pub fn end_servers() -> Result<String, String> {
    if mods::game_running() {
        return Err(GAME_OPEN.to_owned());
    }
    let Some(before) = mods::process_count(files::EXE) else {
        return Err(NO_LIST.to_owned());
    };
    if before == 0 {
        return Ok("No leftover server is running.".to_owned());
    }
    server::end_strays();
    ended_outcome(before, mods::process_count(files::EXE).unwrap_or(before))
}

fn ended_outcome(before: usize, left: usize) -> Result<String, String> {
    match (before - left.min(before), left) {
        (_, 0) => Ok(format!("Ended {before} leftover server process(es).")),
        (0, _) => Err(format!("{left} {STILL_RUNNING}")),
        (ended, _) => Ok(format!(
            "Ended {ended} leftover server process(es). {left} {STILL_RUNNING}"
        )),
    }
}

pub fn clear_links() -> Result<String, String> {
    if mods::game_running() {
        return Err(GAME_OPEN.to_owned());
    }
    if mods::process_running(files::EXE) {
        return Err(SERVER_RUNNING.to_owned());
    }
    let dir = link_dir().ok_or_else(|| NO_APPDATA.to_owned())?;
    let removed = clear_link_files(&dir)?;
    Ok(format!("Removed {removed} hosting link file(s)."))
}

pub fn repair(pin: Option<&Pin>) -> Result<String, String> {
    let pin = pin.ok_or_else(|| NO_MO2.to_owned())?;
    match server::ensure(pin) {
        Some(problem) => Err(problem),
        None if !files::installed(&files::dir(&pin.instance)) => Err(NO_PACKAGE.to_owned()),
        None => Ok("The server files were reinstalled.".to_owned()),
    }
}

pub fn reset_folder() -> Result<String, String> {
    if mods::game_running() {
        return Err(GAME_OPEN.to_owned());
    }
    let path = saved_folder().ok_or_else(|| NO_DOCUMENTS.to_owned())?;
    match fs::remove_file(&path) {
        Ok(()) => Ok("Forgot the saved server folder.".to_owned()),
        Err(error) if error.kind() == ErrorKind::NotFound => {
            Ok("No saved server folder was remembered.".to_owned())
        }
        Err(error) => Err(format!("Could not remove {}: {error}", path.display())),
    }
}

pub fn server_files(pin: Option<&Pin>) -> Event {
    let Some(pin) = pin else {
        return fix(Task::CheckServerFiles, Err(NO_MO2.to_owned()));
    };
    let list: Vec<ServerFileView> = server_copies(pin)
        .into_iter()
        .filter_map(|path| server_file(&path))
        .collect();
    Event::FixResult {
        what: Task::CheckServerFiles,
        ok: true,
        message: format!("Found {} server file(s).", list.len()),
        list,
    }
}

pub(crate) fn link_dir() -> Option<PathBuf> {
    let app = dirs::data_local_dir()?.join("CommonwealthOnline");
    Some(app.join("SteamHostLink"))
}

fn saved_folder() -> Option<PathBuf> {
    let profile = std::env::var_os("USERPROFILE")?;
    let folder = Path::new(&profile).join(SAVED_FOLDER_DIR);
    Some(folder.join(SAVED_FOLDER))
}

fn clear_link_files(dir: &Path) -> Result<usize, String> {
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(error) if error.kind() == ErrorKind::NotFound => return Ok(0),
        Err(error) => return Err(format!("Could not read {}: {error}", dir.display())),
    };
    let mut removed = 0;
    for path in entries.flatten().map(|entry| entry.path()) {
        if is_link_file(&path) && path.is_file() {
            if let Err(error) = fs::remove_file(&path) {
                return Err(format!("Could not remove {}: {error}", path.display()));
            }
            removed += 1;
        }
    }
    Ok(removed)
}

pub(crate) fn is_link_file(path: &Path) -> bool {
    match path.extension().and_then(|ext| ext.to_str()) {
        Some(ext) => ext.eq_ignore_ascii_case("port") || ext.eq_ignore_ascii_case("token"),
        None => false,
    }
}

fn server_copies(pin: &Pin) -> Vec<PathBuf> {
    let mut dirs: Vec<PathBuf> = fs::read_dir(pin.instance.join("mods"))
        .into_iter()
        .flatten()
        .flatten()
        .map(|entry| entry.path().join("server"))
        .collect();
    dirs.push(pin.instance.join("overwrite").join("server"));
    let mut copies: Vec<PathBuf> = dirs
        .into_iter()
        .map(|dir| dir.join(files::EXE))
        .filter(|path| path.is_file())
        .collect();
    copies.sort();
    copies
}

fn server_file(path: &Path) -> Option<ServerFileView> {
    let meta = fs::metadata(path).ok()?;
    Some(ServerFileView {
        path: path.display().to_string(),
        size: meta.len(),
        modified: meta.modified().map_or(0, unix_seconds),
    })
}

fn unix_seconds(at: SystemTime) -> u64 {
    at.duration_since(UNIX_EPOCH).map_or(0, |d| d.as_secs())
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn pin(instance: &Path) -> Pin {
        Pin {
            game: PathBuf::new(),
            exe: None,
            instance: instance.to_path_buf(),
            profile: None,
        }
    }

    #[test]
    fn clear_link_files_removes_only_port_and_token_files() {
        let dir = tempfile::tempdir().unwrap();
        for name in ["1.port", "1.token", "2.PORT", "notes.txt"] {
            fs::write(dir.path().join(name), b"x").unwrap();
        }
        assert_eq!(clear_link_files(dir.path()), Ok(3));
        assert!(dir.path().join("notes.txt").is_file());
        assert!(!dir.path().join("1.port").exists());
        assert_eq!(clear_link_files(&dir.path().join("missing")), Ok(0));
    }

    #[test]
    fn server_files_lists_the_mod_copies_and_the_overwrite_copy() {
        let instance = tempfile::tempdir().unwrap();
        let root = instance.path();
        for folder in [
            "mods/Commonwealth Online Server/server",
            "mods/Other Mod/server",
            "overwrite/server",
        ] {
            let dir = root.join(folder);
            fs::create_dir_all(&dir).unwrap();
            fs::write(dir.join(files::EXE), b"exe").unwrap();
        }
        fs::create_dir_all(root.join("mods/No Server/server")).unwrap();
        let Event::FixResult { list, .. } = server_files(Some(&pin(root))) else {
            panic!("no result");
        };
        assert_eq!(list.len(), 3);
        assert!(list.iter().all(|file| file.size == 3));
    }

    #[test]
    fn fix_result_event_matches_contract() {
        let event = fix(
            Task::ClearLinks,
            Ok("Removed 2 hosting link file(s).".to_owned()),
        );
        assert_eq!(
            serde_json::to_value(&event).unwrap(),
            json!({
                "type": "fix_result",
                "what": "clear_links",
                "ok": true,
                "message": "Removed 2 hosting link file(s).",
                "list": []
            })
        );
    }

    #[test]
    fn repair_needs_a_pinned_mod_organizer() {
        assert_eq!(repair(None), Err(NO_MO2.to_owned()));
    }

    #[test]
    fn end_servers_fails_while_any_server_process_is_left() {
        assert_eq!(
            ended_outcome(2, 0),
            Ok("Ended 2 leftover server process(es).".to_owned())
        );
        let Err(message) = ended_outcome(1, 1) else {
            panic!("ending nothing must fail");
        };
        assert!(message.starts_with("1 server process(es) are still running."));
        assert!(message.contains("SERVER page"));
        let Ok(partly) = ended_outcome(2, 1) else {
            panic!("ending some strays is a success");
        };
        assert!(partly.starts_with("Ended 1 leftover server process(es). 1 server"));
    }
}
