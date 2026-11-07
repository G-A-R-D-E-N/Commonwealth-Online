use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};

use crate::error::Result;
use crate::fsutil::write_atomic;

pub const SCHEMA: u32 = 1;
const MAX_STATE: u64 = 64 << 10;

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default)]
pub struct State {
    pub schema: u32,
    pub etag: Option<String>,
    pub last_check_unix: Option<u64>,
    pub last_seen_version: Option<String>,
    pub last_seen_prerelease: bool,
    pub last_seen_launcher: bool,
    pub last_seen_mod: bool,
    pub last_seen_notes: Option<String>,
    pub skipped_version: Option<String>,
    pub include_prereleases: bool,
    pub auto_install: bool,
    pub notify_only: bool,
}

impl Default for State {
    fn default() -> Self {
        State {
            schema: SCHEMA,
            etag: None,
            last_check_unix: None,
            last_seen_version: None,
            last_seen_prerelease: false,
            last_seen_launcher: false,
            last_seen_mod: false,
            last_seen_notes: None,
            skipped_version: None,
            include_prereleases: true,
            auto_install: false,
            notify_only: false,
        }
    }
}

pub fn load(path: &Path) -> State {
    let parsed = fs::metadata(path)
        .ok()
        .filter(|meta| meta.len() <= MAX_STATE)
        .and_then(|_| fs::read(path).ok())
        .and_then(|bytes| serde_json::from_slice::<State>(&bytes).ok());
    match parsed {
        Some(state) if state.schema == SCHEMA => state,
        _ => State::default(),
    }
}

pub fn save(path: &Path, state: &State) -> Result<()> {
    let bytes = serde_json::to_vec_pretty(state).map_err(|e| {
        crate::error::Error::Update(format!("could not encode the update state: {e}"))
    })?;
    write_atomic(path, &bytes)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn missing_corrupt_or_foreign_files_give_defaults() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("launcher-state.json");
        assert_eq!(load(&path), State::default());
        fs::write(&path, b"{ this is not json").unwrap();
        assert_eq!(load(&path), State::default());
        fs::write(&path, br#"{"schema":7,"etag":"x"}"#).unwrap();
        assert_eq!(load(&path), State::default());
        fs::write(&path, vec![b' '; (MAX_STATE + 1) as usize]).unwrap();
        assert_eq!(load(&path), State::default());
    }

    #[test]
    fn defaults_include_prereleases_and_no_auto_install() {
        let state = State::default();
        assert!(state.include_prereleases);
        assert!(!state.auto_install);
        assert!(!state.notify_only);
    }

    #[test]
    fn a_saved_state_round_trips_and_partial_files_fill_defaults() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("sub").join("launcher-state.json");
        let state = State {
            etag: Some("\"abc\"".to_owned()),
            skipped_version: Some("0.0.0.8".to_owned()),
            include_prereleases: false,
            ..State::default()
        };
        save(&path, &state).unwrap();
        assert_eq!(load(&path), state);
        fs::write(&path, br#"{"schema":1,"auto_install":true}"#).unwrap();
        let partial = load(&path);
        assert!(partial.auto_install && partial.include_prereleases);
    }
}
