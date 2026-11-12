use std::cmp::Ordering;
use std::fs::{self, File};
use std::io::Read;
use std::path::Path;

use serde::de::DeserializeOwned;
use serde::{Deserialize, Serialize};

use crate::fsutil::find_ci;
use crate::modlist::{enabled_names, profile_modlist};
use crate::modmgr::read_bytes_capped;
use crate::runtime::Runtime;
use crate::versions::is_co_mod;

pub const SCHEMA: u32 = 1;
pub const WABBAJACK_MARKER: &str = "co-channel.json";
pub const STANDALONE_MARKER: &str = ".co-launcher.json";
const WABBAJACK: &str = "wabbajack";
const STANDALONE: &str = "standalone";
const MAX_MARKER: u64 = 64 << 10;
const MAX_TEXT: usize = 256;
const MAX_VERSION: usize = 32;
const MODS: &str = "mods";
const PORTABLE: &str = "portable.txt";
const WABBAJACK_TEXT: &str = "Created by Wabbajack";
const COMPILER_SUFFIX: &str = ".compiler_settings";
const UTF8_BOM: &[u8] = b"\xEF\xBB\xBF";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum InstallChannel {
    Wabbajack {
        modlist: Option<String>,
        version: Option<String>,
    },
    Standalone,
    Unknown,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ChannelDecision {
    pub channel: InstallChannel,
    pub reasons: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Wabbajack {
    pub modlist: Option<String>,
    pub version: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Standalone {
    pub version: String,
    pub runtime: Runtime,
    pub installed_unix: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct StandaloneMarker {
    pub schema: u32,
    pub channel: String,
    pub installed_version: String,
    pub runtime: String,
    pub installed_unix: u64,
}

#[derive(Deserialize)]
struct RawWabbajack {
    schema: u32,
    channel: String,
    modlist: Option<String>,
    version: Option<String>,
}

pub fn install_channel(instance: &Path) -> InstallChannel {
    decide(instance).channel
}

pub fn decide(instance: &Path) -> ChannelDecision {
    let mut reasons = Vec::new();
    let mut wabbajack = None;
    let mut standalone = Vec::new();
    for dir in co_mod_dirs(instance) {
        let folder = dir
            .file_name()
            .map(|n| n.to_string_lossy().into_owned())
            .unwrap_or_default();
        match read_wabbajack(&dir) {
            Some(marker) => {
                reasons.push(format!(
                    "{folder} holds a Wabbajack marker ({WABBAJACK_MARKER})"
                ));
                wabbajack.get_or_insert(marker);
            }
            None if find_ci(&dir, WABBAJACK_MARKER).is_some() => reasons.push(format!(
                "ignored {WABBAJACK_MARKER} in {folder}: it is not a valid marker"
            )),
            None => {}
        }
        match read_standalone(&dir) {
            Some(marker) => {
                reasons.push(format!(
                    "{folder} holds a launcher marker ({} {})",
                    marker.runtime.label(),
                    marker.version
                ));
                standalone.push(marker);
            }
            None if find_ci(&dir, STANDALONE_MARKER).is_some() => reasons.push(format!(
                "ignored {STANDALONE_MARKER} in {folder}: it is not a valid marker"
            )),
            None => {}
        }
    }
    let channel = match (wabbajack, standalone.is_empty()) {
        (Some(marker), true) => InstallChannel::Wabbajack {
            modlist: marker.modlist,
            version: marker.version,
        },
        (None, false) => InstallChannel::Standalone,
        (Some(_), false) => {
            reasons.push(
                "both a Wabbajack marker and a launcher marker are in this instance, so the install is unclear"
                    .to_owned(),
            );
            InstallChannel::Unknown
        }
        (None, true) => match weak_hint(instance) {
            Some(hint) => {
                reasons.push(hint);
                InstallChannel::Wabbajack {
                    modlist: None,
                    version: None,
                }
            }
            None => {
                reasons.push("no install marker or Wabbajack hint was found".to_owned());
                InstallChannel::Unknown
            }
        },
    };
    ChannelDecision { channel, reasons }
}

pub fn may_install_mod_files(channel: &InstallChannel, user_forced_notify_only: bool) -> bool {
    !user_forced_notify_only && *channel == InstallChannel::Standalone
}

pub fn may_adopt(instance: &Path) -> bool {
    decide(instance).channel == InstallChannel::Unknown
        && weak_hint(instance).is_none()
        && co_mod_dirs(instance)
            .iter()
            .all(|dir| find_ci(dir, WABBAJACK_MARKER).is_none())
}

pub fn installed_version(instance: &Path, profile: &str, runtime: Runtime) -> Option<String> {
    let enabled = profile_modlist(instance, profile)
        .and_then(|path| read_bytes_capped(&path))
        .map(|bytes| enabled_names(&String::from_utf8_lossy(&bytes)))
        .unwrap_or_default();
    let builds: Vec<(String, String)> = co_mod_dirs(instance)
        .iter()
        .filter_map(|dir| {
            let name = dir.file_name()?.to_string_lossy().into_owned();
            let marker = read_standalone(dir)?;
            (marker.runtime == runtime).then_some((name, marker.version))
        })
        .collect();
    let newest = |list: Vec<&(String, String)>| {
        list.into_iter()
            .max_by(|a, b| compare_versions(&a.1, &b.1))
            .map(|build| build.1.clone())
    };
    newest(
        builds
            .iter()
            .filter(|(name, _)| enabled.contains(&name.to_ascii_lowercase()))
            .collect(),
    )
    .or_else(|| newest(builds.iter().collect()))
}

pub fn read_wabbajack(dir: &Path) -> Option<Wabbajack> {
    let raw: RawWabbajack = find_ci(dir, WABBAJACK_MARKER).and_then(|p| read_json(&p))?;
    let text_ok = |t: &Option<String>| {
        t.as_deref()
            .is_none_or(|s| s.len() <= MAX_TEXT && !s.chars().any(char::is_control))
    };
    (raw.schema == SCHEMA
        && raw.channel == WABBAJACK
        && text_ok(&raw.modlist)
        && text_ok(&raw.version))
    .then_some(Wabbajack {
        modlist: raw.modlist,
        version: raw.version,
    })
}

pub fn read_standalone(dir: &Path) -> Option<Standalone> {
    let raw: StandaloneMarker = find_ci(dir, STANDALONE_MARKER).and_then(|p| read_json(&p))?;
    let runtime = Runtime::ALL
        .into_iter()
        .find(|r| r.label() == raw.runtime)?;
    (raw.schema == SCHEMA && raw.channel == STANDALONE && is_version(&raw.installed_version))
        .then_some(Standalone {
            version: raw.installed_version,
            runtime,
            installed_unix: raw.installed_unix,
        })
}

pub(crate) fn co_mod_dirs(instance: &Path) -> Vec<std::path::PathBuf> {
    let Some(mods) = find_ci(instance, MODS) else {
        return Vec::new();
    };
    let mut dirs: Vec<_> = fs::read_dir(&mods)
        .map(|entries| {
            entries
                .flatten()
                .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
                .filter(|e| e.file_name().to_str().is_some_and(is_co_mod))
                .map(|e| e.path())
                .collect()
        })
        .unwrap_or_default();
    dirs.sort();
    dirs
}

pub(crate) fn is_version(text: &str) -> bool {
    text.len() <= MAX_VERSION
        && text.split('.').all(|part| {
            !part.is_empty() && part.len() <= 9 && part.bytes().all(|b| b.is_ascii_digit())
        })
}

pub(crate) fn compare_versions(a: &str, b: &str) -> Ordering {
    let key = |text: &str| -> Vec<u32> {
        text.split('.')
            .map(|part| part.parse().unwrap_or(0))
            .collect()
    };
    let (a, b) = (key(a), key(b));
    (0..a.len().max(b.len()))
        .map(|i| a.get(i).unwrap_or(&0).cmp(b.get(i).unwrap_or(&0)))
        .find(|order| order.is_ne())
        .unwrap_or(Ordering::Equal)
}

fn weak_hint(instance: &Path) -> Option<String> {
    let portable = find_ci(instance, PORTABLE).and_then(|p| read_capped(&p));
    if portable.is_some_and(|bytes| String::from_utf8_lossy(&bytes).contains(WABBAJACK_TEXT)) {
        return Some(format!("{PORTABLE} says it was created by Wabbajack"));
    }
    fs::read_dir(instance).ok()?.flatten().find_map(|entry| {
        let name = entry.file_name().to_string_lossy().into_owned();
        (name.to_ascii_lowercase().ends_with(COMPILER_SUFFIX) && entry.path().is_file())
            .then(|| format!("{name} is a Wabbajack compiler settings file"))
    })
}

fn read_capped(path: &Path) -> Option<Vec<u8>> {
    if !fs::metadata(path).ok()?.is_file() {
        return None;
    }
    let mut bytes = Vec::new();
    File::open(path)
        .ok()?
        .take(MAX_MARKER + 1)
        .read_to_end(&mut bytes)
        .ok()?;
    (bytes.len() as u64 <= MAX_MARKER).then_some(bytes)
}

fn read_json<T: DeserializeOwned>(path: &Path) -> Option<T> {
    let bytes = read_capped(path)?;
    serde_json::from_slice(bytes.strip_prefix(UTF8_BOM).unwrap_or(&bytes)).ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    const WABBA: &str =
        r#"{"schema":1,"channel":"wabbajack","modlist":"Commonwealth Online","version":"2.0"}"#;

    fn put(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn standalone_text(version: &str, runtime: &str) -> String {
        format!(
            r#"{{"schema":1,"channel":"standalone","installed_version":"{version}","runtime":"{runtime}","installed_unix":1700000000}}"#
        )
    }

    fn mod_dir(instance: &Path, name: &str) -> PathBuf {
        instance.join("mods").join(name)
    }

    fn wabbajack_channel(modlist: Option<&str>, version: Option<&str>) -> InstallChannel {
        InstallChannel::Wabbajack {
            modlist: modlist.map(str::to_owned),
            version: version.map(str::to_owned),
        }
    }

    #[test]
    fn a_wabbajack_marker_alone_means_wabbajack() {
        let dir = tempfile::tempdir().unwrap();
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            WABBA,
        );
        assert_eq!(
            install_channel(dir.path()),
            wabbajack_channel(Some("Commonwealth Online"), Some("2.0"))
        );
    }

    #[test]
    fn a_launcher_marker_alone_means_standalone() {
        let dir = tempfile::tempdir().unwrap();
        put(
            &mod_dir(dir.path(), "Commonwealth Online NG 1.2").join(STANDALONE_MARKER),
            &standalone_text("1.2", "NG"),
        );
        assert_eq!(install_channel(dir.path()), InstallChannel::Standalone);
    }

    #[test]
    fn both_markers_in_one_instance_are_unknown() {
        let dir = tempfile::tempdir().unwrap();
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            WABBA,
        );
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 1.2").join(STANDALONE_MARKER),
            &standalone_text("1.2", "OG"),
        );
        let decision = decide(dir.path());
        assert_eq!(decision.channel, InstallChannel::Unknown);
        assert!(decision.reasons.iter().any(|r| r.contains("unclear")));
    }

    #[test]
    fn no_marker_with_a_wabbajack_hint_is_wabbajack_with_unknown_list() {
        let portable = tempfile::tempdir().unwrap();
        put(
            &portable.path().join("portable.txt"),
            "Created by Wabbajack 3.0\n",
        );
        assert_eq!(
            install_channel(portable.path()),
            wabbajack_channel(None, None)
        );
        let settings = tempfile::tempdir().unwrap();
        put(&settings.path().join("Modlist.compiler_settings"), "{}");
        assert_eq!(
            install_channel(settings.path()),
            wabbajack_channel(None, None)
        );
    }

    #[test]
    fn no_marker_and_no_hint_is_unknown() {
        let dir = tempfile::tempdir().unwrap();
        put(&dir.path().join("portable.txt"), "made by hand");
        let decision = decide(dir.path());
        assert_eq!(decision.channel, InstallChannel::Unknown);
        assert_eq!(decision.reasons.len(), 1);
    }

    #[test]
    fn a_launcher_marker_beats_the_weak_hint() {
        let dir = tempfile::tempdir().unwrap();
        put(&dir.path().join("portable.txt"), "Created by Wabbajack");
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 1.2").join(STANDALONE_MARKER),
            &standalone_text("1.2", "OG"),
        );
        assert_eq!(install_channel(dir.path()), InstallChannel::Standalone);
    }

    #[test]
    fn malformed_oversized_or_wrong_markers_do_not_count() {
        let bad = [
            "{ not json".to_owned(),
            " ".repeat((MAX_MARKER + 1) as usize),
            standalone_text("1.2", "OG").replace("\"schema\":1", "\"schema\":2"),
            standalone_text("1.2", "OG").replace("standalone", "wabbajack"),
            standalone_text("1.2", "XX"),
            standalone_text("1.2; rm", "OG"),
            standalone_text("", "OG"),
            r#"{"schema":1,"channel":"standalone","runtime":"OG","installed_unix":1}"#.to_owned(),
        ];
        for text in bad {
            let dir = tempfile::tempdir().unwrap();
            put(
                &mod_dir(dir.path(), "Commonwealth Online OG 1.2").join(STANDALONE_MARKER),
                &text,
            );
            assert_eq!(
                install_channel(dir.path()),
                InstallChannel::Unknown,
                "{}",
                text.len()
            );
        }
        let dir = tempfile::tempdir().unwrap();
        let text = WABBA.replace("\"schema\":1", "\"schema\":3");
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            &text,
        );
        assert_eq!(install_channel(dir.path()), InstallChannel::Unknown);
        let long = WABBA.replace("Commonwealth Online", &"x".repeat(MAX_TEXT + 1));
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            &long,
        );
        assert_eq!(install_channel(dir.path()), InstallChannel::Unknown);
    }

    #[test]
    fn a_marker_with_a_byte_order_mark_is_read() {
        let dir = tempfile::tempdir().unwrap();
        let mut bytes = UTF8_BOM.to_vec();
        bytes.extend(standalone_text("1.2", "AE").as_bytes());
        let folder = mod_dir(dir.path(), "Commonwealth Online AE 1.2");
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join(STANDALONE_MARKER), bytes).unwrap();
        assert_eq!(install_channel(dir.path()), InstallChannel::Standalone);
    }

    #[test]
    fn names_are_matched_case_insensitively() {
        let dir = tempfile::tempdir().unwrap();
        put(
            &mod_dir(dir.path(), "COMMONWEALTHONLINE ng 1.2").join(".CO-LAUNCHER.JSON"),
            &standalone_text("1.2", "NG"),
        );
        assert_eq!(install_channel(dir.path()), InstallChannel::Standalone);
        assert_eq!(
            installed_version(dir.path(), "Default", Runtime::Ng).as_deref(),
            Some("1.2")
        );
    }

    #[test]
    fn a_marker_outside_a_co_mod_folder_does_not_count_even_when_valid() {
        let dir = tempfile::tempdir().unwrap();
        put(
            &mod_dir(dir.path(), "Some Armor").join(STANDALONE_MARKER),
            &standalone_text("1.2", "OG"),
        );
        assert_eq!(install_channel(dir.path()), InstallChannel::Unknown);
        assert_eq!(installed_version(dir.path(), "Default", Runtime::Og), None);
    }

    #[test]
    fn installed_version_is_the_highest_valid_one_for_the_runtime() {
        let dir = tempfile::tempdir().unwrap();
        for (name, version, runtime) in [
            ("Commonwealth Online OG 1.2", "1.2", "OG"),
            ("Commonwealth Online OG 1.10", "1.10", "OG"),
            ("Commonwealth Online NG 9.0", "9.0", "NG"),
        ] {
            put(
                &mod_dir(dir.path(), name).join(STANDALONE_MARKER),
                &standalone_text(version, runtime),
            );
        }
        put(
            &mod_dir(dir.path(), "Commonwealth Online OG 3.0").join(STANDALONE_MARKER),
            &standalone_text("3.0", "NG"),
        );
        assert_eq!(
            installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
            Some("1.10")
        );
        assert_eq!(installed_version(dir.path(), "Default", Runtime::Ae), None);
    }

    #[test]
    fn the_build_enabled_in_the_profile_wins_over_a_newer_disabled_one() {
        let dir = tempfile::tempdir().unwrap();
        for (version, runtime) in [("1.2", "OG"), ("1.1", "OG")] {
            let name = format!("Commonwealth Online {runtime} {version}");
            put(
                &mod_dir(dir.path(), &name).join(STANDALONE_MARKER),
                &standalone_text(version, runtime),
            );
        }
        let list = dir
            .path()
            .join("profiles")
            .join("Default")
            .join("modlist.txt");
        put(
            &list,
            "+Main\n-Commonwealth Online OG 1.2\n+Commonwealth Online OG 1.1\n",
        );
        assert_eq!(
            installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
            Some("1.1")
        );
        put(
            &list,
            "+Main\n+Commonwealth Online OG 1.2\n-commonwealth online og 1.1\n",
        );
        assert_eq!(
            installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
            Some("1.2")
        );
        put(&list, "+Main\n");
        assert_eq!(
            installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
            Some("1.2")
        );
        assert_eq!(
            installed_version(dir.path(), "../Default", Runtime::Og).as_deref(),
            Some("1.2")
        );
    }

    #[test]
    fn only_an_unmarked_instance_without_a_wabbajack_trace_may_be_adopted() {
        let plain = tempfile::tempdir().unwrap();
        assert!(may_adopt(plain.path()));
        let wabbajack = tempfile::tempdir().unwrap();
        put(
            &mod_dir(wabbajack.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            WABBA,
        );
        assert!(!may_adopt(wabbajack.path()));
        let broken = tempfile::tempdir().unwrap();
        put(
            &mod_dir(broken.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            "{ not json",
        );
        assert!(!may_adopt(broken.path()));
        let hinted = tempfile::tempdir().unwrap();
        put(&hinted.path().join("portable.txt"), "Created by Wabbajack");
        assert!(!may_adopt(hinted.path()));
        let both = tempfile::tempdir().unwrap();
        put(
            &mod_dir(both.path(), "Commonwealth Online OG 2.0").join(WABBAJACK_MARKER),
            WABBA,
        );
        put(
            &mod_dir(both.path(), "Commonwealth Online OG 1.2").join(STANDALONE_MARKER),
            &standalone_text("1.2", "OG"),
        );
        assert!(!may_adopt(both.path()));
        let standalone = tempfile::tempdir().unwrap();
        put(
            &mod_dir(standalone.path(), "Commonwealth Online OG 1.2").join(STANDALONE_MARKER),
            &standalone_text("1.2", "OG"),
        );
        assert!(!may_adopt(standalone.path()));
    }

    #[test]
    fn only_a_standalone_channel_may_install_mod_files_and_only_unless_forced() {
        let standalone = InstallChannel::Standalone;
        assert!(may_install_mod_files(&standalone, false));
        assert!(!may_install_mod_files(&standalone, true));
        assert!(!may_install_mod_files(&InstallChannel::Unknown, false));
        assert!(!may_install_mod_files(
            &wabbajack_channel(None, None),
            false
        ));
    }

    #[test]
    fn version_text_and_order_follow_the_numbers() {
        assert!(is_version("1.10") && !is_version("1..0") && !is_version("1.0."));
        assert!(!is_version("v1") && !is_version("1.2-beta") && !is_version(""));
        assert_eq!(compare_versions("1.10", "1.9"), Ordering::Greater);
    }

    #[test]
    fn compare_versions_pads_missing_components() {
        assert_eq!(compare_versions("0.0.0.8", "0.0.0.8.0"), Ordering::Equal);
        assert_eq!(compare_versions("1.2.3.0", "1.2.3"), Ordering::Equal);
        assert_eq!(compare_versions("1.2.3.1", "1.2.3"), Ordering::Greater);
        assert_eq!(compare_versions("0.0.1", "0.0.0.99"), Ordering::Greater);
    }
}
