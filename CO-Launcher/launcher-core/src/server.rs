use std::collections::BTreeMap;
use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use zip::ZipArchive;

use crate::channel::{compare_versions, is_version};
use crate::error::{Error, Result};
use crate::fsutil::{resolve_ci, sha256_file, sha256_hex, write_atomic};

pub const EXE: &str = "CommonwealthOnline.Server.exe";
pub const MOD: &str = "Commonwealth Online Server";
pub const VERSION_FILE: &str = ".co-server-version";
const SETTINGS: [&str; 2] = ["commonwealth-server.json", "world-policy.json"];
const HOST_DATA: [&str; 2] = ["workshop-zones.json", "blocked-words.txt"];
const DEFAULTS: &str = "launcher-defaults.json";
const STAGING: &str = ".co-install";
const HOST_RECORD: &str = "launcher-host.pid";
const MAX_FILE: u64 = 256 << 20;

pub fn dir(instance: &Path) -> PathBuf {
    resolve_ci(instance, &["mods", MOD, "server"])
}

pub fn installed(dir: &Path) -> bool {
    dir.join(EXE).is_file()
}

fn archive_error(error: impl ToString) -> Error {
    Error::Archive(error.to_string())
}

pub fn installed_version(dir: &Path) -> Option<String> {
    let text = fs::read_to_string(dir.join(VERSION_FILE)).ok()?;
    let text = text.trim();
    is_version(text).then(|| text.to_owned())
}

pub fn install(dir: &Path, bundle: &[u8], version: &str) -> Result<Vec<String>> {
    if bundle.is_empty() {
        return Ok(Vec::new());
    }
    let version = is_version(version).then_some(version);
    let keeps_newer = |have: String| version.is_some_and(|v| compare_versions(&have, v).is_gt());
    if installed(dir) && installed_version(dir).is_some_and(keeps_newer) {
        return Ok(Vec::new());
    }
    let mut archive = ZipArchive::new(Cursor::new(bundle)).map_err(archive_error)?;
    fs::create_dir_all(dir).map_err(Error::io(dir))?;
    let staging = dir.join(STAGING);
    let _ = fs::remove_dir_all(&staging);
    fs::create_dir_all(&staging).map_err(Error::io(&staging))?;
    let recorded = recorded_defaults(dir);
    let mut defaults = recorded.clone();
    let outcome =
        stage(&mut archive, dir, &staging, &mut defaults).and_then(|plan| swap(&plan, &staging));
    let _ = fs::remove_dir_all(&staging);
    let written = outcome?;
    if defaults != recorded {
        if let Ok(json) = serde_json::to_vec(&defaults) {
            let _ = write_atomic(&dir.join(DEFAULTS), &json);
        }
    }
    match version {
        Some(v) if installed_version(dir).as_deref() != Some(v) => {
            write_atomic(&dir.join(VERSION_FILE), v.as_bytes())?;
        }
        None if !written.is_empty() => {
            let _ = fs::remove_file(dir.join(VERSION_FILE));
        }
        _ => {}
    }
    Ok(written)
}

type Defaults = BTreeMap<String, String>;

fn recorded_defaults(dir: &Path) -> Defaults {
    fs::read(dir.join(DEFAULTS))
        .ok()
        .and_then(|bytes| serde_json::from_slice(&bytes).ok())
        .unwrap_or_default()
}

struct Planned {
    staged: PathBuf,
    destination: PathBuf,
}

fn stage(
    archive: &mut ZipArchive<Cursor<&[u8]>>,
    dir: &Path,
    staging: &Path,
    defaults: &mut Defaults,
) -> Result<Vec<Planned>> {
    let mut plan = Vec::new();
    let mut has_exe = false;
    for index in 0..archive.len() {
        let mut entry = archive.by_index(index).map_err(archive_error)?;
        let Some(name) = entry
            .is_file()
            .then(|| entry.enclosed_name())
            .flatten()
            .filter(|path| path.components().count() == 1)
            .and_then(|path| path.to_str().map(str::to_owned))
        else {
            continue;
        };
        if entry.size() > MAX_FILE {
            return Err(Error::Archive(format!("{name} is too large")));
        }
        has_exe |= name.eq_ignore_ascii_case(EXE);
        let mut bytes = Vec::new();
        entry
            .read_to_end(&mut bytes)
            .map_err(Error::io(dir.join(&name)))?;
        let want = sha256_hex(&bytes);
        let last = listed(&HOST_DATA, &name)
            .then(|| defaults.insert(name.clone(), want.clone()))
            .flatten();
        let Some(destination) = target_for(dir, &name, &want, last.as_deref()) else {
            continue;
        };
        let staged = staging.join(index.to_string());
        fs::write(&staged, &bytes).map_err(Error::io(&staged))?;
        plan.push(Planned {
            staged,
            destination,
        });
    }
    if !has_exe {
        return Err(Error::Archive(format!(
            "the bundled server package has no {EXE}"
        )));
    }
    Ok(plan)
}

fn target_for(dir: &Path, name: &str, want: &str, last: Option<&str>) -> Option<PathBuf> {
    let same = |path: &Path, hash: &str| sha256_file(path).is_ok_and(|have| have == hash);
    let target = dir.join(name);
    if listed(&SETTINGS, name) && target.exists() {
        return None;
    }
    if listed(&HOST_DATA, name) && target.exists() && !same(&target, want) {
        if last.is_some_and(|hash| same(&target, hash)) {
            return Some(target);
        }
        let beside = dir.join(format!("{name}.new"));
        return (!same(&beside, want)).then_some(beside);
    }
    (!same(&target, want)).then_some(target)
}

fn listed(list: &[&str], name: &str) -> bool {
    list.iter().any(|item| item.eq_ignore_ascii_case(name))
}

fn swap(plan: &[Planned], staging: &Path) -> Result<Vec<String>> {
    let previous = staging.join("previous");
    fs::create_dir_all(&previous).map_err(Error::io(&previous))?;
    let mut done: Vec<(&Path, Option<PathBuf>)> = Vec::new();
    let mut written = Vec::new();
    for (index, item) in plan.iter().enumerate() {
        let destination = item.destination.as_path();
        let backup = destination
            .exists()
            .then(|| previous.join(backup_name(index)));
        if let Some(backup) = &backup {
            if locked(destination) {
                restore(&done);
                return Err(Error::InUse(destination.display().to_string()));
            }
            if let Err(source) = fs::rename(destination, backup) {
                restore(&done);
                return Err(Error::Io {
                    path: destination.display().to_string(),
                    source,
                });
            }
        }
        done.push((destination, backup));
        if let Err(source) = fs::rename(&item.staged, destination) {
            restore(&done);
            return Err(Error::Io {
                path: destination.display().to_string(),
                source,
            });
        }
        if let Some(name) = destination.file_name() {
            written.push(name.to_string_lossy().into_owned());
        }
    }
    Ok(written)
}

fn restore(done: &[(&Path, Option<PathBuf>)]) {
    for (destination, backup) in done.iter().rev() {
        let _ = fs::remove_file(destination);
        if let Some(backup) = backup {
            let _ = fs::rename(backup, destination);
        }
    }
}

fn locked(path: &Path) -> bool {
    fs::OpenOptions::new().write(true).open(path).is_err()
}

fn backup_name(index: usize) -> String {
    let stamp = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map_or(0, |at| at.as_nanos());
    format!("{index}-{stamp}")
}

pub fn record_host(dir: &Path, pid: u32) -> Result<()> {
    write_atomic(&dir.join(HOST_RECORD), pid.to_string().as_bytes())
}

pub fn recorded_host(dir: &Path) -> Option<u32> {
    fs::read_to_string(dir.join(HOST_RECORD))
        .ok()?
        .trim()
        .parse()
        .ok()
}

pub fn forget_host(dir: &Path) {
    let _ = fs::remove_file(dir.join(HOST_RECORD));
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use zip::write::SimpleFileOptions;

    fn bundle(files: &[(&str, &[u8])]) -> Vec<u8> {
        let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
        for (name, bytes) in files {
            zip.start_file(*name, SimpleFileOptions::default()).unwrap();
            zip.write_all(bytes).unwrap();
        }
        zip.finish().unwrap().into_inner()
    }

    #[test]
    fn install_copies_program_files_and_keeps_the_hosts_settings() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        assert_eq!(dir, instance.path().join("mods").join(MOD).join("server"));
        let first = bundle(&[
            (EXE, b"v1"),
            ("commonwealth-server.json", b"{}"),
            ("../escape.txt", b"x"),
        ]);
        assert_eq!(install(&dir, &first, "").unwrap().len(), 2);
        assert!(installed(&dir) && !instance.path().join("escape.txt").exists());
        assert!(install(&dir, &first, "").unwrap().is_empty());
        fs::write(dir.join("commonwealth-server.json"), b"mine").unwrap();
        let second = bundle(&[(EXE, b"v2"), ("commonwealth-server.json", b"{}")]);
        assert_eq!(install(&dir, &second, "").unwrap(), [EXE]);
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v2");
        assert_eq!(
            fs::read(dir.join("commonwealth-server.json")).unwrap(),
            b"mine"
        );
        assert!(install(&dir, &[], "").unwrap().is_empty());
        assert!(install(&dir, &bundle(&[("wrapper/x.dll", b"x")]), "").is_err());
    }

    #[test]
    fn install_records_the_bundled_version_and_follows_a_newer_bundle() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        let v12 = bundle(&[(EXE, b"v12")]);
        let v13 = bundle(&[(EXE, b"v13")]);
        assert_eq!(installed_version(&dir), None);
        assert_eq!(install(&dir, &v12, "0.0.0.12").unwrap(), [EXE]);
        assert_eq!(installed_version(&dir).as_deref(), Some("0.0.0.12"));
        assert_eq!(install(&dir, &v13, "0.0.0.13").unwrap(), [EXE]);
        assert_eq!(installed_version(&dir).as_deref(), Some("0.0.0.13"));
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v13");
    }

    #[test]
    fn install_keeps_a_newer_recorded_server_when_the_bundle_is_older() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        install(&dir, &bundle(&[(EXE, b"v13")]), "0.0.0.13").unwrap();
        let stale = bundle(&[(EXE, b"v9")]);
        assert!(install(&dir, &stale, "0.0.0.9").unwrap().is_empty());
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v13");
        assert_eq!(installed_version(&dir).as_deref(), Some("0.0.0.13"));
        assert_eq!(install(&dir, &stale, "").unwrap(), [EXE]);
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v9");
        assert_eq!(installed_version(&dir), None);
        assert!(!dir.join(VERSION_FILE).exists());
    }

    #[test]
    fn install_reinstalls_when_the_recorded_server_exe_is_gone() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        install(&dir, &bundle(&[(EXE, b"v13")]), "0.0.0.13").unwrap();
        fs::remove_file(dir.join(EXE)).unwrap();
        assert!(!installed(&dir));
        let older = bundle(&[(EXE, b"v9")]);
        assert_eq!(install(&dir, &older, "0.0.0.9").unwrap(), [EXE]);
        assert!(installed(&dir));
        assert_eq!(installed_version(&dir).as_deref(), Some("0.0.0.9"));
    }

    #[test]
    fn install_accepts_a_program_file_larger_than_64_mib() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        let host = vec![0u8; 65 << 20];
        let bundled = bundle(&[(EXE, b"v1"), ("CommonwealthOnline.Host.exe", &host)]);
        assert_eq!(install(&dir, &bundled, "").unwrap().len(), 2);
        assert_eq!(
            fs::metadata(dir.join("CommonwealthOnline.Host.exe"))
                .unwrap()
                .len(),
            65 << 20
        );
    }

    #[test]
    fn a_failed_install_leaves_the_program_files_alone() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        install(
            &dir,
            &bundle(&[(EXE, b"v1"), ("CommonwealthOnline.Host.dll", b"v1")]),
            "",
        )
        .unwrap();
        let stored =
            || SimpleFileOptions::default().compression_method(zip::CompressionMethod::Stored);
        let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
        zip.start_file(EXE, stored()).unwrap();
        zip.write_all(b"v2").unwrap();
        zip.start_file("late.dll", stored()).unwrap();
        zip.write_all(b"LATE-PAYLOAD").unwrap();
        let mut late = zip.finish().unwrap().into_inner();
        let at = late.windows(12).position(|w| w == b"LATE-PAYLOAD").unwrap();
        late[at] ^= 0xFF;
        assert!(install(&dir, &late, "").is_err());
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v1");
        assert!(!dir.join("late.dll").exists());
        let missing_exe = bundle(&[("CommonwealthOnline.Host.dll", b"v2")]);
        assert!(install(&dir, &missing_exe, "").is_err());
        assert_eq!(
            fs::read(dir.join("CommonwealthOnline.Host.dll")).unwrap(),
            b"v1"
        );
        assert!(!dir.join(STAGING).exists());
    }

    #[test]
    fn host_edited_data_files_are_kept_and_the_new_default_goes_beside_them() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        install(
            &dir,
            &bundle(&[
                (EXE, b"v1"),
                ("workshop-zones.json", b"sample"),
                ("blocked-words.txt", b"a"),
            ]),
            "",
        )
        .unwrap();
        fs::write(dir.join("workshop-zones.json"), b"mine").unwrap();
        let next = bundle(&[
            (EXE, b"v1"),
            ("workshop-zones.json", b"sample2"),
            ("blocked-words.txt", b"a"),
        ]);
        assert_eq!(
            install(&dir, &next, "").unwrap(),
            ["workshop-zones.json.new"]
        );
        assert_eq!(fs::read(dir.join("workshop-zones.json")).unwrap(), b"mine");
        assert_eq!(
            fs::read(dir.join("workshop-zones.json.new")).unwrap(),
            b"sample2"
        );
        assert!(install(&dir, &next, "").unwrap().is_empty());
    }

    #[test]
    fn an_unedited_data_file_takes_the_new_default() {
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        install(
            &dir,
            &bundle(&[(EXE, b"v1"), ("workshop-zones.json", b"sample")]),
            "",
        )
        .unwrap();
        let next = bundle(&[(EXE, b"v1"), ("workshop-zones.json", b"sample2")]);
        assert_eq!(install(&dir, &next, "").unwrap(), ["workshop-zones.json"]);
        assert_eq!(
            fs::read(dir.join("workshop-zones.json")).unwrap(),
            b"sample2"
        );
        assert!(!dir.join("workshop-zones.json.new").exists());
    }

    #[cfg(windows)]
    #[test]
    fn an_install_refuses_to_replace_a_file_another_process_holds() {
        use std::os::windows::fs::OpenOptionsExt;
        let instance = tempfile::tempdir().unwrap();
        let dir = dir(instance.path());
        install(
            &dir,
            &bundle(&[(EXE, b"v1"), ("CommonwealthOnline.Host.dll", b"v1")]),
            "",
        )
        .unwrap();
        let held = fs::OpenOptions::new()
            .read(true)
            .share_mode(0)
            .open(dir.join(EXE))
            .unwrap();
        let next = bundle(&[(EXE, b"v2"), ("CommonwealthOnline.Host.dll", b"v2")]);
        assert!(matches!(install(&dir, &next, ""), Err(Error::InUse(_))));
        drop(held);
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v1");
        assert_eq!(
            fs::read(dir.join("CommonwealthOnline.Host.dll")).unwrap(),
            b"v1"
        );
    }

    #[test]
    fn the_launcher_remembers_only_the_host_it_started() {
        let dir = tempfile::tempdir().unwrap();
        assert_eq!(recorded_host(dir.path()), None);
        record_host(dir.path(), 4242).unwrap();
        assert_eq!(recorded_host(dir.path()), Some(4242));
        forget_host(dir.path());
        assert_eq!(recorded_host(dir.path()), None);
    }
}
