use std::collections::HashSet;
use std::fs::{self, File};
use std::io::Read;
use std::path::{Path, PathBuf};

use serde_json::Value;

use crate::fsutil::find_ci;
use crate::modmgr::Roots;

pub const NOTE: &str = "Deployed per the last Vortex deploy; may be stale. Read-only.";
const MAX_DEPLOY: u64 = 16 << 20;
const UTF8_BOM: &[u8] = b"\xEF\xBB\xBF";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Install {
    pub exe: Option<PathBuf>,
    pub app_data: Option<PathBuf>,
}

// Constraint: Vortex's state.v2 LevelDB is never read or written.
pub fn detect(roots: &Roots) -> Option<Install> {
    detect_with(roots, registry_locations())
}

pub fn detect_with(roots: &Roots, registry: Vec<PathBuf>) -> Option<Install> {
    let exe = roots
        .program_files
        .iter()
        .map(|pf| {
            pf.join("Black Tree Gaming Ltd")
                .join("Vortex")
                .join("Vortex.exe")
        })
        .chain(
            roots
                .local_app_data
                .iter()
                .map(|local| local.join("Programs").join("Vortex").join("Vortex.exe")),
        )
        .chain(registry.into_iter().map(|dir| dir.join("Vortex.exe")))
        .find(|path| path.is_file());
    let app_data = roots
        .app_data
        .as_ref()
        .map(|app| app.join("Vortex"))
        .filter(|path| path.is_dir());
    if exe.is_none() && app_data.is_none() {
        return None;
    }
    Some(Install { exe, app_data })
}

pub fn default_staging(roots: &Roots) -> Option<PathBuf> {
    roots
        .app_data
        .as_ref()
        .map(|app| app.join("Vortex").join("fallout4").join("mods"))
}

pub fn deploys_mod(game: &Path, prefix: &str) -> bool {
    let prefix = prefix.to_ascii_lowercase();
    match read_deployment(game) {
        Deploy::Read(found) => found
            .sources
            .iter()
            .any(|source| source.to_ascii_lowercase().starts_with(&prefix)),
        _ => false,
    }
}

#[cfg(windows)]
fn registry_locations() -> Vec<PathBuf> {
    use winreg::enums::{HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE, KEY_READ};
    use winreg::RegKey;

    const UNINSTALL: [&str; 2] = [
        r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall",
        r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall",
    ];
    let mut found = Vec::new();
    for hive in [HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER] {
        let root = RegKey::predef(hive);
        for path in UNINSTALL {
            let Ok(key) = root.open_subkey_with_flags(path, KEY_READ) else {
                continue;
            };
            for name in key.enum_keys().flatten() {
                let Ok(app) = key.open_subkey_with_flags(&name, KEY_READ) else {
                    continue;
                };
                let display: Result<String, _> = app.get_value("DisplayName");
                if display.is_ok_and(|d| d == "Vortex") {
                    if let Ok(location) = app.get_value::<String, _>("InstallLocation") {
                        let location = location.trim().trim_matches('"');
                        if !location.is_empty() {
                            found.push(PathBuf::from(location));
                        }
                    }
                }
            }
        }
    }
    found
}

#[cfg(not(windows))]
fn registry_locations() -> Vec<PathBuf> {
    Vec::new()
}

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Deployment {
    pub sources: Vec<String>,
    pub staging: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Deploy {
    Missing,
    Unavailable(String),
    Read(Deployment),
}

pub fn parse_deployment(bytes: &[u8]) -> Result<Deployment, String> {
    let body = bytes.strip_prefix(UTF8_BOM).unwrap_or(bytes);
    let value: Value = serde_json::from_slice(body)
        .map_err(|_| "the deployment file is not valid JSON".to_owned())?;
    let object = value
        .as_object()
        .ok_or_else(|| "the deployment file is not a JSON object".to_owned())?;
    let mut sources: Vec<String> = Vec::new();
    let mut seen: HashSet<String> = HashSet::new();
    match object.get("files") {
        None | Some(Value::Null) => {}
        Some(Value::Array(files)) => {
            for item in files {
                let Some(source) = item.get("source").and_then(Value::as_str) else {
                    continue;
                };
                let source = source.trim();
                if source.is_empty()
                    || source.contains(['/', '\\', '\0'])
                    || source == "."
                    || source == ".."
                {
                    continue;
                }
                if seen.insert(source.to_ascii_lowercase()) {
                    sources.push(source.to_owned());
                }
            }
        }
        Some(_) => return Err("the deployment file's 'files' is not a list".to_owned()),
    }
    let staging = object
        .get("stagingPath")
        .and_then(Value::as_str)
        .map(str::trim)
        .filter(|s| !s.is_empty())
        .map(str::to_owned);
    Ok(Deployment { sources, staging })
}

fn manifests(game: &Path) -> Vec<PathBuf> {
    let mut found = Vec::new();
    let mut dirs = vec![game.to_path_buf()];
    if let Some(data) = find_ci(game, "Data").filter(|p| p.is_dir()) {
        dirs.push(data);
    }
    for dir in dirs {
        let Ok(entries) = fs::read_dir(&dir) else {
            continue;
        };
        for entry in entries.flatten() {
            let name = entry.file_name().to_string_lossy().to_ascii_lowercase();
            if name.starts_with("vortex.deployment")
                && name.ends_with(".json")
                && entry.path().is_file()
            {
                found.push(entry.path());
            }
        }
    }
    found.sort();
    found
}

fn read_capped(path: &Path) -> Result<Vec<u8>, String> {
    let file = File::open(path).map_err(|_| "a deployment file could not be opened".to_owned())?;
    let mut bytes = Vec::new();
    file.take(MAX_DEPLOY + 1)
        .read_to_end(&mut bytes)
        .map_err(|_| "a deployment file could not be read".to_owned())?;
    if bytes.len() as u64 > MAX_DEPLOY {
        return Err("the deployment file is too large".to_owned());
    }
    Ok(bytes)
}

pub fn read_deployment(game: &Path) -> Deploy {
    let files = manifests(game);
    if files.is_empty() {
        return Deploy::Missing;
    }
    let mut merged = Deployment::default();
    let mut seen: HashSet<String> = HashSet::new();
    for file in &files {
        let part = read_capped(file).and_then(|bytes| parse_deployment(&bytes));
        match part {
            Ok(part) => {
                for source in part.sources {
                    if seen.insert(source.to_ascii_lowercase()) {
                        merged.sources.push(source);
                    }
                }
                if merged.staging.is_none() {
                    merged.staging = part.staging;
                }
            }
            Err(reason) => return Deploy::Unavailable(reason),
        }
    }
    Deploy::Read(merged)
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Mod {
    pub id: String,
    pub name: String,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Listing {
    pub manages_game: bool,
    pub staging: Option<PathBuf>,
    pub mods: Vec<Mod>,
    pub problem: Option<String>,
}

pub fn display_name(id: &str) -> String {
    let mut name = id;
    while let Some((head, tail)) = name.rsplit_once('-') {
        if head.is_empty() || tail.is_empty() || !tail.bytes().all(|b| b.is_ascii_digit()) {
            break;
        }
        name = head;
    }
    let trimmed = name.trim();
    if trimmed.is_empty() {
        id.to_owned()
    } else {
        trimmed.to_owned()
    }
}

pub fn listing(game: Option<&Path>, configured: Option<&Path>, fallback: Option<&Path>) -> Listing {
    let deploy = game.map(read_deployment).unwrap_or(Deploy::Missing);
    let (manages_game, deployed, manifest_staging, problem) = match deploy {
        Deploy::Missing => (false, Vec::new(), None, None),
        Deploy::Unavailable(reason) => (true, Vec::new(), None, Some(reason)),
        Deploy::Read(found) => (true, found.sources, found.staging, None),
    };
    let staging = manifest_staging
        .map(PathBuf::from)
        .filter(|p| p.is_dir())
        .or_else(|| configured.filter(|p| p.is_dir()).map(Path::to_path_buf))
        .or_else(|| fallback.filter(|p| p.is_dir()).map(Path::to_path_buf));
    let deployed_lower: HashSet<String> = deployed.iter().map(|s| s.to_ascii_lowercase()).collect();
    let mut mods: Vec<Mod> = Vec::new();
    let mut seen: HashSet<String> = HashSet::new();
    if let Some(dir) = &staging {
        for entry in fs::read_dir(dir).into_iter().flatten().flatten() {
            if !entry.file_type().is_ok_and(|t| t.is_dir()) {
                continue;
            }
            let id = entry.file_name().to_string_lossy().into_owned();
            if id.starts_with('.') || id.starts_with("__") {
                continue;
            }
            let key = id.to_ascii_lowercase();
            seen.insert(key.clone());
            mods.push(Mod {
                name: display_name(&id),
                enabled: deployed_lower.contains(&key),
                id,
            });
        }
    }
    for source in &deployed {
        if !seen.contains(&source.to_ascii_lowercase()) {
            mods.push(Mod {
                id: source.clone(),
                name: display_name(source),
                enabled: true,
            });
        }
    }
    mods.sort_by_key(|m| m.name.to_lowercase());
    Listing {
        manages_game,
        staging,
        mods,
        problem,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    #[test]
    fn a_good_deployment_lists_its_sources_once_in_order() {
        let text = r#"{"version":1,"instance":"x","stagingPath":"D:\\staging",
            "files":[{"relPath":"a.esp","source":"Cool Mod-1-1-1700000000","target":"a.esp"},
                     {"relPath":"b.esp","source":"Other Mod-2-0-1700000001"},
                     {"relPath":"c.esp","source":"cool mod-1-1-1700000000"}]}"#;
        let parsed = parse_deployment(text.as_bytes()).unwrap();
        assert_eq!(
            parsed.sources,
            vec!["Cool Mod-1-1-1700000000", "Other Mod-2-0-1700000001"]
        );
        assert_eq!(parsed.staging.as_deref(), Some("D:\\staging"));
    }

    #[test]
    fn a_bom_is_tolerated_and_missing_fields_are_optional() {
        let mut bytes = UTF8_BOM.to_vec();
        bytes.extend_from_slice(b"{}");
        let parsed = parse_deployment(&bytes).unwrap();
        assert!(parsed.sources.is_empty() && parsed.staging.is_none());
    }

    #[test]
    fn malformed_and_wrong_shaped_files_are_unavailable() {
        assert!(parse_deployment(b"{not json").is_err());
        assert!(parse_deployment(b"[1,2]").is_err());
        assert!(parse_deployment(br#"{"files":"nope"}"#).is_err());
        let hostile = parse_deployment(
            br#"{"files":[{"source":"../evil"},{"source":"a/b"},{"source":""},{"nosource":1},"x"]}"#,
        )
        .unwrap();
        assert!(hostile.sources.is_empty());
    }

    #[test]
    fn missing_and_oversized_deployments_are_reported_as_such() {
        let game = tempfile::tempdir().unwrap();
        assert_eq!(read_deployment(game.path()), Deploy::Missing);
        let big = vec![b' '; (MAX_DEPLOY + 1) as usize];
        write(&game.path().join("Data").join("vortex.deployment.json"), "");
        fs::write(
            game.path().join("Data").join("vortex.deployment.json"),
            &big,
        )
        .unwrap();
        assert!(matches!(
            read_deployment(game.path()),
            Deploy::Unavailable(reason) if reason.contains("too large")
        ));
    }

    #[test]
    fn manifests_in_the_game_root_and_data_are_merged() {
        let game = tempfile::tempdir().unwrap();
        write(
            &game.path().join("Data").join("vortex.deployment.json"),
            r#"{"files":[{"source":"One"}]}"#,
        );
        write(
            &game.path().join("vortex.deployment.textures.json"),
            r#"{"files":[{"source":"Two"},{"source":"one"}]}"#,
        );
        match read_deployment(game.path()) {
            Deploy::Read(found) => assert_eq!(found.sources, vec!["One", "Two"]),
            other => panic!("unexpected {other:?}"),
        }
        write(&game.path().join("vortex.deployment.json"), "{broken");
        assert!(matches!(
            read_deployment(game.path()),
            Deploy::Unavailable(_)
        ));
    }

    #[test]
    fn detection_uses_the_install_roots() {
        let root = tempfile::tempdir().unwrap();
        let roots = Roots {
            launcher_exe: None,
            local_app_data: Some(root.path().join("local")),
            app_data: Some(root.path().join("roaming")),
            program_files: vec![root.path().join("pf")],
        };
        assert!(detect_with(&roots, Vec::new()).is_none());
        write(
            &root
                .path()
                .join("pf")
                .join("Black Tree Gaming Ltd")
                .join("Vortex")
                .join("Vortex.exe"),
            "",
        );
        let found = detect_with(&roots, Vec::new()).unwrap();
        assert!(found.exe.is_some());
        fs::create_dir_all(root.path().join("roaming").join("Vortex")).unwrap();
        assert!(detect_with(&roots, Vec::new()).unwrap().app_data.is_some());
        assert_eq!(
            default_staging(&roots),
            Some(
                root.path()
                    .join("roaming")
                    .join("Vortex")
                    .join("fallout4")
                    .join("mods")
            )
        );
        let local = Roots {
            launcher_exe: None,
            local_app_data: Some(root.path().join("local")),
            app_data: None,
            program_files: Vec::new(),
        };
        write(
            &root
                .path()
                .join("local")
                .join("Programs")
                .join("Vortex")
                .join("Vortex.exe"),
            "",
        );
        assert!(detect_with(&local, Vec::new()).unwrap().exe.is_some());
        let from_registry = Roots::default();
        let registered = root.path().join("registered");
        write(&registered.join("Vortex.exe"), "");
        assert_eq!(
            detect_with(&from_registry, vec![registered.clone()])
                .unwrap()
                .exe,
            Some(registered.join("Vortex.exe"))
        );
    }

    #[test]
    fn a_deployed_release_is_found_by_its_name_prefix() {
        let game = tempfile::tempdir().unwrap();
        assert!(!deploys_mod(game.path(), "Commonwealth Online"));
        write(
            &game.path().join("Data").join("vortex.deployment.json"),
            r#"{"files":[{"source":"Commonwealth Online 2.0 OG (Vortex)-1-1"},{"source":"Other Mod-2-1"}]}"#,
        );
        assert!(deploys_mod(game.path(), "Commonwealth Online"));
        assert!(deploys_mod(game.path(), "commonwealth online"));
        assert!(!deploys_mod(game.path(), "Nothing Here"));
    }

    #[test]
    fn staging_names_drop_the_download_suffix_for_display_only() {
        assert_eq!(display_name("Cool Mod-1234-1-0-1700000000"), "Cool Mod");
        assert_eq!(display_name("Plain Mod"), "Plain Mod");
        assert_eq!(display_name("-12"), "-12");
    }

    #[test]
    fn listing_marks_deployed_mods_on_and_staging_only_mods_off() {
        let root = tempfile::tempdir().unwrap();
        let game = root.path().join("game");
        let staging = root.path().join("staging");
        fs::create_dir_all(game.join("Data")).unwrap();
        fs::create_dir_all(staging.join("On Mod-9-1")).unwrap();
        fs::create_dir_all(staging.join("Off Mod-8-1")).unwrap();
        fs::create_dir_all(staging.join("__cache")).unwrap();
        fs::write(staging.join("loose.txt"), "").unwrap();
        write(
            &game.join("Data").join("vortex.deployment.json"),
            r#"{"files":[{"source":"On Mod-9-1"},{"source":"Gone Mod-7-1"}]}"#,
        );
        let found = listing(Some(&game), Some(&staging), None);
        assert!(found.manages_game && found.problem.is_none());
        let shown: Vec<(&str, bool)> = found
            .mods
            .iter()
            .map(|m| (m.name.as_str(), m.enabled))
            .collect();
        assert_eq!(
            shown,
            [("Gone Mod", true), ("Off Mod", false), ("On Mod", true)]
        );
        assert_eq!(found.staging.as_deref(), Some(staging.as_path()));
    }

    #[test]
    fn an_unreadable_deployment_is_listed_as_a_problem_not_a_crash() {
        let root = tempfile::tempdir().unwrap();
        write(&root.path().join("vortex.deployment.json"), "{nope");
        let found = listing(Some(root.path()), None, None);
        assert!(found.manages_game);
        assert!(found.problem.is_some());
        assert!(found.mods.is_empty());
        assert!(!listing(None, None, None).manages_game);
    }
}
