use launcher_core::discover::{find_game, Env};
use launcher_core::logs::{self, Crash, Locations};

use crate::mods::Pin;
use crate::protocol::{CrashView, Event};
use crate::state;

const NO_DOCUMENTS: &str = "cannot find your Documents folder";
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
    }
}

fn crash_lines(crashes: &[Crash]) -> Vec<String> {
    let mut lines = vec![format!("Crashes found: {}", crashes.len())];
    lines.extend(crashes.iter().map(|c| {
        format!(
            "  {} | {} | {} | {} | {}",
            c.time, c.file, c.exception, c.fault, c.verdict
        )
    }));
    lines
}

fn report(pin: Option<&Pin>, crashes: &[Crash]) -> String {
    let mut lines = vec![
        format!("Commonwealth Online launcher {}", env!("CARGO_PKG_VERSION")),
        format!("OS: {}", std::env::consts::OS),
    ];
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
    use launcher_core::logs::Culprit;
    use serde_json::json;

    fn crash() -> Crash {
        Crash {
            file: "CommonwealthOnline.log".to_owned(),
            time: "2026-10-29 02:23:22.166".to_owned(),
            exception: "access violation (0xC0000005)".to_owned(),
            fault: "CommonwealthOnlineUI.dll+0x1D09A0".to_owned(),
            culprit: Culprit::CommonwealthOnline,
            verdict: "Commonwealth Online crashed.".to_owned(),
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
        };
        assert_eq!(
            serde_json::to_value(&event).unwrap(),
            json!({"type": "logs_shared", "path": "C:\\Users\\x\\Desktop\\a.zip", "files": 7})
        );
    }

    #[test]
    fn report_lists_every_crash() {
        let lines = crash_lines(&[crash()]);
        assert_eq!(lines[0], "Crashes found: 1");
        assert!(lines[1].contains("CommonwealthOnlineUI.dll+0x1D09A0"));
    }
}
