use super::*;
use crate::fsutil::sha256_hex;
use crate::update::http::fake::Fake;
use serde_json::json;
use std::io::{Cursor, Write};

const PREFIX: &str =
    "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases/download/0.0.0.11/";
const LIST: &str =
    "https://api.github.com/repos/G-A-R-D-E-N/Commonwealth-Online/releases?per_page=100&page=1";
const ZIP: &str = "CommonwealthOnline-0.0.0.11.zip";

fn data_zip() -> Vec<u8> {
    let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
    writer
        .start_file("Data/CommonwealthOnline.esp", SimpleFileOptions::default())
        .unwrap();
    writer.write_all(b"esp bytes").unwrap();
    writer.finish().unwrap().into_inner()
}

fn plain_release(zip: &[u8], digest: String) -> Fake {
    let list = json!([{
        "tag_name": "0.0.0.11", "prerelease": true, "draft": false, "body": "",
        "assets": [{
            "name": ZIP, "size": zip.len(),
            "browser_download_url": format!("{PREFIX}{ZIP}"), "digest": digest
        }]
    }])
    .to_string();
    let fake = Fake::default();
    fake.serve(LIST, 200, Some("\"v1\""), list.as_bytes());
    fake.serve(&format!("{PREFIX}{ZIP}"), 200, None, zip);
    fake
}

fn fetch(fake: &Fake, root: &Path, runtime: Runtime) -> Result<Fetched> {
    fetch_release(fake, root, None, "0.0.0.7", true, runtime, &mut |_, _| {})
}

#[test]
fn a_plain_release_is_fetched_without_a_key_and_checked_by_its_digest() {
    let zip = data_zip();
    let fake = plain_release(&zip, format!("sha256:{}", sha256_hex(&zip)));
    let root = tempfile::tempdir().unwrap();
    let fetched = fetch(&fake, root.path(), Runtime::Ng).unwrap();
    assert_eq!(fetched.tag, "0.0.0.11");
    assert_eq!(fetched.name, ZIP);
    assert_eq!(fs::read(&fetched.zip).unwrap(), zip);
    assert!(fake
        .requests
        .borrow()
        .iter()
        .all(|(url, _)| !url.contains("latest.json")));
}

#[test]
fn a_plain_release_with_the_wrong_digest_is_refused_and_leaves_no_folder() {
    let zip = data_zip();
    let wrong = plain_release(&zip, format!("sha256:{}", sha256_hex(b"other")));
    let root = tempfile::tempdir().unwrap();
    let error = fetch(&wrong, root.path(), Runtime::Og)
        .unwrap_err()
        .to_string();
    assert!(error.contains("checksum"), "{error}");
    assert!(!root.path().join("0.0.0.11").exists());
}

#[test]
fn a_plain_release_without_a_digest_is_not_offered_and_nothing_is_downloaded() {
    let zip = data_zip();
    let fake = plain_release(&zip, String::new());
    let root = tempfile::tempdir().unwrap();
    let error = fetch(&fake, root.path(), Runtime::Og).unwrap_err();
    assert_eq!(
        error.to_string(),
        "there is no Commonwealth Online release to download"
    );
    assert!(fake.requests.borrow().iter().all(|(url, _)| url == LIST));
    assert!(!root.path().join("0.0.0.11").exists());
}
