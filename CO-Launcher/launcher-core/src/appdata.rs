use std::fs;
use std::path::PathBuf;
use std::time::UNIX_EPOCH;

use serde::Serialize;

use crate::setup;

#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Entry {
    pub kind: &'static str,
    pub path: String,
    pub size_bytes: u64,
    pub modified_unix: u64,
    pub description: &'static str,
}

pub fn scan() -> Vec<Entry> {
    collect([
        (
            "setup",
            "Launcher setup choices and progress.",
            setup::user_file(),
        ),
        (
            "setup",
            "Record of the files copied into the game Data folder.",
            setup::loose_record_file(),
        ),
        (
            "launcher",
            "Last startup error from the launcher.",
            dirs::data_local_dir().map(|dir| {
                dir.join("CommonwealthOnline")
                    .join("launcher-gui-error.txt")
            }),
        ),
        (
            "game",
            "Server folder you saved for hosting.",
            dirs::document_dir().map(|dir| {
                dir.join("My Games")
                    .join("Fallout4")
                    .join("CommonwealthOnline")
                    .join("host-server-folder.json")
            }),
        ),
    ])
}

fn collect(
    candidates: impl IntoIterator<Item = (&'static str, &'static str, Option<PathBuf>)>,
) -> Vec<Entry> {
    candidates
        .into_iter()
        .filter_map(|(kind, description, path)| {
            let path = path?;
            let meta = fs::metadata(&path).ok().filter(|meta| meta.is_file())?;
            let modified_unix = meta
                .modified()
                .ok()
                .and_then(|time| time.duration_since(UNIX_EPOCH).ok())
                .map_or(0, |age| age.as_secs());
            Some(Entry {
                kind,
                path: path.display().to_string(),
                size_bytes: meta.len(),
                modified_unix,
                description,
            })
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn lists_only_the_files_that_exist_with_their_size() {
        let temp = tempfile::tempdir().unwrap();
        let present = temp.path().join("launcher-setup.json");
        fs::write(&present, b"{}").unwrap();
        let entries = collect([
            ("setup", "present", Some(present.clone())),
            (
                "game",
                "missing",
                Some(temp.path().join("host-server-folder.json")),
            ),
            ("launcher", "no path", None),
            ("launcher", "a folder", Some(temp.path().to_path_buf())),
        ]);
        assert_eq!(entries.len(), 1);
        assert_eq!(entries[0].kind, "setup");
        assert_eq!(entries[0].size_bytes, 2);
        assert_eq!(entries[0].path, present.display().to_string());
        assert!(entries[0].modified_unix > 0);
    }
}
