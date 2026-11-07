use std::fs;

use super::github::{self, Listing};
use super::http::Http;
use super::{download, manifest, signed_manifest, Paths};
use crate::channel::{compare_versions, installed_version};
use crate::error::{Error, Result};
use crate::modinstall::{self, InstallGuards, InstallReport, Target, KEEP};
use crate::runtime::Runtime;

pub use crate::modinstall::rollback as roll_back;

const ZIP_NAME: &str = "CommonwealthOnline-Mod-update.zip";
const UNREADABLE_LIST: &str = "GitHub did not return the release list";
const NO_RELEASE: &str = "there is no mod release to install";
const OLDER: &str = "the installed build is newer than this release, so it was not replaced";
const SAME: &str = "this build is already installed";

#[derive(Debug, Clone, Copy)]
pub struct Signing<'a> {
    pub key: Option<&'a [u8; 32]>,
    pub running: &'a str,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Installed {
    pub report: InstallReport,
    pub pruned: Vec<String>,
}

pub fn install_mod(
    http: &dyn Http,
    paths: &Paths,
    target: Target,
    adopt_unknown: bool,
    guards: &dyn InstallGuards,
    signing: Signing,
    progress: &mut dyn FnMut(u64, u64),
) -> Result<Installed> {
    if signing.key.is_none() {
        return Err(Error::Update(manifest::NO_KEY.to_owned()));
    }
    modinstall::preflight(target, adopt_unknown, guards)?;
    let include = super::state::load(&paths.state_file()).include_prereleases;
    let Listing::Fresh { releases, .. } = github::fetch(http, None)? else {
        return Err(Error::Update(UNREADABLE_LIST.to_owned()));
    };
    let release =
        github::select(&releases, include).ok_or_else(|| Error::Update(NO_RELEASE.to_owned()))?;
    let parsed = signed_manifest(http, release, signing.key)?;
    manifest::mod_install_allowed(&parsed, signing.running)?;
    let part = match target.runtime {
        Runtime::Og => &parsed.runtimes.og,
        Runtime::Ng => &parsed.runtimes.ng,
        Runtime::Ae => &parsed.runtimes.ae,
    };
    let version = parsed.version.as_str();
    if let Some(installed) = installed_version(target.instance, target.profile, target.runtime) {
        match compare_versions(&installed, version) {
            std::cmp::Ordering::Greater => return Err(Error::Update(OLDER.to_owned())),
            std::cmp::Ordering::Equal => return Err(Error::Update(SAME.to_owned())),
            std::cmp::Ordering::Less => {}
        }
    }
    let zip = download::fetch_verified(
        http,
        &part.url,
        part.size,
        &part.sha256,
        &paths.dir,
        ZIP_NAME,
        progress,
    )?;
    let installed = modinstall::install_mod_release(target, version, &zip, adopt_unknown, guards);
    let _ = fs::remove_file(&zip);
    let report = installed?;
    let pruned = modinstall::prune_old(target, KEEP, guards).unwrap_or_default();
    Ok(Installed { report, pruned })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::channel::{STANDALONE_MARKER, WABBAJACK_MARKER};
    use crate::fsutil::sha256_hex;
    use crate::update::http::fake::Fake;
    use base64::Engine as _;
    use ed25519_dalek::{Signer, SigningKey};
    use serde_json::json;
    use std::io::{Cursor, Write};
    use std::path::Path;
    use zip::write::SimpleFileOptions;

    const PREFIX: &str =
        "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/";
    const LIST: &str = "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases";
    const HEX: &str = "3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273";
    const OG: &str = "CommonwealthOnline-OG-0.0.0.8.zip";
    const RUNNING: &str = "0.0.0.7";
    const WABBA: &str =
        r#"{"schema":1,"channel":"wabbajack","modlist":"Commonwealth Online","version":"2.0"}"#;

    struct Guards {
        game: bool,
    }

    impl InstallGuards for Guards {
        fn game_or_f4se_running(&self) -> bool {
            self.game
        }
        fn mo2_running(&self) -> bool {
            false
        }
    }

    fn free() -> Guards {
        Guards { game: false }
    }

    fn sign_key() -> SigningKey {
        SigningKey::from_bytes(&[9u8; 32])
    }

    fn put(path: &Path, text: &str) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, text).unwrap();
    }

    fn standalone(version: &str, runtime: &str) -> String {
        format!(
            r#"{{"schema":1,"channel":"standalone","installed_version":"{version}","runtime":"{runtime}","installed_unix":1700000000}}"#
        )
    }

    fn instance(list: &str) -> tempfile::TempDir {
        let dir = tempfile::tempdir().unwrap();
        put(&dir.path().join("mods").join("placeholder.txt"), "");
        put(
            &dir.path()
                .join("profiles")
                .join("Default")
                .join("modlist.txt"),
            list,
        );
        dir
    }

    fn og(dir: &Path) -> Target<'_> {
        Target {
            instance: dir,
            profile: "Default",
            runtime: Runtime::Og,
        }
    }

    fn paths(dir: &Path) -> Paths {
        Paths::new(dir.join("state"))
    }

    fn modlist(dir: &Path) -> String {
        fs::read_to_string(dir.join("profiles").join("Default").join("modlist.txt")).unwrap()
    }

    fn mod_zip() -> Vec<u8> {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .start_file("Data/CommonwealthOnline.esp", SimpleFileOptions::default())
            .unwrap();
        writer.write_all(b"esp bytes").unwrap();
        writer.finish().unwrap().into_inner()
    }

    fn part(name: &str, bytes: &[u8]) -> serde_json::Value {
        json!({"url": format!("{PREFIX}{name}"), "sha256": sha256_hex(bytes), "size": bytes.len()})
    }

    fn release(zip: &[u8], og_sha: Option<String>, min_launcher: &str) -> Fake {
        let mut og_part = part(OG, zip);
        if let Some(sha) = og_sha {
            og_part["sha256"] = json!(sha);
        }
        let manifest = json!({
            "schema": 2,
            "version": "0.0.0.8",
            "minLauncher": min_launcher,
            "runtimes": {
                "og": og_part,
                "ng": part("CommonwealthOnline-NG-0.0.0.8.zip", b"ng"),
                "ae": part("CommonwealthOnline-AE-0.0.0.8.zip", b"ae")
            },
            "ui": part("CommonwealthOnlineUI-0.0.0.8.zip", b"ui"),
            "contentFingerprint": HEX
        })
        .to_string()
        .into_bytes();
        let signature =
            base64::engine::general_purpose::STANDARD.encode(sign_key().sign(&manifest).to_bytes());
        let names = [
            "latest.json",
            "latest.json.sig",
            OG,
            "CommonwealthOnline-NG-0.0.0.8.zip",
            "CommonwealthOnline-AE-0.0.0.8.zip",
            "CommonwealthOnlineUI-0.0.0.8.zip",
        ];
        let assets: Vec<_> = names
            .iter()
            .map(|name| json!({"name": name, "size": 1, "browser_download_url": format!("{PREFIX}{name}")}))
            .collect();
        let list = json!([{
            "tag_name": "0.0.0.8", "prerelease": true, "draft": false,
            "body": "notes", "assets": assets
        }])
        .to_string();
        let fake = Fake::default();
        fake.serve(LIST, 200, Some("\"v1\""), list.as_bytes());
        fake.serve(&format!("{PREFIX}latest.json"), 200, None, &manifest);
        fake.serve(
            &format!("{PREFIX}latest.json.sig"),
            200,
            None,
            signature.as_bytes(),
        );
        fake.serve(&format!("{PREFIX}{OG}"), 200, None, zip);
        fake
    }

    fn run(
        fake: &Fake,
        dir: &Path,
        adopt: bool,
        guards: &Guards,
        key: Option<&[u8; 32]>,
    ) -> Result<Installed> {
        let signing = Signing {
            key,
            running: RUNNING,
        };
        install_mod(
            fake,
            &paths(dir),
            og(dir),
            adopt,
            guards,
            signing,
            &mut |_, _| {},
        )
    }

    fn public() -> [u8; 32] {
        sign_key().verifying_key().to_bytes()
    }

    fn asked_for_zip(fake: &Fake) -> bool {
        fake.requests
            .borrow()
            .iter()
            .any(|(url, _)| url.ends_with(OG))
    }

    #[test]
    fn no_key_refuses_before_any_request() {
        let dir = instance("+Main\n");
        let fake = release(&mod_zip(), None, "0.0.0.1");
        let error = run(&fake, dir.path(), false, &free(), None).unwrap_err();
        assert_eq!(error.to_string(), manifest::NO_KEY);
        assert!(fake.requests.borrow().is_empty());
    }

    #[test]
    fn a_running_game_refuses_before_any_request() {
        let dir = instance("+Main\n");
        let fake = release(&mod_zip(), None, "0.0.0.1");
        let guards = Guards { game: true };
        let error = run(&fake, dir.path(), false, &guards, Some(&public())).unwrap_err();
        assert!(error.to_string().starts_with("Fallout 4 or F4SE"));
        assert!(fake.requests.borrow().is_empty());
    }

    #[test]
    fn a_wabbajack_install_is_never_touched_even_when_adopting() {
        let dir = instance("+Main\n");
        put(
            &dir.path()
                .join("mods")
                .join("Commonwealth Online OG 2.0")
                .join(WABBAJACK_MARKER),
            WABBA,
        );
        let fake = release(&mod_zip(), None, "0.0.0.1");
        for adopt in [false, true] {
            let error = run(&fake, dir.path(), adopt, &free(), Some(&public())).unwrap_err();
            assert!(error.to_string().contains("Wabbajack"), "{error}");
        }
        assert!(fake.requests.borrow().is_empty());
    }

    #[test]
    fn an_unknown_install_needs_adoption_before_any_request() {
        let dir = instance("+Main\n");
        let fake = release(&mod_zip(), None, "0.0.0.1");
        assert!(run(&fake, dir.path(), false, &free(), Some(&public())).is_err());
        assert!(fake.requests.borrow().is_empty());
        let installed = run(&fake, dir.path(), true, &free(), Some(&public())).unwrap();
        assert_eq!(installed.report.folder, "Commonwealth Online OG 0.0.0.8");
        assert!(dir
            .path()
            .join("mods/Commonwealth Online OG 0.0.0.8")
            .join(STANDALONE_MARKER)
            .is_file());
        assert!(modlist(dir.path()).contains("+Commonwealth Online OG 0.0.0.8"));
        assert!(!paths(dir.path()).dir.join(ZIP_NAME).exists());
    }

    #[test]
    fn a_too_new_minimum_launcher_stops_before_the_download() {
        let dir = instance("+Main\n");
        let fake = release(&mod_zip(), None, "9.0.0");
        let error = run(&fake, dir.path(), true, &free(), Some(&public())).unwrap_err();
        assert!(
            error.to_string().contains("update the launcher first"),
            "{error}"
        );
        assert!(!asked_for_zip(&fake));
    }

    #[test]
    fn a_hash_mismatch_installs_nothing() {
        let dir = instance("+Main\n");
        let fake = release(&mod_zip(), Some(sha256_hex(b"other")), "0.0.0.1");
        assert!(run(&fake, dir.path(), true, &free(), Some(&public())).is_err());
        assert!(!dir
            .path()
            .join("mods/Commonwealth Online OG 0.0.0.8")
            .exists());
        assert_eq!(modlist(dir.path()), "+Main\n");
    }

    #[test]
    fn a_newer_installed_build_is_not_replaced_and_the_same_one_is_refused() {
        let dir = instance("+Main\n+Commonwealth Online OG 0.0.0.9\n");
        put(
            &dir.path()
                .join("mods/Commonwealth Online OG 0.0.0.9")
                .join(STANDALONE_MARKER),
            &standalone("0.0.0.9", "OG"),
        );
        let fake = release(&mod_zip(), None, "0.0.0.1");
        let error = run(&fake, dir.path(), false, &free(), Some(&public())).unwrap_err();
        assert!(error.to_string().contains("newer"));
        assert!(!asked_for_zip(&fake));
    }

    #[test]
    fn a_release_replaces_the_old_build_and_rollback_restores_it() {
        let dir = instance("+Main\n+Commonwealth Online OG 0.0.0.7\n");
        put(
            &dir.path()
                .join("mods/Commonwealth Online OG 0.0.0.7")
                .join(STANDALONE_MARKER),
            &standalone("0.0.0.7", "OG"),
        );
        let fake = release(&mod_zip(), None, "0.0.0.1");
        let installed = run(&fake, dir.path(), false, &free(), Some(&public())).unwrap();
        assert_eq!(installed.report.folder, "Commonwealth Online OG 0.0.0.8");
        assert!(installed.pruned.is_empty());
        let text = modlist(dir.path());
        assert!(text.contains("+Commonwealth Online OG 0.0.0.8"));
        assert!(text.contains("-Commonwealth Online OG 0.0.0.7"));
        let back = roll_back(og(dir.path()), &free()).unwrap();
        assert_eq!(back.restored, "Commonwealth Online OG 0.0.0.7");
        let text = modlist(dir.path());
        assert!(text.contains("+Commonwealth Online OG 0.0.0.7"));
        assert!(text.contains("-Commonwealth Online OG 0.0.0.8"));
    }

    #[test]
    fn a_running_mo2_refuses_the_rollback_too() {
        let dir = instance("+Main\n");
        struct Mo2;
        impl InstallGuards for Mo2 {
            fn game_or_f4se_running(&self) -> bool {
                false
            }
            fn mo2_running(&self) -> bool {
                true
            }
        }
        assert!(roll_back(og(dir.path()), &Mo2).is_err());
    }
}
