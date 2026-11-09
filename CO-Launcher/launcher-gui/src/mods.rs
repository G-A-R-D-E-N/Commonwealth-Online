use std::path::{Path, PathBuf};

use launcher_core::discover::{find_game, Env};
use launcher_core::fsutil::find_ci;
use launcher_core::modmgr::{self, ManagerInstall, Roots};
use launcher_core::runtime::{pe_file_version, Runtime};
use launcher_core::versions::{self, is_co_build, is_f4se_mod, VersionPick};

use launcher_core::vortex;

use crate::locations::{self, Locations};
use crate::protocol::{DetailView, Event, ManagerView, ModView, PluginView, ProfileView};

pub const MODS_WARNING: &str = "Playing with mods is highly experimental and results may vary.";

pub fn list(pin: Option<&Pin>) -> Event {
    let pick = sync_versions(pin);
    let installs = match pin {
        Some(pin) => modmgr::selected_instance(&pin.instance, Some(&pin.game))
            .map(|mut instance| {
                instance.ours = true;
                vec![instance]
            })
            .unwrap_or_default(),
        None => modmgr::detect(None),
    };
    event_with(
        &installs,
        pick.as_ref(),
        vortex_view(pin, &locations::configured()),
    )
}

pub fn resolve_game(configured: &str, pin: Option<&Pin>) -> Option<PathBuf> {
    let configured = configured.trim();
    if !configured.is_empty() {
        return find_game(Some(Path::new(configured)), &Env::detect()).ok();
    }
    if let Some(pin) = pin {
        return Some(pin.game.clone());
    }
    find_game(None, &Env::detect()).ok()
}

fn vortex_view(pin: Option<&Pin>, locations: &Locations) -> Option<ManagerView> {
    let roots = Roots::from_env();
    let configured = Some(locations.vortex.trim().to_owned())
        .filter(|v| !v.is_empty())
        .map(PathBuf::from);
    let game = resolve_game(&locations.game, pin);
    let fallback = vortex::default_staging(&roots);
    vortex_view_for(
        vortex::detect(&roots),
        game.as_deref(),
        configured.as_deref(),
        fallback.as_deref(),
    )
}

fn vortex_view_for(
    install: Option<vortex::Install>,
    game: Option<&Path>,
    configured: Option<&Path>,
    fallback: Option<&Path>,
) -> Option<ManagerView> {
    let found = vortex::listing(game, configured, fallback);
    if install.is_none() && !found.manages_game {
        return None;
    }
    let note = match &found.problem {
        Some(problem) => format!(
            "{} The deployment file could not be read: {problem}.",
            vortex::NOTE
        ),
        None => vortex::NOTE.to_owned(),
    };
    let staging = found.staging.as_deref().map(text).unwrap_or_default();
    let detail = found.manages_game.then(|| DetailView {
        profile: "Vortex deployment".to_owned(),
        dir: staging.clone(),
        f4se_note: note,
        files: Vec::new(),
        mods: found
            .mods
            .iter()
            .map(|m| ModView {
                name: m.name.clone(),
                enabled: m.enabled,
                separator: false,
            })
            .collect(),
        plugins: Vec::new(),
    });
    Some(ManagerView {
        kind: "vortex",
        name: "Vortex".to_owned(),
        dir: staging,
        exe: install.as_ref().and_then(|i| i.exe.as_deref().map(text)),
        manages_this_game: found.manages_game,
        ours: found.manages_game,
        profiles: Vec::new(),
        profiles_listed: false,
        detail,
    })
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

pub fn process_count(image: &str) -> Option<usize> {
    let needle = image.to_lowercase();
    let text = listing(image, false)?;
    Some(text.lines().filter(|line| line.contains(&needle)).count())
}

pub fn process_running(image: &str) -> bool {
    process_count(image).is_some_and(|count| count > 0)
}

pub fn mo2_running() -> bool {
    listing("ModOrganizer.exe", false).is_none_or(|text| text.contains("modorganizer.exe"))
}

pub fn mo2_window_open() -> bool {
    listing("ModOrganizer.exe", true).is_some_and(|text| text.contains("mod organizer"))
}

pub fn game_running() -> bool {
    ["Fallout4.exe", "f4se_loader.exe"]
        .iter()
        .any(|image| listing(image, false).is_none_or(|text| text.contains(&image.to_lowercase())))
}

pub fn detected_runtime(game: &Path) -> Option<Runtime> {
    let exe = find_ci(game, "Fallout4.exe")?;
    Runtime::from_version(&pe_file_version(&exe).ok()?)
}

pub fn sync_versions(pin: Option<&Pin>) -> Option<VersionPick> {
    let pin = pin?;
    let Some(tag) = detected_runtime(&pin.game).map(Runtime::label) else {
        return Some(versions::warning(
            "Could not tell which Fallout 4 version is installed, so F4SE and Commonwealth Online were not checked. Check the Fallout 4 folder.".to_owned(),
        ));
    };
    let Some(ours) = modmgr::selected_instance(&pin.instance, Some(&pin.game)) else {
        return Some(versions::warning(
            "The Mod Organizer 2 instance could not be read, so F4SE and Commonwealth Online were not checked. Check the instance.".to_owned(),
        ));
    };
    let Some(profile) = ours.profiles.iter().find(|p| p.active) else {
        return Some(versions::warning(
            "No Mod Organizer 2 profile is active, so F4SE and Commonwealth Online were not checked. Make a profile active in Mod Organizer 2.".to_owned(),
        ));
    };
    Some(versions::select_versions(
        &ours.dir,
        &profile.name,
        tag,
        !mo2_running(),
    ))
}

fn active_profile(install: &ManagerInstall) -> Option<&modmgr::ProfileInfo> {
    install.profiles.iter().find(|p| p.active)
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

pub fn pin_with_locations(locations: &Locations) -> Option<Pin> {
    if locations.game.is_empty() && locations.mo2.is_empty() {
        return pin();
    }
    let game = if locations.game.is_empty() {
        None
    } else {
        Some(find_game(Some(Path::new(&locations.game)), &Env::detect()).ok()?)
    };
    let m = if locations.mo2.is_empty() {
        let installs = modmgr::detect(game.as_deref());
        installs
            .into_iter()
            .find(|m| m.ours && m.manages_this_game)?
    } else {
        modmgr::selected_instance(Path::new(&locations.mo2), game.as_deref())?
    };
    let game = m.game_dir.clone()?;
    find_ci(&game, "Fallout4.exe")?;
    let exe = if locations.mo2.is_empty() || modmgr::global_instance_name(&m.dir).is_some() {
        m.exe.clone()
    } else {
        find_ci(&m.dir, "ModOrganizer.exe").filter(|path| path.is_file())
    };
    Some(Pin {
        game,
        exe,
        instance: m.dir.clone(),
        profile: active_profile(&m).map(|p| p.name.clone()),
    })
}

pub fn event(installs: &[ManagerInstall], pick: Option<&VersionPick>) -> Event {
    event_with(installs, pick, None)
}

pub fn event_with(
    installs: &[ManagerInstall],
    pick: Option<&VersionPick>,
    extra: Option<ManagerView>,
) -> Event {
    let chosen_idx = chosen(installs);
    let mut managers: Vec<ManagerView> = installs
        .iter()
        .enumerate()
        .filter(|(i, _)| chosen_idx.is_none_or(|p| p == *i))
        .map(|(i, m)| view(m, Some(i) == chosen_idx, pick))
        .collect();
    managers.extend(extra);
    Event::Mods {
        managers,
        warning: MODS_WARNING,
    }
}

fn view(install: &ManagerInstall, ours: bool, pick: Option<&VersionPick>) -> ManagerView {
    ManagerView {
        kind: "mo2",
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
    fn explicit_instance_is_bound_to_the_selected_game() {
        let root = tempfile::tempdir().unwrap();
        let game = root.path().join("game");
        let other = root.path().join("other");
        let instance = root.path().join("instance");
        for dir in [&game, &other, &instance] {
            std::fs::create_dir(dir).unwrap();
        }
        for dir in [&game, &other] {
            std::fs::write(dir.join("Fallout4.exe"), b"").unwrap();
        }
        let ini = format!(
            "[General]\ngameName=Fallout 4\ngamePath=@ByteArray({})\n",
            game.to_string_lossy().replace('\\', "\\\\")
        );
        std::fs::write(instance.join("ModOrganizer.ini"), ini).unwrap();
        std::fs::write(instance.join("ModOrganizer.exe"), b"").unwrap();
        let mut locations = Locations {
            game: game.to_string_lossy().into_owned(),
            mo2: instance.to_string_lossy().into_owned(),
            vortex: String::new(),
        };
        let pin = pin_with_locations(&locations).unwrap();
        assert_eq!(pin.game, game);
        assert_eq!(pin.instance, instance);
        assert_eq!(pin.exe, Some(instance.join("ModOrganizer.exe")));
        std::fs::remove_file(instance.join("ModOrganizer.exe")).unwrap();
        assert!(pin_with_locations(&locations).unwrap().exe.is_none());
        locations.game = other.to_string_lossy().into_owned();
        assert!(pin_with_locations(&locations).is_none());
    }

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
                    "kind": "mo2",
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

    #[test]
    fn a_vortex_only_user_gets_the_deployment_list_without_a_pinned_instance() {
        let root = tempfile::tempdir().unwrap();
        let game = root.path().join("game");
        std::fs::create_dir_all(game.join("Data")).unwrap();
        std::fs::write(game.join("Fallout4.exe"), b"").unwrap();
        std::fs::write(
            game.join("Data").join("vortex.deployment.json"),
            r#"{"files":[{"source":"Cool Mod-1-1-1700000000"}]}"#,
        )
        .unwrap();
        let locations = Locations {
            game: game.to_string_lossy().into_owned(),
            ..Locations::default()
        };
        let resolved = resolve_game(&locations.game, None);
        assert_eq!(resolved.as_deref(), Some(game.as_path()));
        let view = vortex_view_for(None, resolved.as_deref(), None, None)
            .expect("the deployment makes Vortex a listed setup");
        assert!(view.manages_this_game && view.ours);
        let detail = view.detail.expect("a managed game has a detail list");
        assert_eq!(detail.mods[0].name, "Cool Mod");
        assert!(detail.mods[0].enabled);
    }

    #[test]
    fn the_warning_text_is_exact_and_a_vortex_entry_is_read_only() {
        assert_eq!(
            MODS_WARNING,
            "Playing with mods is highly experimental and results may vary."
        );
        let view = ManagerView {
            kind: "vortex",
            name: "Vortex".to_owned(),
            dir: "D:/staging".to_owned(),
            exe: None,
            manages_this_game: true,
            ours: true,
            profiles: Vec::new(),
            profiles_listed: false,
            detail: Some(DetailView {
                profile: "Vortex deployment".to_owned(),
                dir: "D:/staging".to_owned(),
                f4se_note: vortex::NOTE.to_owned(),
                files: Vec::new(),
                mods: vec![ModView {
                    name: "Cool Mod".to_owned(),
                    enabled: true,
                    separator: false,
                }],
                plugins: Vec::new(),
            }),
        };
        let value = serde_json::to_value(Event::Mods {
            managers: vec![view],
            warning: MODS_WARNING,
        })
        .unwrap();
        assert_eq!(value["managers"][0]["kind"], "vortex");
        assert_eq!(
            value["managers"][0]["detail"]["mods"][0]["name"],
            "Cool Mod"
        );
        assert_eq!(value["warning"], MODS_WARNING);
    }

    #[test]
    fn a_missing_vortex_install_adds_nothing_to_the_list() {
        let Event::Mods { managers, .. } = event_with(&[], None, None) else {
            panic!("not a mods event");
        };
        assert!(managers.is_empty());
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
    fn a_profile_is_used_only_when_mod_organizer_marks_it_active() {
        let dir = tempfile::tempdir().unwrap();
        let profile = dir.path().join("profiles").join("Default");
        std::fs::create_dir_all(&profile).unwrap();
        std::fs::write(profile.join("modlist.txt"), "+Commonwealth Online AE\n").unwrap();
        let mut install = install("CO Modlist", dir.path(), None, true);
        install.profiles = vec![
            ProfileInfo {
                name: "Default".to_owned(),
                enabled_mods: Some(1),
                active: false,
            },
            ProfileInfo {
                name: "Second".to_owned(),
                enabled_mods: Some(0),
                active: true,
            },
        ];
        assert_eq!(
            active_profile(&install).map(|p| p.name.as_str()),
            Some("Second")
        );
        install.profiles[1].active = false;
        assert!(active_profile(&install).is_none());
        assert!(detail(&install, None).is_none());
    }

    #[test]
    fn an_undetectable_game_is_a_warning_and_does_not_block_play() {
        let root = tempfile::tempdir().unwrap();
        let pin = Pin {
            game: root.path().to_path_buf(),
            exe: None,
            instance: root.path().to_path_buf(),
            profile: None,
        };
        let pick = sync_versions(Some(&pin)).unwrap();
        assert!(!pick.incomplete && !pick.pending && pick.f4se.is_none());
        assert!(pick.note.contains("Fallout 4 version"));
        assert!(sync_versions(None).is_none());
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
            incomplete: false,
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
