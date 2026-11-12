use launcher_core::discover::{find_game, Env};
use launcher_core::logs::{self, Crash, Culprit, Locations};

use crate::mods::Pin;
use crate::protocol::{CrashView, Event};
use crate::state;

pub(crate) const NO_DOCUMENTS: &str = "cannot find your Documents folder";
const NO_SAVE_FOLDER: &str = "cannot find a Desktop, Downloads or Documents folder to save the zip";

fn failure(message: &str) -> Event {
    Event::Error {
        message: message.to_owned(),
    }
}

fn locations(pin: Option<&Pin>) -> Option<Locations> {
    let game = find_game(pin.map(|pin| pin.game.as_path()), &Env::detect()).ok();
    Locations::detect(game.as_deref())
}

pub fn crashes(pin: Option<&Pin>) -> Event {
    match locations(pin) {
        Some(loc) => Event::Crashes {
            list: logs::scan(&loc).iter().map(view).collect(),
        },
        None => failure(NO_DOCUMENTS),
    }
}

pub fn share(pin: Option<&Pin>) -> Event {
    let Some(loc) = locations(pin) else {
        return failure(NO_DOCUMENTS);
    };
    let Some(folder) = dirs::desktop_dir()
        .or_else(dirs::download_dir)
        .or_else(dirs::document_dir)
    else {
        return failure(NO_SAVE_FOLDER);
    };
    let report = report(pin, &logs::scan(&loc));
    match logs::share(&loc, &folder, &report) {
        Ok(shared) => Event::LogsShared {
            path: shared.path.to_string_lossy().into_owned(),
            files: shared.files.len(),
            skipped: shared.skipped.len(),
        },
        Err(problem) => failure(&problem.to_string()),
    }
}

fn view(crash: &Crash) -> CrashView {
    CrashView {
        file: crash.file.clone(),
        time: crash.time.clone(),
        exception: crash.exception.clone(),
        fault: crash.fault.clone(),
        culprit: crash.culprit.key(),
        verdict: crash.verdict.clone(),
        scanned: crash.scanned.clone(),
    }
}

fn crash_lines(crashes: &[Crash]) -> Vec<String> {
    let readable = crashes
        .iter()
        .filter(|c| c.culprit != Culprit::Unreadable)
        .count();
    let mut lines = vec![format!("Crashes found: {readable}")];
    lines.extend(crashes.iter().map(|c| {
        let mut line = format!(
            "  {} | {} | {} | {} | {}",
            c.time, c.file, c.exception, c.fault, c.verdict
        );
        if !c.scanned.is_empty() {
            line.push_str(&format!(
                " | seen in scan entries: {}",
                c.scanned.join(", ")
            ));
        }
        line
    }));
    lines
}

fn heading() -> String {
    format!(
        "Commonwealth Online launcher {}",
        launcher_core::update::running_version()
    )
}

fn report(pin: Option<&Pin>, crashes: &[Crash]) -> String {
    let mut lines = vec![heading(), format!("OS: {}", std::env::consts::OS)];
    if let Event::State(s) = state::snapshot(pin) {
        lines.push(format!(
            "Game folder: {}",
            s.game_dir.as_deref().unwrap_or("not found")
        ));
        lines.push(format!(
            "Game version: {}",
            s.exe_version.as_deref().unwrap_or("unknown")
        ));
        if let Some(runtime) = &s.runtime {
            lines.push(format!("Runtime: {}", runtime.label));
        }
        lines.extend(s.error.iter().map(|e| format!("Problem: {e}")));
        lines.extend(
            s.unsupported
                .iter()
                .map(|v| format!("Unsupported version: {v}")),
        );
    }
    lines.extend(crash_lines(crashes));
    lines.join("\n") + "\n"
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn crash() -> Crash {
        Crash {
            file: "CommonwealthOnline.log".to_owned(),
            time: "2026-10-29 02:23:22.166".to_owned(),
            exception: "access violation (0xC0000005)".to_owned(),
            fault: "CommonwealthOnlineUI.dll+0x1D09A0".to_owned(),
            culprit: Culprit::CommonwealthOnline,
            verdict: "Commonwealth Online crashed.".to_owned(),
            scanned: Vec::new(),
        }
    }

    #[test]
    fn crashes_event_matches_contract() {
        let event = Event::Crashes {
            list: vec![view(&crash())],
        };
        assert_eq!(
            serde_json::to_value(&event).unwrap(),
            json!({
                "type": "crashes",
                "list": [{
                    "file": "CommonwealthOnline.log",
                    "time": "2026-10-29 02:23:22.166",
                    "exception": "access violation (0xC0000005)",
                    "fault": "CommonwealthOnlineUI.dll+0x1D09A0",
                    "culprit": "commonwealth_online",
                    "verdict": "Commonwealth Online crashed."
                }]
            })
        );
    }

    #[test]
    fn shared_event_matches_contract() {
        let event = Event::LogsShared {
            path: "C:\\Users\\x\\Desktop\\a.zip".to_owned(),
            files: 7,
            skipped: 2,
        };
        assert_eq!(
            serde_json::to_value(&event).unwrap(),
            json!({"type": "logs_shared", "path": "C:\\Users\\x\\Desktop\\a.zip", "files": 7, "skipped": 2})
        );
    }

    #[test]
    fn report_lists_every_crash() {
        let lines = crash_lines(&[crash()]);
        assert_eq!(lines[0], "Crashes found: 1");
        assert!(lines[1].contains("CommonwealthOnlineUI.dll+0x1D09A0"));
    }

    #[test]
    fn scanned_modules_are_sent_and_reported_only_when_present() {
        let mut seen = crash();
        seen.scanned = vec!["SomeMod.dll".to_owned()];
        assert_eq!(
            serde_json::to_value(view(&seen)).unwrap()["scanned"],
            json!(["SomeMod.dll"])
        );
        let plain = serde_json::to_value(view(&crash())).unwrap();
        assert!(plain.get("scanned").is_none());
        let lines = crash_lines(&[seen]);
        assert!(lines[1].ends_with("| seen in scan entries: SomeMod.dll"));
        let plain_lines = crash_lines(&[crash()]);
        assert!(!plain_lines[1].contains("seen in scan entries"));
    }

    #[test]
    fn report_counts_only_readable_crashes() {
        let mut unreadable = crash();
        unreadable.culprit = Culprit::Unreadable;
        let lines = crash_lines(&[crash(), unreadable]);
        assert_eq!(lines[0], "Crashes found: 1");
        assert_eq!(lines.len(), 3);
    }

    #[test]
    fn heading_names_the_running_release_version() {
        let version = launcher_core::update::running_version();
        assert_eq!(heading(), format!("Commonwealth Online launcher {version}"));
    }
}
