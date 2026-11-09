use std::fs;
use std::path::Path;

use crate::error::Result;
use crate::fsutil::{find_ci, resolve_ci, write_atomic};
use crate::modmgr::{mod_entries, read_bytes_capped, write_replacing, ModEntry, MODLIST};

const CO_MOD_PREFIXES: [&str; 2] = ["commonwealth online", "commonwealthonline"];
const CO_BUILD_PREFIXES: [&str; 2] = ["Commonwealth Online ", "CommonwealthOnline "];
const BUNDLED_CO: &str = "CommonwealthOnline";
pub const F4SE_PREFIX: &str = "F4SE ";
// TODO: recheck when the Wabbajack list changes; source:docs/code-notes/CO-Launcher.md
const OWN_MODS: [&str; 2] = ["address library", "prismaui"];
const BUILD_TAGS: [&str; 3] = ["OG", "NG", "AE"];
const MODS: &str = "mods";
const HOLD_PREFIX: &str = "co-build-hold-";
const F4SE_LOADER: &str = "f4se_loader.exe";
const CO_DLL: &str = "CommonwealthOnline.dll";
const UNREADABLE: &str = "The profile's modlist.txt cannot be read, so F4SE and Commonwealth Online were not checked. Open the profile in Mod Organizer 2, then press PLAY again.";

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VersionPick {
    pub f4se: Option<String>,
    pub co: Option<String>,
    pub changed: bool,
    pub pending: bool,
    pub incomplete: bool,
    pub note: String,
}

fn has_prefix(name: &str, prefix: &str) -> bool {
    name.get(..prefix.len())
        .is_some_and(|head| head.eq_ignore_ascii_case(prefix))
}

pub(crate) fn is_co_mod(name: &str) -> bool {
    CO_MOD_PREFIXES
        .iter()
        .any(|prefix| has_prefix(name, prefix))
}

pub fn is_f4se_mod(name: &str) -> bool {
    BUILD_TAGS
        .iter()
        .any(|tag| variant_version(name, F4SE_PREFIX, tag).is_some())
}

pub fn is_co_build(name: &str) -> bool {
    CO_BUILD_PREFIXES.iter().any(|prefix| {
        BUILD_TAGS
            .iter()
            .any(|tag| variant_version(name, prefix, tag).is_some())
    })
}

fn variant_version(name: &str, prefix: &str, tag: &str) -> Option<Vec<u32>> {
    let rest = name
        .get(prefix.len()..)
        .filter(|_| has_prefix(name, prefix))?;
    let rest = rest.get(tag.len()..).filter(|_| has_prefix(rest, tag))?;
    if rest.is_empty() {
        return Some(Vec::new());
    }
    rest.strip_prefix(' ')?
        .split('.')
        .map(|n| n.parse().ok())
        .collect()
}

fn is_own_mod(name: &str) -> bool {
    is_f4se_mod(name) || is_co_mod(name) || OWN_MODS.iter().any(|own| has_prefix(name, own))
}

pub fn incomplete(note: String) -> VersionPick {
    VersionPick {
        f4se: None,
        co: None,
        changed: false,
        pending: false,
        incomplete: true,
        note,
    }
}

pub fn warning(note: String) -> VersionPick {
    VersionPick {
        f4se: None,
        co: None,
        changed: false,
        pending: false,
        incomplete: false,
        note,
    }
}

fn hold_name(tag: &str) -> String {
    format!("{HOLD_PREFIX}{tag}.txt")
}

pub fn is_hold_file(name: &str) -> bool {
    has_prefix(name, HOLD_PREFIX)
}

pub fn record_rollback(modlist: &Path, runtime: &str, restored: &str) -> Result<()> {
    write_atomic(
        &modlist.with_file_name(hold_name(runtime)),
        restored.as_bytes(),
    )
}

fn read_hold(dir: &Path, tag: &str) -> Option<String> {
    find_ci(dir, &hold_name(tag))
        .and_then(|path| read_bytes_capped(&path))
        .and_then(|bytes| String::from_utf8(bytes).ok())
}

fn has_file(dir: &Path, name: &str) -> bool {
    find_ci(dir, name).is_some_and(|path| path.is_file())
}

fn complete(mods: Option<&Path>, name: &str, co: bool, tag: &str) -> bool {
    let plain = !name.is_empty() && !name.contains(['/', '\\', ':']) && name != "." && name != "..";
    let folder = mods
        .filter(|_| plain)
        .map(|mods| resolve_ci(mods, &[name]))
        .filter(|folder| folder.is_dir());
    let Some(folder) = folder else {
        return false;
    };
    if co {
        let plugins = resolve_ci(&folder, &["F4SE", "Plugins"]);
        has_file(&plugins, CO_DLL) || has_file(&plugins, &format!("CommonwealthOnline-{tag}.dll"))
    } else {
        has_file(&folder, F4SE_LOADER) || has_file(&resolve_ci(&folder, &["Root"]), F4SE_LOADER)
    }
}

fn ranked<'a>(
    entries: &'a [ModEntry],
    prefixes: &[&str],
    tag: &str,
    usable: impl Fn(&str) -> bool,
    unusable: &mut Vec<&'a str>,
) -> Vec<(Vec<u32>, bool, &'a String)> {
    let mut found: Vec<_> = entries
        .iter()
        .filter(|m| !m.separator)
        .filter_map(|m| {
            let version = prefixes
                .iter()
                .find_map(|prefix| variant_version(&m.name, prefix, tag))?;
            if usable(m.name.as_str()) {
                Some((version, m.enabled, &m.name))
            } else {
                unusable.push(m.name.as_str());
                None
            }
        })
        .collect();
    found.sort_by(|a, b| b.cmp(a));
    found
}

fn chosen_co<'a>(
    ranked: &[(Vec<u32>, bool, &'a String)],
    hold: Option<&str>,
) -> Option<&'a String> {
    let top = ranked.first()?;
    let rolled_back = hold
        .filter(|restored| !top.1 && !top.2.eq_ignore_ascii_case(restored))
        .and_then(|restored| ranked.iter().find(|c| c.2.eq_ignore_ascii_case(restored)));
    Some(rolled_back.unwrap_or(top).2)
}

fn sentences(parts: &[&str]) -> String {
    parts
        .iter()
        .copied()
        .filter(|part| !part.is_empty())
        .collect::<Vec<_>>()
        .join(" ")
}

pub fn select_versions(instance: &Path, profile: &str, tag: &str, apply: bool) -> VersionPick {
    let dir = resolve_ci(instance, &["profiles", profile]);
    let path = find_ci(&dir, MODLIST);
    let Some(text) = path
        .as_deref()
        .and_then(read_bytes_capped)
        .and_then(|bytes| String::from_utf8(bytes).ok())
    else {
        return incomplete(UNREADABLE.to_owned());
    };
    let mods = find_ci(instance, MODS);
    let entries = mod_entries(&text);
    let hold = read_hold(&dir, tag);
    let foreign: Vec<&str> = entries
        .iter()
        .filter(|m| m.enabled && !m.separator && !is_own_mod(&m.name))
        .map(|m| m.name.as_str())
        .collect();
    let left_alone = format!(
        "Other mods are turned on ({}), so the launcher left F4SE and Commonwealth Online as they are.",
        foreign.join(", ")
    );
    let mut unusable = Vec::new();
    let f4se_ranked = ranked(
        &entries,
        &[F4SE_PREFIX],
        tag,
        |name| complete(mods.as_deref(), name, false, tag),
        &mut unusable,
    );
    let co_ranked = ranked(
        &entries,
        &CO_BUILD_PREFIXES,
        tag,
        |name| complete(mods.as_deref(), name, true, tag),
        &mut unusable,
    );
    let f4se = f4se_ranked.first().map(|c| c.2.clone());
    let co = chosen_co(&co_ranked, hold.as_deref()).cloned();
    if apply && co.is_some() && hold.is_some() && co_ranked.first().map(|c| c.2) == co.as_ref() {
        if let Some(path) = find_ci(&dir, &hold_name(tag)) {
            fs::remove_file(path).ok();
        }
    }
    let f4se_broken = f4se.is_none()
        && entries
            .iter()
            .any(|m| !m.separator && variant_version(&m.name, F4SE_PREFIX, tag).is_some());
    let bundled = entries
        .iter()
        .any(|m| m.enabled && m.name.eq_ignore_ascii_case(BUNDLED_CO));
    let co_missing = co.is_none() && !bundled;
    let missing = co_missing || f4se_broken;
    let skipped = if unusable.is_empty() {
        String::new()
    } else {
        format!(
            "{} is in the mod list but its folder or required files are missing, so it was not used.",
            unusable.join(", ")
        )
    };
    let mut gaps = Vec::new();
    if f4se.is_none() {
        gaps.push(if f4se_broken {
            format!("No complete F4SE for {tag} is in the mod list.")
        } else {
            format!("No F4SE for {tag} is in the mod list, so the launcher did not check F4SE.")
        });
    }
    if co_missing {
        gaps.push(format!(
            "No complete Commonwealth Online build for {tag} is in the mod list."
        ));
    }
    let gaps = gaps.join(" ");
    if f4se.is_none() && co.is_none() {
        let main = if foreign.is_empty() {
            ""
        } else {
            left_alone.as_str()
        };
        let note = sentences(&[main, skipped.as_str(), gaps.as_str()]);
        return if missing {
            incomplete(note)
        } else {
            warning(note)
        };
    }
    let eol = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let mut changed = false;
    let lines: Vec<String> = text
        .lines()
        .map(|line| {
            let (flag, name) = match (line.strip_prefix('+'), line.strip_prefix('-')) {
                (Some(name), _) => ("+", name),
                (_, Some(name)) => ("-", name),
                _ => return line.to_owned(),
            };
            let target = if is_f4se_mod(name) {
                &f4se
            } else if is_co_build(name) {
                &co
            } else {
                return line.to_owned();
            };
            let Some(best) = target else {
                return line.to_owned();
            };
            let want = if name == best { "+" } else { "-" };
            changed |= want != flag;
            format!("{want}{name}")
        })
        .collect();
    let using = [&f4se, &co]
        .into_iter()
        .flatten()
        .map(String::as_str)
        .collect::<Vec<_>>()
        .join(" and ");
    if !foreign.is_empty() {
        let note = if changed {
            format!("{left_alone} Your game needs {using}. Turn that on in Mod Organizer 2 or turn off the other mods, then press PLAY again.")
        } else {
            left_alone
        };
        return VersionPick {
            f4se: None,
            co: None,
            changed: false,
            pending: changed,
            incomplete: missing,
            note: sentences(&[note.as_str(), skipped.as_str(), gaps.as_str()]),
        };
    }
    let written =
        changed && apply && path.is_some_and(|p| write_replacing(&p, &(lines.join(eol) + eol)));
    let note = if changed && !written {
        format!("Close Mod Organizer 2 so the launcher can switch to {using}.")
    } else {
        format!("Using {using} for your game.")
    };
    VersionPick {
        f4se,
        co,
        changed: written,
        pending: changed && !written,
        incomplete: missing,
        note: sentences(&[note.as_str(), skipped.as_str(), gaps.as_str()]),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;

    fn write(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn f4se_instance(modlist: &str) -> (tempfile::TempDir, PathBuf) {
        let root = tempfile::tempdir().unwrap();
        let list = root
            .path()
            .join("profiles")
            .join("Default")
            .join("modlist.txt");
        write(&list, modlist);
        (root, list)
    }

    fn builds(root: &Path, names: &[&str]) {
        for name in names {
            let folder = root.join("mods").join(name);
            write(&folder.join("Root").join(F4SE_LOADER), "exe");
            write(&folder.join("F4SE").join("Plugins").join(CO_DLL), "dll");
        }
    }

    #[test]
    fn f4se_for_the_game_is_switched_on_and_the_rest_off() {
        let (root, list) = f4se_instance(
            "# h\r\n+F4SE AE 0.7.9\r\n-F4SE OG 0.6.22\r\n-F4SE OG 0.6.23\r\n+Commonwealth Online AE\r\n-Commonwealth Online OG\r\n+Commonwealth Online Maps\r\n+Commonwealth Online OG Patch\r\n+PrismaUI\r\n*DLC: Far Harbor\r\n",
        );
        builds(root.path(), &["F4SE OG 0.6.23", "Commonwealth Online OG"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.f4se.as_deref(), Some("F4SE OG 0.6.23"));
        assert_eq!(pick.co.as_deref(), Some("Commonwealth Online OG"));
        assert!(pick.changed);
        assert_eq!(
            fs::read_to_string(&list).unwrap(),
            "# h\r\n-F4SE AE 0.7.9\r\n-F4SE OG 0.6.22\r\n+F4SE OG 0.6.23\r\n-Commonwealth Online AE\r\n+Commonwealth Online OG\r\n+Commonwealth Online Maps\r\n+Commonwealth Online OG Patch\r\n+PrismaUI\r\n*DLC: Far Harbor\r\n"
        );
        assert!(!select_versions(root.path(), "Default", "OG", true).changed);
    }

    #[test]
    fn f4se_switch_waits_when_not_allowed_to_write() {
        let text = "+F4SE AE 0.7.9
-F4SE OG 0.6.23
";
        let (root, list) = f4se_instance(text);
        builds(root.path(), &["F4SE OG 0.6.23"]);
        let pick = select_versions(root.path(), "Default", "OG", false);
        assert!(!pick.changed && pick.pending && pick.note.contains("Close Mod Organizer 2"));
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
    }

    #[test]
    fn f4se_is_left_alone_with_other_mods_or_no_match() {
        let text = "+F4SE AE 0.7.9
+Some Armor
-F4SE OG 0.6.23
";
        let (root, list) = f4se_instance(text);
        builds(root.path(), &["F4SE OG 0.6.23"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.f4se.is_none() && pick.note.contains("Some Armor"));
        assert!(pick.pending && pick.note.contains("F4SE OG 0.6.23"));
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n+Some Armor\n-F4SE AE 0.7.9\n");
        builds(root.path(), &["F4SE OG 0.6.23"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(!pick.pending && !pick.changed && pick.note.contains("Some Armor"));
        let (root, _) = f4se_instance("+Some Armor\n");
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(!pick.pending && pick.note.contains("Some Armor"));
        let (root, _) = f4se_instance(
            "+F4SE AE 0.7.9
",
        );
        let pick = select_versions(root.path(), "Default", "NG", true);
        assert!(pick.f4se.is_none() && pick.note.contains("NG"));
    }

    #[test]
    fn a_mod_that_only_starts_with_f4se_is_not_a_build_and_is_left_on() {
        for name in ["F4SE Plugin Preloader", "F4SE OG Patch", "F4SEX"] {
            assert!(!is_f4se_mod(name), "{name}");
        }
        assert!(is_f4se_mod("f4se ng 0.7.2"));
        let text = "+F4SE AE 0.7.9\n+F4SE Plugin Preloader\n-F4SE OG 0.6.23\n";
        let (root, list) = f4se_instance(text);
        builds(root.path(), &["F4SE OG 0.6.23"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.pending && pick.note.contains("F4SE Plugin Preloader"));
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
    }

    // TODO: recheck when the Wabbajack list changes; source:docs/code-notes/CO-Launcher.md
    #[test]
    fn the_shipped_modlist_names_count_as_our_own() {
        let shipped = "+CommonwealthOnlineUI\n+CommonwealthOnline AE\n+PrismaUI F4 2.2.0\n-Commonwealth Online NG\n-Commonwealth Online AE\n-Commonwealth Online OG\n+F4SE AE 0.7.9\n-F4SE NG 0.7.2\n-F4SE OG 0.6.23\n+Address Library\n*DLC: Far Harbor\n+Commonwealth Online Server\n";
        let (root, list) = f4se_instance(shipped);
        builds(
            root.path(),
            &[
                "F4SE AE 0.7.9",
                "F4SE OG 0.6.23",
                "CommonwealthOnline AE",
                "Commonwealth Online OG",
            ],
        );
        let pick = select_versions(root.path(), "Default", "AE", true);
        assert_eq!(pick.f4se.as_deref(), Some("F4SE AE 0.7.9"));
        assert_eq!(pick.co.as_deref(), Some("CommonwealthOnline AE"));
        assert!(!pick.changed && !pick.pending);
        assert_eq!(fs::read_to_string(&list).unwrap(), shipped);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.co.as_deref(), Some("Commonwealth Online OG"));
        assert!(pick.changed);
        let switched = fs::read_to_string(&list).unwrap();
        for line in [
            "+CommonwealthOnlineUI",
            "-CommonwealthOnline AE",
            "+Commonwealth Online OG",
            "+F4SE OG 0.6.23",
            "-F4SE AE 0.7.9",
            "+Commonwealth Online Server",
        ] {
            assert!(switched.lines().any(|l| l == line), "{line}");
        }
    }

    #[test]
    fn the_spelling_already_on_wins_a_version_tie() {
        let text = "+Commonwealth Online AE\n-CommonwealthOnline AE\n+Some Armor\n";
        let (root, list) = f4se_instance(text);
        builds(
            root.path(),
            &["Commonwealth Online AE", "CommonwealthOnline AE"],
        );
        let pick = select_versions(root.path(), "Default", "AE", true);
        assert!(!pick.pending && !pick.changed);
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
    }

    #[test]
    fn f4se_is_left_alone_when_modlist_is_not_utf8() {
        let (root, list) = f4se_instance("");
        let bytes = b"-Caf\xe9 Armor\r\n+F4SE AE 0.7.9\r\n-F4SE OG 0.6.23\r\n";
        fs::write(&list, bytes).unwrap();
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.f4se.is_none() && !pick.changed && pick.incomplete);
        assert_eq!(fs::read(&list).unwrap(), bytes);
    }

    #[test]
    fn f4se_switch_covers_names_in_any_case_and_leaves_no_temp_file() {
        let (root, list) = f4se_instance("+f4se OG 0.6.22\n-F4SE OG 0.6.23\n");
        builds(root.path(), &["F4SE OG 0.6.23"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.changed);
        assert_eq!(
            fs::read_to_string(&list).unwrap(),
            "-f4se OG 0.6.22\n+F4SE OG 0.6.23\n"
        );
        assert!(!list.with_extension("txt.tmp").exists());
    }

    #[test]
    fn a_newer_build_whose_folder_is_missing_or_incomplete_is_not_enabled() {
        let (root, list) = f4se_instance(
            "+F4SE OG 0.6.23\n-Commonwealth Online OG 0.3.0\n+Commonwealth Online OG 0.2.0\n-Commonwealth Online OG 0.1.0\n",
        );
        builds(
            root.path(),
            &["F4SE OG 0.6.23", "Commonwealth Online OG 0.1.0"],
        );
        let newer = root
            .path()
            .join("mods")
            .join("Commonwealth Online OG 0.3.0");
        write(&newer.join("readme.txt"), "no build");
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.co.as_deref(), Some("Commonwealth Online OG 0.1.0"));
        assert!(pick.changed && !pick.pending && !pick.incomplete);
        assert!(pick.note.contains("Commonwealth Online OG 0.3.0"));
        assert!(pick.note.contains("Commonwealth Online OG 0.2.0"));
        assert_eq!(
            fs::read_to_string(&list).unwrap(),
            "+F4SE OG 0.6.23\n-Commonwealth Online OG 0.3.0\n-Commonwealth Online OG 0.2.0\n+Commonwealth Online OG 0.1.0\n"
        );
    }

    #[test]
    fn a_build_is_found_through_case_differences_and_a_complete_install_is_left_alone() {
        let text = "+F4SE OG 0.6.23\n+commonwealth online og 0.2.0\n";
        let (root, list) = f4se_instance(text);
        builds(
            root.path(),
            &["F4SE OG 0.6.23", "Commonwealth Online OG 0.2.0"],
        );
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.co.as_deref(), Some("commonwealth online og 0.2.0"));
        assert!(!pick.changed && !pick.pending && !pick.incomplete);
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
    }

    #[test]
    fn a_check_with_a_missing_component_or_profile_is_incomplete() {
        let (root, _) = f4se_instance("+Some Armor\n");
        assert!(select_versions(root.path(), "Default", "OG", true).incomplete);
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n");
        builds(root.path(), &["F4SE OG 0.6.23"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.incomplete && !pick.pending && pick.f4se.is_some());
        assert!(pick
            .note
            .contains("No complete Commonwealth Online build for OG"));
        let (root, _) = f4se_instance("+Commonwealth Online OG 0.2.0\n");
        builds(root.path(), &["Commonwealth Online OG 0.2.0"]);
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(!pick.incomplete && pick.note.contains("No F4SE for OG"));
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n+Commonwealth Online OG 0.2.0\n");
        builds(
            root.path(),
            &["F4SE OG 0.6.23", "Commonwealth Online OG 0.2.0"],
        );
        assert!(!select_versions(root.path(), "Default", "OG", true).incomplete);
        let root = tempfile::tempdir().unwrap();
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.incomplete && !pick.pending && pick.note.contains("PLAY"));
    }

    #[test]
    fn an_f4se_loader_at_the_folder_top_is_complete() {
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n+Commonwealth Online OG 0.2.0\n");
        builds(root.path(), &["Commonwealth Online OG 0.2.0"]);
        write(
            &root.path().join("mods/F4SE OG 0.6.23/f4se_loader.exe"),
            "exe",
        );
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(!pick.incomplete && pick.f4se.as_deref() == Some("F4SE OG 0.6.23"));
    }

    #[test]
    fn an_f4se_loader_under_root_is_complete() {
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n+Commonwealth Online OG 0.2.0\n");
        builds(
            root.path(),
            &["F4SE OG 0.6.23", "Commonwealth Online OG 0.2.0"],
        );
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(!pick.incomplete && pick.f4se.as_deref() == Some("F4SE OG 0.6.23"));
    }

    #[test]
    fn an_f4se_entry_without_its_loader_blocks_play() {
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n+Commonwealth Online OG 0.2.0\n");
        builds(root.path(), &["Commonwealth Online OG 0.2.0"]);
        write(
            &root.path().join("mods/F4SE OG 0.6.23/readme.txt"),
            "no loader",
        );
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.incomplete && pick.f4se.is_none() && pick.co.is_some());
        assert!(pick.note.contains("No complete F4SE for OG"));
    }

    #[test]
    fn the_hold_is_per_runtime_and_ends_when_the_newest_build_is_back_on() {
        let before = "+F4SE OG 0.6.23\n-Commonwealth Online OG 1.1\n+Commonwealth Online OG 1.0\n";
        let (root, list) = f4se_instance(before);
        builds(
            root.path(),
            &[
                "F4SE OG 0.6.23",
                "Commonwealth Online OG 1.1",
                "Commonwealth Online OG 1.0",
            ],
        );
        let hold_og = list.with_file_name("co-build-hold-OG.txt");
        record_rollback(&list, "NG", "Commonwealth Online NG 1.0").unwrap();
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.co.as_deref(), Some("Commonwealth Online OG 1.1"));
        fs::write(&list, before).unwrap();
        record_rollback(&list, "OG", "Commonwealth Online OG 1.0").unwrap();
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.co.as_deref(), Some("Commonwealth Online OG 1.0"));
        assert!(!pick.changed && hold_og.exists());
        fs::write(
            &list,
            "+F4SE OG 0.6.23\n+Commonwealth Online OG 1.1\n-Commonwealth Online OG 1.0\n",
        )
        .unwrap();
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert_eq!(pick.co.as_deref(), Some("Commonwealth Online OG 1.1"));
        assert!(!hold_og.exists() && list.with_file_name("co-build-hold-NG.txt").exists());
    }

    #[test]
    fn the_bundled_package_is_not_a_missing_build() {
        let (root, _) = f4se_instance("+CommonwealthOnline\n+F4SE AE 0.7.9\n");
        builds(root.path(), &["F4SE AE 0.7.9"]);
        let pick = select_versions(root.path(), "Default", "AE", true);
        assert_eq!(pick.f4se.as_deref(), Some("F4SE AE 0.7.9"));
        assert!(pick.co.is_none() && !pick.changed && !pick.pending && !pick.incomplete);
        assert!(pick.note == "Using F4SE AE 0.7.9 for your game.");
    }

    #[test]
    fn a_bundled_package_without_an_f4se_entry_warns_but_does_not_block_play() {
        let (root, _) = f4se_instance("+CommonwealthOnline\n");
        let pick = select_versions(root.path(), "Default", "AE", true);
        assert!(pick.f4se.is_none() && pick.co.is_none());
        assert!(!pick.incomplete && !pick.pending && !pick.changed);
        assert!(pick.note.contains("No F4SE for AE is in the mod list"));
    }

    #[test]
    fn a_list_with_neither_f4se_nor_a_build_says_so() {
        let (root, _) = f4se_instance("+PrismaUI\n");
        let pick = select_versions(root.path(), "Default", "AE", true);
        assert!(pick.f4se.is_none() && pick.co.is_none() && pick.incomplete);
        assert!(pick.note.contains("No F4SE for AE is in the mod list"));
        assert!(pick
            .note
            .contains("No complete Commonwealth Online build for AE is in the mod list."));
    }
}
