use std::fs::{self, File};
use std::io::{self, Write};
use std::path::{Path, PathBuf};

use sha2::{Digest, Sha256};

use super::http::{Http, TOO_LARGE};
use super::manifest::MAX_PART;
use crate::error::{Error, Result};
use crate::fsutil::hex;

const REPORT_STEP: u64 = 256 << 10;

struct Sink<'a> {
    file: File,
    hasher: Sha256,
    count: u64,
    limit: u64,
    reported: u64,
    progress: &'a mut dyn FnMut(u64, u64),
}

impl Write for Sink<'_> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if self.count + buf.len() as u64 > self.limit {
            return Err(io::Error::other(TOO_LARGE));
        }
        let written = self.file.write(buf)?;
        self.hasher.update(&buf[..written]);
        self.count += written as u64;
        if self.count - self.reported >= REPORT_STEP || self.count == self.limit {
            (self.progress)(self.count, self.limit);
            self.reported = self.count;
        }
        Ok(written)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.file.flush()
    }
}

pub fn fetch_verified(
    http: &dyn Http,
    url: &str,
    size: u64,
    sha256: &str,
    dir: &Path,
    name: &str,
    progress: &mut dyn FnMut(u64, u64),
) -> Result<PathBuf> {
    if size == 0 || size > MAX_PART {
        return Err(Error::Update(
            "a download size is outside 1 byte to 512 MiB".to_owned(),
        ));
    }
    fs::create_dir_all(dir).map_err(Error::io(dir))?;
    let part = dir.join(format!("{name}.part"));
    let target = dir.join(name);
    match stream(http, url, size, sha256, &part, progress) {
        Ok(()) => {
            fs::rename(&part, &target).map_err(|source| {
                let _ = fs::remove_file(&part);
                Error::Io {
                    path: target.display().to_string(),
                    source,
                }
            })?;
            Ok(target)
        }
        Err(error) => {
            let _ = fs::remove_file(&part);
            Err(error)
        }
    }
}

fn stream(
    http: &dyn Http,
    url: &str,
    size: u64,
    sha256: &str,
    part: &Path,
    progress: &mut dyn FnMut(u64, u64),
) -> Result<()> {
    let file = File::create(part).map_err(Error::io(part))?;
    let mut sink = Sink {
        file,
        hasher: Sha256::new(),
        count: 0,
        limit: size,
        reported: 0,
        progress,
    };
    let reply = http.get(url, None, &mut sink)?;
    if reply.status != 200 {
        return Err(Error::Update(format!(
            "HTTP {} for the download",
            reply.status
        )));
    }
    if sink.count != size {
        return Err(Error::Update(format!(
            "the download is {} bytes, expected {size}",
            sink.count
        )));
    }
    if hex(&sink.hasher.finalize()) != sha256 {
        return Err(Error::Update(
            "the download does not match its checksum".to_owned(),
        ));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fsutil::sha256_hex;
    use crate::update::http::fake::Fake;

    const URL: &str =
        "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/x.zip";

    #[test]
    fn a_matching_download_is_kept_under_its_name() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(URL, 200, None, b"payload");
        let mut seen = Vec::new();
        let path = fetch_verified(
            &fake,
            URL,
            7,
            &sha256_hex(b"payload"),
            dir.path(),
            "x.zip",
            &mut |done, total| seen.push((done, total)),
        )
        .unwrap();
        assert_eq!(fs::read(&path).unwrap(), b"payload");
        assert!(!dir.path().join("x.zip.part").exists());
        assert_eq!(seen.last(), Some(&(7, 7)));
    }

    #[test]
    fn a_hash_mismatch_leaves_no_file_behind() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(URL, 200, None, b"payload");
        let error = fetch_verified(
            &fake,
            URL,
            7,
            &sha256_hex(b"other!!"),
            dir.path(),
            "x.zip",
            &mut |_, _| {},
        )
        .unwrap_err();
        assert!(error.to_string().contains("checksum"));
        assert!(!dir.path().join("x.zip").exists());
        assert!(!dir.path().join("x.zip.part").exists());
    }

    #[test]
    fn a_body_larger_than_the_manifest_size_is_cut_off() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(URL, 200, None, b"payload-and-more");
        let error = fetch_verified(
            &fake,
            URL,
            7,
            &sha256_hex(b"payload"),
            dir.path(),
            "x.zip",
            &mut |_, _| {},
        )
        .unwrap_err();
        assert!(error.to_string().contains("larger"));
        assert!(!dir.path().join("x.zip.part").exists());
    }

    #[test]
    fn a_short_body_and_a_zero_size_are_refused() {
        let dir = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(URL, 200, None, b"pay");
        assert!(fetch_verified(
            &fake,
            URL,
            7,
            &sha256_hex(b"pay"),
            dir.path(),
            "x.zip",
            &mut |_, _| {}
        )
        .is_err());
        assert!(fetch_verified(
            &fake,
            URL,
            0,
            &sha256_hex(b""),
            dir.path(),
            "x.zip",
            &mut |_, _| {}
        )
        .is_err());
    }
}
