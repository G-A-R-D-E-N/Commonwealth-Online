use std::fs;
use std::path::{Path, PathBuf};

use launcher_core::modmgr::{self, ModEntry};
use launcher_core::server as files;
use launcher_core::versions::{is_co_build, is_f4se_mod};

use crate::admin;
use crate::mods::{self, Pin};
use crate::protocol::{CheckView, Event};
use crate::server::{self, check};
use crate::state::NO_MO2;
use crate::troubleshoot;

type Matcher = fn(&str, &str) -> bool;

pub fn setup(pin: Option<&Pin>) -> Event {
    let game_open = mods::game_running();
    let mut checks = vec![if game_open {
        check(
            "warn",
            "Fallout 4 is running. Close it before using the fixes below.",
        )
    } else {
        check("ok", "Fallout 4 is closed.")
    }];
    checks.push(server::steam_running(mods::process_running("steam.exe")));
    let server_up = mods::process_running(files::EXE);
    if let Some(pin) = pin.filter(|_| server_up) {
        let port = admin::game_port(&server::folders(pin));
        checks.push(server::port_check(true, server::udp_in_use(port), port));
    }
    checks.push(links_check(game_open || server_up, leftover_links()));
    match pin {
        Some(pin) => {
            let mods = profile_mods(pin);
            checks.push(intro_check(&intro_scripts(pin, &mods)));
            checks.extend(mod_checks(pin, &mods));
        }
        None => checks.push(check("fail", NO_MO2)),
    }
    Event::SetupChecks { checks }
}

fn leftover_links() -> usize {
    troubleshoot::link_dir()
        .and_then(|dir| fs::read_dir(dir).ok())
        .map(|entries| {
            entries
                .flatten()
                .filter(|entry| troubleshoot::is_link_file(&entry.path()))
                .count()
        })
        .unwrap_or(0)
}

fn links_check(in_use: bool, links: usize) -> CheckView {
    if links == 0 || in_use {
        check("ok", "No leftover hosting link files.")
    } else {
        check(
            "warn",
            format!("{links} leftover hosting link file(s) and no server running. Use CLEAR HOSTING LINK FILES."),
        )
    }
}

fn profile_mods(pin: &Pin) -> Vec<ModEntry> {
    pin.profile
        .as_deref()
        .and_then(|profile| modmgr::profile_detail(&pin.instance, profile, Some(&pin.game)))
        .map(|detail| detail.mods)
        .unwrap_or_default()
}

fn intro_scripts(pin: &Pin, mods: &[ModEntry]) -> Vec<PathBuf> {
    let mut roots = vec![
        pin.game.join("Data").join("Scripts"),
        pin.instance.join("overwrite").join("Scripts"),
    ];
    roots.extend(
        mods.iter()
            .filter(|entry| entry.enabled && !entry.separator)
            .map(|entry| pin.instance.join("mods").join(&entry.name).join("Scripts")),
    );
    let mut found = Vec::new();
    for root in roots {
        find_intro_scripts(&root, &mut found);
    }
    found.sort();
    found
}

fn find_intro_scripts(dir: &Path, found: &mut Vec<PathBuf>) {
    for entry in fs::read_dir(dir).into_iter().flatten().flatten() {
        let path = entry.path();
        if entry.file_type().is_ok_and(|kind| kind.is_dir()) {
            find_intro_scripts(&path, found);
        } else if is_intro_script(&path) {
            found.push(path);
        }
    }
}

fn is_intro_script(path: &Path) -> bool {
    let Some(name) = path.file_name().and_then(|name| name.to_str()) else {
        return false;
    };
    let pex = path
        .extension()
        .is_some_and(|ext| ext.eq_ignore_ascii_case("pex"));
    pex && ["MQ101", "MQ102", "QF_MQ101", "QF_MQ102"]
        .iter()
        .any(|prefix| starts_with(name, prefix))
}

fn intro_check(found: &[PathBuf]) -> CheckView {
    if found.is_empty() {
        return check("ok", "No loose files replace the game's intro scripts.");
    }
    let mut list: Vec<String> = found
        .iter()
        .take(5)
        .map(|path| path.display().to_string())
        .collect();
    if found.len() > 5 {
        list.push(format!("and {} more", found.len() - 5));
    }
    check(
        "fail",
        format!(
            "Loose intro script files replace the game's own and can cause a black screen after character creation. Delete or move them: {}",
            list.join("; ")
        ),
    )
}

fn mod_checks(pin: &Pin, mods: &[ModEntry]) -> Vec<CheckView> {
    let mut checks = vec![if files::installed(&files::dir(&pin.instance)) {
        check("ok", "The server files are installed.")
    } else {
        check(
            "info",
            "The server files are not installed yet. PLAY or the SERVER page installs them, or use REPAIR SERVER FILES.",
        )
    }];
    let Some(tag) = mods::detected_runtime(&pin.game).map(|runtime| runtime.label()) else {
        checks.push(check(
            "fail",
            "Could not tell which Fallout 4 version is installed.",
        ));
        return checks;
    };
    if mods.is_empty() {
        checks.push(check(
            "fail",
            "Could not read the Mod Organizer 2 mod list.",
        ));
        return checks;
    }
    let wanted: [(String, Matcher); 5] = [
        (format!("F4SE for {tag}"), |name, tag| {
            is_f4se_mod(name) && has_tag(name, tag)
        }),
        (format!("Commonwealth Online for {tag}"), |name, tag| {
            is_co_build(name) && has_tag(name, tag)
        }),
        (files::MOD.to_owned(), |name, _| {
            starts_with(name, files::MOD)
        }),
        ("Address Library".to_owned(), |name, _| {
            starts_with(name, "Address Library")
        }),
        ("PrismaUI".to_owned(), |name, _| {
            starts_with(name, "PrismaUI")
        }),
    ];
    checks.extend(
        wanted
            .iter()
            .map(|(label, matches)| mod_check(mods, label, |name| matches(name, tag))),
    );
    checks
}

fn mod_check(mods: &[ModEntry], label: &str, matches: impl Fn(&str) -> bool) -> CheckView {
    let found: Vec<bool> = mods
        .iter()
        .filter(|entry| !entry.separator && matches(&entry.name))
        .map(|entry| entry.enabled)
        .collect();
    if found.contains(&true) {
        check("ok", format!("{label} is turned on."))
    } else if found.is_empty() {
        check(
            "fail",
            format!("{label} is missing. Reinstall the modlist with Wabbajack."),
        )
    } else {
        check(
            "fail",
            format!("{label} is turned off. Turn it on in Mod Organizer 2."),
        )
    }
}

fn has_tag(name: &str, tag: &str) -> bool {
    name.split_whitespace()
        .any(|word| word.eq_ignore_ascii_case(tag))
}

fn starts_with(name: &str, prefix: &str) -> bool {
    name.get(..prefix.len())
        .is_some_and(|head| head.eq_ignore_ascii_case(prefix))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn entry(name: &str, enabled: bool) -> ModEntry {
        ModEntry {
            name: name.to_owned(),
            enabled,
            separator: false,
        }
    }

    #[test]
    fn a_mod_is_on_off_or_missing() {
        let mods = [
            entry("Commonwealth Online AE", false),
            entry("Commonwealth Online AE Patch", true),
            entry("prismaui", true),
        ];
        let off = mod_check(&mods, "x", |name| is_co_build(name) && has_tag(name, "AE"));
        assert!(off.level == "fail" && off.text.contains("turned off"));
        assert_eq!(
            mod_check(&mods, "x", |name| starts_with(name, "PrismaUI")).level,
            "ok"
        );
        let gone = mod_check(&mods, "x", |name| starts_with(name, "Address Library"));
        assert!(gone.level == "fail" && gone.text.contains("missing"));
    }

    #[test]
    fn loose_intro_scripts_fail_the_check() {
        let root = tempfile::tempdir().unwrap();
        let pin = Pin {
            game: root.path().join("game"),
            exe: None,
            instance: root.path().join("mo2"),
            profile: None,
        };
        let mods = [entry("Some Mod", true), entry("Off Mod", false)];
        assert_eq!(intro_check(&intro_scripts(&pin, &mods)).level, "ok");
        for name in ["Some Mod", "Off Mod"] {
            let fragments = pin
                .instance
                .join("mods")
                .join(name)
                .join("Scripts/Fragments/Quests");
            fs::create_dir_all(&fragments).unwrap();
            fs::write(fragments.join("QF_MQ101_0001ED86.pex"), b"").unwrap();
            fs::write(fragments.join("QF_MQ101_0001ED86.psc"), b"").unwrap();
        }
        let loose = pin.game.join("Data/Scripts");
        fs::create_dir_all(&loose).unwrap();
        fs::write(loose.join("mq101questscript.pex"), b"").unwrap();
        fs::write(loose.join("Actor.pex"), b"").unwrap();
        let found = intro_scripts(&pin, &mods);
        assert_eq!(found.len(), 2);
        assert_eq!(intro_check(&found).level, "fail");
    }

    #[test]
    fn link_files_only_warn_with_no_server() {
        assert_eq!(links_check(false, 0).level, "ok");
        assert_eq!(links_check(true, 2).level, "ok");
        assert_eq!(links_check(false, 2).level, "warn");
    }
}
