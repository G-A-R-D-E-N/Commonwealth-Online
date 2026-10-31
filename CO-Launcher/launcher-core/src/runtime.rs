use std::io;
use std::path::Path;

use pelite::{FileMap, PeFile};

use crate::error::{Error, Result};
use crate::fsutil;

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Runtime {
    Og,
    Ng,
    Ae,
}

impl Runtime {
    pub const ALL: [Runtime; 3] = [Runtime::Og, Runtime::Ng, Runtime::Ae];

    pub fn game_version(self) -> &'static str {
        match self {
            Runtime::Og => "1.10.163",
            Runtime::Ng => "1.10.984",
            Runtime::Ae => "1.11.240",
        }
    }

    pub fn label(self) -> &'static str {
        match self {
            Runtime::Og => "OG",
            Runtime::Ng => "NG",
            Runtime::Ae => "AE",
        }
    }

    pub fn from_version(version: &str) -> Option<Runtime> {
        Runtime::ALL
            .into_iter()
            .find(|r| r.game_version() == version)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Detected {
    Supported(Runtime),
    Unsupported(String),
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ExeInfo {
    pub version: String,
    pub detected: Detected,
    pub sha256: String,
}

pub fn detect(game_dir: &Path) -> Result<ExeInfo> {
    let exe = fsutil::find_ci(game_dir, "Fallout4.exe").ok_or_else(|| Error::Io {
        path: game_dir.join("Fallout4.exe").display().to_string(),
        source: io::Error::from(io::ErrorKind::NotFound),
    })?;
    let version = pe_file_version(&exe)?;
    let detected = match Runtime::from_version(&version) {
        Some(runtime) => Detected::Supported(runtime),
        None => Detected::Unsupported(version.clone()),
    };
    Ok(ExeInfo {
        version,
        detected,
        sha256: fsutil::sha256_file(&exe)?,
    })
}

pub fn pe_file_version(path: &Path) -> Result<String> {
    let map = FileMap::open(path).map_err(Error::io(path))?;
    let pe_error = |reason: String| Error::Pe {
        path: path.display().to_string(),
        reason,
    };
    let file = PeFile::from_bytes(&map).map_err(|e| pe_error(e.to_string()))?;
    let resources = file.resources().map_err(|e| pe_error(e.to_string()))?;
    let version_info = resources
        .version_info()
        .map_err(|e| pe_error(e.to_string()))?;
    let fixed = version_info
        .fixed()
        .ok_or_else(|| pe_error("no VS_FIXEDFILEINFO in the version resource".to_string()))?;
    let v = fixed.dwFileVersion;
    Ok(format!("{}.{}.{}", v.Major, v.Minor, v.Patch))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fsutil::sha256_hex;
    use crate::testutil::{fake_game, write_fake_pe};
    use std::fs;

    #[test]
    fn detects_each_supported_runtime() {
        for (runtime, version) in [
            (Runtime::Og, "1.10.163"),
            (Runtime::Ng, "1.10.984"),
            (Runtime::Ae, "1.11.240"),
        ] {
            let dir = tempfile::tempdir().unwrap();
            fake_game(dir.path(), runtime);
            let info = detect(dir.path()).unwrap();
            assert_eq!(info.version, version);
            assert_eq!(info.detected, Detected::Supported(runtime));
            let bytes = fs::read(dir.path().join("Fallout4.exe")).unwrap();
            assert_eq!(info.sha256, sha256_hex(&bytes));
        }
    }

    #[test]
    fn unsupported_versions_are_reported_as_is() {
        for version in [(1, 10, 980, 0), (1, 11, 221, 0)] {
            let dir = tempfile::tempdir().unwrap();
            write_fake_pe(&dir.path().join("Fallout4.exe"), version);
            let info = detect(dir.path()).unwrap();
            let text = format!("{}.{}.{}", version.0, version.1, version.2);
            assert_eq!(info.version, text);
            assert_eq!(info.detected, Detected::Unsupported(text));
        }
    }

    #[test]
    fn version_resource_fields_are_read_in_order() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("sample.exe");
        write_fake_pe(&path, (1, 10, 163, 0));
        assert_eq!(pe_file_version(&path).unwrap(), "1.10.163");
        write_fake_pe(&path, (2, 1, 0, 7));
        assert_eq!(pe_file_version(&path).unwrap(), "2.1.0");
    }

    #[test]
    fn upper_case_exe_name_is_found() {
        let dir = tempfile::tempdir().unwrap();
        write_fake_pe(&dir.path().join("Fallout4.EXE"), (1, 11, 240, 0));
        let info = detect(dir.path()).unwrap();
        assert_eq!(info.detected, Detected::Supported(Runtime::Ae));
    }

    #[test]
    fn not_a_pe_file_is_a_pe_error() {
        let dir = tempfile::tempdir().unwrap();
        fs::write(
            dir.path().join("Fallout4.exe"),
            b"not a portable executable",
        )
        .unwrap();
        assert!(matches!(detect(dir.path()), Err(Error::Pe { .. })));
        let path = dir.path().join("Fallout4.exe");
        assert!(matches!(pe_file_version(&path), Err(Error::Pe { .. })));
    }

    #[test]
    fn missing_exe_is_an_io_error() {
        let dir = tempfile::tempdir().unwrap();
        assert!(matches!(detect(dir.path()), Err(Error::Io { .. })));
    }

    #[test]
    fn runtime_from_version() {
        assert_eq!(Runtime::from_version("1.10.984"), Some(Runtime::Ng));
        assert_eq!(Runtime::from_version("1.10.980"), None);
    }
}
