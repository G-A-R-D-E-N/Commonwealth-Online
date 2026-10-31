use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use zip::write::SimpleFileOptions;

use crate::error::{Error, Result};
use crate::fsutil::find_ci;
use crate::redact::redact;

const TAIL_CAP: u64 = 4 * 1024 * 1024;
const CRASH_LOG_FILES: usize = 5;
const MAX_CRASHES: usize = 10;
const CO_BLOCK: &str = "[CO CRASH] ";
const CO_BLOCK_START: &str = "===== game crash report";
const LOG_NAMES: [&str; 6] = [
    "f4se.log",
    "f4se_loader.log",
    "f4se_steam_loader.log",
    "addictol.log",
    "addictolcrashlogger.log",
    "xse pluginpreloader.log",
];
const SYSTEM_MODULES: [&str; 33] = [
    "d3d11",
    "d3d12",
    "dxgi",
    "nvwgf2umx",
    "nvoglv64",
    "nvd3dumx",
    "atidxx64",
    "atiumd",
    "amdxx64",
    "igd",
    "shell32",
    "advapi32",
    "sechost",
    "winhttp",
    "wininet",
    "mswsock",
    "xinput",
    "dinput8",
    "ntdll",
    "kernel32",
    "kernelbase",
    "ucrtbase",
    "msvcr",
    "msvcp",
    "vcruntime",
    "user32",
    "gdi32",
    "win32u",
    "combase",
    "rpcrt4",
    "ole32",
    "ws2_32",
    "wow64",
];

#[derive(Debug, Clone)]
pub struct Locations {
    pub f4se: PathBuf,
    pub script: PathBuf,
    pub game: Option<PathBuf>,
}

impl Locations {
    pub fn detect(game: Option<&Path>) -> Option<Self> {
        let base = dirs::document_dir()?.join("My Games").join("Fallout4");
        Some(Self {
            f4se: base.join("F4SE"),
            script: base.join("Logs").join("Script"),
            game: game.map(Path::to_path_buf),
        })
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Culprit {
    CommonwealthOnline,
    PrismaUi,
    F4se,
    Game,
    Mod(String),
    Unknown,
}

impl Culprit {
    pub fn key(&self) -> &'static str {
        match self {
            Culprit::CommonwealthOnline => "commonwealth_online",
            Culprit::PrismaUi => "prismaui",
            Culprit::F4se => "f4se",
            Culprit::Game => "game",
            Culprit::Mod(_) => "mod",
            Culprit::Unknown => "unknown",
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Crash {
    pub file: String,
    pub time: String,
    pub exception: String,
    pub fault: String,
    pub culprit: Culprit,
    pub verdict: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Shared {
    pub path: PathBuf,
    pub files: Vec<String>,
}

fn is_co_log(name: &str) -> bool {
    name.starts_with("commonwealthonline") && name.ends_with(".log")
}

fn is_crash_log(name: &str) -> bool {
    name.starts_with("crash-") && name.ends_with(".log")
}

fn is_shared_log(name: &str) -> bool {
    is_co_log(name) || LOG_NAMES.contains(&name) || name == "prismaui_f4.log"
}

fn files_in(dir: &Path) -> Vec<(String, PathBuf)> {
    let Ok(entries) = fs::read_dir(dir) else {
        return Vec::new();
    };
    let mut found: Vec<_> = entries
        .flatten()
        .filter(|e| e.file_type().is_ok_and(|t| t.is_file()))
        .map(|e| (e.file_name().to_string_lossy().into_owned(), e.path()))
        .collect();
    found.sort_by_key(|(name, _)| name.to_ascii_lowercase());
    found
}

fn crash_logs(loc: &Locations) -> Vec<(String, PathBuf)> {
    let mut logs: Vec<_> = files_in(&loc.f4se)
        .into_iter()
        .filter(|(name, _)| is_crash_log(&name.to_ascii_lowercase()))
        .collect();
    logs.reverse();
    logs.truncate(CRASH_LOG_FILES);
    logs
}

fn read_tail(path: &Path) -> Option<String> {
    let mut file = File::open(path).ok()?;
    let len = file.metadata().ok()?.len();
    let cut = len > TAIL_CAP;
    if cut {
        file.seek(SeekFrom::Start(len - TAIL_CAP)).ok()?;
    }
    let mut bytes = Vec::new();
    file.take(TAIL_CAP).read_to_end(&mut bytes).ok()?;
    let text = String::from_utf8_lossy(&bytes);
    Some(match (cut, text.find('\n')) {
        (true, Some(nl)) => text[nl + 1..].to_owned(),
        _ => text.into_owned(),
    })
}

fn log_files(loc: &Locations) -> Vec<(String, PathBuf)> {
    let mut found: Vec<(String, PathBuf)> = files_in(&loc.f4se)
        .into_iter()
        .filter(|(name, _)| is_shared_log(&name.to_ascii_lowercase()))
        .map(|(name, path)| (format!("F4SE/{name}"), path))
        .collect();
    found.extend(
        crash_logs(loc)
            .into_iter()
            .map(|(name, path)| (format!("F4SE/{name}"), path)),
    );
    if let Some(game) = &loc.game {
        for name in ["f4se_loader.log", "f4se_steam_loader.log"] {
            if let Some(path) = find_ci(game, name) {
                found.push((format!("Game/{name}"), path));
            }
        }
    }
    if let Some(path) = find_ci(&loc.script, "Papyrus.0.log") {
        found.push(("Logs/Script/Papyrus.0.log".to_owned(), path));
    }
    found
}

pub fn scan(loc: &Locations) -> Vec<Crash> {
    let mut crashes = Vec::new();
    for (name, path) in files_in(&loc.f4se) {
        if is_co_log(&name.to_ascii_lowercase()) {
            if let Some(text) = read_tail(&path) {
                crashes.extend(parse_plugin_crashes(&name, &text));
            }
        }
    }
    for (name, path) in crash_logs(loc) {
        if let Some(crash) = read_tail(&path).and_then(|t| parse_crash_logger(&name, &t)) {
            crashes.push(crash);
        }
    }
    crashes.sort_by(|a, b| b.time.cmp(&a.time));
    let mut seen = std::collections::HashSet::new();
    crashes.retain(|c| seen.insert((c.time.clone(), c.fault.clone())));
    crashes.truncate(MAX_CRASHES);
    crashes
}

fn module_of(token: &str) -> Option<&str> {
    let (module, offset) = token.split_once('+')?;
    let named = module.contains('.') && module.bytes().all(|b| b.is_ascii_graphic() && b != b'[');
    (named && !offset.is_empty() && !module.starts_with("0x")).then_some(module)
}

fn classify(module: &str) -> Option<Culprit> {
    let name = module.to_ascii_lowercase();
    if SYSTEM_MODULES.iter().any(|s| name.starts_with(s)) {
        return None;
    }
    Some(if name.starts_with("commonwealthonline") {
        Culprit::CommonwealthOnline
    } else if name.starts_with("prismaui") {
        Culprit::PrismaUi
    } else if name.starts_with("f4se") {
        Culprit::F4se
    } else if name == "fallout4.exe" {
        Culprit::Game
    } else {
        Culprit::Mod(module.to_owned())
    })
}

fn exception_name(code: &str) -> String {
    let known = match code.trim_start_matches("0x").to_ascii_uppercase().as_str() {
        "C0000005" => "access violation",
        "C00000FD" => "stack overflow",
        "C0000094" => "integer divide by zero",
        "C000001D" => "illegal instruction",
        "C0000409" => "stack buffer overrun",
        "C0000374" => "heap corruption",
        "80000003" => "breakpoint",
        _ => return code.to_owned(),
    };
    format!("{known} ({code})")
}

fn crash_from(
    file: &str,
    time: String,
    exception: String,
    fault: String,
    stack: &[String],
) -> Crash {
    let modules: Vec<&str> = std::iter::once(fault.as_str())
        .chain(stack.iter().map(String::as_str))
        .filter_map(module_of)
        .collect();
    let found = modules.iter().find_map(|m| classify(m).map(|c| (*m, c)));
    let (culprit, place) = match &found {
        Some((module, culprit)) => (culprit.clone(), (*module).to_owned()),
        None => (Culprit::Unknown, String::new()),
    };
    let on_stack = modules
        .iter()
        .any(|m| classify(m) == Some(Culprit::CommonwealthOnline));
    let mut verdict = match &culprit {
        Culprit::CommonwealthOnline => format!("Commonwealth Online crashed in {place}. Press SHARE LOGS and send the zip to the team."),
        Culprit::PrismaUi => format!("PrismaUI crashed in {place}. Make sure it is the PrismaUI version the launcher installed."),
        Culprit::F4se => format!("F4SE crashed in {place}. Check that the F4SE build matches your game version."),
        Culprit::Game => "The game itself crashed. That is usually another mod's data or a mod conflict.".to_owned(),
        Culprit::Mod(name) => format!("{name} crashed. It is not part of Commonwealth Online; try disabling it."),
        Culprit::Unknown => "The cause could not be read from this crash.".to_owned(),
    };
    if on_stack && culprit != Culprit::CommonwealthOnline {
        verdict.push_str(" Commonwealth Online code was on the call stack, so share the logs too.");
    }
    Crash {
        file: file.to_owned(),
        time,
        exception,
        fault,
        culprit,
        verdict,
    }
}

#[derive(Default)]
struct Block {
    time: String,
    exception: String,
    fault: String,
    frames: Vec<String>,
}

fn parse_plugin_crashes(file: &str, text: &str) -> Vec<Crash> {
    let mut crashes = Vec::new();
    let mut current: Option<Block> = None;
    let mut flush = |block: Option<Block>| {
        if let Some(b) = block {
            crashes.push(crash_from(file, b.time, b.exception, b.fault, &b.frames));
        }
    };
    for line in text.lines() {
        let Some(rest) = line.strip_prefix(CO_BLOCK) else {
            if !line.trim().is_empty() {
                flush(current.take());
            }
            continue;
        };
        if rest.starts_with(CO_BLOCK_START) {
            flush(current.replace(Block::default()));
        } else if let Some(block) = current.as_mut() {
            if let Some(time) = rest.strip_prefix("time=") {
                block.time = time.to_owned();
            } else if let Some(both) = rest.strip_prefix("exception=") {
                let (code, fault) = both.split_once(" fault=").unwrap_or((both, ""));
                block.exception = exception_name(code);
                block.fault = fault.to_owned();
            } else if let Some(frame) = rest.strip_prefix("frame ") {
                block
                    .frames
                    .extend(frame.split_whitespace().nth(1).map(str::to_owned));
            }
        }
    }
    flush(current);
    crashes
}

fn parse_crash_logger(file: &str, text: &str) -> Option<Crash> {
    let mut time = String::new();
    let mut exception = String::new();
    let mut stack = Vec::new();
    let mut in_stack = false;
    for line in text.lines() {
        if in_stack {
            if !line.trim_start().starts_with('[') {
                in_stack = false;
                continue;
            }
            let module = line
                .split_whitespace()
                .find(|t| module_of(t).is_some() && !t.starts_with("0x"));
            stack.extend(module.map(str::to_owned));
        } else if let Some(t) = line.strip_prefix("CRASH TIME:") {
            time = t.trim().to_owned();
        } else if let Some(rest) = line.strip_prefix("Unhandled exception \"") {
            exception = rest.split('"').next().unwrap_or_default().to_owned();
        } else if line.starts_with("CALL STACK") && stack.is_empty() {
            in_stack = true;
        }
    }
    if time.is_empty() && exception.is_empty() {
        return None;
    }
    let fault = stack.first().cloned().unwrap_or_default();
    Some(crash_from(file, time, exception, fault, &stack))
}

fn stamp() -> String {
    let secs = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |d| d.as_secs());
    let (days, rem) = ((secs / 86_400) as i64, secs % 86_400);
    let z = days + 719_468;
    let (era, doe) = (z.div_euclid(146_097), z.rem_euclid(146_097));
    let yoe = (doe - doe / 1460 + doe / 36_524 - doe / 146_096) / 365;
    let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    let mp = (5 * doy + 2) / 153;
    let day = doy - (153 * mp + 2) / 5 + 1;
    let month = if mp < 10 { mp + 3 } else { mp - 9 };
    let year = yoe + era * 400 + i64::from(month <= 2);
    format!(
        "{year:04}{month:02}{day:02}-{:02}{:02}{:02}",
        rem / 3600,
        rem % 3600 / 60,
        rem % 60
    )
}

fn archive_error(e: impl std::fmt::Display) -> Error {
    Error::Archive(e.to_string())
}

pub fn share(loc: &Locations, out_dir: &Path, summary: &str) -> Result<Shared> {
    let sources = log_files(loc);
    if sources.is_empty() {
        return Err(Error::NoLogs);
    }
    fs::create_dir_all(out_dir).map_err(Error::io(out_dir))?;
    let path = out_dir.join(format!("CommonwealthOnline-logs-{}.zip", stamp()));
    match write_zip(&path, sources, summary) {
        Ok(files) => Ok(Shared { path, files }),
        Err(e) => {
            let _ = fs::remove_file(&path);
            Err(e)
        }
    }
}

fn write_zip(path: &Path, sources: Vec<(String, PathBuf)>, summary: &str) -> Result<Vec<String>> {
    let mut zip = zip::ZipWriter::new(File::create(path).map_err(Error::io(path))?);
    let options = SimpleFileOptions::default().compression_method(zip::CompressionMethod::Deflated);
    let mut add = |name: &str, text: &str| -> Result<()> {
        zip.start_file(name, options).map_err(archive_error)?;
        zip.write_all(redact(text).as_bytes())
            .map_err(Error::io(path))
    };
    add("launcher-report.txt", summary)?;
    let mut files = Vec::new();
    for (name, source) in sources {
        if let Some(text) = read_tail(&source) {
            add(&name, &text)?;
            files.push(name);
        }
    }
    zip.finish().map_err(archive_error)?;
    Ok(files)
}

#[cfg(test)]
mod tests {
    use super::*;

    const CO_LOG: &str = "[02:23:15.065] [21860] [I] hello password=hunter2\n[CO CRASH] first-chance exception=0xC0000005 fault=CommonwealthOnlineUI.dll+0x1D09A0 (the game may still handle it)\n\n[CO CRASH] ===== game crash report =====\n[CO CRASH] time=2026-10-29 02:23:22.166\n[CO CRASH] exception=0xC0000005 fault=CommonwealthOnlineUI.dll+0x1D09A0\n[CO CRASH] frame 00 CommonwealthOnlineUI.dll+0x1D09A0\n[CO CRASH] frame 11 ucrtbase.dll+0x142D6\n";
    const CRASH_LOG: &str = "CRASH TIME: 2026-09-23 22:41:20\nFallout 4 v1.10.163\n\nUnhandled exception \"EXCEPTION_ACCESS_VIOLATION\" at 0x017C27CA2160\n\nCALL STACK ([P]robable / [S]tack scan):\n\t[ 0][P] 0x017C27CA2160               \n\t[ 1][S] 0x7FF70B7B1358   Fallout4.exe+0141358\ttest rdi, rdi |  BGSKeywordForm::HasKeyword\n\t[ 2][S] 0x7FF6C0000000   CommonwealthOnline.dll+0x10\tmov [rax+8], eax\n\nREGISTERS:\n";

    fn fixture() -> (tempfile::TempDir, Locations) {
        let dir = tempfile::tempdir().unwrap();
        let f4se = dir.path().join("F4SE");
        let script = dir.path().join("Script");
        fs::create_dir_all(&f4se).unwrap();
        fs::create_dir_all(&script).unwrap();
        fs::write(f4se.join("CommonwealthOnline.log"), CO_LOG).unwrap();
        fs::write(f4se.join("crash-2026-09-23-22-41-20.log"), CRASH_LOG).unwrap();
        fs::write(f4se.join("f4se.log"), "C:\\Users\\Jane\\x\n").unwrap();
        fs::write(f4se.join("f4se_loader.log"), "loader\n").unwrap();
        fs::write(f4se.join("CommonwealthOnlineNG.log"), CO_LOG).unwrap();
        fs::write(f4se.join("SomeOtherMod.log"), "private\n").unwrap();
        fs::write(f4se.join("CommonwealthBallistics.log"), "other\n").unwrap();
        fs::write(script.join("Papyrus.0.log"), "papyrus\n").unwrap();
        let loc = Locations {
            f4se,
            script,
            game: None,
        };
        (dir, loc)
    }

    #[test]
    fn plugin_block_names_commonwealth_online_and_skips_first_chance() {
        let crashes = parse_plugin_crashes("CommonwealthOnline.log", CO_LOG);
        assert_eq!(crashes.len(), 1);
        let c = &crashes[0];
        assert_eq!(c.culprit, Culprit::CommonwealthOnline);
        assert_eq!(c.time, "2026-10-29 02:23:22.166");
        assert_eq!(c.exception, "access violation (0xC0000005)");
        assert_eq!(c.fault, "CommonwealthOnlineUI.dll+0x1D09A0");
    }

    #[test]
    fn crash_logger_game_crash_flags_commonwealth_online_on_the_stack() {
        let c = parse_crash_logger("crash.log", CRASH_LOG).unwrap();
        assert_eq!(c.culprit, Culprit::Game);
        assert_eq!(c.exception, "EXCEPTION_ACCESS_VIOLATION");
        assert!(c
            .verdict
            .contains("Commonwealth Online code was on the call stack"));
        assert!(parse_crash_logger("x.log", "nothing here").is_none());
    }

    #[test]
    fn another_mod_is_named_not_blamed_on_us() {
        let log = "CRASH TIME: 2026-01-01 00:00:00\nUnhandled exception \"X\" at 0x1\nCALL STACK\n\t[ 0][S] 0x1 ntdll.dll+0x10\n\t[ 1][S] 0x2 SomeMod.dll+0x20\n\n";
        let c = parse_crash_logger("c.log", log).unwrap();
        assert_eq!(c.culprit, Culprit::Mod("SomeMod.dll".to_owned()));
        assert!(!c.verdict.contains("Commonwealth Online code"));
    }

    #[test]
    fn graphics_drivers_are_skipped_not_blamed_as_mods() {
        let log = "CRASH TIME: 2026-01-01 00:00:00\nUnhandled exception \"X\" at 0x1\nCALL STACK\n\t[ 0][S] 0x1 d3d11.dll+0x10\n\t[ 1][S] 0x2 nvwgf2umx.dll+0x20\n\t[ 2][S] 0x3 Fallout4.exe+0x30\n\n";
        assert_eq!(
            parse_crash_logger("c.log", log).unwrap().culprit,
            Culprit::Game
        );
    }

    #[test]
    fn scan_orders_newest_first() {
        let (_dir, loc) = fixture();
        let times: Vec<_> = scan(&loc).into_iter().map(|c| c.time).collect();
        assert_eq!(times, ["2026-10-29 02:23:22.166", "2026-09-23 22:41:20"]);
    }

    #[test]
    fn share_zips_redacted_logs_and_leaves_other_mods_out() {
        let (dir, loc) = fixture();
        let shared = share(&loc, &dir.path().join("out"), "summary C:\\Users\\Bob\\x").unwrap();
        let mut zip = zip::ZipArchive::new(File::open(&shared.path).unwrap()).unwrap();
        let mut names: Vec<_> = zip.file_names().map(|n| n.unwrap().into_owned()).collect();
        names.sort();
        assert_eq!(
            names,
            [
                "F4SE/CommonwealthOnline.log",
                "F4SE/CommonwealthOnlineNG.log",
                "F4SE/crash-2026-09-23-22-41-20.log",
                "F4SE/f4se.log",
                "F4SE/f4se_loader.log",
                "Logs/Script/Papyrus.0.log",
                "launcher-report.txt",
            ]
        );
        let mut text = String::new();
        zip.by_name("F4SE/CommonwealthOnline.log")
            .unwrap()
            .read_to_string(&mut text)
            .unwrap();
        assert!(text.contains("password=<redacted>") && !text.contains("hunter2"));
        let mut report = String::new();
        zip.by_name("launcher-report.txt")
            .unwrap()
            .read_to_string(&mut report)
            .unwrap();
        assert!(!report.contains("Bob"));
    }

    #[test]
    fn nothing_to_share_is_an_error_not_an_empty_zip() {
        let dir = tempfile::tempdir().unwrap();
        let loc = Locations {
            f4se: dir.path().join("none"),
            script: dir.path().join("none"),
            game: None,
        };
        assert!(matches!(share(&loc, dir.path(), ""), Err(Error::NoLogs)));
        assert!(fs::read_dir(dir.path()).unwrap().next().is_none());
    }

    #[test]
    fn stamp_is_a_sortable_utc_timestamp() {
        let s = stamp();
        assert_eq!(s.len(), 15);
        assert!(s.starts_with("20"));
    }
}
