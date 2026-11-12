use serde::Deserialize;

use super::http::{Capped, Http};
use super::manifest;
use super::version;
use crate::error::{Error, Result};

pub const LIST_URL: &str = "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases";
pub const RELEASE_PREFIX: &str =
    "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/";
pub const MANIFEST: &str = "latest.json";
pub const SIGNATURE: &str = "latest.json.sig";
const PLAIN_ZIP: &str = "CommonwealthOnline.zip";
const MAX_LIST: u64 = 4 << 20;
const PER_PAGE: usize = 100;
const MAX_PAGES: usize = 5;
const NOTES_CHARS: usize = 2000;

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct Release {
    pub tag_name: String,
    #[serde(default)]
    pub prerelease: bool,
    #[serde(default)]
    pub draft: bool,
    #[serde(default)]
    pub body: Option<String>,
    #[serde(default)]
    pub assets: Vec<Asset>,
}

#[derive(Debug, Clone, PartialEq, Eq, Deserialize)]
pub struct Asset {
    pub name: String,
    #[serde(default)]
    pub size: u64,
    pub browser_download_url: String,
    #[serde(default)]
    pub digest: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Listing {
    NotModified,
    Fresh {
        releases: Vec<Release>,
        etag: Option<String>,
    },
}

pub fn launcher_asset(tag: &str) -> String {
    format!("CommonwealthOnline-Launcher-{tag}.zip")
}

pub fn page_url(page: usize) -> String {
    format!("{LIST_URL}?per_page={PER_PAGE}&page={page}")
}

pub fn fetch(http: &dyn Http, etag: Option<&str>) -> Result<Listing> {
    let mut releases = Vec::new();
    let mut first_etag = None;
    let mut pages = 0;
    for page in 1..=MAX_PAGES {
        let mut body = Vec::new();
        let sent = etag.filter(|_| page == 1);
        let reply = http.get(&page_url(page), sent, &mut Capped::new(&mut body, MAX_LIST))?;
        match reply.status {
            304 if page == 1 => return Ok(Listing::NotModified),
            200 => {}
            403 | 429 => {
                return Err(Error::Update(
                    "GitHub is limiting requests right now. Wait a few minutes and check again."
                        .to_owned(),
                ))
            }
            404 => {
                return Err(Error::Update(
                    "The Commonwealth Online release list was not found on GitHub.".to_owned(),
                ))
            }
            status => {
                return Err(Error::Update(format!(
                    "GitHub answered HTTP {status} for the release list"
                )))
            }
        }
        let batch = parse_list(&body)?;
        let last = batch.len() < PER_PAGE;
        releases.extend(batch);
        pages = page;
        if page == 1 {
            first_etag = reply.etag;
        }
        if last {
            break;
        }
    }
    Ok(Listing::Fresh {
        releases,
        etag: first_etag.filter(|_| pages == 1),
    })
}

pub fn parse_list(bytes: &[u8]) -> Result<Vec<Release>> {
    serde_json::from_slice(bytes)
        .map_err(|e| Error::Update(format!("the release list is not valid: {e}")))
}

pub fn select(
    releases: &[Release],
    include_prereleases: bool,
    ready: impl Fn(&Release) -> bool,
) -> Option<&Release> {
    releases
        .iter()
        .filter(|release| !release.draft && (include_prereleases || !release.prerelease))
        .filter(|release| ready(release))
        .filter_map(|release| version::parse(&release.tag_name).map(|v| (v, release)))
        .max_by(|a, b| version::compare(&a.0, &b.0))
        .map(|(_, release)| release)
}

pub fn asset<'a>(release: &'a Release, name: &str) -> Option<&'a Asset> {
    release.assets.iter().find(|asset| asset.name == name)
}

pub fn has_signed_manifest(release: &Release) -> bool {
    asset(release, MANIFEST).is_some() && asset(release, SIGNATURE).is_some()
}

pub fn mod_zip(release: &Release) -> Option<&Asset> {
    let versioned = format!("CommonwealthOnline-{}.zip", release.tag_name);
    let mut found = release
        .assets
        .iter()
        .filter(|asset| asset.name == versioned || asset.name == PLAIN_ZIP);
    let first = found.next()?;
    found.next().is_none().then_some(first)
}

pub fn mod_ready(release: &Release) -> bool {
    has_signed_manifest(release) || manifest::plain_part(release).is_ok()
}

pub fn notes(release: &Release) -> Option<String> {
    release
        .body
        .as_deref()
        .map(|body| body.chars().take(NOTES_CHARS).collect::<String>())
        .filter(|text| !text.trim().is_empty())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::update::http::fake::Fake;

    fn release(tag: &str, prerelease: bool, draft: bool) -> Release {
        Release {
            tag_name: tag.to_owned(),
            prerelease,
            draft,
            body: Some("notes".to_owned()),
            assets: Vec::new(),
        }
    }

    const LAUNCHER: &str = "launcher.zip";
    const HEX: &str = "3f9a0c7e52b18d64a9e1f0c3d7b2e58a46c1f9d0b3e7a2c5d8f1b4e6a9c0d273";

    fn any(_: &Release) -> bool {
        true
    }

    #[test]
    fn highest_version_wins_and_junk_tags_are_ignored() {
        let list = [
            release("0.0.0.9", false, false),
            release("0.0.0.10", false, false),
            release("nightly", false, false),
            release("v0.0.0.99", false, false),
        ];
        assert_eq!(select(&list, true, any).unwrap().tag_name, "0.0.0.10");
    }

    #[test]
    fn prereleases_are_skipped_when_the_channel_excludes_them() {
        let list = [
            release("0.0.0.8", false, false),
            release("0.0.0.9", true, false),
        ];
        assert_eq!(select(&list, true, any).unwrap().tag_name, "0.0.0.9");
        assert_eq!(select(&list, false, any).unwrap().tag_name, "0.0.0.8");
        assert!(select(&[release("0.0.0.9", true, false)], false, any).is_none());
    }

    #[test]
    fn drafts_are_never_selected() {
        let list = [
            release("0.0.0.8", false, false),
            release("0.0.0.9", false, true),
        ];
        assert_eq!(select(&list, true, any).unwrap().tag_name, "0.0.0.8");
        assert!(select(&[release("0.0.0.9", false, true)], true, any).is_none());
    }

    #[test]
    fn three_and_four_component_tags_compare_by_value() {
        let list = [
            release("0.0.1", false, false),
            release("0.0.0.99", false, false),
        ];
        assert_eq!(select(&list, true, any).unwrap().tag_name, "0.0.1");
    }

    #[test]
    fn the_newest_release_that_has_the_asset_is_selected() {
        let with = |tag: &str, prerelease: bool, names: &[&str]| Release {
            assets: names
                .iter()
                .map(|name| Asset {
                    name: (*name).to_owned(),
                    size: 1,
                    browser_download_url: String::new(),
                    digest: None,
                })
                .collect(),
            ..release(tag, prerelease, false)
        };
        let list = [
            with(
                "0.0.0.8",
                false,
                &[LAUNCHER, "latest.json", "latest.json.sig"],
            ),
            with("0.0.0.9", false, &["latest.json", "latest.json.sig"]),
            with(
                "0.0.0.10",
                true,
                &[LAUNCHER, "latest.json", "latest.json.sig"],
            ),
        ];
        let launcher = |r: &Release| asset(r, LAUNCHER).is_some();
        assert_eq!(select(&list, true, launcher).unwrap().tag_name, "0.0.0.10");
        assert_eq!(select(&list, false, launcher).unwrap().tag_name, "0.0.0.8");
        assert_eq!(
            select(&list, true, has_signed_manifest).unwrap().tag_name,
            "0.0.0.10"
        );
        assert!(select(&list, true, |r| asset(r, "missing.zip").is_some()).is_none());
    }

    fn with_assets(tag: &str, names: &[&str]) -> Release {
        Release {
            assets: names
                .iter()
                .map(|name| Asset {
                    name: (*name).to_owned(),
                    size: 1,
                    browser_download_url: format!("{RELEASE_PREFIX}{tag}/{name}"),
                    digest: Some(format!("sha256:{HEX}")),
                })
                .collect(),
            ..release(tag, false, false)
        }
    }

    #[test]
    fn the_mod_zip_is_the_one_versioned_or_plain_archive() {
        let named = |release: &Release| mod_zip(release).map(|asset| asset.name.clone());
        let versioned = with_assets("0.0.0.9", &["CommonwealthOnline-0.0.0.9.zip", "notes.txt"]);
        assert_eq!(
            named(&versioned).as_deref(),
            Some("CommonwealthOnline-0.0.0.9.zip")
        );
        let plain = with_assets("0.0.0.9", &["CommonwealthOnline.zip"]);
        assert_eq!(named(&plain).as_deref(), Some("CommonwealthOnline.zip"));
        assert!(mod_ready(&plain));
    }

    #[test]
    fn two_matching_archives_a_wrong_tag_or_the_launcher_zip_are_not_a_mod_zip() {
        let both = with_assets(
            "0.0.0.9",
            &["CommonwealthOnline-0.0.0.9.zip", "CommonwealthOnline.zip"],
        );
        assert!(mod_zip(&both).is_none() && !mod_ready(&both));
        let other_tag = with_assets("0.0.0.9", &["CommonwealthOnline-0.0.0.8.zip"]);
        assert!(mod_zip(&other_tag).is_none());
        let launcher_name = launcher_asset("0.0.0.9");
        let launcher = with_assets("0.0.0.9", &[launcher_name.as_str()]);
        assert!(mod_zip(&launcher).is_none() && !mod_ready(&launcher));
        assert!(mod_zip(&with_assets("0.0.0.9", &[])).is_none());
    }

    #[test]
    fn a_plain_zip_without_a_usable_digest_is_not_ready_and_does_not_hide_an_older_release() {
        let older = with_assets("0.0.0.9", &["CommonwealthOnline.zip"]);
        let mut bare = with_assets("0.0.0.10", &["CommonwealthOnline.zip"]);
        bare.assets[0].digest = None;
        assert!(!mod_ready(&bare));
        let mut bad = with_assets("0.0.0.10", &["CommonwealthOnline.zip"]);
        bad.assets[0].digest = Some("sha256:abc".to_owned());
        assert!(!mod_ready(&bad));
        let list = [older, bare];
        assert_eq!(select(&list, true, mod_ready).unwrap().tag_name, "0.0.0.9");
    }

    #[test]
    fn a_signed_manifest_makes_a_release_mod_ready_without_a_plain_zip() {
        let signed = with_assets("0.0.0.9", &["latest.json", "latest.json.sig"]);
        assert!(mod_zip(&signed).is_none() && mod_ready(&signed));
        let unsigned = with_assets("0.0.0.9", &["latest.json"]);
        assert!(!mod_ready(&unsigned));
    }

    #[test]
    fn an_asset_digest_is_read_when_present_and_optional_when_not() {
        let list = parse_list(
            br#"[{"tag_name":"0.0.0.9","assets":[
                {"name":"a.zip","browser_download_url":"","digest":"sha256:ab"},
                {"name":"b.zip","browser_download_url":"","digest":null},
                {"name":"c.zip","browser_download_url":""}]}]"#,
        )
        .unwrap();
        let digests: Vec<_> = list[0].assets.iter().map(|a| a.digest.as_deref()).collect();
        assert_eq!(digests, [Some("sha256:ab"), None, None]);
    }

    #[test]
    fn a_304_keeps_the_stored_etag_and_a_200_stores_the_new_one() {
        let fake = Fake::default();
        fake.serve(&page_url(1), 200, Some("\"v1\""), b"[]");
        match fetch(&fake, None).unwrap() {
            Listing::Fresh { releases, etag } => {
                assert!(releases.is_empty());
                assert_eq!(etag.as_deref(), Some("\"v1\""));
            }
            other => panic!("unexpected {other:?}"),
        }
        assert_eq!(fetch(&fake, Some("\"v1\"")).unwrap(), Listing::NotModified);
        let sent = fake.requests.borrow();
        assert_eq!(sent[1].1.as_deref(), Some("\"v1\""));
    }

    #[test]
    fn list_errors_and_bad_json_are_reported() {
        let fake = Fake::default();
        fake.serve(&page_url(1), 403, None, b"rate limited");
        assert!(fetch(&fake, None)
            .unwrap_err()
            .to_string()
            .contains("limiting requests"));
        fake.serve(&page_url(1), 404, None, b"");
        assert!(fetch(&fake, None)
            .unwrap_err()
            .to_string()
            .contains("was not found"));
        fake.serve(&page_url(1), 200, None, b"{not json");
        assert!(fetch(&fake, None).is_err());
    }

    #[test]
    fn a_release_on_a_later_page_is_found_and_the_etag_is_not_kept() {
        let fake = Fake::default();
        let first: Vec<String> = (0..PER_PAGE)
            .map(|i| format!(r#"{{"tag_name":"0.0.0.{i}"}}"#))
            .collect();
        fake.serve(
            &page_url(1),
            200,
            Some("\"v1\""),
            format!("[{}]", first.join(",")).as_bytes(),
        );
        fake.serve(
            &page_url(2),
            200,
            None,
            br#"[{"tag_name":"0.0.1.0","assets":[{"name":"launcher.zip","browser_download_url":""}]}]"#,
        );
        let Listing::Fresh { releases, etag } = fetch(&fake, None).unwrap() else {
            panic!("the list was not fresh");
        };
        assert_eq!(releases.len(), PER_PAGE + 1);
        assert_eq!(etag, None);
        let launcher = |r: &Release| asset(r, LAUNCHER).is_some();
        assert_eq!(
            select(&releases, true, launcher).unwrap().tag_name,
            "0.0.1.0"
        );
        assert_eq!(fake.requests.borrow().len(), 2);
    }

    #[test]
    fn notes_are_cut_to_two_thousand_characters() {
        let mut long = release("0.0.0.9", false, false);
        long.body = Some("é".repeat(3000));
        assert_eq!(notes(&long).unwrap().chars().count(), 2000);
        long.body = Some("   ".to_owned());
        assert!(notes(&long).is_none());
    }
}
