use std::collections::HashSet;
use std::fs;
use std::io::Read;
use std::path::{Path, PathBuf};

use crate::fsutil::{find_ci, resolve_ci};
use crate::qtini::{ini_get, ini_section};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProfileInfo {
    pub name: String,
    pub enabled_mods: Option<usize>,
    pub active: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ModEntry {
    pub name: String,
    pub enabled: bool,
    pub separator: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PluginEntry {
    pub name: String,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ProfileDetail {
    pub dir: PathBuf,
    pub files: Vec<String>,
    pub mods: Vec<ModEntry>,
    pub plugins: Vec<PluginEntry>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ManagerInstall {
    pub name: String,
    pub dir: PathBuf,
    pub exe: Option<PathBuf>,
    pub game_dir: Option<PathBuf>,
    pub manages_this_game: bool,
    pub ours: bool,
    pub profiles: Vec<ProfileInfo>,
    pub profiles_listed: bool,
}

#[derive(Debug, Clone, Default)]
pub struct Roots {
    pub launcher_exe: Option<PathBuf>,
    pub local_app_data: Option<PathBuf>,
    pub program_files: Vec<PathBuf>,
}

impl Roots {
    pub fn from_env() -> Roots {
        let var = |name: &str| std::env::var_os(name).map(PathBuf::from);
        Roots {
            launcher_exe: std::env::current_exe().ok(),
            local_app_data: var("LOCALAPPDATA"),
            program_files: ["ProgramFiles", "ProgramFiles(x86)"]
                .into_iter()
                .filter_map(var)
                .collect(),
        }
    }
}

pub fn detect(game_dir: Option<&Path>) -> Vec<ManagerInstall> {
    detect_in(&Roots::from_env(), game_dir)
}

const MAX_READ: usize = 16 << 20;
const MO2_ROOT: &str = "ModOrganizer";
const MO2_EXE: &str = "ModOrganizer.exe";
const MO2_INI: &str = "ModOrganizer.ini";
const NXM_INI: &str = "nxmhandler.ini";
const GAME_NAME: &str = "Fallout 4";
const GAME_ID: &str = "fallout4";
pub(crate) const MODLIST: &str = "modlist.txt";
const LOADORDER: &str = "loadorder.txt";
const PLUGINS: &str = "plugins.txt";
const CC_LIST: &str = "Fallout4.ccc";
const SEPARATOR: &str = "_separator";
const LAUNCHER_DEPTH: usize = 4;

pub fn detect_in(roots: &Roots, game_dir: Option<&Path>) -> Vec<ManagerInstall> {
    mo2_instances(roots, game_dir)
}

fn mo2_instances(roots: &Roots, game_dir: Option<&Path>) -> Vec<ManagerInstall> {
    let mut candidates: Vec<PathBuf> = roots
        .launcher_exe
        .iter()
        .flat_map(|exe| exe.ancestors().skip(1).take(LAUNCHER_DEPTH))
        .filter(|dir| find_ci(dir, MO2_INI).is_some())
        .map(Path::to_path_buf)
        .collect();
    if let Some(local) = roots.local_app_data.as_deref() {
        let mo_root = resolve_ci(local, &[MO2_ROOT]);
        candidates.extend(handler_dirs(&mo_root));
        candidates.extend(subdirs(&mo_root));
    }
    let mut seen = HashSet::new();
    let mut found: Vec<ManagerInstall> = candidates
        .into_iter()
        .filter(|dir| seen.insert(path_key(dir)))
        .filter_map(|dir| mo2_instance(roots, &dir, game_dir))
        .collect();
    found.sort_by_key(|m| m.name.to_lowercase());
    found
}

fn handler_dirs(mo_root: &Path) -> Vec<PathBuf> {
    let text = find_ci(mo_root, NXM_INI)
        .and_then(|p| read_capped(&p))
        .unwrap_or_default();
    let handlers = ini_section(&text, "handlers");
    handlers
        .iter()
        .filter_map(|(key, exe)| {
            let index = key.strip_suffix("\\executable")?;
            let games = ini_get(&handlers, &format!("{index}\\games"))?;
            if !games
                .split(',')
                .any(|g| g.trim().eq_ignore_ascii_case(GAME_ID))
            {
                return None;
            }
            Path::new(exe).parent().map(Path::to_path_buf)
        })
        .collect()
}

fn mo2_instance(roots: &Roots, dir: &Path, game_dir: Option<&Path>) -> Option<ManagerInstall> {
    let ini = read_capped(&find_ci(dir, MO2_INI)?)?;
    let general = ini_section(&ini, "General");
    if !ini_get(&general, "gamename")?.eq_ignore_ascii_case(GAME_NAME) {
        return None;
    }
    let game = ini_get(&general, "gamepath")
        .filter(|v| !v.is_empty())
        .map(PathBuf::from);
    let manages_this_game = match (game_dir, &game) {
        (Some(want), Some(have)) => path_key(want) == path_key(have),
        _ => false,
    };
    Some(ManagerInstall {
        name: dir.file_name()?.to_string_lossy().into_owned(),
        dir: dir.to_path_buf(),
        exe: find_ci(dir, MO2_EXE).or_else(|| mo2_program_exe(roots)),
        game_dir: game,
        manages_this_game,
        ours: holds_launcher(roots, dir) || has_co_mod(dir),
        profiles: mo2_profiles(dir, ini_get(&general, "selected_profile")),
        profiles_listed: true,
    })
}

fn holds_launcher(roots: &Roots, dir: &Path) -> bool {
    let want = path_key(dir);
    roots.launcher_exe.iter().any(|exe| {
        exe.ancestors()
            .skip(1)
            .take(LAUNCHER_DEPTH)
            .any(|a| path_key(a) == want)
    })
}

fn has_co_mod(dir: &Path) -> bool {
    find_ci(dir, "mods").is_some_and(|mods| {
        subdirs(&mods).iter().any(|m| {
            m.file_name()
                .is_some_and(|n| crate::versions::is_co_mod(&n.to_string_lossy()))
        })
    })
}

fn mo2_program_exe(roots: &Roots) -> Option<PathBuf> {
    roots
        .program_files
        .iter()
        .map(|pf| resolve_ci(pf, &["Mod Organizer 2", MO2_EXE]))
        .find(|p| p.is_file())
}

fn mo2_profiles(dir: &Path, selected: Option<&str>) -> Vec<ProfileInfo> {
    let Some(profiles) = find_ci(dir, "profiles") else {
        return Vec::new();
    };
    let mut out: Vec<ProfileInfo> = subdirs(&profiles)
        .into_iter()
        .filter_map(|path| {
            let name = path.file_name()?.to_string_lossy().into_owned();
            let modlist = find_ci(&path, MODLIST);
            let backup = name.to_ascii_lowercase().starts_with("_backup");
            let has_loadorder = find_ci(&path, LOADORDER).is_some();
            if modlist.is_none() && (backup || !has_loadorder) {
                return None;
            }
            Some(ProfileInfo {
                active: selected.is_some_and(|s| s.eq_ignore_ascii_case(&name)),
                enabled_mods: modlist
                    .and_then(|p| read_capped(&p))
                    .map(|text| enabled_mods(&text)),
                name,
            })
        })
        .collect();
    out.sort_by_key(|p| p.name.to_lowercase());
    out
}

pub fn profile_detail(
    instance: &Path,
    profile: &str,
    game_dir: Option<&Path>,
) -> Option<ProfileDetail> {
    let dir = resolve_ci(instance, &["profiles", profile]);
    if !dir.is_dir() {
        return None;
    }
    let read = |name: &str| find_ci(&dir, name).and_then(|p| read_capped(&p));
    let mut files: Vec<String> = fs::read_dir(&dir)
        .map(|entries| {
            entries
                .flatten()
                .filter(|e| e.path().is_file())
                .map(|e| e.file_name().to_string_lossy().into_owned())
                .collect()
        })
        .unwrap_or_default();
    files.sort_by_key(|f| f.to_lowercase());
    Some(ProfileDetail {
        files,
        mods: read(MODLIST).map(|t| mod_entries(&t)).unwrap_or_default(),
        plugins: plugin_entries(
            read(PLUGINS).as_deref().unwrap_or_default(),
            read(LOADORDER).as_deref(),
            &game_plugins(game_dir),
        ),
        dir,
    })
}

pub(crate) fn mod_entries(modlist: &str) -> Vec<ModEntry> {
    let mut mods: Vec<ModEntry> = modlist
        .lines()
        .filter_map(|line| {
            let enabled = match line.chars().next()? {
                '+' => true,
                '-' => false,
                _ => return None,
            };
            let name = &line[1..];
            let separator = name.to_ascii_lowercase().ends_with(SEPARATOR);
            let name = if separator {
                &name[..name.len() - SEPARATOR.len()]
            } else {
                name
            };
            Some(ModEntry {
                name: name.to_owned(),
                enabled,
                separator,
            })
        })
        .collect();
    mods.reverse();
    mods
}

fn game_plugins(game_dir: Option<&Path>) -> HashSet<String> {
    game_dir
        .and_then(|dir| find_ci(dir, CC_LIST))
        .and_then(|path| read_capped(&path))
        .map(|text| {
            text.lines()
                .map(|l| l.trim().trim_start_matches('\u{feff}').to_lowercase())
                .filter(|l| !l.is_empty())
                .collect()
        })
        .unwrap_or_default()
}

fn plugin_entries(
    plugins: &str,
    loadorder: Option<&str>,
    implicit: &HashSet<String>,
) -> Vec<PluginEntry> {
    let enabled: HashSet<String> = plugins
        .lines()
        .filter_map(|l| l.trim_start_matches('\u{feff}').strip_prefix('*'))
        .map(|n| n.trim().to_lowercase())
        .collect();
    let names = |text: &str| -> Vec<String> {
        text.lines()
            .map(|l| {
                l.trim()
                    .trim_start_matches('\u{feff}')
                    .trim_start_matches('*')
                    .trim()
            })
            .filter(|l| !l.is_empty() && !l.starts_with('#'))
            .map(str::to_owned)
            .collect()
    };
    let ordered = names(loadorder.unwrap_or(plugins));
    ordered
        .into_iter()
        .map(|name| PluginEntry {
            enabled: enabled.contains(&name.to_lowercase())
                || is_base_master(&name)
                || implicit.contains(&name.to_lowercase()),
            name,
        })
        .collect()
}

fn is_base_master(name: &str) -> bool {
    let name = name.to_ascii_lowercase();
    name == "fallout4.esm" || (name.starts_with("dlc") && name.ends_with(".esm"))
}

fn enabled_mods(modlist: &str) -> usize {
    modlist
        .lines()
        .filter_map(|line| line.strip_prefix('+'))
        .filter(|name| !name.to_ascii_lowercase().ends_with(SEPARATOR))
        .count()
}

pub(crate) fn read_bytes_capped(path: &Path) -> Option<Vec<u8>> {
    let mut bytes = Vec::new();
    fs::File::open(path)
        .ok()?
        .take((MAX_READ + 1) as u64)
        .read_to_end(&mut bytes)
        .ok()?;
    (bytes.len() <= MAX_READ).then_some(bytes)
}

fn read_capped(path: &Path) -> Option<String> {
    read_bytes_capped(path).map(|bytes| String::from_utf8_lossy(&bytes).into_owned())
}

pub(crate) fn write_replacing(path: &Path, text: &str) -> bool {
    let tmp = path.with_extension("txt.tmp");
    let done = fs::write(&tmp, text).is_ok() && fs::rename(&tmp, path).is_ok();
    if !done {
        let _ = fs::remove_file(&tmp);
    }
    done
}

fn subdirs(dir: &Path) -> Vec<PathBuf> {
    fs::read_dir(dir)
        .map(|entries| {
            entries
                .flatten()
                .map(|e| e.path())
                .filter(|p| p.is_dir())
                .collect()
        })
        .unwrap_or_default()
}

fn path_key(path: &Path) -> String {
    path.to_string_lossy()
        .replace('/', "\\")
        .trim_end_matches('\\')
        .to_lowercase()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn write(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn qt(path: &Path) -> String {
        path.to_string_lossy().replace('\\', "\\\\")
    }

    fn roots(root: &Path) -> Roots {
        Roots {
            launcher_exe: None,
            local_app_data: Some(root.join("local")),
            program_files: vec![root.join("pf")],
        }
    }

    fn make_instance(dir: &Path, game_name: &str) {
        write(
            &dir.join("ModOrganizer.ini"),
            &format!(
                "[General]\ngameName={game_name}\nselected_profile=@ByteArray(Default)\ngamePath=@ByteArray({})\nversion=2.5.2\n",
                qt(&dir.join("Stock Folder"))
            ),
        );
    }

    fn handlers(local: &Path, entries: &[(&str, &Path)]) {
        let mut text = String::from("[handlers]\nsize=4\n");
        for (i, (games, exe)) in entries.iter().enumerate() {
            text.push_str(&format!(
                "{}\\games={games}\n{}\\executable={}\n",
                i + 1,
                i + 1,
                qt(exe)
            ));
        }
        write(&local.join("ModOrganizer").join("nxmhandler.ini"), &text);
    }

    #[test]
    fn portable_mo2_found_through_nxmhandler() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let inst = root.path().join("games").join("NewModList");
        let exe = inst.join("ModOrganizer.exe");
        make_instance(&inst, "Fallout 4");
        write(&exe, "");
        let profiles = inst.join("profiles");
        write(&profiles.join("Default").join("modlist.txt"), "# This file was automatically generated by Mod Organizer.\n+Main\n-Off\n+Foo_separator\n*Official\n+Extra\n");
        write(
            &profiles.join("Alpha").join("modlist.txt"),
            "# header\n+X\n+Y_separator\n",
        );
        write(&profiles.join("notes.txt"), "+Z\n");
        write(&profiles.join("empty").join("readme.txt"), "");
        handlers(&root.path().join("local"), &[("fallout4", &exe)]);

        let found = detect_in(&r, Some(&inst.join("Stock Folder")));
        assert_eq!(found.len(), 1);
        let mo2 = &found[0];
        assert_eq!(mo2.name, "NewModList");
        assert_eq!(mo2.dir, inst);
        assert_eq!(mo2.exe, Some(exe));
        assert_eq!(mo2.game_dir, Some(inst.join("Stock Folder")));
        assert!(mo2.manages_this_game);
        assert!(mo2.profiles_listed);
        assert_eq!(
            mo2.profiles,
            vec![
                ProfileInfo {
                    name: "Alpha".into(),
                    enabled_mods: Some(1),
                    active: false
                },
                ProfileInfo {
                    name: "Default".into(),
                    enabled_mods: Some(2),
                    active: true
                },
            ]
        );

        let upper = PathBuf::from(format!(
            "{}/",
            inst.join("Stock Folder").to_string_lossy().to_uppercase()
        ));
        assert!(detect_in(&r, Some(&upper))[0].manages_this_game);
        assert!(!detect_in(&r, Some(root.path()))[0].manages_this_game);
    }

    #[test]
    fn other_games_and_missing_folders_are_ignored() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let vr = root.path().join("vr");
        let sse = root.path().join("sse");
        let sky = root.path().join("skyrim");
        make_instance(&vr, "Fallout 4 VR");
        make_instance(&sse, "Skyrim Special Edition");
        make_instance(&sky, "Fallout 4");
        handlers(
            &root.path().join("local"),
            &[
                ("skyrim", &sky.join("ModOrganizer.exe")),
                (
                    "fallout4",
                    &root.path().join("missing").join("ModOrganizer.exe"),
                ),
                ("fallout4", &vr.join("ModOrganizer.exe")),
                ("fallout4", &sse.join("ModOrganizer.exe")),
            ],
        );
        assert!(detect_in(&r, None).is_empty());
    }

    #[test]
    fn global_instance_found_and_cache_ignored() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let mo_root = root.path().join("local").join("ModOrganizer");
        let global = mo_root.join("MyGlobal");
        make_instance(&global, "Fallout 4");
        write(&mo_root.join("cache").join("data.bin"), "x");
        handlers(
            &root.path().join("local"),
            &[("fallout4", &global.join("ModOrganizer.exe"))],
        );
        let mo2_exe = root
            .path()
            .join("pf")
            .join("Mod Organizer 2")
            .join("ModOrganizer.exe");
        write(&mo2_exe, "");

        let found = detect_in(&r, None);
        assert_eq!(found.len(), 1);
        assert_eq!(found[0].name, "MyGlobal");
        assert_eq!(found[0].dir, global);
        assert_eq!(found[0].exe, Some(mo2_exe));
        assert!(found[0].profiles_listed);
    }

    #[test]
    fn quoted_multi_game_handler_is_found() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let inst = root.path().join("games").join("Multi");
        let exe = inst.join("ModOrganizer.exe");
        make_instance(&inst, "Fallout 4");
        write(&exe, "");
        handlers(
            &root.path().join("local"),
            &[("\"fallout4, skyrimse\"", &exe)],
        );
        let found = detect_in(&r, None);
        assert_eq!(found.len(), 1);
        assert_eq!(found[0].dir, inst);
    }

    #[test]
    fn nothing_installed_gives_empty() {
        let root = tempfile::tempdir().unwrap();
        assert!(detect_in(&roots(root.path()), None).is_empty());
        assert!(detect_in(&Roots::default(), None).is_empty());
    }

    #[test]
    fn ini_decoding_and_missing_modlist() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let game = root.path().join("Game Dir");
        let mods = root.path().join("local").join("ModOrganizer").join("Mods");
        write(
            &mods.join("profiles").join("NoList").join("loadorder.txt"),
            "",
        );
        write(
            &mods.join("profiles").join("Huge").join("modlist.txt"),
            &"+".repeat((MAX_READ) + 1),
        );
        let plain = root.path().join("local").join("ModOrganizer").join("plain");
        write(
            &plain.join("ModOrganizer.ini"),
            &format!("[General]\nGameName=Fallout 4\nGamePath={}\n", qt(&game)),
        );
        write(
            &mods.join("ModOrganizer.ini"),
            &format!(
                "[General]\ngameName=Fallout 4\ngamePath=@ByteArray({})\n",
                qt(&game)
            ),
        );

        let found = detect_in(&r, None);
        assert_eq!(found.len(), 2);
        assert_eq!(found[0].name, "Mods");
        assert_eq!(found[0].game_dir, Some(game.clone()));
        assert_eq!(
            found[0].profiles,
            vec![
                ProfileInfo {
                    name: "Huge".into(),
                    enabled_mods: None,
                    active: false
                },
                ProfileInfo {
                    name: "NoList".into(),
                    enabled_mods: None,
                    active: false
                },
            ]
        );
        assert_eq!(found[1].name, "plain");
        assert_eq!(found[1].game_dir, Some(game));
    }

    #[test]
    fn a_non_ascii_game_path_written_by_qt_is_decoded() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let inst = root
            .path()
            .join("local")
            .join("ModOrganizer")
            .join("Spieleliste");
        write(
            &inst.join("ModOrganizer.ini"),
            "[General]\ngameName=Fallout 4\ngamePath=@ByteArray(C:\\\\Spiele\\\\Fallout\\x00204)\n",
        );
        write(&inst.join("ModOrganizer.exe"), "");
        let game = PathBuf::from("C:\\Spiele\\Fallout 4");
        let found = detect_in(&r, Some(&game));
        assert_eq!(found.len(), 1);
        assert_eq!(found[0].game_dir, Some(game));
        assert!(found[0].manages_this_game);
    }

    #[test]
    fn launcher_instance_is_found_unregistered_and_marked_ours() {
        let root = tempfile::tempdir().unwrap();
        let mut r = roots(root.path());
        let inst = root.path().join("CO Modlist");
        let other = root.path().join("Other");
        make_instance(&inst, "Fallout 4");
        make_instance(&other, "Fallout 4");
        let exe = inst.join("Launcher").join("CommonwealthOnlineLauncher.exe");
        write(&exe, "");
        r.launcher_exe = Some(exe);
        handlers(
            &root.path().join("local"),
            &[
                ("fallout4", &other.join("ModOrganizer.exe")),
                ("fallout4", &inst.join("ModOrganizer.exe")),
            ],
        );

        let found = detect_in(&r, None);
        let ours: Vec<&str> = found
            .iter()
            .filter(|m| m.ours)
            .map(|m| m.name.as_str())
            .collect();
        assert_eq!(ours, ["CO Modlist"]);
        assert_eq!(found.len(), 2);
    }

    #[test]
    fn instance_with_a_commonwealth_online_mod_is_ours() {
        let root = tempfile::tempdir().unwrap();
        let r = roots(root.path());
        let inst = root.path().join("List");
        make_instance(&inst, "Fallout 4");
        write(
            &inst
                .join("mods")
                .join("Commonwealth Online")
                .join("meta.ini"),
            "",
        );
        write(&inst.join("ModOrganizer.exe"), "");
        handlers(
            &root.path().join("local"),
            &[("fallout4", &inst.join("ModOrganizer.exe"))],
        );
        assert!(detect_in(&r, None)[0].ours);
    }

    #[test]
    fn profile_detail_reads_mods_plugins_and_files() {
        let root = tempfile::tempdir().unwrap();
        let profile = root.path().join("profiles").join("Default");
        write(
            &profile.join("modlist.txt"),
            "# header
+Top
-Off
+Group_separator
*Unmanaged
+Base
",
        );
        write(
            &profile.join("plugins.txt"),
            "# x
*A.esp
B.esp
*c.esl
",
        );
        write(
            &profile.join("loadorder.txt"),
            "\u{feff}Fallout4.esm
A.esp
c.esl
B.esp
D.esl
",
        );
        write(&profile.join("fallout4.ini"), "");
        let game = tempfile::tempdir().unwrap();
        write(&game.path().join("fallout4.ccc"), "\u{feff}d.esl\n");
        let detail = profile_detail(root.path(), "default", Some(game.path())).unwrap();
        let mods: Vec<(&str, bool, bool)> = detail
            .mods
            .iter()
            .map(|m| (m.name.as_str(), m.enabled, m.separator))
            .collect();
        assert_eq!(
            mods,
            [
                ("Base", true, false),
                ("Group", true, true),
                ("Off", false, false),
                ("Top", true, false)
            ]
        );
        let plugins: Vec<(&str, bool)> = detail
            .plugins
            .iter()
            .map(|p| (p.name.as_str(), p.enabled))
            .collect();
        assert_eq!(
            plugins,
            [
                ("Fallout4.esm", true),
                ("A.esp", true),
                ("c.esl", true),
                ("B.esp", false),
                ("D.esl", true)
            ]
        );
        assert_eq!(
            detail.files,
            [
                "fallout4.ini",
                "loadorder.txt",
                "modlist.txt",
                "plugins.txt"
            ]
        );
        assert!(profile_detail(root.path(), "Missing", None).is_none());
    }
}
