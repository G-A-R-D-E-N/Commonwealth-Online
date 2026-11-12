use super::github::{self, Release};
use super::http::Http;
use super::{manifest, signed_manifest};
use crate::error::{Error, Result};
use crate::runtime::Runtime;

pub(crate) fn mod_part(
    http: &dyn Http,
    release: &Release,
    key: Option<&[u8; 32]>,
    running: &str,
    runtime: Runtime,
) -> Result<manifest::Part> {
    if !github::has_signed_manifest(release) {
        return manifest::plain_part(release);
    }
    if key.is_none() {
        return Err(Error::Update(manifest::NO_KEY.to_owned()));
    }
    let parsed = signed_manifest(http, release, key)?;
    manifest::mod_install_allowed(&parsed, running)?;
    Ok(match runtime {
        Runtime::Og => parsed.runtimes.og,
        Runtime::Ng => parsed.runtimes.ng,
        Runtime::Ae => parsed.runtimes.ae,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::update::github::Asset;
    use crate::update::http::fake::Fake;
    use crate::update::{check_with, Paths};
    use serde_json::json;

    const PREFIX: &str =
        "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.8/";
    const LIST: &str =
        "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases?per_page=100&page=1";
    const HEX: &str = "3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273";

    fn asset(name: &str, digest: Option<String>) -> Asset {
        Asset {
            name: name.to_owned(),
            size: 5,
            browser_download_url: format!("{PREFIX}{name}"),
            digest,
        }
    }

    fn listing(names: &[&str]) -> Vec<u8> {
        let assets: Vec<_> = names
            .iter()
            .map(|name| json!({"name": name, "size": 1, "browser_download_url": format!("{PREFIX}{name}"), "digest": format!("sha256:{HEX}")}))
            .collect();
        json!([{
            "tag_name": "0.0.0.8", "prerelease": true, "draft": false,
            "body": "notes", "assets": assets
        }])
        .to_string()
        .into_bytes()
    }

    type Offered = Option<(Option<String>, Option<String>)>;

    fn offered(names: &[&str], running: &str, installed: Option<&str>) -> Offered {
        let root = tempfile::tempdir().unwrap();
        let paths = Paths::new(root.path().join("state"));
        let fake = Fake::default();
        fake.serve(LIST, 200, Some("\"v1\""), &listing(names));
        check_with(&fake, &paths, running, true, installed, 1)
            .unwrap()
            .map(|offer| (offer.mod_version, offer.launcher_version))
    }

    #[test]
    fn a_plain_release_zip_is_offered_when_newer_and_silent_when_installed() {
        let names = ["CommonwealthOnline-0.0.0.8.zip"];
        let mod_only = Some((Some("0.0.0.8".to_owned()), None));
        assert_eq!(offered(&names, "0.0.0.8", Some("0.0.0.7")), mod_only);
        assert_eq!(offered(&names, "0.0.0.8", None), mod_only);
        assert_eq!(offered(&names, "0.0.0.8", Some("0.0.0.8")), None);
        assert_eq!(offered(&names, "0.0.0.8", Some("0.0.0.9")), None);
    }

    #[test]
    fn a_release_with_two_plain_zips_or_only_the_launcher_zip_is_not_offered() {
        let both = ["CommonwealthOnline-0.0.0.8.zip", "CommonwealthOnline.zip"];
        assert_eq!(offered(&both, "0.0.0.8", None), None);
        let launcher = ["CommonwealthOnline-Launcher-0.0.0.8.zip"];
        assert_eq!(offered(&launcher, "0.0.0.8", None), None);
    }

    #[test]
    fn a_plain_part_needs_no_signing_key_but_a_signed_release_does() {
        let plain = Release {
            tag_name: "0.0.0.8".to_owned(),
            prerelease: false,
            draft: false,
            body: None,
            assets: vec![asset(
                "CommonwealthOnline-0.0.0.8.zip",
                Some(format!("sha256:{HEX}")),
            )],
        };
        let fake = Fake::default();
        let part = mod_part(&fake, &plain, None, "0.0.0.7", Runtime::Ng).unwrap();
        assert_eq!((part.sha256.as_str(), part.size), (HEX, 5));
        assert!(fake.requests.borrow().is_empty());
        let signed = Release {
            assets: vec![asset("latest.json", None), asset("latest.json.sig", None)],
            ..plain
        };
        let error = mod_part(&fake, &signed, None, "0.0.0.7", Runtime::Ng).unwrap_err();
        assert_eq!(error.to_string(), manifest::NO_KEY);
        assert!(fake.requests.borrow().is_empty());
    }
}
