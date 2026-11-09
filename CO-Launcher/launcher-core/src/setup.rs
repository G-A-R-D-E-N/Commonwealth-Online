use std::collections::{BTreeMap, BTreeSet};
use std::fs::{self, File, OpenOptions};
use std::io::{self, Read};
use std::path::{Path, PathBuf};

use serde::{Deserialize, Serialize};
use zip::write::SimpleFileOptions;
use zip::ZipArchive;

use crate::addrlib::{self, write_zip_atomic, Mo2Link};
use crate::error::{Error, Result};
use crate::fsutil::write_atomic;
use crate::modinstall::{self, InstallGuards, InstallReport, Target, LIMITS};
use crate::runtime::Runtime;
use crate::update::{self, download, github, http::Http, manifest, Paths};

pub const SCHEMA: u32 = 1;
const STATE_DIR: &str = "CommonwealthOnlineLauncher";
const STATE_FILE: &str = "launcher-setup.json";
const LOOSE_RECORD: &str = "loose-installs.json";
const MAX_STATE: u64 = 64 << 10;
const UTF8_BOM: &[u8] = b"\xEF\xBB\xBF";
pub const PRERELEASE_NOTE: &str =
    "Setup turns on pre-release builds so you get the newest unified Commonwealth Online build.";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ManagerKind {
    Mo2,
    Vortex,
}

impl ManagerKind {
    pub fn label(self) -> &'static str {
        match self {
            ManagerKind::Mo2 => "mo2",
            ManagerKind::Vortex => "vortex",
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize, Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct State {
    pub schema: u32,
    pub setup_complete: bool,
    pub ack_experimental: bool,
    pub download_dir: String,
    pub manager: String,
}

pub const MANAGER_NONE: &str = "none";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PlayTarget {
    Mo2,
    Loader,
    DeployInVortex,
    Setup,
}

pub fn play_target(manager: &str, mo2_ready: bool, vortex_deployed: bool) -> PlayTarget {
    if manager == ManagerKind::Vortex.label() {
        return if vortex_deployed {
            PlayTarget::Loader
        } else {
            PlayTarget::DeployInVortex
        };
    }
    if manager == MANAGER_NONE {
        return PlayTarget::Loader;
    }
    if mo2_ready {
        PlayTarget::Mo2
    } else {
        PlayTarget::Setup
    }
}

pub fn f4se_version(names: &[String]) -> Option<String> {
    names.iter().find_map(|name| {
        let lower = name.to_ascii_lowercase();
        let parts = lower.strip_prefix("f4se_")?.strip_suffix(".dll")?;
        let numbers: Vec<&str> = parts.split('_').collect();
        let plain = numbers
            .iter()
            .all(|n| !n.is_empty() && n.bytes().all(|b| b.is_ascii_digit()));
        (numbers.len() == 3 && plain).then(|| numbers.join("."))
    })
}

pub fn dependency_notes(
    runtime: Runtime,
    loader: bool,
    f4se: Option<&str>,
    address_bin: bool,
) -> Vec<String> {
    let mut notes = Vec::new();
    if !loader {
        notes.push(format!(
            "f4se_loader.exe was not found in the game folder. Install the script extender for Fallout 4 {}.",
            runtime.game_version()
        ));
    }
    if let Some(version) = f4se.filter(|v| *v != runtime.game_version()) {
        notes.push(format!(
            "The script extender files are for {version}, but Fallout 4 is {} ({}). Install the build for your version.",
            runtime.game_version(),
            runtime.label()
        ));
    }
    if !address_bin {
        notes.push(format!(
            "{} is not in Data\\F4SE\\Plugins. Copy it there, or link it in setup.",
            addrlib::file_name(runtime)
        ));
    }
    notes
}

pub fn user_file() -> Option<PathBuf> {
    dirs::data_local_dir().map(|dir| dir.join(STATE_DIR).join(STATE_FILE))
}

pub fn default_download_root() -> Option<PathBuf> {
    dirs::data_local_dir().map(|dir| dir.join(STATE_DIR).join("downloads"))
}

pub fn default_server_root() -> Option<PathBuf> {
    dirs::data_local_dir().map(|dir| dir.join(STATE_DIR).join("CommonwealthOnlineServer"))
}

fn read_capped(path: &Path) -> Option<Vec<u8>> {
    let mut bytes = Vec::new();
    File::open(path)
        .ok()?
        .take(MAX_STATE + 1)
        .read_to_end(&mut bytes)
        .ok()?;
    (bytes.len() as u64 <= MAX_STATE).then_some(bytes)
}

pub fn load(path: &Path) -> State {
    read_capped(path)
        .and_then(|bytes| {
            let body = bytes.strip_prefix(UTF8_BOM).unwrap_or(&bytes);
            serde_json::from_slice::<State>(body).ok()
        })
        .filter(|state| state.schema <= SCHEMA)
        .unwrap_or_default()
}

pub fn save(path: &Path, state: &State) -> Result<()> {
    let mut value = state.clone();
    value.schema = SCHEMA;
    let bytes = serde_json::to_vec_pretty(&value).map_err(|e| Error::Update(e.to_string()))?;
    write_atomic(path, &bytes)
}

pub fn enable_prereleases(paths: &Paths) -> Result<()> {
    let state = update::load_state(paths);
    update::set_prefs(paths, true, state.auto_install, state.notify_only)
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Fetched {
    pub tag: String,
    pub name: String,
    pub zip: PathBuf,
}

pub fn fetch_release(
    http: &dyn Http,
    root: &Path,
    key: Option<&[u8; 32]>,
    running: &str,
    include_prereleases: bool,
    runtime: Runtime,
    progress: &mut dyn FnMut(u64, u64),
) -> Result<Fetched> {
    if key.is_none() {
        return Err(Error::Update(manifest::NO_KEY.to_owned()));
    }
    let github::Listing::Fresh { releases, .. } = github::fetch(http, None)? else {
        return Err(Error::Update(
            "GitHub did not return the release list".to_owned(),
        ));
    };
    let release = github::select(&releases, include_prereleases, github::has_signed_manifest)
        .ok_or_else(|| {
            Error::Update("there is no Commonwealth Online release to download".to_owned())
        })?;
    let parsed = update::signed_manifest(http, release, key)?;
    manifest::mod_install_allowed(&parsed, running)?;
    let part = match runtime {
        Runtime::Og => &parsed.runtimes.og,
        Runtime::Ng => &parsed.runtimes.ng,
        Runtime::Ae => &parsed.runtimes.ae,
    };
    let name = manifest::release_file(&part.url, &release.tag_name)?.to_owned();
    let dir = root.join(&release.tag_name);
    let existed = dir.exists();
    let zip = match download::fetch_verified(
        http,
        &part.url,
        part.size,
        &part.sha256,
        &dir,
        &name,
        progress,
    ) {
        Ok(zip) => zip,
        Err(error) => {
            if !existed {
                let _ = fs::remove_dir(&dir);
            }
            return Err(error);
        }
    };
    Ok(Fetched {
        tag: release.tag_name.clone(),
        name,
        zip,
    })
}

pub struct Mo2Plan<'a> {
    pub target: Target<'a>,
    pub version: &'a str,
    pub zip: &'a Path,
    pub address: Option<&'a [u8]>,
    pub adopt_unknown: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Mo2Done {
    pub report: InstallReport,
    pub address_linked: bool,
}

pub fn install_mo2(plan: &Mo2Plan, guards: &dyn InstallGuards) -> Result<Mo2Done> {
    modinstall::preflight(plan.target, plan.adopt_unknown, guards)?;
    let link: Option<Mo2Link> = match plan.address {
        Some(bytes) => Some(addrlib::link_mo2(
            plan.target.instance,
            plan.target.profile,
            plan.target.runtime,
            bytes,
        )?),
        None => None,
    };
    let created = link.is_some_and(|l| l.created);
    match modinstall::install_mod_release(
        plan.target,
        plan.version,
        plan.zip,
        plan.adopt_unknown,
        guards,
    ) {
        Ok(report) => Ok(Mo2Done {
            report,
            address_linked: plan.address.is_some(),
        }),
        Err(error) => {
            if created {
                let _ = addrlib::unlink_mo2(plan.target.instance, plan.target.profile);
            }
            Err(error)
        }
    }
}

fn archive_error(error: impl ToString) -> Error {
    Error::Archive(error.to_string())
}

pub fn vortex_package(zip: &Path, runtime: Runtime, tag: &str, dir: &Path) -> Result<PathBuf> {
    let file = File::open(zip).map_err(Error::io(zip))?;
    let mut archive = ZipArchive::new(file).map_err(archive_error)?;
    let entries = modinstall::plan(&mut archive, &LIMITS)?;
    if entries.is_empty() {
        return Err(Error::Archive(
            "the release has no files under Data/".to_owned(),
        ));
    }
    let out = dir.join(format!(
        "Commonwealth Online {tag} {} (Vortex).zip",
        runtime.label()
    ));
    write_zip_atomic(&out, |writer| {
        for entry in &entries {
            writer
                .start_file(rel_name(&entry.rel), SimpleFileOptions::default())
                .map_err(archive_error)?;
            let mut source = archive.by_index(entry.index).map_err(archive_error)?;
            let copied = io::copy(&mut (&mut source).take(entry.size + 1), writer)
                .map_err(Error::io(&out))?;
            if copied != entry.size {
                return Err(Error::Archive(format!(
                    "{} does not match its listed size",
                    entry.rel.display()
                )));
            }
        }
        Ok(())
    })?;
    Ok(out)
}

#[derive(Debug, Default, Serialize, Deserialize)]
#[serde(default)]
struct LooseRecord {
    games: BTreeMap<String, BTreeSet<String>>,
}

pub fn loose_record_file() -> Option<PathBuf> {
    dirs::data_local_dir().map(|dir| dir.join(STATE_DIR).join(LOOSE_RECORD))
}

fn game_key(game: &Path) -> String {
    game.to_string_lossy().to_ascii_lowercase()
}

fn rel_name(rel: &Path) -> String {
    rel.components()
        .map(|c| c.as_os_str().to_string_lossy().into_owned())
        .collect::<Vec<_>>()
        .join("/")
}

fn load_loose(path: &Path) -> LooseRecord {
    read_capped(path)
        .and_then(|bytes| serde_json::from_slice(&bytes).ok())
        .unwrap_or_default()
}

pub fn install_loose(zip: &Path, game: &Path, record: &Path) -> Result<usize> {
    let file = File::open(zip).map_err(Error::io(zip))?;
    let mut archive = ZipArchive::new(file).map_err(archive_error)?;
    let entries = modinstall::plan(&mut archive, &LIMITS)?;
    if entries.is_empty() {
        return Err(Error::Archive(
            "the release has no files under Data/".to_owned(),
        ));
    }
    let data = game.join("Data");
    let key = game_key(game);
    let mut book = load_loose(record);
    let owned = book.games.get(&key).cloned().unwrap_or_default();
    let names: Vec<String> = entries.iter().map(|e| rel_name(&e.rel)).collect();
    let targets: Vec<PathBuf> = entries.iter().map(|e| data.join(&e.rel)).collect();
    if let Some((taken, _)) = targets
        .iter()
        .zip(&names)
        .zip(&entries)
        .find(|((t, n), e)| t.exists() && !owned.contains(*n) && !is_loose_script(&e.rel))
        .map(|((t, n), _)| (t, n))
    {
        return Err(Error::Archive(format!(
            "{} already exists, so setup will not overwrite it. Move it aside, then run setup again.",
            taken.display()
        )));
    }
    let mut written: Vec<PathBuf> = Vec::new();
    let mut backups: Vec<(PathBuf, PathBuf)> = Vec::new();
    let result = (|| -> Result<()> {
        for ((entry, target), name) in entries.iter().zip(&targets).zip(&names) {
            if target.exists() && !owned.contains(name) && is_loose_script(&entry.rel) {
                let mut backup = target.clone().into_os_string();
                backup.push(".pre-co");
                let backup = PathBuf::from(backup);
                fs::rename(target, &backup).map_err(Error::io(target))?;
                backups.push((target.clone(), backup));
            }
            let replacing = target.exists();
            if let Some(parent) = target.parent() {
                fs::create_dir_all(parent).map_err(Error::io(parent))?;
            }
            let mut options = OpenOptions::new();
            options.write(true).truncate(true);
            if replacing {
                options.create(true);
            } else {
                options.create_new(true);
            }
            let mut out = options.open(target).map_err(Error::io(target))?;
            written.push(target.clone());
            let mut source = archive.by_index(entry.index).map_err(archive_error)?;
            let copied = io::copy(&mut (&mut source).take(entry.size + 1), &mut out)
                .map_err(Error::io(target))?;
            if copied != entry.size {
                return Err(Error::Archive(format!(
                    "{} does not match its listed size",
                    entry.rel.display()
                )));
            }
        }
        book.games.entry(key).or_default().extend(names);
        save_loose(record, &book)
    })();
    if let Err(error) = result {
        for path in written.iter().rev() {
            let _ = fs::remove_file(path);
        }
        for (target, backup) in backups.iter().rev() {
            let _ = fs::rename(backup, target);
        }
        return Err(error);
    }
    Ok(entries.len())
}

// Constraint: only compiled or source Papyrus scripts under Data/Scripts may replace a file the launcher did not write, after the old file is kept as <name>.pre-co.
fn is_loose_script(rel: &Path) -> bool {
    let mut parts = rel.components();
    let in_scripts = parts
        .next()
        .is_some_and(|c| c.as_os_str().eq_ignore_ascii_case("Scripts"));
    let script = rel
        .extension()
        .is_some_and(|e| e.eq_ignore_ascii_case("pex") || e.eq_ignore_ascii_case("psc"));
    in_scripts && script
}

fn save_loose(record: &Path, book: &LooseRecord) -> Result<()> {
    let bytes = serde_json::to_vec_pretty(book).map_err(|e| Error::Update(e.to_string()))?;
    write_atomic(record, &bytes)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fsutil::sha256_hex;
    use crate::update::http::fake::Fake;
    use base64::Engine as _;
    use ed25519_dalek::{Signer, SigningKey};
    use serde_json::json;
    use std::io::{Cursor, Write};

    const PREFIX: &str =
        "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.11/";
    const STABLE: &str =
        "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/";
    const LIST: &str =
        "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases?per_page=100&page=1";
    const HEX: &str = "3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273";
    const ZIP: &str = "CommonwealthOnline-0.0.0.11.zip";

    struct Free;

    impl InstallGuards for Free {
        fn game_or_f4se_running(&self) -> bool {
            false
        }
        fn mo2_running(&self) -> bool {
            false
        }
    }

    fn data_zip() -> Vec<u8> {
        data_zip_with(b"esp bytes")
    }

    fn data_zip_with(esp: &[u8]) -> Vec<u8> {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .start_file("Data/CommonwealthOnline.esp", SimpleFileOptions::default())
            .unwrap();
        writer.write_all(esp).unwrap();
        writer
            .start_file(
                "Data/F4SE/Plugins/CommonwealthOnline.dll",
                SimpleFileOptions::default(),
            )
            .unwrap();
        writer.write_all(b"dll bytes").unwrap();
        writer.finish().unwrap().into_inner()
    }

    fn unified_release(zip: &[u8]) -> Fake {
        release_with(zip, |_| {}, 9)
    }

    fn release_with(zip: &[u8], edit: impl FnOnce(&mut serde_json::Value), signer: u8) -> Fake {
        let part =
            json!({"url": format!("{PREFIX}{ZIP}"), "sha256": sha256_hex(zip), "size": zip.len()});
        let mut value = json!({
            "schema": 2,
            "version": "0.0.0.11",
            "minLauncher": "0.0.0.0",
            "runtimes": {"og": part.clone(), "ng": part.clone(), "ae": part.clone()},
            "ui": part,
            "contentFingerprint": HEX
        });
        edit(&mut value);
        let manifest = value.to_string().into_bytes();
        let key = SigningKey::from_bytes(&[signer; 32]);
        let signature =
            base64::engine::general_purpose::STANDARD.encode(key.sign(&manifest).to_bytes());
        let list = json!([
            {"tag_name": "0.0.0.11", "prerelease": true, "draft": false, "body": "", "assets": [
                {"name": "latest.json", "size": 1, "browser_download_url": format!("{PREFIX}latest.json")},
                {"name": "latest.json.sig", "size": 1, "browser_download_url": format!("{PREFIX}latest.json.sig")},
                {"name": ZIP, "size": 1, "browser_download_url": format!("{PREFIX}{ZIP}")}
            ]},
            {"tag_name": "0.0.0.8", "prerelease": false, "draft": false, "body": "", "assets": [
                {"name": "latest.json", "size": 1, "browser_download_url": format!("{STABLE}latest.json")},
                {"name": "latest.json.sig", "size": 1, "browser_download_url": format!("{STABLE}latest.json.sig")}
            ]}
        ])
        .to_string();
        let fake = Fake::default();
        fake.serve(LIST, 200, Some("\"v1\""), list.as_bytes());
        fake.serve(&format!("{PREFIX}latest.json"), 200, None, &manifest);
        fake.serve(&format!("{STABLE}latest.json"), 200, None, &manifest);
        fake.serve(
            &format!("{PREFIX}latest.json.sig"),
            200,
            None,
            signature.as_bytes(),
        );
        fake.serve(&format!("{PREFIX}{ZIP}"), 200, None, zip);
        fake
    }

    fn public() -> [u8; 32] {
        SigningKey::from_bytes(&[9u8; 32])
            .verifying_key()
            .to_bytes()
    }

    #[test]
    fn the_state_file_round_trips_and_bad_files_fall_back_to_defaults() {
        let dir = tempfile::tempdir().unwrap();
        let path = dir.path().join("launcher-setup.json");
        assert_eq!(load(&path), State::default());
        let state = State {
            schema: 0,
            setup_complete: true,
            ack_experimental: true,
            download_dir: "D:\\downloads".to_owned(),
            manager: "vortex".to_owned(),
        };
        save(&path, &state).unwrap();
        let back = load(&path);
        assert!(back.setup_complete && back.ack_experimental);
        assert_eq!(back.download_dir, "D:\\downloads");
        assert_eq!(back.manager, "vortex");
        assert_eq!(back.schema, SCHEMA);
        fs::write(&path, b"{ not json").unwrap();
        assert_eq!(load(&path), State::default());
        fs::write(&path, vec![b' '; (MAX_STATE + 1) as usize]).unwrap();
        assert_eq!(load(&path), State::default());
        fs::write(&path, br#"{"schema":99,"setupComplete":true}"#).unwrap();
        assert_eq!(load(&path), State::default());
    }

    #[test]
    fn a_unified_prerelease_is_fetched_once_under_its_tag_and_checked() {
        let zip = data_zip();
        let fake = unified_release(&zip);
        let root = tempfile::tempdir().unwrap();
        let key = public();
        let fetched = fetch_release(
            &fake,
            root.path(),
            Some(&key),
            "0.0.0.7",
            true,
            Runtime::Ae,
            &mut |_, _| {},
        )
        .unwrap();
        assert_eq!(fetched.tag, "0.0.0.11");
        assert_eq!(fetched.name, ZIP);
        assert_eq!(fetched.zip, root.path().join("0.0.0.11").join(ZIP));
        assert_eq!(fs::read(&fetched.zip).unwrap(), zip);
        let zip_requests = fake
            .requests
            .borrow()
            .iter()
            .filter(|(url, _)| url.ends_with(".zip"))
            .count();
        assert_eq!(zip_requests, 1);
    }

    #[test]
    fn without_prereleases_the_older_stable_release_is_the_one_selected() {
        let zip = data_zip();
        let fake = unified_release(&zip);
        let root = tempfile::tempdir().unwrap();
        let key = public();
        let refused = fetch_release(
            &fake,
            root.path(),
            Some(&key),
            "0.0.0.7",
            false,
            Runtime::Og,
            &mut |_, _| {},
        );
        let error = refused.unwrap_err().to_string();
        assert!(error.contains("latest.json.sig"), "{error}");
        assert!(error.contains("0.0.0.8"), "{error}");
        assert!(fake
            .requests
            .borrow()
            .iter()
            .all(|(url, _)| !url.contains("/0.0.0.11/")));
        assert!(!root.path().join("0.0.0.11").exists());
    }

    fn fetch_fails(fake: &Fake, root: &Path, include: bool, runtime: Runtime) -> String {
        let key = public();
        fetch_release(
            fake,
            root,
            Some(&key),
            "0.0.0.7",
            include,
            runtime,
            &mut |_, _| {},
        )
        .unwrap_err()
        .to_string()
    }

    #[test]
    fn a_bad_signature_a_checksum_or_size_mismatch_and_a_newer_minimum_are_refused() {
        let zip = data_zip();
        let root = tempfile::tempdir().unwrap();
        let wrong_key = release_with(&zip, |_| {}, 3);
        let error = fetch_fails(&wrong_key, root.path(), true, Runtime::Og);
        assert!(error.contains("signature does not match"), "{error}");

        let bad_sha = release_with(
            &zip,
            |v| v["runtimes"]["og"]["sha256"] = json!("0".repeat(64)),
            9,
        );
        let error = fetch_fails(&bad_sha, root.path(), true, Runtime::Og);
        assert!(error.contains("checksum"), "{error}");

        let bad_size = release_with(
            &zip,
            |v| v["runtimes"]["og"]["size"] = json!(zip.len() + 1),
            9,
        );
        let error = fetch_fails(&bad_size, root.path(), true, Runtime::Og);
        assert!(error.contains("expected"), "{error}");

        let newer = release_with(&zip, |v| v["minLauncher"] = json!("9.0.0"), 9);
        let error = fetch_fails(&newer, root.path(), true, Runtime::Og);
        assert!(error.contains("update the launcher first"), "{error}");
        assert!(!root.path().join("0.0.0.11").exists());
    }

    #[test]
    fn a_manifest_missing_a_runtime_is_refused_for_that_runtime() {
        let zip = data_zip();
        let root = tempfile::tempdir().unwrap();
        let missing_ae = release_with(
            &zip,
            |v| {
                v["runtimes"].as_object_mut().unwrap().remove("ae");
            },
            9,
        );
        let error = fetch_fails(&missing_ae, root.path(), true, Runtime::Ae);
        assert!(error.contains("latest.json"), "{error}");
    }

    #[test]
    fn a_release_without_data_files_cannot_become_a_vortex_package() {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .start_file("readme.txt", SimpleFileOptions::default())
            .unwrap();
        writer.write_all(b"hi").unwrap();
        let dir = tempfile::tempdir().unwrap();
        let zip = dir.path().join("release.zip");
        fs::write(&zip, writer.finish().unwrap().into_inner()).unwrap();
        let error = vortex_package(&zip, Runtime::Og, "0.0.0.11", &dir.path().join("out"))
            .unwrap_err()
            .to_string();
        assert!(error.contains("not under Data/"), "{error}");
        assert!(!dir
            .path()
            .join("out")
            .join("Commonwealth Online 0.0.0.11 OG (Vortex).zip")
            .exists());
    }

    #[test]
    fn a_missing_key_refuses_before_any_request() {
        let fake = Fake::default();
        let root = tempfile::tempdir().unwrap();
        let error = fetch_release(
            &fake,
            root.path(),
            None,
            "0.0.0.7",
            true,
            Runtime::Og,
            &mut |_, _| {},
        )
        .unwrap_err();
        assert_eq!(error.to_string(), manifest::NO_KEY);
        assert!(fake.requests.borrow().is_empty());
    }

    fn instance(list: &str) -> tempfile::TempDir {
        let dir = tempfile::tempdir().unwrap();
        fs::create_dir_all(dir.path().join("mods")).unwrap();
        let profile = dir.path().join("profiles").join("Default");
        fs::create_dir_all(&profile).unwrap();
        fs::write(profile.join("modlist.txt"), list).unwrap();
        dir
    }

    fn og(dir: &Path) -> Target<'_> {
        Target {
            instance: dir,
            profile: "Default",
            runtime: Runtime::Og,
        }
    }

    #[test]
    fn a_failed_mo2_install_removes_the_address_library_it_linked() {
        let dir = instance("+Main\n");
        let bad_zip = dir.path().join("broken.zip");
        fs::write(&bad_zip, b"not a zip").unwrap();
        let bin = b"address bytes".to_vec();
        let plan = Mo2Plan {
            target: og(dir.path()),
            version: "0.0.0.11",
            zip: &bad_zip,
            address: Some(&bin),
            adopt_unknown: true,
        };
        assert!(install_mo2(&plan, &Free).is_err());
        assert!(!dir.path().join("mods").join(addrlib::ADDRLIB_MOD).exists());
        assert_eq!(
            fs::read_to_string(
                dir.path()
                    .join("profiles")
                    .join("Default")
                    .join("modlist.txt")
            )
            .unwrap(),
            "+Main\n"
        );
    }

    #[test]
    fn a_successful_mo2_install_links_the_address_library_and_the_mod() {
        let dir = instance("+Main\n");
        let good_zip = dir.path().join("release.zip");
        fs::write(&good_zip, data_zip()).unwrap();
        let bin = b"address bytes".to_vec();
        let plan = Mo2Plan {
            target: og(dir.path()),
            version: "0.0.0.11",
            zip: &good_zip,
            address: Some(&bin),
            adopt_unknown: true,
        };
        let done = install_mo2(&plan, &Free).unwrap();
        assert!(done.address_linked);
        assert_eq!(done.report.folder, "Commonwealth Online OG 0.0.0.11");
        let list = fs::read_to_string(
            dir.path()
                .join("profiles")
                .join("Default")
                .join("modlist.txt"),
        )
        .unwrap();
        assert!(list.contains("+Commonwealth Online OG 0.0.0.11"), "{list}");
        assert!(
            list.contains(&format!("+{}", addrlib::ADDRLIB_MOD)),
            "{list}"
        );
    }

    #[test]
    fn the_vortex_package_strips_the_data_prefix() {
        let dir = tempfile::tempdir().unwrap();
        let zip = dir.path().join("release.zip");
        fs::write(&zip, data_zip()).unwrap();
        let out = vortex_package(&zip, Runtime::Ng, "0.0.0.11", &dir.path().join("out")).unwrap();
        assert_eq!(
            out.file_name().unwrap().to_string_lossy(),
            "Commonwealth Online 0.0.0.11 NG (Vortex).zip"
        );
        let mut archive = ZipArchive::new(File::open(&out).unwrap()).unwrap();
        let mut names: Vec<String> = (0..archive.len())
            .map(|i| archive.by_index(i).unwrap().name().unwrap().into_owned())
            .collect();
        names.sort();
        assert_eq!(
            names,
            [
                "CommonwealthOnline.esp",
                "F4SE/Plugins/CommonwealthOnline.dll"
            ]
        );
    }

    #[test]
    fn a_loose_install_never_overwrites_and_writes_nothing_on_conflict() {
        let game = tempfile::tempdir().unwrap();
        let state = tempfile::tempdir().unwrap();
        let record = state.path().join(LOOSE_RECORD);
        let zip = game.path().join("release.zip");
        fs::write(&zip, data_zip()).unwrap();
        let data = game.path().join("Data");
        fs::create_dir_all(&data).unwrap();
        fs::write(data.join("CommonwealthOnline.esp"), b"player").unwrap();
        let error = install_loose(&zip, game.path(), &record)
            .unwrap_err()
            .to_string();
        assert!(error.contains("will not overwrite"), "{error}");
        assert_eq!(
            fs::read(data.join("CommonwealthOnline.esp")).unwrap(),
            b"player"
        );
        assert!(!data.join("F4SE").exists());
        fs::remove_file(data.join("CommonwealthOnline.esp")).unwrap();
        assert_eq!(install_loose(&zip, game.path(), &record).unwrap(), 2);
        assert!(data
            .join("F4SE")
            .join("Plugins")
            .join("CommonwealthOnline.dll")
            .is_file());
    }

    #[test]
    fn a_second_loose_install_replaces_only_the_files_it_wrote_before() {
        let game = tempfile::tempdir().unwrap();
        let state = tempfile::tempdir().unwrap();
        let record = state.path().join(LOOSE_RECORD);
        let zip = game.path().join("release.zip");
        fs::write(&zip, data_zip()).unwrap();
        install_loose(&zip, game.path(), &record).unwrap();
        fs::write(&zip, data_zip_with(b"esp v2")).unwrap();
        assert_eq!(install_loose(&zip, game.path(), &record).unwrap(), 2);
        let data = game.path().join("Data");
        assert_eq!(
            fs::read(data.join("CommonwealthOnline.esp")).unwrap(),
            b"esp v2"
        );
    }

    #[test]
    fn a_second_loose_install_still_refuses_a_file_the_launcher_did_not_write() {
        let game = tempfile::tempdir().unwrap();
        let state = tempfile::tempdir().unwrap();
        let record = state.path().join(LOOSE_RECORD);
        let zip = game.path().join("release.zip");
        fs::write(&zip, data_zip()).unwrap();
        install_loose(&zip, game.path(), &record).unwrap();
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .start_file("Data/CommonwealthOnline.esp", SimpleFileOptions::default())
            .unwrap();
        writer.write_all(b"esp v2").unwrap();
        writer
            .start_file("Data/Other.esp", SimpleFileOptions::default())
            .unwrap();
        writer.write_all(b"new").unwrap();
        fs::write(&zip, writer.finish().unwrap().into_inner()).unwrap();
        let data = game.path().join("Data");
        fs::write(data.join("Other.esp"), b"someone else").unwrap();
        let error = install_loose(&zip, game.path(), &record)
            .unwrap_err()
            .to_string();
        assert!(error.contains("will not overwrite"), "{error}");
        assert_eq!(fs::read(data.join("Other.esp")).unwrap(), b"someone else");
        assert_eq!(
            fs::read(data.join("CommonwealthOnline.esp")).unwrap(),
            b"esp bytes"
        );
    }

    #[test]
    fn a_stale_script_the_launcher_did_not_write_is_kept_aside_and_replaced() {
        let game = tempfile::tempdir().unwrap();
        let state = tempfile::tempdir().unwrap();
        let record = state.path().join(LOOSE_RECORD);
        let zip = game.path().join("release.zip");
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .start_file(
                "Data/Scripts/CommonwealthOnlineQuest.pex",
                SimpleFileOptions::default(),
            )
            .unwrap();
        writer.write_all(b"new pex").unwrap();
        writer
            .start_file("Data/Other.esp", SimpleFileOptions::default())
            .unwrap();
        writer.write_all(b"new esp").unwrap();
        fs::write(&zip, writer.finish().unwrap().into_inner()).unwrap();
        let data = game.path().join("Data");
        fs::create_dir_all(data.join("Scripts")).unwrap();
        let pex = data.join("Scripts").join("CommonwealthOnlineQuest.pex");
        fs::write(&pex, b"old pex").unwrap();
        fs::write(data.join("Other.esp"), b"someone else").unwrap();
        assert!(install_loose(&zip, game.path(), &record).is_err());
        assert_eq!(fs::read(&pex).unwrap(), b"old pex");
        assert!(!data
            .join("Scripts")
            .join("CommonwealthOnlineQuest.pex.pre-co")
            .exists());
        fs::remove_file(data.join("Other.esp")).unwrap();
        assert_eq!(install_loose(&zip, game.path(), &record).unwrap(), 2);
        assert_eq!(fs::read(&pex).unwrap(), b"new pex");
        assert_eq!(
            fs::read(
                data.join("Scripts")
                    .join("CommonwealthOnlineQuest.pex.pre-co")
            )
            .unwrap(),
            b"old pex"
        );
    }

    #[test]
    fn a_failed_loose_install_removes_only_what_it_wrote_and_nothing_else() {
        let game = tempfile::tempdir().unwrap();
        let state = tempfile::tempdir().unwrap();
        let record = state.path().join(LOOSE_RECORD);
        let zip = game.path().join("release.zip");
        fs::write(&zip, data_zip()).unwrap();
        let data = game.path().join("Data");
        fs::create_dir_all(&data).unwrap();
        fs::write(data.join("Keep.esp"), b"mine").unwrap();
        fs::write(data.join("F4SE"), b"blocks the plugin folder").unwrap();
        assert!(install_loose(&zip, game.path(), &record).is_err());
        assert!(!data.join("CommonwealthOnline.esp").exists());
        assert!(data.join("F4SE").is_file());
        assert_eq!(fs::read(data.join("Keep.esp")).unwrap(), b"mine");
        assert!(!record.exists());
    }

    #[test]
    fn play_follows_the_saved_manager_and_its_readiness() {
        assert_eq!(play_target("mo2", true, false), PlayTarget::Mo2);
        assert_eq!(play_target("mo2", false, false), PlayTarget::Setup);
        assert_eq!(play_target("vortex", false, true), PlayTarget::Loader);
        assert_eq!(
            play_target("vortex", false, false),
            PlayTarget::DeployInVortex
        );
        assert_eq!(play_target("none", false, false), PlayTarget::Loader);
        assert_eq!(play_target("", true, false), PlayTarget::Mo2);
        assert_eq!(play_target("", false, false), PlayTarget::Setup);
    }

    #[test]
    fn the_script_extender_version_comes_from_its_dll_name() {
        let names = vec!["other.dll".to_owned(), "f4se_1_10_163.dll".to_owned()];
        assert_eq!(f4se_version(&names).as_deref(), Some("1.10.163"));
        assert_eq!(f4se_version(&[]), None);
    }

    #[test]
    fn dependency_notes_name_each_missing_or_mismatched_piece() {
        assert!(dependency_notes(Runtime::Og, true, Some("1.10.163"), true).is_empty());
        let notes = dependency_notes(Runtime::Ng, false, Some("1.10.163"), false);
        assert_eq!(notes.len(), 3, "{notes:?}");
        assert!(notes[0].contains("f4se_loader.exe"));
        assert!(notes[1].contains("1.10.163") && notes[1].contains("1.10.984"));
        assert!(notes[2].contains(&addrlib::file_name(Runtime::Ng)));
    }

    #[test]
    #[ignore]
    fn live_release_list_has_the_assets_the_setup_expects() {
        use crate::update::http::UreqHttp;
        let http = UreqHttp::new();
        let github::Listing::Fresh { releases, .. } = github::fetch(&http, None).unwrap() else {
            panic!("no fresh listing");
        };
        let tags: Vec<&str> = releases.iter().map(|r| r.tag_name.as_str()).collect();
        println!("live tags (newest unfiltered): {tags:?}");
        let release = github::select(&releases, true, github::has_signed_manifest)
            .expect("a release with prereleases on");
        println!("selected with prereleases: {}", release.tag_name);
        for asset in &release.assets {
            println!("  asset {} ({} bytes)", asset.name, asset.size);
        }
        assert!(github::asset(release, github::MANIFEST).is_some());
        assert!(github::asset(release, github::SIGNATURE).is_some());
    }
}
