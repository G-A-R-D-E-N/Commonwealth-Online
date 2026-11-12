use base64::Engine as _;
use ed25519_dalek::{Signature, VerifyingKey};
use serde::Deserialize;

use super::github::{self, Asset};
use super::version;
use crate::error::{Error, Result};

pub const SCHEMA: u32 = 2;
pub const MAX_PART: u64 = 512 << 20;
pub const MAX_MANIFEST: u64 = 256 << 10;
pub const NO_KEY: &str = "Update signing key is not configured";

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Part {
    pub url: String,
    pub sha256: String,
    pub size: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Runtimes {
    pub og: Part,
    pub ng: Part,
    pub ae: Part,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Manifest {
    pub schema: u32,
    pub version: String,
    pub min_launcher: String,
    pub runtimes: Runtimes,
    pub ui: Part,
    #[serde(default)]
    pub launcher: Option<Part>,
    pub content_fingerprint: String,
}

fn bad(text: &str) -> Error {
    Error::Update(text.to_owned())
}

pub fn verify(key: Option<&[u8; 32]>, manifest: &[u8], signature: &str) -> Result<()> {
    let key = key.ok_or_else(|| bad(NO_KEY))?;
    let raw = base64::engine::general_purpose::STANDARD
        .decode(signature.trim())
        .map_err(|_| bad("latest.json.sig is not base64"))?;
    let raw: [u8; 64] = raw
        .try_into()
        .map_err(|_| bad("latest.json.sig is not a 64-byte signature"))?;
    let key =
        VerifyingKey::from_bytes(key).map_err(|_| bad("the update signing key is invalid"))?;
    key.verify_strict(manifest, &Signature::from_bytes(&raw))
        .map_err(|_| bad("the update signature does not match latest.json"))
}

pub fn parse(bytes: &[u8]) -> Result<Manifest> {
    serde_json::from_slice(bytes)
        .map_err(|e| Error::Update(format!("latest.json is not valid: {e}")))
}

pub fn release_file<'a>(url: &'a str, tag: &str) -> Result<&'a str> {
    let prefix = format!("{}{tag}/", github::RELEASE_PREFIX);
    let name = url
        .strip_prefix(prefix.as_str())
        .ok_or_else(|| bad("a file is not in this release on GitHub"))?;
    let plain = !name.is_empty()
        && !name.contains("..")
        && name
            .bytes()
            .all(|b| b.is_ascii_graphic() && !b"/\\?#%".contains(&b));
    if !plain {
        return Err(bad(
            "a file name in latest.json is not a plain release asset name",
        ));
    }
    Ok(name)
}

pub fn is_sha256(text: &str) -> bool {
    text.len() == 64 && text.bytes().all(|b| matches!(b, b'0'..=b'9' | b'a'..=b'f'))
}

fn check_part(part: &Part, tag: &str, assets: &[Asset]) -> Result<()> {
    let name = release_file(&part.url, tag)?;
    if !assets.iter().any(|asset| asset.name == name) {
        return Err(bad("a file in latest.json is not in the release"));
    }
    if !is_sha256(&part.sha256) {
        return Err(bad(
            "a sha256 in latest.json is not 64 lowercase hex digits",
        ));
    }
    if part.size == 0 || part.size > MAX_PART {
        return Err(bad(
            "a file size in latest.json is outside 1 byte to 512 MiB",
        ));
    }
    Ok(())
}

pub fn validate(manifest: &Manifest, tag: &str, assets: &[Asset]) -> Result<()> {
    if manifest.schema != SCHEMA {
        return Err(bad("latest.json schema is not 2"));
    }
    if manifest.version != tag {
        return Err(bad(
            "latest.json names a different version than its release",
        ));
    }
    if version::parse(&manifest.min_launcher).is_none() {
        return Err(bad("minLauncher in latest.json is not dotted digits"));
    }
    if !is_sha256(&manifest.content_fingerprint) {
        return Err(bad(
            "contentFingerprint in latest.json is not 64 lowercase hex digits",
        ));
    }
    let runtimes = &manifest.runtimes;
    let mut parts = vec![&runtimes.og, &runtimes.ng, &runtimes.ae, &manifest.ui];
    parts.extend(manifest.launcher.as_ref());
    parts
        .into_iter()
        .try_for_each(|part| check_part(part, tag, assets))
}

pub fn mod_install_allowed(manifest: &Manifest, running: &str) -> Result<()> {
    if version::newer(&manifest.min_launcher, running) {
        return Err(Error::Update(format!(
            "this update needs launcher {} or newer; update the launcher first",
            manifest.min_launcher
        )));
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use ed25519_dalek::{Signer, SigningKey};
    use serde_json::json;

    const TAG: &str = "0.0.0.8";
    const HEX: &str = "3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273";

    fn url(name: &str) -> String {
        format!("{}{TAG}/{name}", github::RELEASE_PREFIX)
    }

    fn part(name: &str) -> serde_json::Value {
        json!({"url": url(name), "sha256": HEX, "size": 10})
    }

    fn manifest_json() -> serde_json::Value {
        json!({
            "schema": 2,
            "version": TAG,
            "minLauncher": "0.0.0.0",
            "runtimes": {
                "og": part("CommonwealthOnline-OG-0.0.0.8.zip"),
                "ng": part("CommonwealthOnline-NG-0.0.0.8.zip"),
                "ae": part("CommonwealthOnline-AE-0.0.0.8.zip")
            },
            "ui": part("CommonwealthOnlineUI-0.0.0.8.zip"),
            "launcher": part("CommonwealthOnline-Launcher-0.0.0.8.zip"),
            "contentFingerprint": HEX
        })
    }

    fn assets(names: &[&str]) -> Vec<Asset> {
        names
            .iter()
            .map(|name| Asset {
                name: (*name).to_owned(),
                size: 10,
                browser_download_url: url(name),
            })
            .collect()
    }

    fn all_assets() -> Vec<Asset> {
        assets(&[
            "CommonwealthOnline-OG-0.0.0.8.zip",
            "CommonwealthOnline-NG-0.0.0.8.zip",
            "CommonwealthOnline-AE-0.0.0.8.zip",
            "CommonwealthOnlineUI-0.0.0.8.zip",
            "CommonwealthOnline-Launcher-0.0.0.8.zip",
        ])
    }

    fn check(value: serde_json::Value) -> Result<()> {
        let parsed = parse(value.to_string().as_bytes())?;
        validate(&parsed, TAG, &all_assets())
    }

    #[test]
    fn a_well_formed_manifest_passes() {
        assert!(check(manifest_json()).is_ok());
    }

    #[test]
    fn one_archive_can_satisfy_all_runtime_and_ui_entries() {
        let mut value = manifest_json();
        let unified = part("CommonwealthOnline-0.0.0.8.zip");
        value["runtimes"] = json!({"og": unified, "ng": unified, "ae": unified});
        value["ui"] = unified;
        let parsed = parse(value.to_string().as_bytes()).unwrap();
        assert!(validate(
            &parsed,
            TAG,
            &assets(&[
                "CommonwealthOnline-0.0.0.8.zip",
                "CommonwealthOnline-Launcher-0.0.0.8.zip",
            ])
        )
        .is_ok());
    }

    #[test]
    fn each_rule_rejects_its_own_violation() {
        let mut cases: Vec<(&str, serde_json::Value)> = Vec::new();
        let mut v = manifest_json();
        v["schema"] = json!(1);
        cases.push(("schema", v));
        let mut v = manifest_json();
        v["version"] = json!("0.0.0.9");
        cases.push(("version", v));
        let mut v = manifest_json();
        v["minLauncher"] = json!("0.1.x");
        cases.push(("minLauncher", v));
        let mut v = manifest_json();
        v["contentFingerprint"] = json!(HEX.to_uppercase());
        cases.push(("fingerprint", v));
        let mut v = manifest_json();
        v["ui"]["sha256"] = json!("abc");
        cases.push(("sha short", v));
        let mut v = manifest_json();
        v["ui"]["sha256"] = json!(HEX.to_uppercase());
        cases.push(("sha uppercase", v));
        let mut v = manifest_json();
        v["ui"]["size"] = json!(0);
        cases.push(("size zero", v));
        let mut v = manifest_json();
        v["ui"]["size"] = json!(MAX_PART + 1);
        cases.push(("size too big", v));
        let mut v = manifest_json();
        v["ui"]["url"] = json!(url("Missing.zip"));
        cases.push(("not in release", v));
        for bad_url in [
            "https://github.com.evil.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/CommonwealthOnlineUI-0.0.0.8.zip",
            "http://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/CommonwealthOnlineUI-0.0.0.8.zip",
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.7/CommonwealthOnlineUI-0.0.0.8.zip",
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/../../../evil.zip",
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/..%2F..%2Fevil.zip",
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/CommonwealthOnlineUI-0.0.0.8.zip?x=1",
            "https://user@github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/CommonwealthOnlineUI-0.0.0.8.zip",
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/",
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/sub/x.zip",
        ] {
            let mut v = manifest_json();
            v["ui"]["url"] = json!(bad_url);
            cases.push(("url", v));
        }
        for (label, value) in cases {
            assert!(check(value).is_err(), "{label} was accepted");
        }
    }

    #[test]
    fn the_launcher_part_is_optional_but_checked_when_present() {
        let mut v = manifest_json();
        v.as_object_mut().unwrap().remove("launcher");
        assert!(check(v).is_ok());
        let mut v = manifest_json();
        v["launcher"]["url"] = json!("https://github.com.evil.com/x.zip");
        assert!(check(v).is_err());
    }

    #[test]
    fn mod_installs_wait_for_a_newer_launcher() {
        let parsed = parse(manifest_json().to_string().as_bytes()).unwrap();
        assert!(mod_install_allowed(&parsed, "0.0.0.8").is_ok());
        let mut v = manifest_json();
        v["minLauncher"] = json!("0.0.0.9");
        let newer = parse(v.to_string().as_bytes()).unwrap();
        assert!(mod_install_allowed(&newer, "0.0.0.8").is_err());
        assert!(mod_install_allowed(&newer, "0.0.0.9").is_ok());
        assert!(mod_install_allowed(&newer, "0.0.0.10").is_ok());
    }

    #[test]
    fn the_default_min_launcher_never_blocks_a_packaged_or_unversioned_build() {
        let parsed = parse(manifest_json().to_string().as_bytes()).unwrap();
        for running in [crate::update::running_version(), "0.0.0.8"] {
            assert!(mod_install_allowed(&parsed, running).is_ok());
        }
    }

    fn signing_key() -> SigningKey {
        SigningKey::from_bytes(&[9u8; 32])
    }

    fn signature_of(key: &SigningKey, bytes: &[u8]) -> String {
        base64::engine::general_purpose::STANDARD.encode(key.sign(bytes).to_bytes())
    }

    #[test]
    fn a_good_signature_verifies_and_a_changed_byte_does_not() {
        let key = signing_key();
        let public = key.verifying_key().to_bytes();
        let bytes = manifest_json().to_string().into_bytes();
        let sig = signature_of(&key, &bytes);
        assert!(verify(Some(&public), &bytes, &sig).is_ok());
        let mut changed = bytes.clone();
        changed[5] ^= 1;
        assert!(verify(Some(&public), &changed, &sig).is_err());
    }

    #[test]
    fn a_signature_from_another_key_or_a_malformed_one_is_refused() {
        let key = signing_key();
        let other = SigningKey::from_bytes(&[3u8; 32])
            .verifying_key()
            .to_bytes();
        let bytes = b"{}";
        let sig = signature_of(&key, bytes);
        assert!(verify(Some(&other), bytes, &sig).is_err());
        let public = key.verifying_key().to_bytes();
        assert!(verify(Some(&public), bytes, "not base64!").is_err());
        assert!(verify(Some(&public), bytes, "AAAA").is_err());
    }

    #[test]
    fn a_missing_key_refuses_every_signature() {
        let key = signing_key();
        let sig = signature_of(&key, b"{}");
        let error = verify(None, b"{}", &sig).unwrap_err();
        assert_eq!(error.to_string(), NO_KEY);
    }
}
