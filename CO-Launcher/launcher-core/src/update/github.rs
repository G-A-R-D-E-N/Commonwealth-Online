use serde::Deserialize;

use super::http::{Capped, Http};
use super::version;
use crate::error::{Error, Result};

pub const LIST_URL: &str = "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases";
pub const RELEASE_PREFIX: &str =
    "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/";
pub const MANIFEST: &str = "latest.json";
pub const SIGNATURE: &str = "latest.json.sig";
const MAX_LIST: u64 = 4 << 20;
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

pub fn fetch(http: &dyn Http, etag: Option<&str>) -> Result<Listing> {
    let mut body = Vec::new();
    let reply = http.get(LIST_URL, etag, &mut Capped::new(&mut body, MAX_LIST))?;
    match reply.status {
        304 => Ok(Listing::NotModified),
        200 => Ok(Listing::Fresh {
            releases: parse_list(&body)?,
            etag: reply.etag,
        }),
        status => Err(Error::Update(format!(
            "GitHub answered HTTP {status} for the release list"
        ))),
    }
}

pub fn parse_list(bytes: &[u8]) -> Result<Vec<Release>> {
    serde_json::from_slice(bytes)
        .map_err(|e| Error::Update(format!("the release list is not valid: {e}")))
}

pub fn select(releases: &[Release], include_prereleases: bool) -> Option<&Release> {
    releases
        .iter()
        .filter(|release| !release.draft && (include_prereleases || !release.prerelease))
        .filter_map(|release| version::parse(&release.tag_name).map(|v| (v, release)))
        .max_by(|a, b| version::compare(&a.0, &b.0))
        .map(|(_, release)| release)
}

pub fn asset<'a>(release: &'a Release, name: &str) -> Option<&'a Asset> {
    release.assets.iter().find(|asset| asset.name == name)
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

    #[test]
    fn highest_version_wins_and_junk_tags_are_ignored() {
        let list = [
            release("0.0.0.9", false, false),
            release("0.0.0.10", false, false),
            release("nightly", false, false),
            release("v0.0.0.99", false, false),
        ];
        assert_eq!(select(&list, true).unwrap().tag_name, "0.0.0.10");
    }

    #[test]
    fn prereleases_are_skipped_when_the_channel_excludes_them() {
        let list = [
            release("0.0.0.8", false, false),
            release("0.0.0.9", true, false),
        ];
        assert_eq!(select(&list, true).unwrap().tag_name, "0.0.0.9");
        assert_eq!(select(&list, false).unwrap().tag_name, "0.0.0.8");
        assert!(select(&[release("0.0.0.9", true, false)], false).is_none());
    }

    #[test]
    fn drafts_are_never_selected() {
        let list = [
            release("0.0.0.8", false, false),
            release("0.0.0.9", false, true),
        ];
        assert_eq!(select(&list, true).unwrap().tag_name, "0.0.0.8");
        assert!(select(&[release("0.0.0.9", false, true)], true).is_none());
    }

    #[test]
    fn three_and_four_component_tags_compare_by_value() {
        let list = [
            release("0.0.1", false, false),
            release("0.0.0.99", false, false),
        ];
        assert_eq!(select(&list, true).unwrap().tag_name, "0.0.1");
    }

    #[test]
    fn a_304_keeps_the_stored_etag_and_a_200_stores_the_new_one() {
        let fake = Fake::default();
        fake.serve(LIST_URL, 200, Some("\"v1\""), b"[]");
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
        fake.serve(LIST_URL, 403, None, b"rate limited");
        assert!(fetch(&fake, None).is_err());
        fake.serve(LIST_URL, 200, None, b"{not json");
        assert!(fetch(&fake, None).is_err());
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
