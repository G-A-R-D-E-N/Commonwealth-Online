pub mod download;
pub mod github;
pub mod http;
pub mod manifest;
pub mod mods;
pub mod selfupdate;
pub mod state;
mod version;

use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::error::{Error, Result};
use github::{Listing, Release};
use http::{Capped, Http};
use selfupdate::Sys;
pub use state::State;

pub const UPDATE_PUBLIC_KEY: Option<[u8; 32]> = Some([
    0x04, 0x47, 0x8d, 0xe7, 0x03, 0xdf, 0x53, 0x44, 0x36, 0x1b, 0xa1, 0x06, 0xa4, 0xeb, 0xc3, 0x87,
    0x1c, 0xf6, 0x1a, 0x38, 0xfa, 0x1a, 0xfe, 0x43, 0x77, 0x01, 0x19, 0x2a, 0xdd, 0xd2, 0xbf, 0x34,
]);
pub const FINISH_FLAG: &str = "--finish-update";
const STATE_DIR: &str = "CommonwealthOnlineLauncher";
const STATE_FILE: &str = "launcher-state.json";
const LOG_FILE: &str = "launcher-update.log";
const LOG_CAP: u64 = 256 << 10;
const ZIP_NAME: &str = "CommonwealthOnline-Launcher-update.zip";
const SIGNATURE_CAP: u64 = 4 << 10;
const UNREADABLE_LIST: &str = "GitHub did not return the release list";

// Constraint: same numbering space as release tags (0.0.0.x), never the Cargo version.
const UNVERSIONED: &str = "0.0.0.0";

pub fn running_version() -> &'static str {
    match option_env!("CO_LAUNCHER_VERSION") {
        Some(version) => version,
        None => UNVERSIONED,
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Paths {
    dir: PathBuf,
}

impl Paths {
    pub fn new(dir: impl Into<PathBuf>) -> Paths {
        Paths { dir: dir.into() }
    }

    pub fn user() -> Option<Paths> {
        dirs::data_local_dir().map(|dir| Paths::new(dir.join(STATE_DIR)))
    }

    fn state_file(&self) -> PathBuf {
        self.dir.join(STATE_FILE)
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Offer {
    pub version: String,
    pub prerelease: bool,
    pub launcher_update: bool,
    pub mod_update: bool,
    pub notes: Option<String>,
    pub signing_configured: bool,
}

fn offer_for(release: &Release, signing: bool) -> Offer {
    let launcher = github::launcher_asset(&release.tag_name);
    Offer {
        version: release.tag_name.clone(),
        prerelease: release.prerelease,
        launcher_update: github::asset(release, &launcher).is_some(),
        mod_update: github::asset(release, github::MANIFEST).is_some(),
        notes: github::notes(release),
        signing_configured: signing,
    }
}

fn stored_offer(state: &State, signing: bool) -> Option<Offer> {
    Some(Offer {
        version: state.last_seen_version.clone()?,
        prerelease: state.last_seen_prerelease,
        launcher_update: state.last_seen_launcher,
        mod_update: state.last_seen_mod,
        notes: state.last_seen_notes.clone(),
        signing_configured: signing,
    })
}

pub fn check(
    http: &dyn Http,
    paths: &Paths,
    installed: Option<&str>,
    now: u64,
) -> Result<Option<Offer>> {
    check_with(
        http,
        paths,
        running_version(),
        UPDATE_PUBLIC_KEY.is_some(),
        installed,
        now,
    )
}

fn check_with(
    http: &dyn Http,
    paths: &Paths,
    running: &str,
    signing: bool,
    installed: Option<&str>,
    now: u64,
) -> Result<Option<Offer>> {
    let file = paths.state_file();
    let mut state = state::load(&file);
    let offer = match github::fetch(http, state.etag.as_deref())? {
        Listing::NotModified => stored_offer(&state, signing),
        Listing::Fresh { releases, etag } => {
            state.etag = etag;
            match github::select(&releases, state.include_prereleases) {
                Some(release) => {
                    let offer = offer_for(release, signing);
                    state.last_seen_version = Some(offer.version.clone());
                    state.last_seen_prerelease = offer.prerelease;
                    state.last_seen_launcher = offer.launcher_update;
                    state.last_seen_mod = offer.mod_update;
                    state.last_seen_notes = offer.notes.clone();
                    Some(offer)
                }
                None => {
                    state.last_seen_version = None;
                    None
                }
            }
        }
    };
    state.last_check_unix = Some(now);
    let _ = state::save(&file, &state);
    Ok(offer.filter(|offer| {
        let mod_behind = offer.mod_update && installed != Some(offer.version.as_str());
        (version::newer(&offer.version, running) || mod_behind)
            && state.skipped_version.as_deref() != Some(offer.version.as_str())
    }))
}

pub fn load_state(paths: &Paths) -> State {
    state::load(&paths.state_file())
}

pub fn set_prefs(
    paths: &Paths,
    include_prereleases: bool,
    auto_install: bool,
    notify_only: bool,
) -> Result<()> {
    let file = paths.state_file();
    let mut state = state::load(&file);
    if state.include_prereleases != include_prereleases {
        state.etag = None;
    }
    state.include_prereleases = include_prereleases;
    state.auto_install = auto_install;
    state.notify_only = notify_only;
    state::save(&file, &state)
}

pub fn skip(paths: &Paths, version: &str) -> Result<()> {
    if version::parse(version).is_none() {
        return Err(Error::Update("that is not a version number".to_owned()));
    }
    let file = paths.state_file();
    let mut state = state::load(&file);
    state.skipped_version = Some(version.to_owned());
    state::save(&file, &state)
}

pub fn install_launcher(
    http: &dyn Http,
    paths: &Paths,
    exe: &Path,
    progress: &mut dyn FnMut(u64, u64),
) -> Result<()> {
    install_with(
        http,
        paths,
        exe,
        UPDATE_PUBLIC_KEY.as_ref(),
        running_version(),
        &selfupdate::Real,
        progress,
    )
}

fn install_with(
    http: &dyn Http,
    paths: &Paths,
    exe: &Path,
    key: Option<&[u8; 32]>,
    running: &str,
    sys: &dyn Sys,
    progress: &mut dyn FnMut(u64, u64),
) -> Result<()> {
    if key.is_none() {
        return Err(Error::Update(manifest::NO_KEY.to_owned()));
    }
    let folder = exe
        .parent()
        .ok_or_else(|| Error::Update("the launcher path has no folder".to_owned()))?;
    if !sys.writable(folder) {
        return Err(Error::Update(
            "notify only: the launcher folder is read-only, so the update was not installed"
                .to_owned(),
        ));
    }
    let include = state::load(&paths.state_file()).include_prereleases;
    let Listing::Fresh { releases, .. } = github::fetch(http, None)? else {
        return Err(Error::Update(UNREADABLE_LIST.to_owned()));
    };
    let release = github::select(&releases, include)
        .ok_or_else(|| Error::Update("there is no release to install".to_owned()))?;
    let tag = release.tag_name.as_str();
    if !version::newer(tag, running) {
        return Err(Error::Update(
            "the launcher is already up to date".to_owned(),
        ));
    }
    let parsed = signed_manifest(http, release, key)?;
    let part = parsed
        .launcher
        .as_ref()
        .ok_or_else(|| Error::Update("this release has no launcher build".to_owned()))?;
    let zip = download::fetch_verified(
        http,
        &part.url,
        part.size,
        &part.sha256,
        &paths.dir,
        ZIP_NAME,
        progress,
    )?;
    let staged = extract(&zip);
    let _ = fs::remove_file(&zip);
    selfupdate::swap(sys, exe, &staged?)
}

pub(crate) fn signed_manifest(
    http: &dyn Http,
    release: &Release,
    key: Option<&[u8; 32]>,
) -> Result<manifest::Manifest> {
    let tag = release.tag_name.as_str();
    let manifest_url = &github::asset(release, github::MANIFEST)
        .ok_or_else(|| Error::Update("this release has no latest.json".to_owned()))?
        .browser_download_url;
    let signature_url = &github::asset(release, github::SIGNATURE)
        .ok_or_else(|| Error::Update("this release has no latest.json.sig".to_owned()))?
        .browser_download_url;
    let bytes = fetch_small(http, tag, manifest_url, manifest::MAX_MANIFEST)?;
    let signature = String::from_utf8(fetch_small(http, tag, signature_url, SIGNATURE_CAP)?)
        .map_err(|_| Error::Update("latest.json.sig is not text".to_owned()))?;
    manifest::verify(key, &bytes, &signature)?;
    let parsed = manifest::parse(&bytes)?;
    manifest::validate(&parsed, tag, &release.assets)?;
    Ok(parsed)
}

fn extract(zip: &Path) -> Result<Vec<u8>> {
    let file = File::open(zip).map_err(Error::io(zip))?;
    selfupdate::exe_from_zip(file)
}

fn fetch_small(http: &dyn Http, tag: &str, url: &str, cap: u64) -> Result<Vec<u8>> {
    manifest::release_file(url, tag)?;
    let mut body = Vec::new();
    let reply = http.get(url, None, &mut Capped::new(&mut body, cap))?;
    if reply.status != 200 {
        return Err(Error::Update(format!(
            "GitHub answered HTTP {} for a release file",
            reply.status
        )));
    }
    Ok(body)
}

pub fn finish_arg(args: &[String]) -> Option<u32> {
    args.windows(2)
        .find(|pair| pair[0] == FINISH_FLAG)
        .and_then(|pair| pair[1].parse().ok())
}

pub fn finish_update(old_pid: u32) -> bool {
    match std::env::current_exe() {
        Ok(exe) => selfupdate::finish(&selfupdate::Real, &exe, old_pid),
        Err(_) => false,
    }
}

pub fn log(paths: &Paths, line: &str) {
    let file = paths.dir.join(LOG_FILE);
    let _ = fs::create_dir_all(&paths.dir);
    let full = fs::metadata(&file).is_ok_and(|meta| meta.len() > LOG_CAP);
    let mut options = OpenOptions::new();
    options.create(true);
    if full {
        options.write(true).truncate(true);
    } else {
        options.append(true);
    }
    if let Ok(mut out) = options.open(&file) {
        let _ = writeln!(out, "{line}");
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fsutil::sha256_hex;
    use crate::update::http::fake::Fake;
    use crate::update::selfupdate::fake::FakeSys;
    use base64::Engine as _;
    use ed25519_dalek::{Signer, SigningKey};
    use serde_json::json;
    use std::io::Cursor;
    use zip::write::SimpleFileOptions;

    const PREFIX: &str =
        "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/";
    const LIST: &str = "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases";
    const HEX: &str = "3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273";
    const LAUNCHER_ZIP: &str = "CommonwealthOnline-Launcher-0.0.0.8.zip";

    fn key() -> SigningKey {
        SigningKey::from_bytes(&[9u8; 32])
    }

    fn zip_with_exe(exe: &[u8]) -> Vec<u8> {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .start_file(
                "CommonwealthOnline-Launcher-0.0.0.8/CommonwealthOnlineLauncher.exe",
                SimpleFileOptions::default(),
            )
            .unwrap();
        writer.write_all(exe).unwrap();
        writer.finish().unwrap().into_inner()
    }

    fn part(name: &str, bytes: &[u8]) -> serde_json::Value {
        json!({"url": format!("{PREFIX}{name}"), "sha256": sha256_hex(bytes), "size": bytes.len()})
    }

    fn release_json(assets: &[&str]) -> Vec<u8> {
        let assets: Vec<_> = assets
            .iter()
            .map(|name| json!({"name": name, "size": 1, "browser_download_url": format!("{PREFIX}{name}")}))
            .collect();
        json!([{
            "tag_name": "0.0.0.8", "prerelease": true, "draft": false,
            "body": "Fixed things", "assets": assets
        }])
        .to_string()
        .into_bytes()
    }

    fn paths(root: &Path) -> Paths {
        Paths::new(root.join("state"))
    }

    fn fresh_release_fake(exe: &[u8], sign: &SigningKey) -> (Fake, Vec<u8>) {
        let zip = zip_with_exe(exe);
        let runtime = b"runtime";
        let manifest = json!({
            "schema": 2,
            "version": "0.0.0.8",
            "minLauncher": "0.0.0.0",
            "runtimes": {
                "og": part("CommonwealthOnline-OG-0.0.0.8.zip", runtime),
                "ng": part("CommonwealthOnline-NG-0.0.0.8.zip", runtime),
                "ae": part("CommonwealthOnline-AE-0.0.0.8.zip", runtime)
            },
            "ui": part("CommonwealthOnlineUI-0.0.0.8.zip", runtime),
            "launcher": part(LAUNCHER_ZIP, &zip),
            "contentFingerprint": HEX
        })
        .to_string()
        .into_bytes();
        let signature =
            base64::engine::general_purpose::STANDARD.encode(sign.sign(&manifest).to_bytes());
        let fake = Fake::default();
        fake.serve(
            LIST,
            200,
            Some("\"v1\""),
            &release_json(&[
                "latest.json",
                "latest.json.sig",
                LAUNCHER_ZIP,
                "CommonwealthOnline-OG-0.0.0.8.zip",
                "CommonwealthOnline-NG-0.0.0.8.zip",
                "CommonwealthOnline-AE-0.0.0.8.zip",
                "CommonwealthOnlineUI-0.0.0.8.zip",
            ]),
        );
        fake.serve(&format!("{PREFIX}latest.json"), 200, None, &manifest);
        fake.serve(
            &format!("{PREFIX}latest.json.sig"),
            200,
            None,
            signature.as_bytes(),
        );
        fake.serve(&format!("{PREFIX}{LAUNCHER_ZIP}"), 200, None, &zip);
        (fake, zip)
    }

    #[test]
    fn a_newer_release_is_offered_with_its_asset_flags_and_notes() {
        let root = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(
            LIST,
            200,
            Some("\"v1\""),
            &release_json(&["latest.json", LAUNCHER_ZIP]),
        );
        let offer = check_with(&fake, &paths(root.path()), "0.0.0.7", false, None, 5)
            .unwrap()
            .unwrap();
        assert_eq!(offer.version, "0.0.0.8");
        assert!(offer.prerelease && offer.launcher_update && offer.mod_update);
        assert_eq!(offer.notes.as_deref(), Some("Fixed things"));
        assert!(!offer.signing_configured);
        let saved = load_state(&paths(root.path()));
        assert_eq!(saved.etag.as_deref(), Some("\"v1\""));
        assert_eq!(saved.last_check_unix, Some(5));
    }

    #[test]
    fn a_304_reuses_the_last_seen_release_and_sends_the_etag() {
        let root = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(LIST, 200, Some("\"v1\""), &release_json(&["latest.json"]));
        check_with(&fake, &paths(root.path()), "0.0.0.7", false, None, 1).unwrap();
        let again = check_with(&fake, &paths(root.path()), "0.0.0.7", false, None, 2)
            .unwrap()
            .unwrap();
        assert_eq!(again.version, "0.0.0.8");
        assert!(again.mod_update && !again.launcher_update);
        assert_eq!(again.notes.as_deref(), Some("Fixed things"));
        assert_eq!(fake.requests.borrow()[1].1.as_deref(), Some("\"v1\""));
    }

    #[test]
    fn a_running_or_skipped_version_is_not_offered() {
        let root = tempfile::tempdir().unwrap();
        let fake = Fake::default();
        fake.serve(LIST, 200, Some("\"v1\""), &release_json(&["latest.json"]));
        assert!(check_with(
            &fake,
            &paths(root.path()),
            "0.0.0.8",
            false,
            Some("0.0.0.8"),
            1
        )
        .unwrap()
        .is_none());
        skip(&paths(root.path()), "0.0.0.8").unwrap();
        assert!(
            check_with(&fake, &paths(root.path()), "0.0.0.7", false, None, 1)
                .unwrap()
                .is_none()
        );
        assert!(skip(&paths(root.path()), "0.0.0.8.x").is_err());
    }

    #[test]
    fn switching_the_channel_drops_the_etag_so_the_list_is_read_again() {
        let root = tempfile::tempdir().unwrap();
        let p = paths(root.path());
        let fake = Fake::default();
        fake.serve(LIST, 200, Some("\"v1\""), &release_json(&["latest.json"]));
        check_with(&fake, &p, "0.0.0.7", false, None, 1).unwrap();
        set_prefs(&p, false, true, false).unwrap();
        let state = load_state(&p);
        assert!(state.etag.is_none() && !state.include_prereleases && state.auto_install);
        check_with(&fake, &p, "0.0.0.7", false, None, 2).unwrap();
        assert_eq!(fake.requests.borrow()[1].1, None);
    }

    #[test]
    fn a_full_launcher_install_swaps_the_exe_after_the_signature_checks() {
        let root = tempfile::tempdir().unwrap();
        let exe = root.path().join(selfupdate::EXE);
        let mut old = b"MZ".to_vec();
        old.extend_from_slice(b"old");
        fs::write(&exe, &old).unwrap();
        let mut new = b"MZ".to_vec();
        new.extend_from_slice(b"new build");
        let signing = key();
        let (fake, zip) = fresh_release_fake(&new, &signing);
        let public = signing.verifying_key().to_bytes();
        let mut seen = Vec::new();
        install_with(
            &fake,
            &paths(root.path()),
            &exe,
            Some(&public),
            "0.0.0.7",
            &selfupdate::Real,
            &mut |d, t| seen.push((d, t)),
        )
        .unwrap();
        assert_eq!(fs::read(&exe).unwrap(), new);
        assert_eq!(fs::read(format!("{}.old", exe.display())).unwrap(), old);
        assert!(!paths(root.path()).dir.join(ZIP_NAME).exists());
        assert_eq!(seen.last().map(|p| p.0), Some(zip.len() as u64));
    }

    #[test]
    fn install_refuses_without_a_key_a_bad_signature_or_an_unchanged_version() {
        let root = tempfile::tempdir().unwrap();
        let exe = root.path().join(selfupdate::EXE);
        fs::write(&exe, b"MZold").unwrap();
        let signing = key();
        let (fake, _) = fresh_release_fake(b"MZnew", &signing);
        let public = signing.verifying_key().to_bytes();
        let p = paths(root.path());
        let none = install_with(
            &fake,
            &p,
            &exe,
            None,
            "0.0.0.7",
            &selfupdate::Real,
            &mut |_, _| {},
        )
        .unwrap_err();
        assert_eq!(none.to_string(), manifest::NO_KEY);
        let other = SigningKey::from_bytes(&[4u8; 32])
            .verifying_key()
            .to_bytes();
        assert!(install_with(
            &fake,
            &p,
            &exe,
            Some(&other),
            "0.0.0.7",
            &selfupdate::Real,
            &mut |_, _| {}
        )
        .is_err());
        assert!(install_with(
            &fake,
            &p,
            &exe,
            Some(&public),
            "0.0.0.8",
            &selfupdate::Real,
            &mut |_, _| {}
        )
        .is_err());
        assert_eq!(fs::read(&exe).unwrap(), b"MZold");
    }

    #[test]
    fn a_read_only_launcher_folder_is_refused_before_any_download() {
        let root = tempfile::tempdir().unwrap();
        let exe = root.path().join(selfupdate::EXE);
        fs::write(&exe, b"MZold").unwrap();
        let signing = key();
        let (fake, _) = fresh_release_fake(b"MZnew", &signing);
        let public = signing.verifying_key().to_bytes();
        let sys = FakeSys {
            writable: false,
            ..FakeSys::default()
        };
        let error = install_with(
            &fake,
            &paths(root.path()),
            &exe,
            Some(&public),
            "0.0.0.7",
            &sys,
            &mut |_, _| {},
        )
        .unwrap_err();
        assert!(error.to_string().starts_with("notify only"));
        assert!(fake.requests.borrow().iter().all(|(url, _)| url == LIST));
    }

    #[test]
    fn finish_arguments_are_read_from_the_command_line() {
        let args: Vec<String> = ["app.exe", FINISH_FLAG, "4242"].map(str::to_owned).to_vec();
        assert_eq!(finish_arg(&args), Some(4242));
        assert_eq!(finish_arg(&["app.exe".to_owned()]), None);
        assert_eq!(finish_arg(&[FINISH_FLAG.to_owned(), "x".to_owned()]), None);
    }

    #[test]
    fn the_running_version_is_never_empty() {
        assert!(!running_version().is_empty());
    }

    #[test]
    fn the_embedded_public_key_is_a_valid_ed25519_key() {
        let key = UPDATE_PUBLIC_KEY.expect("the signing key is configured");
        assert!(ed25519_dalek::VerifyingKey::from_bytes(&key).is_ok());
    }
}
