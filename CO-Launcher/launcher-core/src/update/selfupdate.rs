use std::fs;
use std::io::{Read, Seek};
use std::path::{Component, Path, PathBuf};
use std::time::Duration;

use zip::ZipArchive;

use crate::error::{Error, Result};

pub const EXE: &str = "CommonwealthOnlineLauncher.exe";
const BUNDLE_DIR: &str = "CommonwealthOnline-Launcher-";
pub const MAX_EXE: u64 = 128 << 20;
const STEP: Duration = Duration::from_millis(250);
const TRIES: u32 = 40;

pub trait Sys {
    fn writable(&self, dir: &Path) -> bool;
    fn rename(&self, from: &Path, to: &Path) -> std::io::Result<()>;
    fn remove(&self, path: &Path) -> std::io::Result<()>;
    fn exists(&self, path: &Path) -> bool;
    fn alive(&self, pid: u32) -> bool;
    fn sleep(&self, step: Duration);
}

pub struct Real;

impl Sys for Real {
    fn writable(&self, dir: &Path) -> bool {
        let probe = dir.join(".co-update-probe");
        let writable = fs::write(&probe, b"").is_ok();
        let _ = fs::remove_file(&probe);
        writable
    }

    fn rename(&self, from: &Path, to: &Path) -> std::io::Result<()> {
        fs::rename(from, to)
    }

    fn remove(&self, path: &Path) -> std::io::Result<()> {
        fs::remove_file(path)
    }

    fn exists(&self, path: &Path) -> bool {
        path.exists()
    }

    fn alive(&self, pid: u32) -> bool {
        process_alive(pid)
    }

    fn sleep(&self, step: Duration) {
        std::thread::sleep(step);
    }
}

#[cfg(windows)]
fn process_alive(pid: u32) -> bool {
    use std::os::windows::process::CommandExt;
    const NO_WINDOW: u32 = 0x0800_0000;
    let root = std::env::var_os("SystemRoot").unwrap_or_else(|| "C:\\Windows".into());
    let tasklist = PathBuf::from(root).join("System32").join("tasklist.exe");
    let filter = format!("PID eq {pid}");
    match std::process::Command::new(tasklist)
        .args(["/FI", filter.as_str(), "/NH", "/FO", "CSV"])
        .creation_flags(NO_WINDOW)
        .output()
    {
        Ok(out) => String::from_utf8_lossy(&out.stdout).contains(&format!("\"{pid}\"")),
        Err(_) => true,
    }
}

#[cfg(not(windows))]
fn process_alive(pid: u32) -> bool {
    Path::new("/proc").join(pid.to_string()).exists()
}

fn archive_error(error: impl std::fmt::Display) -> Error {
    Error::Update(format!("the launcher archive is not valid: {error}"))
}

fn is_launcher_path(path: &Path) -> bool {
    let names: Vec<&str> = path
        .components()
        .filter_map(|part| match part {
            Component::Normal(name) => name.to_str(),
            _ => None,
        })
        .collect();
    match names.as_slice() {
        [file] => file.eq_ignore_ascii_case(EXE),
        [dir, file] => file.eq_ignore_ascii_case(EXE) && dir.starts_with(BUNDLE_DIR),
        _ => false,
    }
}

pub fn exe_from_zip<R: Read + Seek>(reader: R) -> Result<Vec<u8>> {
    let mut archive = ZipArchive::new(reader).map_err(archive_error)?;
    let mut found = None;
    for index in 0..archive.len() {
        let entry = archive.by_index(index).map_err(archive_error)?;
        let Some(path) = entry.enclosed_name() else {
            return Err(Error::Update(
                "the launcher archive has an unsafe file name".to_owned(),
            ));
        };
        if entry.is_file() && is_launcher_path(&path) && found.replace(index).is_some() {
            return Err(Error::Update(
                "the launcher archive holds more than one launcher".to_owned(),
            ));
        }
    }
    let index = found.ok_or_else(|| Error::Update(format!("the launcher archive has no {EXE}")))?;
    let mut entry = archive.by_index(index).map_err(archive_error)?;
    if entry.size() > MAX_EXE {
        return Err(Error::Update(
            "the launcher executable is too large".to_owned(),
        ));
    }
    let mut bytes = Vec::new();
    (&mut entry)
        .take(MAX_EXE + 1)
        .read_to_end(&mut bytes)
        .map_err(archive_error)?;
    if bytes.len() as u64 > MAX_EXE || !bytes.starts_with(b"MZ") {
        return Err(Error::Update(
            "the launcher archive does not hold a Windows executable".to_owned(),
        ));
    }
    Ok(bytes)
}

fn suffixed(path: &Path, suffix: &str) -> PathBuf {
    let mut name = path.as_os_str().to_owned();
    name.push(suffix);
    PathBuf::from(name)
}

pub fn swap(sys: &dyn Sys, exe: &Path, bytes: &[u8]) -> Result<()> {
    let dir = exe
        .parent()
        .ok_or_else(|| Error::Update("the launcher path has no folder".to_owned()))?;
    if !sys.writable(dir) {
        return Err(Error::Update(
            "notify only: the launcher folder is read-only, so the update was not installed"
                .to_owned(),
        ));
    }
    let new = suffixed(exe, ".new");
    let old = suffixed(exe, ".old");
    fs::write(&new, bytes).map_err(|source| {
        let _ = fs::remove_file(&new);
        Error::Io {
            path: new.display().to_string(),
            source,
        }
    })?;
    if let Err(source) = sys.rename(exe, &old) {
        let _ = sys.remove(&new);
        return Err(Error::Io {
            path: exe.display().to_string(),
            source,
        });
    }
    if let Err(source) = sys.rename(&new, exe) {
        let restored = sys.rename(&old, exe).is_ok();
        let _ = sys.remove(&new);
        return Err(Error::Update(if restored {
            format!(
                "could not put the new launcher in place ({source}); the previous version was kept"
            )
        } else {
            format!(
                "could not put the new launcher in place ({source}) and the previous version could not be restored from {}",
                old.display()
            )
        }));
    }
    Ok(())
}

pub fn recover(sys: &dyn Sys, exe: &Path) -> bool {
    if sys.exists(exe) {
        return false;
    }
    let old = suffixed(exe, ".old");
    if !sys.exists(&old) {
        return false;
    }
    let new = suffixed(exe, ".new");
    if sys.exists(&new) {
        let _ = sys.remove(&new);
    }
    sys.rename(&old, exe).is_ok()
}

pub fn finish(sys: &dyn Sys, exe: &Path, old_pid: Option<u32>) -> bool {
    if !sys.exists(exe) {
        return false;
    }
    if let Some(pid) = old_pid {
        for _ in 0..TRIES {
            if !sys.alive(pid) {
                break;
            }
            sys.sleep(STEP);
        }
    }
    let leftovers = [suffixed(exe, ".old"), suffixed(exe, ".new")];
    for _ in 0..TRIES {
        let mut pending = false;
        for path in &leftovers {
            if sys.exists(path) && sys.remove(path).is_err() {
                pending = true;
            }
        }
        if !pending {
            return true;
        }
        sys.sleep(STEP);
    }
    false
}

#[cfg(test)]
pub(crate) mod fake {
    use std::cell::{Cell, RefCell};

    use super::*;

    pub struct FakeSys {
        pub writable: bool,
        pub fail_rename_to: RefCell<Option<PathBuf>>,
        pub alive_for: Cell<u32>,
        pub fail_removes: Cell<u32>,
        pub sleeps: Cell<u32>,
    }

    impl Default for FakeSys {
        fn default() -> Self {
            FakeSys {
                writable: true,
                fail_rename_to: RefCell::new(None),
                alive_for: Cell::new(0),
                fail_removes: Cell::new(0),
                sleeps: Cell::new(0),
            }
        }
    }

    impl Sys for FakeSys {
        fn writable(&self, _dir: &Path) -> bool {
            self.writable
        }

        fn rename(&self, from: &Path, to: &Path) -> std::io::Result<()> {
            if self.fail_rename_to.borrow().as_deref() == Some(to) {
                *self.fail_rename_to.borrow_mut() = None;
                return Err(std::io::Error::other("locked"));
            }
            fs::rename(from, to)
        }

        fn remove(&self, path: &Path) -> std::io::Result<()> {
            if self.fail_removes.get() > 0 {
                self.fail_removes.set(self.fail_removes.get() - 1);
                return Err(std::io::Error::other("sharing violation"));
            }
            fs::remove_file(path)
        }

        fn exists(&self, path: &Path) -> bool {
            path.exists()
        }

        fn alive(&self, _pid: u32) -> bool {
            if self.alive_for.get() > 0 {
                self.alive_for.set(self.alive_for.get() - 1);
                return true;
            }
            false
        }

        fn sleep(&self, _step: Duration) {
            self.sleeps.set(self.sleeps.get() + 1);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::fake::FakeSys;
    use super::*;
    use std::io::{Cursor, Write};
    use zip::write::SimpleFileOptions;

    fn zip_of(entries: &[(&str, &[u8])]) -> Vec<u8> {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        for (name, bytes) in entries {
            writer
                .start_file(*name, SimpleFileOptions::default())
                .unwrap();
            writer.write_all(bytes).unwrap();
        }
        writer.finish().unwrap().into_inner()
    }

    fn exe_bytes(tag: &[u8]) -> Vec<u8> {
        [b"MZ".as_slice(), tag].concat()
    }

    fn installed(dir: &Path) -> PathBuf {
        let exe = dir.join(EXE);
        fs::write(&exe, exe_bytes(b"old")).unwrap();
        exe
    }

    #[test]
    fn only_the_launcher_executable_is_taken_from_the_bundle() {
        let zip = zip_of(&[
            ("CommonwealthOnline-Launcher-0.0.0.8/README.txt", b"hi"),
            (
                "CommonwealthOnline-Launcher-0.0.0.8/CommonwealthOnlineLauncher.exe",
                &exe_bytes(b"new"),
            ),
        ]);
        assert_eq!(exe_from_zip(Cursor::new(zip)).unwrap(), exe_bytes(b"new"));
    }

    #[test]
    fn unsafe_names_duplicates_and_non_executables_are_refused() {
        let traversal = zip_of(&[("../CommonwealthOnlineLauncher.exe", &exe_bytes(b"x"))]);
        assert!(exe_from_zip(Cursor::new(traversal)).is_err());
        let nested = zip_of(&[("a/b/c/CommonwealthOnlineLauncher.exe", &exe_bytes(b"x"))]);
        assert!(exe_from_zip(Cursor::new(nested)).is_err());
        let twice = zip_of(&[
            ("CommonwealthOnlineLauncher.exe", &exe_bytes(b"x")),
            (
                "CommonwealthOnline-Launcher-1/CommonwealthOnlineLauncher.exe",
                &exe_bytes(b"y"),
            ),
        ]);
        assert!(exe_from_zip(Cursor::new(twice)).is_err());
        let not_pe = zip_of(&[("CommonwealthOnlineLauncher.exe", b"plain text")]);
        assert!(exe_from_zip(Cursor::new(not_pe)).is_err());
        let missing = zip_of(&[("README.txt", b"x")]);
        assert!(exe_from_zip(Cursor::new(missing)).is_err());
    }

    #[test]
    fn a_swap_puts_the_new_exe_in_place_and_keeps_the_old_as_dot_old() {
        let dir = tempfile::tempdir().unwrap();
        let exe = installed(dir.path());
        swap(&Real, &exe, &exe_bytes(b"new")).unwrap();
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"new"));
        assert_eq!(fs::read(suffixed(&exe, ".old")).unwrap(), exe_bytes(b"old"));
        assert!(!suffixed(&exe, ".new").exists());
    }

    #[test]
    fn a_failed_second_rename_restores_the_old_exe() {
        let dir = tempfile::tempdir().unwrap();
        let exe = installed(dir.path());
        let sys = FakeSys::default();
        *sys.fail_rename_to.borrow_mut() = Some(exe.clone());
        let error = swap(&sys, &exe, &exe_bytes(b"new")).unwrap_err();
        assert!(error.to_string().contains("previous version was kept"));
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"old"));
        assert!(!suffixed(&exe, ".old").exists());
        assert!(!suffixed(&exe, ".new").exists());
    }

    #[test]
    fn a_read_only_folder_is_notify_only_and_nothing_is_touched() {
        let dir = tempfile::tempdir().unwrap();
        let exe = installed(dir.path());
        let sys = FakeSys {
            writable: false,
            ..FakeSys::default()
        };
        let error = swap(&sys, &exe, &exe_bytes(b"new")).unwrap_err();
        assert!(error.to_string().starts_with("notify only"));
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"old"));
        assert!(!suffixed(&exe, ".new").exists());
    }

    #[test]
    fn finish_waits_for_the_old_process_and_removes_leftovers() {
        let dir = tempfile::tempdir().unwrap();
        let exe = installed(dir.path());
        fs::write(suffixed(&exe, ".old"), b"old").unwrap();
        fs::write(suffixed(&exe, ".new"), b"stale").unwrap();
        let sys = FakeSys {
            alive_for: std::cell::Cell::new(2),
            fail_removes: std::cell::Cell::new(1),
            ..FakeSys::default()
        };
        assert!(finish(&sys, &exe, Some(4242)));
        assert!(!suffixed(&exe, ".old").exists());
        assert!(!suffixed(&exe, ".new").exists());
        assert!(sys.sleeps.get() >= 3);
        assert!(exe.exists());
    }

    #[test]
    fn finish_gives_up_after_a_bounded_number_of_tries() {
        let dir = tempfile::tempdir().unwrap();
        let exe = installed(dir.path());
        fs::write(suffixed(&exe, ".old"), b"old").unwrap();
        let sys = FakeSys {
            fail_removes: std::cell::Cell::new(u32::MAX),
            ..FakeSys::default()
        };
        assert!(!finish(&sys, &exe, Some(4242)));
        assert!(sys.sleeps.get() <= TRIES * 2);
    }

    #[test]
    fn a_crash_between_the_renames_restores_the_old_exe() {
        let dir = tempfile::tempdir().unwrap();
        let exe = dir.path().join(EXE);
        fs::write(suffixed(&exe, ".old"), exe_bytes(b"old")).unwrap();
        fs::write(suffixed(&exe, ".new"), exe_bytes(b"new")).unwrap();
        assert!(recover(&Real, &exe));
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"old"));
        assert!(!suffixed(&exe, ".old").exists());
        assert!(!suffixed(&exe, ".new").exists());
    }

    #[test]
    fn a_completed_swap_is_left_for_finish_and_a_clean_start_does_nothing() {
        let dir = tempfile::tempdir().unwrap();
        let exe = installed(dir.path());
        assert!(!recover(&Real, &exe));
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"old"));
        fs::write(suffixed(&exe, ".old"), exe_bytes(b"old")).unwrap();
        fs::write(&exe, exe_bytes(b"new")).unwrap();
        assert!(!recover(&Real, &exe));
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"new"));
        assert!(suffixed(&exe, ".old").exists());
        assert!(finish(&Real, &exe, None));
        assert!(!suffixed(&exe, ".old").exists());
        assert_eq!(fs::read(&exe).unwrap(), exe_bytes(b"new"));
    }

    #[test]
    fn finish_never_deletes_the_only_copy() {
        let dir = tempfile::tempdir().unwrap();
        let exe = dir.path().join(EXE);
        fs::write(suffixed(&exe, ".old"), exe_bytes(b"old")).unwrap();
        assert!(!finish(&Real, &exe, None));
        assert_eq!(fs::read(suffixed(&exe, ".old")).unwrap(), exe_bytes(b"old"));
    }
}
