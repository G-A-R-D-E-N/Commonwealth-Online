use std::fs;
use std::path::{Path, PathBuf};

use launcher_core::fsutil::find_ci;
use launcher_core::runtime::pe_file_version;

use crate::protocol::ModFinding;

const INJECTOR: &str =
    "A graphics DLL that ReShade or ENB may use. It can conflict with Commonwealth Online.";
const PROXY: &str =
    "A DLL that some mods use to load code. Check what installed it if the game will not start.";
const LOOSE_PLUGIN: &str =
    "A plugin file loose in the Data folder. The launcher did not install it, so it may conflict with other plugins.";
const F4SE_PLUGIN: &str =
    "A script extender plugin. It loads with the game, so remove it if the game crashes on start.";
const NO_F4SE: &str =
    "The script extender F4SE is not in the game folder. Commonwealth Online needs it to start.";
const STOCK_PREFIXES: [&str; 3] = ["fallout4", "dlc", "commonwealthonline"];

pub fn has_f4se(game: &Path) -> bool {
    find_ci(game, "f4se_loader.exe").is_some_and(|path| path.is_file())
}

pub fn f4se_version(game: &Path) -> Option<String> {
    find_ci(game, "f4se_loader.exe")
        .filter(|path| path.is_file())
        .and_then(|path| pe_file_version(&path).ok())
}

pub fn dependency_notes(game: &Path) -> Vec<String> {
    if has_f4se(game) {
        Vec::new()
    } else {
        vec![NO_F4SE.to_owned()]
    }
}

pub fn scan_mod_files(game: &Path) -> Vec<ModFinding> {
    let mut found = Vec::new();
    for (name, reason) in [
        ("dxgi.dll", INJECTOR),
        ("d3d11.dll", INJECTOR),
        ("winhttp.dll", PROXY),
    ] {
        if let Some(path) = find_ci(game, name).filter(|path| path.is_file()) {
            found.push(finding(&path, reason));
        }
    }
    if let Some(data) = find_ci(game, "Data") {
        for path in files_with(&data, &["esp", "esm", "esl"]) {
            if !is_stock(&path) {
                found.push(finding(&path, LOOSE_PLUGIN));
            }
        }
        if let Some(plugins) = find_ci(&data, "F4SE").and_then(|f4se| find_ci(&f4se, "Plugins")) {
            for path in files_with(&plugins, &["dll"]) {
                if !is_stock(&path) {
                    found.push(finding(&path, F4SE_PLUGIN));
                }
            }
        }
    }
    found.sort_by(|a, b| a.path.cmp(&b.path));
    found
}

fn finding(path: &Path, reason: &str) -> ModFinding {
    ModFinding {
        path: path.to_string_lossy().into_owned(),
        reason: reason.to_owned(),
    }
}

fn files_with(dir: &Path, extensions: &[&str]) -> Vec<PathBuf> {
    let Ok(entries) = fs::read_dir(dir) else {
        return Vec::new();
    };
    entries
        .flatten()
        .map(|entry| entry.path())
        .filter(|path| {
            path.is_file()
                && path
                    .extension()
                    .and_then(|s| s.to_str())
                    .is_some_and(|ext| extensions.iter().any(|e| ext.eq_ignore_ascii_case(e)))
        })
        .collect()
}

fn is_stock(path: &Path) -> bool {
    let name = path
        .file_name()
        .map(|name| name.to_string_lossy().to_ascii_lowercase())
        .unwrap_or_default();
    name == "update.esm"
        || STOCK_PREFIXES
            .iter()
            .any(|prefix| name.starts_with(*prefix))
        || is_creation_club(&name)
}

fn is_creation_club(name: &str) -> bool {
    let Some(body) = name.strip_prefix("cc") else {
        return false;
    };
    let (Some(tag), Some(rest)) = (body.get(..3), body.get(3..)) else {
        return false;
    };
    let Some((number, _)) = rest.strip_prefix("fo4").and_then(|r| r.split_once('-')) else {
        return false;
    };
    tag.bytes().all(|b| b.is_ascii_alphanumeric())
        && !number.is_empty()
        && number.bytes().all(|b| b.is_ascii_digit())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn finds_loose_mods_but_not_the_stock_or_our_own_files() {
        let game = tempfile::tempdir().unwrap();
        let data = game.path().join("Data");
        fs::create_dir_all(data.join("F4SE/Plugins")).unwrap();
        fs::write(data.join("custom.esp"), b"plugin").unwrap();
        fs::write(data.join("Fallout4.esm"), b"plugin").unwrap();
        fs::write(data.join("F4SE/Plugins/custom.dll"), b"plugin").unwrap();
        fs::write(data.join("F4SE/Plugins/CommonwealthOnline.dll"), b"plugin").unwrap();
        fs::write(game.path().join("dxgi.dll"), b"injector").unwrap();
        let found = scan_mod_files(game.path());
        let paths: Vec<&str> = found.iter().map(|f| f.path.as_str()).collect();
        assert!(paths.iter().any(|p| p.ends_with("custom.esp")));
        assert!(paths.iter().any(|p| p.ends_with("custom.dll")));
        assert!(paths.iter().any(|p| p.ends_with("dxgi.dll")));
        assert!(!paths
            .iter()
            .any(|p| p.ends_with("Fallout4.esm") || p.ends_with("CommonwealthOnline.dll")));
    }

    #[test]
    fn creation_club_plugins_are_stock_but_a_mod_named_cc_is_not() {
        assert!(is_stock(Path::new("Data/ccbgsfo4001-pipboy.esl")));
        assert!(is_stock(Path::new("Data/ccOTMFO4001-Remnants.esl")));
        assert!(!is_stock(Path::new("Data/ccSomething.esp")));
        assert!(!is_stock(Path::new("Data/ccabcfo4-notes.esp")));
    }

    #[test]
    fn detects_the_script_extender_in_the_selected_game_folder() {
        let game = tempfile::tempdir().unwrap();
        assert!(!has_f4se(game.path()));
        assert_eq!(dependency_notes(game.path()), vec![NO_F4SE.to_owned()]);
        fs::write(game.path().join("f4se_loader.exe"), b"loader").unwrap();
        assert!(has_f4se(game.path()));
        assert!(dependency_notes(game.path()).is_empty());
        assert_eq!(f4se_version(game.path()), None);
    }
}
