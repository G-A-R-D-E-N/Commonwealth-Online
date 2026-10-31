use std::fs;
use std::path::Path;

use crate::fsutil::{find_ci, resolve_ci};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ModSwitch {
    Already,
    Done,
    Pending,
    Unreadable,
}

pub fn enable_mod(instance: &Path, profile: &str, name: &str, apply: bool) -> ModSwitch {
    let dir = resolve_ci(instance, &["profiles", profile]);
    let Some(path) = find_ci(&dir, "modlist.txt") else {
        return ModSwitch::Unreadable;
    };
    let Ok(text) = fs::read_to_string(&path) else {
        return ModSwitch::Unreadable;
    };
    let eol = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let (mut found, mut changed) = (false, false);
    let mut lines: Vec<String> = text
        .lines()
        .map(|line| {
            let listed = line.get(1..).filter(|n| n.eq_ignore_ascii_case(name));
            match (line.as_bytes().first(), listed) {
                (Some(b'+'), Some(_)) => found = true,
                (Some(b'-'), Some(_)) => (found, changed) = (true, true),
                _ => return line.to_owned(),
            }
            format!("+{}", &line[1..])
        })
        .collect();
    if !found {
        lines.push(format!("+{name}"));
        changed = true;
    }
    if !changed {
        return ModSwitch::Already;
    }
    if !apply {
        return ModSwitch::Pending;
    }
    let temp = path.with_extension("txt.tmp");
    let done = fs::write(&temp, lines.join(eol) + eol).is_ok() && fs::rename(&temp, &path).is_ok();
    if done {
        ModSwitch::Done
    } else {
        let _ = fs::remove_file(&temp);
        ModSwitch::Pending
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn instance(modlist: &str) -> (tempfile::TempDir, std::path::PathBuf) {
        let dir = tempfile::tempdir().unwrap();
        let profile = dir.path().join("profiles").join("Default");
        fs::create_dir_all(&profile).unwrap();
        let path = profile.join("modlist.txt");
        fs::write(&path, modlist).unwrap();
        (dir, path)
    }

    #[test]
    fn a_missing_mod_is_appended_a_disabled_one_is_flipped_and_an_enabled_one_is_left() {
        let (dir, path) = instance("# header\r\n+Main\r\n-Commonwealth Online Server\r\n");
        assert_eq!(
            enable_mod(dir.path(), "Default", "commonwealth online server", false),
            ModSwitch::Pending
        );
        assert!(fs::read_to_string(&path).unwrap().contains("-Commonwealth"));
        assert_eq!(
            enable_mod(dir.path(), "default", "Commonwealth Online Server", true),
            ModSwitch::Done
        );
        assert_eq!(
            fs::read_to_string(&path).unwrap(),
            "# header\r\n+Main\r\n+Commonwealth Online Server\r\n"
        );
        assert_eq!(
            enable_mod(dir.path(), "Default", "Commonwealth Online Server", true),
            ModSwitch::Already
        );
        let (dir, path) = instance("+Main\n");
        assert_eq!(
            enable_mod(dir.path(), "Default", "Server", true),
            ModSwitch::Done
        );
        assert_eq!(fs::read_to_string(&path).unwrap(), "+Main\n+Server\n");
    }

    #[test]
    fn only_the_exact_entry_changes_and_everything_else_is_kept() {
        let kept = "\u{feff}# c\n*sep\n_sep\n+\n-\n\n+Server Extra\n-ünï\n+Main";
        let (dir, path) = instance(kept);
        assert_eq!(
            enable_mod(dir.path(), "Default", "Server", true),
            ModSwitch::Done
        );
        assert_eq!(
            fs::read_to_string(&path).unwrap(),
            format!("{kept}\n+Server\n")
        );
        let (dir, path) = instance("-Server\n+Main");
        assert_eq!(
            enable_mod(dir.path(), "Default", "SERVER", true),
            ModSwitch::Done
        );
        assert_eq!(fs::read_to_string(&path).unwrap(), "+Server\n+Main\n");
    }

    #[test]
    fn a_profile_without_a_readable_modlist_is_left_alone() {
        let dir = tempfile::tempdir().unwrap();
        assert_eq!(
            enable_mod(dir.path(), "Default", "Server", true),
            ModSwitch::Unreadable
        );
    }
}
