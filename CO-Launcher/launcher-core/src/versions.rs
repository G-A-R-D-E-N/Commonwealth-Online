use std::path::Path;

use crate::fsutil::{find_ci, resolve_ci};
use crate::modmgr::{mod_entries, read_bytes_capped, write_replacing, MODLIST};

const CO_MOD_PREFIXES: [&str; 2] = ["commonwealth online", "commonwealthonline"];
const CO_BUILD_PREFIXES: [&str; 2] = ["Commonwealth Online ", "CommonwealthOnline "];
pub const F4SE_PREFIX: &str = "F4SE ";
// TODO: recheck when the Wabbajack list changes; source:docs/code-notes/CO-Launcher.md
const OWN_MODS: [&str; 2] = ["address library", "prismaui"];
const BUILD_TAGS: [&str; 3] = ["OG", "NG", "AE"];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct VersionPick {
    pub f4se: Option<String>,
    pub co: Option<String>,
    pub changed: bool,
    pub pending: bool,
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

pub fn select_versions(instance: &Path, profile: &str, tag: &str, apply: bool) -> VersionPick {
    let none = |note: String| VersionPick {
        f4se: None,
        co: None,
        changed: false,
        pending: false,
        note,
    };
    let dir = resolve_ci(instance, &["profiles", profile]);
    let path = find_ci(&dir, MODLIST);
    let Some(text) = path
        .as_deref()
        .and_then(read_bytes_capped)
        .and_then(|bytes| String::from_utf8(bytes).ok())
    else {
        return none(
            "The profile has no readable modlist.txt, so F4SE and Commonwealth Online were left alone."
                .to_owned(),
        );
    };
    let entries = mod_entries(&text);
    let foreign: Vec<&str> = entries
        .iter()
        .filter(|m| m.enabled && !m.separator && !is_own_mod(&m.name))
        .map(|m| m.name.as_str())
        .collect();
    let left_alone = format!(
        "Other mods are turned on ({}), so the launcher left F4SE and Commonwealth Online as they are.",
        foreign.join(", ")
    );
    let best = |prefixes: &[&str]| {
        entries
            .iter()
            .filter(|m| !m.separator)
            .filter_map(|m| {
                let version = prefixes
                    .iter()
                    .find_map(|prefix| variant_version(&m.name, prefix, tag))?;
                Some((version, m.enabled, &m.name))
            })
            .max()
            .map(|(_, _, name)| name.clone())
    };
    let (f4se, co) = (best(&[F4SE_PREFIX]), best(&CO_BUILD_PREFIXES));
    if f4se.is_none() && co.is_none() {
        if !foreign.is_empty() {
            return none(left_alone);
        }
        return none(format!(
            "No F4SE or Commonwealth Online build for {tag} is in the mod list."
        ));
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
            pending: changed,
            ..none(note)
        };
    }
    let written =
        changed && apply && path.is_some_and(|p| write_replacing(&p, &(lines.join(eol) + eol)));
    let mut note = if changed && !written {
        format!("Close Mod Organizer 2 so the launcher can switch to {using}.")
    } else {
        format!("Using {using} for your game.")
    };
    if f4se.is_none() {
        note += &format!(" No F4SE for {tag} is in the mod list.");
    }
    if co.is_none() {
        note += &format!(" No Commonwealth Online build for {tag} is in the mod list.");
    }
    VersionPick {
        f4se,
        co,
        changed: written,
        pending: changed && !written,
        note,
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

    #[test]
    fn f4se_for_the_game_is_switched_on_and_the_rest_off() {
        let (root, list) = f4se_instance(
            "# h\r\n+F4SE AE 0.7.9\r\n-F4SE OG 0.6.22\r\n-F4SE OG 0.6.23\r\n+Commonwealth Online AE\r\n-Commonwealth Online OG\r\n+Commonwealth Online Maps\r\n+Commonwealth Online OG Patch\r\n+PrismaUI\r\n*DLC: Far Harbor\r\n",
        );
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
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.f4se.is_none() && pick.note.contains("Some Armor"));
        assert!(pick.pending && pick.note.contains("F4SE OG 0.6.23"));
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
        let (root, _) = f4se_instance("+F4SE OG 0.6.23\n+Some Armor\n-F4SE AE 0.7.9\n");
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
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.pending && pick.note.contains("F4SE Plugin Preloader"));
        assert_eq!(fs::read_to_string(&list).unwrap(), text);
    }

    // TODO: recheck when the Wabbajack list changes; source:docs/code-notes/CO-Launcher.md
    #[test]
    fn the_shipped_modlist_names_count_as_our_own() {
        let shipped = "+CommonwealthOnlineUI\n+CommonwealthOnline AE\n+PrismaUI F4 2.2.0\n-Commonwealth Online NG\n-Commonwealth Online AE\n-Commonwealth Online OG\n+F4SE AE 0.7.9\n-F4SE NG 0.7.2\n-F4SE OG 0.6.23\n+Address Library\n*DLC: Far Harbor\n+Commonwealth Online Server\n";
        let (root, list) = f4se_instance(shipped);
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
        assert!(pick.f4se.is_none() && !pick.changed);
        assert_eq!(fs::read(&list).unwrap(), bytes);
    }

    #[test]
    fn f4se_switch_covers_names_in_any_case_and_leaves_no_temp_file() {
        let (root, list) = f4se_instance("+f4se OG 0.6.22\n-F4SE OG 0.6.23\n");
        let pick = select_versions(root.path(), "Default", "OG", true);
        assert!(pick.changed);
        assert_eq!(
            fs::read_to_string(&list).unwrap(),
            "-f4se OG 0.6.22\n+F4SE OG 0.6.23\n"
        );
        assert!(!list.with_extension("txt.tmp").exists());
    }
}
