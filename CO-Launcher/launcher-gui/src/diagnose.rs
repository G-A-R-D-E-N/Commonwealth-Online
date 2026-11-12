use std::fs;
use std::io::ErrorKind;
use std::path::{Path, PathBuf};

use launcher_core::addrlib;
use launcher_core::channel;
use launcher_core::modmgr::{self, ModEntry};
use launcher_core::runtime::Runtime;
use launcher_core::server as files;
use launcher_core::versions::is_f4se_mod;

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
        checks.push(server::port_check(true, server::port_state(port), port));
    }
    checks.push(links_check(game_open || server_up, leftover_links()));
    match pin {
        Some(pin) => {
            let mods = profile_mods(pin);
            let (found, complete) = intro_scripts(pin, mods.as_deref());
            checks.push(intro_check(&found, complete));
            checks.extend(mod_checks(pin, mods.as_deref().unwrap_or(&[])));
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

fn profile_mods(pin: &Pin) -> Option<Vec<ModEntry>> {
    pin.profile
        .as_deref()
        .and_then(|profile| modmgr::profile_detail(&pin.instance, profile, Some(&pin.game)))
        .map(|detail| detail.mods)
        .filter(|mods| !mods.is_empty())
}

fn intro_scripts(pin: &Pin, mods: Option<&[ModEntry]>) -> (Vec<PathBuf>, bool) {
    let mut roots = vec![
        pin.game.join("Data").join("Scripts"),
        pin.instance.join("overwrite").join("Scripts"),
    ];
    roots.extend(
        mods.unwrap_or_default()
            .iter()
            .filter(|entry| entry.enabled && !entry.separator)
            .map(|entry| pin.instance.join("mods").join(&entry.name).join("Scripts")),
    );
    let mut complete = mods.is_some();
    let mut found = Vec::new();
    for root in roots {
        complete &= find_intro_scripts(&root, &mut found);
    }
    found.sort();
    (found, complete)
}

fn find_intro_scripts(dir: &Path, found: &mut Vec<PathBuf>) -> bool {
    let entries = match fs::read_dir(dir) {
        Ok(entries) => entries,
        Err(error) => return scan_error_is_complete(error.kind()),
    };
    let mut complete = true;
    for entry in entries {
        let Ok(entry) = entry else {
            complete = false;
            continue;
        };
        let path = entry.path();
        match entry.file_type() {
            Ok(kind) if kind.is_dir() => complete &= find_intro_scripts(&path, found),
            Ok(_) if is_intro_script(&path) => found.push(path),
            Ok(_) => {}
            Err(_) => complete = false,
        }
    }
    complete
}

fn scan_error_is_complete(kind: ErrorKind) -> bool {
    kind == ErrorKind::NotFound
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

fn intro_check(found: &[PathBuf], complete: bool) -> CheckView {
    if found.is_empty() {
        return if complete {
            check("ok", "No loose files replace the game's intro scripts.")
        } else {
            check(
                "warn",
                "Could not check every folder for loose intro scripts, because the mod list or a script folder could not be read. This check is unknown. Check Mod Organizer 2 and try again.",
            )
        };
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
    let Some(runtime) = mods::detected_runtime(&pin.game) else {
        checks.push(check(
            "fail",
            "Could not tell which Fallout 4 version is installed.",
        ));
        return checks;
    };
    let tag = runtime.label();
    if mods.is_empty() {
        checks.push(check(
            "fail",
            "Could not read the Mod Organizer 2 mod list.",
        ));
        checks.extend(package_checks(pin, mods, runtime));
        return checks;
    }
    let wanted: [(String, Matcher); 2] = [
        (format!("F4SE for {tag}"), |name, tag| {
            is_f4se_mod(name) && has_tag(name, tag)
        }),
        (files::MOD.to_owned(), |name, _| {
            starts_with(name, files::MOD)
        }),
    ];
    checks.extend(
        wanted
            .iter()
            .map(|(label, matches)| mod_check(mods, label, |name| matches(name, tag))),
    );
    checks.extend(package_checks(pin, mods, runtime));
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
        check("fail", format!("{label} is missing from the mod list."))
    } else {
        check(
            "fail",
            format!("{label} is turned off. Turn it on in Mod Organizer 2."),
        )
    }
}

fn package_checks(pin: &Pin, mods: &[ModEntry], runtime: Runtime) -> Vec<CheckView> {
    let bin = format!("F4SE/Plugins/{}", addrlib::file_name(runtime));
    vec![
        packaged_file_check(
            pin,
            mods,
            "Commonwealth Online",
            "F4SE/Plugins/CommonwealthOnline.dll",
        ),
        packaged_file_check(pin, mods, "PrismaUI", "F4SE/Plugins/PrismaUI_F4.dll"),
        packaged_file_check(pin, mods, "PrismaUI views folder", "PrismaUI_F4/views"),
        packaged_file_check(pin, mods, "Address Library", &bin),
    ]
}

fn packaged_file_check(pin: &Pin, mods: &[ModEntry], label: &str, rel: &str) -> CheckView {
    let roots = [pin.game.join("Data"), pin.instance.join("overwrite")];
    if roots.iter().any(|root| root.join(rel).exists()) {
        return check("ok", format!("{label} is installed."));
    }
    let mods_dir = pin.instance.join("mods");
    let holders: Vec<&ModEntry> = mods
        .iter()
        .filter(|entry| !entry.separator)
        .filter(|entry| mods_dir.join(&entry.name).join(rel).exists())
        .collect();
    if let Some(entry) = holders.iter().find(|entry| entry.enabled) {
        let version = channel::read_standalone(&mods_dir.join(&entry.name))
            .map(|marker| format!(" {}", marker.version))
            .unwrap_or_default();
        check("ok", format!("{label}{version} is turned on."))
    } else if holders.is_empty() {
        check(
            "fail",
            format!("{label} is missing: {rel} was not found in the game Data folder, the overwrite folder or a mod folder."),
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

    fn pin_in(root: &tempfile::TempDir) -> Pin {
        Pin {
            game: root.path().join("game"),
            exe: None,
            instance: root.path().join("mo2"),
            profile: None,
        }
    }

    fn put(pin: &Pin, mod_name: &str, rel: &str) {
        let path = pin.instance.join("mods").join(mod_name).join(rel);
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, b"").unwrap();
    }

    #[test]
    fn a_mod_is_on_off_or_missing() {
        let mods = [
            entry("F4SE AE 0.7.9", false),
            entry("Commonwealth Online Server", true),
        ];
        let off = mod_check(&mods, "x", |name| starts_with(name, "F4SE"));
        assert!(off.level == "fail" && off.text.contains("turned off"));
        let on = mod_check(&mods, "x", |name| starts_with(name, files::MOD));
        assert_eq!(on.level, "ok");
        let gone = mod_check(&mods, "x", |name| starts_with(name, "F4SE OG"));
        assert!(gone.level == "fail" && gone.text.contains("missing"));
    }

    #[test]
    fn a_packaged_layout_passes_with_one_dll_and_bundled_prisma() {
        let root = tempfile::tempdir().unwrap();
        let pin = pin_in(&root);
        let co = "CommonwealthOnline";
        let bin = format!("F4SE/Plugins/{}", addrlib::file_name(Runtime::Ae));
        put(&pin, co, "F4SE/Plugins/CommonwealthOnline.dll");
        put(&pin, co, "F4SE/Plugins/PrismaUI_F4.dll");
        put(&pin, co, "PrismaUI_F4/views/view.html");
        put(&pin, "Address Library", &bin);
        let mods = [
            entry("CommonwealthOnline", true),
            entry("Address Library", true),
        ];
        let checks = package_checks(&pin, &mods, Runtime::Ae);
        assert!(checks.iter().all(|view| view.level == "ok"));
    }

    #[test]
    fn a_missing_dll_still_fails_the_packaged_check() {
        let root = tempfile::tempdir().unwrap();
        let pin = pin_in(&root);
        put(&pin, "CommonwealthOnline", "F4SE/Plugins/PrismaUI_F4.dll");
        let mods = [entry("CommonwealthOnline", true)];
        let checks = package_checks(&pin, &mods, Runtime::Ae);
        let dll = &checks[0];
        assert!(dll.level == "fail" && dll.text.contains("missing"));
        assert_eq!(checks[1].level, "ok");
    }

    #[test]
    fn a_disabled_package_is_turned_off_not_installed() {
        let root = tempfile::tempdir().unwrap();
        let pin = pin_in(&root);
        put(
            &pin,
            "CommonwealthOnline",
            "F4SE/Plugins/CommonwealthOnline.dll",
        );
        let mods = [entry("CommonwealthOnline", false)];
        let checks = package_checks(&pin, &mods, Runtime::Ae);
        let dll = &checks[0];
        assert!(dll.level == "fail" && dll.text.contains("turned off"));
    }

    #[test]
    fn an_address_library_bin_for_another_runtime_fails() {
        let runtime = Runtime::Ae;
        let og = format!("F4SE/Plugins/{}", addrlib::file_name(Runtime::Og));
        let root = tempfile::tempdir().unwrap();
        let pin = pin_in(&root);
        put(&pin, "Address Library", &og);
        let mods = [entry("Address Library", true)];
        let checks = package_checks(&pin, &mods, runtime);
        assert_eq!(checks[3].level, "fail");
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
        let (found, complete) = intro_scripts(&pin, Some(&mods[..]));
        assert_eq!(intro_check(&found, complete).level, "ok");
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
        let (found, complete) = intro_scripts(&pin, Some(&mods[..]));
        assert_eq!(found.len(), 2);
        assert_eq!(intro_check(&found, complete).level, "fail");
    }

    #[test]
    fn an_unknown_scan_is_not_reported_clean() {
        let root = tempfile::tempdir().unwrap();
        let pin = Pin {
            game: root.path().join("game"),
            exe: None,
            instance: root.path().join("mo2"),
            profile: None,
        };
        let (found, complete) = intro_scripts(&pin, None);
        assert!(found.is_empty() && !complete);
        assert_eq!(intro_check(&found, complete).level, "warn");
        let mods = [entry("Some Mod", true)];
        let (found, complete) = intro_scripts(&pin, Some(&mods[..]));
        assert!(found.is_empty() && complete);
        assert_eq!(intro_check(&found, complete).level, "ok");
        assert!(scan_error_is_complete(ErrorKind::NotFound));
        assert!(!scan_error_is_complete(ErrorKind::PermissionDenied));
    }

    #[test]
    fn link_files_only_warn_with_no_server() {
        assert_eq!(links_check(false, 0).level, "ok");
        assert_eq!(links_check(true, 2).level, "ok");
        assert_eq!(links_check(false, 2).level, "warn");
    }

    #[test]
    fn a_loose_install_under_game_data_is_installed_without_a_mod_list() {
        let root = tempfile::tempdir().unwrap();
        let pin = pin_in(&root);
        let plugins = pin.game.join("Data/F4SE/Plugins");
        fs::create_dir_all(&plugins).unwrap();
        fs::write(plugins.join("CommonwealthOnline.dll"), b"").unwrap();
        let checks = package_checks(&pin, &[], Runtime::Ae);
        let dll = &checks[0];
        assert!(dll.level == "ok" && dll.text.contains("installed"));
    }
}
