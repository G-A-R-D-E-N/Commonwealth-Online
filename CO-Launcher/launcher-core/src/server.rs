use std::fs;
use std::io::{Cursor, Read};
use std::path::{Path, PathBuf};

use zip::ZipArchive;

use crate::error::{Error, Result};
use crate::fsutil::{resolve_ci, sha256_file, sha256_hex};

pub const EXE: &str = "CommonwealthOnline.Server.exe";
pub const MOD: &str = "Commonwealth Online Server";
const SETTINGS: [&str; 2] = ["commonwealth-server.json", "world-policy.json"];
const MAX_FILE: u64 = 64 << 20;

pub fn dir(instance: &Path) -> PathBuf {
    resolve_ci(instance, &["mods", MOD, "server"])
}

pub fn installed(dir: &Path) -> bool {
    dir.join(EXE).is_file()
}

fn archive_error(error: impl ToString) -> Error {
    Error::Archive(error.to_string())
}

fn write_atomic(target: &Path, bytes: &[u8]) -> Result<()> {
    let temp = target.with_extension("co-tmp");
    fs::write(&temp, bytes).map_err(Error::io(&temp))?;
    fs::rename(&temp, target).map_err(|source| {
        let _ = fs::remove_file(&temp);
        Error::Io {
            path: target.display().to_string(),
            source,
        }
    })
}

pub fn install(dir: &Path, bundle: &[u8]) -> Result<Vec<String>> {
    if bundle.is_empty() {
        return Ok(Vec::new());
    }
    let mut archive = ZipArchive::new(Cursor::new(bundle)).map_err(archive_error)?;
    fs::create_dir_all(dir).map_err(Error::io(dir))?;
    let mut written = Vec::new();
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
        let target = dir.join(&name);
        let mut bytes = Vec::new();
        entry.read_to_end(&mut bytes).map_err(Error::io(&target))?;
        let settings = SETTINGS.iter().any(|s| s.eq_ignore_ascii_case(&name));
        let skip = if settings {
            target.exists()
        } else {
            sha256_file(&target).is_ok_and(|have| have == sha256_hex(&bytes))
        };
        if !skip {
            write_atomic(&target, &bytes)?;
            written.push(name);
        }
    }
    if !has_exe {
        return Err(Error::Archive(format!(
            "the bundled server package has no {EXE}"
        )));
    }
    Ok(written)
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
        assert_eq!(install(&dir, &first).unwrap().len(), 2);
        assert!(installed(&dir) && !instance.path().join("escape.txt").exists());
        assert!(install(&dir, &first).unwrap().is_empty());
        fs::write(dir.join("commonwealth-server.json"), b"mine").unwrap();
        let second = bundle(&[(EXE, b"v2"), ("commonwealth-server.json", b"{}")]);
        assert_eq!(install(&dir, &second).unwrap(), [EXE]);
        assert_eq!(fs::read(dir.join(EXE)).unwrap(), b"v2");
        assert_eq!(
            fs::read(dir.join("commonwealth-server.json")).unwrap(),
            b"mine"
        );
        assert!(install(&dir, &[]).unwrap().is_empty());
        assert!(install(&dir, &bundle(&[("wrapper/x.dll", b"x")])).is_err());
    }
}
