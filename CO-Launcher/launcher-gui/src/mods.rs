use std::path::{Path, PathBuf};

use launcher_core::discover::{find_game, Env};
use launcher_core::fsutil::find_ci;
use launcher_core::modmgr::{self, ManagerInstall};
use launcher_core::runtime::{pe_file_version, Runtime};
use launcher_core::versions::{self, is_co_build, is_f4se_mod, VersionPick};

use crate::protocol::{DetailView, Event, ManagerView, ModView, PluginView, ProfileView};

pub const MODS_WARNING: &str = "Modding besides our selection is not yet supported. Commonwealth Online is tested only with the files the launcher installs; other mods, load orders and profiles may break it.";

pub fn list(pinned: Option<&Path>) -> Event {
    let game = find_game(pinned, &Env::detect()).ok();
    let pick = sync_versions(pinned);
    event(&modmgr::detect(game.as_deref()), pick.as_ref())
}

pub fn system_tool(exe: &str) -> std::process::Command {
    let root = std::env::var_os("SystemRoot").unwrap_or_else(|| "C:\\Windows".into());
    #[allow(unused_mut)]
    let mut tool = std::process::Command::new(Path::new(&root).join("System32").join(exe));
    #[cfg(windows)]
    std::os::windows::process::CommandExt::creation_flags(&mut tool, 0x0800_0000);
    tool
}

fn listing(image: &str, titles: bool) -> Option<String> {
    let mut tasklist = system_tool("tasklist.exe");
    tasklist.args(["/FO", "CSV", "/NH", "/FI", &format!("IMAGENAME eq {image}")]);
    if titles {
        tasklist.arg("/V");
    }
    let out = tasklist.output().ok().filter(|out| out.status.success())?;
    Some(String::from_utf8_lossy(&out.stdout).to_lowercase())
}

pub fn process_running(image: &str) -> bool {
    listing(image, false).is_some_and(|text| text.contains(&image.to_lowercase()))
}

pub fn mo2_running() -> bool {
    listing("ModOrganizer.exe", false).is_none_or(|text| text.contains("modorganizer.exe"))
}

pub fn mo2_window_open() -> bool {
    listing("ModOrganizer.exe", true).is_some_and(|text| text.contains("mod organizer"))
}

pub fn sync_versions(pinned: Option<&Path>) -> Option<VersionPick> {
    let game = find_game(pinned, &Env::detect()).ok()?;
    let exe = find_ci(&game, "Fallout4.exe")?;
    let tag = Runtime::from_version(&pe_file_version(&exe).ok()?)?.label();
    let installs = modmgr::detect(Some(&game));
    let ours = &installs[chosen(&installs)?];
    let profile = ours.profiles.iter().find(|p| p.active)?;
    Some(versions::select_versions(
        &ours.dir,
        &profile.name,
        tag,
        !mo2_running(),
    ))
}

fn active_profile(install: &ManagerInstall) -> Option<&modmgr::ProfileInfo> {
    install
        .profiles
        .iter()
        .find(|p| p.active)
        .or(install.profiles.first())
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Pin {
    pub game: PathBuf,
    pub exe: Option<PathBuf>,
    pub instance: PathBuf,
    pub profile: Option<String>,
}

fn chosen(installs: &[ManagerInstall]) -> Option<usize> {
    installs.iter().position(|m| {
        m.ours
            && m.game_dir
                .as_deref()
                .is_some_and(|g| find_ci(g, "Fallout4.exe").is_some())
    })
}

pub fn pin() -> Option<Pin> {
    let installs = modmgr::detect(None);
    let m = &installs[chosen(&installs)?];
    Some(Pin {
        game: m.game_dir.clone()?,
        exe: m.exe.clone(),
        instance: m.dir.clone(),
        profile: active_profile(m).map(|p| p.name.clone()),
    })
}

pub fn event(installs: &[ManagerInstall], pick: Option<&VersionPick>) -> Event {
    let chosen_idx = chosen(installs);
    Event::Mods {
        managers: installs
            .iter()
            .enumerate()
            .filter(|(i, _)| chosen_idx.is_none_or(|p| p == *i))
            .map(|(i, m)| view(m, Some(i) == chosen_idx, pick))
            .collect(),
        warning: MODS_WARNING,
    }
}

fn view(install: &ManagerInstall, ours: bool, pick: Option<&VersionPick>) -> ManagerView {
    ManagerView {
        name: install.name.clone(),
        dir: text(&install.dir),
        exe: install.exe.as_deref().map(text),
        manages_this_game: install.manages_this_game,
        ours,
        profiles: install
            .profiles
            .iter()
            .map(|p| ProfileView {
                name: p.name.clone(),
                enabled_mods: p.enabled_mods,
                active: p.active,
            })
            .collect(),
        profiles_listed: install.profiles_listed,
        detail: ours.then(|| detail(install, pick)).flatten(),
    }
}

fn detail(install: &ManagerInstall, pick: Option<&VersionPick>) -> Option<DetailView> {
    let profile = active_profile(install)?;
    let f4se = pick.and_then(|p| p.f4se.as_deref());
    let co = pick.and_then(|p| p.co.as_deref());
    let found = modmgr::profile_detail(&install.dir, &profile.name, install.game_dir.as_deref())?;
    Some(DetailView {
        profile: profile.name.clone(),
        dir: text(&found.dir),
        f4se_note: pick.map(|p| p.note.clone()).unwrap_or_default(),
        files: found.files,
        mods: found
            .mods
            .into_iter()
            .filter(|m| m.separator || listed(&m.name, f4se, co))
            .map(|m| ModView {
                name: m.name,
                enabled: m.enabled,
                separator: m.separator,
            })
            .collect(),
        plugins: found
            .plugins
            .into_iter()
            .map(|p| PluginView {
                name: p.name,
                enabled: p.enabled,
            })
            .collect(),
    })
}

fn listed(name: &str, f4se: Option<&str>, co: Option<&str>) -> bool {
    let chosen = if is_f4se_mod(name) {
        f4se
    } else if is_co_build(name) {
        co
    } else {
        return true;
    };
    chosen.is_none_or(|c| c == name)
}

fn text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

#[cfg(test)]
mod tests {
    use super::*;
    use launcher_core::modmgr::ProfileInfo;
    use serde_json::json;

    #[test]
    fn mods_event_matches_contract() {
        let game = tempfile::tempdir().unwrap();
        std::fs::write(game.path().join("Fallout4.exe"), b"").unwrap();
        let installs = [ManagerInstall {
            name: "Fallout 4".to_owned(),
            dir: PathBuf::from("D:/MO2"),
            exe: Some(PathBuf::from("D:/MO2/ModOrganizer.exe")),
            game_dir: Some(game.path().to_path_buf()),
            manages_this_game: true,
            ours: true,
            profiles: vec![ProfileInfo {
                name: "Default".to_owned(),
                enabled_mods: Some(3),
                active: true,
            }],
            profiles_listed: true,
        }];
        assert_eq!(
            serde_json::to_value(event(&installs, None)).unwrap(),
            json!({
                "type": "mods",
                "managers": [{
                    "name": "Fallout 4",
                    "dir": "D:/MO2",
                    "exe": "D:/MO2/ModOrganizer.exe",
                    "managesThisGame": true,
                    "ours": true,
                    "profiles": [{"name":"Default","enabledMods":3,"active":true}],
                    "profilesListed": true,
                    "detail": null
                }],
                "warning": MODS_WARNING
            })
        );
    }

    fn install(name: &str, dir: &Path, game: Option<&Path>, ours: bool) -> ManagerInstall {
        ManagerInstall {
            name: name.to_owned(),
            dir: dir.to_path_buf(),
            exe: None,
            game_dir: game.map(Path::to_path_buf),
            manages_this_game: false,
            ours,
            profiles: vec![ProfileInfo {
                name: "Default".to_owned(),
                enabled_mods: Some(1),
                active: true,
            }],
            profiles_listed: true,
        }
    }

    #[test]
    fn our_instance_lists_alone_with_its_profile_detail() {
        let dir = tempfile::tempdir().unwrap();
        let profile = dir.path().join("profiles").join("Default");
        std::fs::create_dir_all(&profile).unwrap();
        std::fs::write(
            profile.join("modlist.txt"),
            "+Mod A
",
        )
        .unwrap();
        std::fs::write(
            profile.join("plugins.txt"),
            "*A.esp
",
        )
        .unwrap();
        let other = tempfile::tempdir().unwrap();
        let game = tempfile::tempdir().unwrap();
        std::fs::write(game.path().join("Fallout4.exe"), b"").unwrap();
        let installs = [
            install("Other", other.path(), None, false),
            install("CO Modlist", dir.path(), Some(game.path()), true),
        ];
        let Event::Mods { managers, .. } = event(&installs, None) else {
            panic!("not a mods event");
        };
        assert_eq!(managers.len(), 1);
        let detail = managers[0].detail.as_ref().unwrap();
        assert_eq!(detail.profile, "Default");
        assert_eq!(detail.mods.len(), 1);
        assert_eq!(detail.plugins[0].name, "A.esp");
        let Event::Mods { managers, .. } = event(&installs[..1], None) else {
            panic!("not a mods event");
        };
        assert!(managers[0].detail.is_none());
    }

    #[test]
    fn our_instance_with_a_missing_game_folder_is_not_chosen() {
        let dir = tempfile::tempdir().unwrap();
        let gone = dir.path().join("gone");
        let installs = [install("CO Modlist", dir.path(), Some(&gone), true)];
        assert_eq!(chosen(&installs), None);
        let Event::Mods { managers, .. } = event(&installs, None) else {
            panic!("not a mods event");
        };
        assert!(!managers[0].ours && managers[0].detail.is_none());
    }

    #[test]
    fn only_the_selected_f4se_is_listed_with_its_note() {
        let dir = tempfile::tempdir().unwrap();
        let profile = dir.path().join("profiles").join("Default");
        std::fs::create_dir_all(&profile).unwrap();
        std::fs::write(
            profile.join("modlist.txt"),
            "+F4SE AE 0.7.9\n-F4SE OG 0.6.23\n+PrismaUI\n+Commonwealth Online AE\n-Commonwealth Online OG\n+Commonwealth Online Maps\n",
        )
        .unwrap();
        let game = tempfile::tempdir().unwrap();
        std::fs::write(game.path().join("Fallout4.exe"), b"").unwrap();
        let installs = [install("CO Modlist", dir.path(), Some(game.path()), true)];
        let pick = VersionPick {
            f4se: Some("F4SE AE 0.7.9".to_owned()),
            co: Some("Commonwealth Online AE".to_owned()),
            changed: false,
            pending: false,
            note: "Using F4SE AE 0.7.9 for your game.".to_owned(),
        };
        let Event::Mods { managers, .. } = event(&installs, Some(&pick)) else {
            panic!("not a mods event");
        };
        let detail = managers[0].detail.as_ref().unwrap();
        let names: Vec<&str> = detail.mods.iter().map(|m| m.name.as_str()).collect();
        assert_eq!(
            names,
            [
                "Commonwealth Online Maps",
                "Commonwealth Online AE",
                "PrismaUI",
                "F4SE AE 0.7.9"
            ]
        );
        assert_eq!(detail.f4se_note, pick.note);
    }
}
