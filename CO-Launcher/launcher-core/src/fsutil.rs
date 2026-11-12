use std::fs;
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

use crate::error::{Error, Result};

pub fn find_ci(dir: &Path, name: &str) -> Option<PathBuf> {
    let exact = dir.join(name);
    if exact.exists() {
        return Some(exact);
    }
    fs::read_dir(dir).ok()?.flatten().find_map(|entry| {
        entry
            .file_name()
            .to_str()
            .filter(|n| n.eq_ignore_ascii_case(name))
            .map(|_| entry.path())
    })
}

pub fn resolve_ci(base: &Path, parts: &[&str]) -> PathBuf {
    let mut path = base.to_path_buf();
    for part in parts {
        path = find_ci(&path, part).unwrap_or_else(|| path.join(part));
    }
    path
}

pub fn sha256_hex(bytes: &[u8]) -> String {
    hex(&Sha256::digest(bytes))
}

pub fn sha256_file(path: &Path) -> Result<String> {
    let mut file = fs::File::open(path).map_err(Error::io(path))?;
    let mut hasher = Sha256::new();
    let mut buffer = [0u8; 1 << 16];
    loop {
        let n = file.read(&mut buffer).map_err(Error::io(path))?;
        if n == 0 {
            break;
        }
        hasher.update(&buffer[..n]);
    }
    Ok(hex(&hasher.finalize()))
}

fn temp_path(target: &Path) -> PathBuf {
    let mut name = target.as_os_str().to_owned();
    name.push(".co-tmp");
    PathBuf::from(name)
}

pub fn write_atomic(target: &Path, bytes: &[u8]) -> Result<()> {
    if let Some(parent) = target.parent() {
        fs::create_dir_all(parent).map_err(Error::io(parent))?;
    }
    let temp = temp_path(target);
    let staged = (|| -> Result<()> {
        let mut file = fs::File::create(&temp).map_err(Error::io(&temp))?;
        file.write_all(bytes).map_err(Error::io(&temp))?;
        file.sync_all().map_err(Error::io(&temp))
    })();
    if staged.is_err() {
        let _ = fs::remove_file(&temp);
        return staged;
    }
    fs::rename(&temp, target).map_err(|source| {
        let _ = fs::remove_file(&temp);
        Error::Io {
            path: target.display().to_string(),
            source,
        }
    })
}

pub fn hex(bytes: &[u8]) -> String {
    use std::fmt::Write;
    bytes
        .iter()
        .fold(String::with_capacity(bytes.len() * 2), |mut s, b| {
            let _ = write!(s, "{b:02x}");
            s
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha256_known_vectors() {
        assert_eq!(
            sha256_hex(b""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        );
        assert_eq!(
            sha256_hex(b"a"),
            "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb"
        );
    }

    #[test]
    fn find_ci_matches_other_case() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join("Fallout4.EXE"), b"x").unwrap();
        assert!(find_ci(dir.path(), "fallout4.exe").is_some());
        assert!(find_ci(dir.path(), "missing.exe").is_none());
    }

    #[test]
    fn temp_names_keep_the_full_file_name() {
        let json = temp_path(Path::new("dir/x.json"));
        let txt = temp_path(Path::new("dir/x.txt"));
        assert_eq!(json, PathBuf::from("dir/x.json.co-tmp"));
        assert_ne!(json, txt);
    }

    #[test]
    fn write_atomic_keeps_sibling_files_intact() {
        let dir = tempfile::tempdir().unwrap();
        let json = dir.path().join("x.json");
        let txt = dir.path().join("x.txt");
        let old_temp = dir.path().join("x.co-tmp");
        fs::write(&txt, b"text").unwrap();
        fs::write(&old_temp, b"other").unwrap();
        write_atomic(&json, b"{}").unwrap();
        assert_eq!(fs::read(&json).unwrap(), b"{}");
        assert_eq!(fs::read(&txt).unwrap(), b"text");
        assert_eq!(fs::read(&old_temp).unwrap(), b"other");
        assert!(!temp_path(&json).exists());
    }
}
