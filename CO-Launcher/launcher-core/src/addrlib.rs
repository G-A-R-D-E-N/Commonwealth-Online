use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{Read, Write};
use std::path::{Path, PathBuf};

use zip::write::SimpleFileOptions;
use zip::ZipArchive;

use crate::error::{Error, Result};
use crate::fsutil::{find_ci, resolve_ci, sha256_hex, write_atomic};
use crate::modinstall::LIMITS;
use crate::modlist::{enable_mod, enabled_names, profile_modlist, ModSwitch};
use crate::modmgr::{read_bytes_capped, write_replacing};
use crate::runtime::Runtime;

pub const ADDRLIB_MOD: &str = "Address Library for F4SE Plugins";

pub const STORE_DIR: &str = "Address Library";

const MAX_BIN: u64 = 16 << 20;
const META: &str = "[General]\ngameName=Fallout4\nmodid=0\nversion=";
// Constraint: unverified cross-check list; a mismatch is a warning for the user, never a refusal.
const KNOWN: [(Runtime, &str); 3] = [
    (
        Runtime::Og,
        "d849ae8989c54a2ceebbc587c2a77f62a44f74d0c42a42e0bed30d65118d3395",
    ),
    (
        Runtime::Ng,
        "c13545f2dab806b7d19c5aa83a0d7524337b5afc89af79460f9915020e049d75",
    ),
    (
        Runtime::Ae,
        "65985cc2259384a13cffb538d74776e422e62b0cd3766485a000620242b72b06",
    ),
];

pub fn file_name(runtime: Runtime) -> String {
    format!("version-{}-0.bin", runtime.game_version().replace('.', "-"))
}

pub fn known_sha256(runtime: Runtime) -> &'static str {
    KNOWN
        .iter()
        .find(|(r, _)| *r == runtime)
        .map(|(_, sha)| *sha)
        .unwrap_or("")
}

pub fn matches_known(runtime: Runtime, bytes: &[u8]) -> bool {
    sha256_hex(bytes) == known_sha256(runtime)
}

fn bad(text: impl Into<String>) -> Error {
    Error::Update(text.into())
}

fn archive(error: impl ToString) -> Error {
    Error::Archive(error.to_string())
}

fn read_capped(path: &Path) -> Result<Vec<u8>> {
    read_bytes_capped(path).ok_or_else(|| {
        bad(format!(
            "{} is missing, unreadable or larger than an Address Library file can be",
            path.display()
        ))
    })
}

fn wanted_entry(name: &str, file: &str) -> bool {
    if name.contains('\\') {
        return false;
    }
    let rest = match name.get(..5) {
        Some(head) if head.eq_ignore_ascii_case("Data/") => &name[5..],
        _ => name,
    };
    let parts: Vec<&str> = rest.split('/').collect();
    let Some((last, folders)) = parts.split_last() else {
        return false;
    };
    if !last.eq_ignore_ascii_case(file) {
        return false;
    }
    match folders {
        [] => true,
        _ => {
            f4se_plugins_under_data(folders)
                || (!folders.is_empty() && f4se_plugins_under_data(&folders[1..]))
        }
    }
}

fn f4se_plugins_under_data(folders: &[&str]) -> bool {
    let inner = match folders {
        [first, rest @ ..] if first.eq_ignore_ascii_case("Data") => rest,
        _ => folders,
    };
    matches!(
        inner,
        [a, b] if a.eq_ignore_ascii_case("F4SE") && b.eq_ignore_ascii_case("Plugins")
    )
}

pub fn from_zip(path: &Path, runtime: Runtime) -> Result<Vec<u8>> {
    let wanted = file_name(runtime);
    let file = File::open(path).map_err(Error::io(path))?;
    let mut zip = ZipArchive::new(file).map_err(archive)?;
    if zip.len() > LIMITS.entries {
        return Err(bad("the Address Library zip has too many files"));
    }
    let mut hit = None;
    for index in 0..zip.len() {
        let entry = zip.by_index(index).map_err(archive)?;
        let name = entry.name().map_err(archive)?.into_owned();
        if entry.is_dir() || !wanted_entry(&name, &wanted) {
            continue;
        }
        if entry.is_symlink() {
            return Err(bad("the Address Library zip holds a symbolic link"));
        }
        hit = Some(index);
        break;
    }
    let index = hit.ok_or_else(|| {
        bad(format!(
            "the zip has no {wanted} plugin file, so it is not the Address Library build for {}",
            runtime.label()
        ))
    })?;
    let mut entry = zip.by_index(index).map_err(archive)?;
    if entry.size() > MAX_BIN {
        return Err(bad(format!("{wanted} in the zip is too large")));
    }
    let mut bytes = Vec::new();
    (&mut entry)
        .take(MAX_BIN + 1)
        .read_to_end(&mut bytes)
        .map_err(archive)?;
    if bytes.is_empty() || bytes.len() as u64 > MAX_BIN {
        return Err(bad(format!("{wanted} in the zip is empty or too large")));
    }
    Ok(bytes)
}

pub fn import(path: &Path, runtime: Runtime) -> Result<Vec<u8>> {
    let name = path
        .file_name()
        .and_then(|n| n.to_str())
        .unwrap_or_default();
    let ext = path
        .extension()
        .and_then(|e| e.to_str())
        .unwrap_or_default()
        .to_ascii_lowercase();
    let wanted = file_name(runtime);
    match ext.as_str() {
        "zip" => from_zip(path, runtime),
        "bin" if name.eq_ignore_ascii_case(&wanted) => {
            let bytes = read_capped(path)?;
            if bytes.is_empty() {
                return Err(bad(format!("{name} is empty")));
            }
            Ok(bytes)
        }
        "bin" => Err(bad(format!(
            "{name} is not the file for {}. Download {wanted} instead.",
            runtime.label()
        ))),
        "7z" => Err(bad(
            "7z archives are not supported. Extract the archive, then choose the .zip or the version-*.bin file.",
        )),
        _ => Err(bad(
            "Choose the Address Library .zip or the version-*.bin file.",
        )),
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Origin {
    GameData,
    Mo2Mod(String),
    VortexMod(String),
    Store,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Found {
    pub path: PathBuf,
    pub origin: Origin,
    pub usable: bool,
    pub known: bool,
}

pub struct Search<'a> {
    pub runtime: Runtime,
    pub game: Option<&'a Path>,
    pub mo2: Option<(&'a Path, &'a str)>,
    pub vortex_staging: Option<&'a Path>,
    pub vortex_deployed: &'a HashSet<String>,
    pub store: Option<&'a Path>,
}

fn subfolders(dir: &Path) -> Vec<PathBuf> {
    fs::read_dir(dir)
        .map(|entries| {
            entries
                .flatten()
                .map(|e| e.path())
                .filter(|p| p.is_dir())
                .collect()
        })
        .unwrap_or_default()
}

fn folder_name(dir: &Path) -> String {
    dir.file_name()
        .map(|n| n.to_string_lossy().into_owned())
        .unwrap_or_default()
}

fn plugin_paths(dir: &Path, file: &str) -> [PathBuf; 2] {
    [
        resolve_ci(dir, &["F4SE", "Plugins", file]),
        resolve_ci(dir, &["Data", "F4SE", "Plugins", file]),
    ]
}

pub fn locate(search: &Search) -> Vec<Found> {
    let file = file_name(search.runtime);
    let mut out = Vec::new();
    let mut consider = |path: PathBuf, origin: Origin, usable: bool| {
        if let Ok(bytes) = read_capped(&path) {
            if bytes.is_empty() {
                return;
            }
            let known = matches_known(search.runtime, &bytes);
            out.push(Found {
                path,
                origin,
                usable,
                known,
            });
        }
    };
    if let Some(game) = search.game {
        consider(
            resolve_ci(game, &["Data", "F4SE", "Plugins", file.as_str()]),
            Origin::GameData,
            true,
        );
    }
    if let Some((instance, profile)) = search.mo2 {
        let on = profile_modlist(instance, profile)
            .and_then(|p| read_capped(&p).ok())
            .map(|b| enabled_names(&String::from_utf8_lossy(&b)))
            .unwrap_or_default();
        if let Some(mods) = find_ci(instance, "mods") {
            for dir in subfolders(&mods) {
                let folder = folder_name(&dir);
                let usable = on.contains(&folder.to_ascii_lowercase());
                for path in plugin_paths(&dir, &file) {
                    consider(path, Origin::Mo2Mod(folder.clone()), usable);
                }
            }
        }
    }
    if let Some(staging) = search.vortex_staging {
        for dir in subfolders(staging) {
            let folder = folder_name(&dir);
            let usable = search
                .vortex_deployed
                .contains(&folder.to_ascii_lowercase());
            for path in plugin_paths(&dir, &file) {
                consider(path, Origin::VortexMod(folder.clone()), usable);
            }
        }
    }
    if let Some(store) = search.store {
        consider(
            resolve_ci(store, &[STORE_DIR, "F4SE", "Plugins", file.as_str()]),
            Origin::Store,
            false,
        );
    }
    out
}

// Constraint: a backup is never overwritten; the next free <path>.bakN name is used.
fn backup_path(path: &Path) -> PathBuf {
    let plain = PathBuf::from(format!("{}.bak", path.display()));
    if !plain.exists() {
        return plain;
    }
    (1..)
        .map(|n| PathBuf::from(format!("{}.bak{n}", path.display())))
        .find(|candidate| !candidate.exists())
        .unwrap_or(plain)
}

fn put(path: &Path, bytes: &[u8]) -> Result<Option<PathBuf>> {
    if fs::read(path).is_ok_and(|existing| existing == bytes) {
        return Ok(None);
    }
    let backup = if path.exists() {
        let bak = backup_path(path);
        fs::rename(path, &bak).map_err(Error::io(path))?;
        Some(bak)
    } else {
        None
    };
    match write_atomic(path, bytes) {
        Ok(()) => Ok(backup),
        Err(error) => {
            if let Some(bak) = &backup {
                let _ = fs::rename(bak, path);
            }
            Err(error)
        }
    }
}

pub fn store(root: &Path, runtime: Runtime, bytes: &[u8]) -> Result<PathBuf> {
    let path = root
        .join(STORE_DIR)
        .join("F4SE")
        .join("Plugins")
        .join(file_name(runtime));
    put(&path, bytes)?;
    Ok(path)
}

pub fn link_game_data(game: &Path, runtime: Runtime, bytes: &[u8]) -> Result<PathBuf> {
    let data = find_ci(game, "Data")
        .filter(|p| p.is_dir())
        .ok_or_else(|| bad("the game folder has no Data folder"))?;
    let path = resolve_ci(&data, &["F4SE", "Plugins"]).join(file_name(runtime));
    put(&path, bytes)?;
    Ok(path)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Mo2Link {
    pub created: bool,
}

pub fn link_mo2(instance: &Path, profile: &str, runtime: Runtime, bytes: &[u8]) -> Result<Mo2Link> {
    let mods = find_ci(instance, "mods")
        .filter(|p| p.is_dir())
        .ok_or_else(|| bad("the instance has no mods folder"))?;
    let dir = mods.join(ADDRLIB_MOD);
    let created = !dir.exists();
    let linked = (|| -> Result<()> {
        put(
            &dir.join("F4SE").join("Plugins").join(file_name(runtime)),
            bytes,
        )?;
        if created {
            let meta = format!("{META}{}\n", runtime.game_version());
            write_atomic(&dir.join("meta.ini"), meta.as_bytes())?;
        }
        match enable_mod(instance, profile, ADDRLIB_MOD, true) {
            ModSwitch::Done | ModSwitch::Already => Ok(()),
            _ => Err(bad(
                "could not turn the Address Library mod on in the profile's modlist.txt",
            )),
        }
    })();
    match linked {
        Ok(()) => Ok(Mo2Link { created }),
        Err(error) => {
            if created {
                let _ = fs::remove_dir_all(&dir);
            }
            Err(error)
        }
    }
}

pub fn unlink_mo2(instance: &Path, profile: &str) -> Result<()> {
    if let Some(mods) = find_ci(instance, "mods") {
        let dir = mods.join(ADDRLIB_MOD);
        if dir.exists() {
            fs::remove_dir_all(&dir).map_err(Error::io(&dir))?;
        }
    }
    if let Some(list) = profile_modlist(instance, profile) {
        if let Some(text) = read_bytes_capped(&list).and_then(|b| String::from_utf8(b).ok()) {
            let next = without_entry(&text, ADDRLIB_MOD);
            if next != text && !write_replacing(&list, &next) {
                return Err(bad("could not update modlist.txt"));
            }
        }
    }
    Ok(())
}

fn without_entry(text: &str, name: &str) -> String {
    let eol = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let kept: Vec<&str> = text
        .lines()
        .filter(|line| {
            !(matches!(line.as_bytes().first(), Some(b'+' | b'-'))
                && line.get(1..).is_some_and(|n| n.eq_ignore_ascii_case(name)))
        })
        .collect();
    if kept.len() == text.lines().count() {
        return text.to_owned();
    }
    let mut out = kept.join(eol);
    out.push_str(eol);
    out
}

pub(crate) fn write_zip_atomic(
    out: &Path,
    write: impl FnOnce(&mut zip::ZipWriter<File>) -> Result<()>,
) -> Result<()> {
    if let Some(parent) = out.parent() {
        fs::create_dir_all(parent).map_err(Error::io(parent))?;
    }
    let temp = PathBuf::from(format!("{}.co-tmp", out.display()));
    let written = (|| -> Result<()> {
        let file = File::create(&temp).map_err(Error::io(&temp))?;
        let mut writer = zip::ZipWriter::new(file);
        write(&mut writer)?;
        writer.finish().map_err(archive)?;
        fs::rename(&temp, out).map_err(Error::io(out))
    })();
    if written.is_err() {
        let _ = fs::remove_file(&temp);
    }
    written
}

pub(crate) fn write_zip(out: &Path, entries: &[(String, Vec<u8>)]) -> Result<()> {
    write_zip_atomic(out, |writer| {
        for (name, bytes) in entries {
            writer
                .start_file(name.as_str(), SimpleFileOptions::default())
                .map_err(archive)?;
            writer.write_all(bytes).map_err(Error::io(out))?;
        }
        Ok(())
    })
}

pub fn vortex_zip(dir: &Path, runtime: Runtime, bytes: &[u8]) -> Result<PathBuf> {
    let out = dir.join(format!("Address Library {} (Vortex).zip", runtime.label()));

    let entry = format!("F4SE/Plugins/{}", file_name(runtime));
    write_zip(&out, &[(entry, bytes.to_vec())])?;
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    const OG_BYTES: &[u8] = b"og library bytes";

    fn zip_with(entries: &[(&str, &[u8])]) -> Vec<u8> {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        for (name, bytes) in entries {
            writer
                .start_file(*name, SimpleFileOptions::default())
                .unwrap();
            writer.write_all(bytes).unwrap();
        }
        writer.finish().unwrap().into_inner()
    }

    fn write(path: &Path, bytes: &[u8]) {
        fs::create_dir_all(path.parent().unwrap()).unwrap();
        fs::write(path, bytes).unwrap();
    }

    fn instance_with_list(list: &str) -> tempfile::TempDir {
        let dir = tempfile::tempdir().unwrap();
        fs::create_dir_all(dir.path().join("mods")).unwrap();
        write(
            &dir.path()
                .join("profiles")
                .join("Default")
                .join("modlist.txt"),
            list.as_bytes(),
        );
        dir
    }

    fn put_entries(entries: &[(&str, &[u8])]) -> tempfile::NamedTempFile {
        let file = tempfile::NamedTempFile::new().unwrap();
        fs::write(file.path(), zip_with(entries)).unwrap();
        file
    }

    #[test]
    fn a_zip_with_a_wrapper_folder_or_a_root_bin_is_accepted() {
        let wrapped =
            put_entries(&[("Address/Data/F4SE/Plugins/version-1-10-163-0.bin", OG_BYTES)]);
        assert_eq!(from_zip(wrapped.path(), Runtime::Og).unwrap(), OG_BYTES);
        let folder = put_entries(&[("Address/F4SE/Plugins/version-1-10-163-0.bin", OG_BYTES)]);
        assert_eq!(from_zip(folder.path(), Runtime::Og).unwrap(), OG_BYTES);
        let root = put_entries(&[("version-1-10-163-0.bin", OG_BYTES)]);
        assert_eq!(from_zip(root.path(), Runtime::Og).unwrap(), OG_BYTES);
        let deep = put_entries(&[("a/b/c/F4SE/Plugins/version-1-10-163-0.bin", OG_BYTES)]);
        assert!(from_zip(deep.path(), Runtime::Og).is_err());
    }

    #[test]
    fn a_symlink_an_oversized_entry_and_hostile_names_are_refused() {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        writer
            .add_symlink(
                "F4SE/Plugins/version-1-10-163-0.bin",
                "/etc/passwd",
                SimpleFileOptions::default(),
            )
            .unwrap();
        let link = tempfile::NamedTempFile::new().unwrap();
        fs::write(link.path(), writer.finish().unwrap().into_inner()).unwrap();
        assert!(from_zip(link.path(), Runtime::Og)
            .unwrap_err()
            .to_string()
            .contains("symbolic link"));

        let big = vec![0u8; (MAX_BIN + 1) as usize];
        let oversized = put_entries(&[("F4SE/Plugins/version-1-10-163-0.bin", &big)]);
        assert!(from_zip(oversized.path(), Runtime::Og)
            .unwrap_err()
            .to_string()
            .contains("too large"));

        let hostile = put_entries(&[
            ("F4SE/Plugins/../version-1-10-163-0.bin", OG_BYTES),
            ("..\\F4SE\\Plugins\\version-1-10-163-0.bin", OG_BYTES),
        ]);
        assert!(from_zip(hostile.path(), Runtime::Og).is_err());
    }

    #[test]
    fn a_zip_with_too_many_entries_is_refused() {
        let mut writer = zip::ZipWriter::new(Cursor::new(Vec::new()));
        for index in 0..=LIMITS.entries {
            writer
                .start_file(format!("junk/{index}.txt"), SimpleFileOptions::default())
                .unwrap();
        }
        let many = tempfile::NamedTempFile::new().unwrap();
        fs::write(many.path(), writer.finish().unwrap().into_inner()).unwrap();
        assert!(from_zip(many.path(), Runtime::Og)
            .unwrap_err()
            .to_string()
            .contains("too many files"));
    }

    #[test]
    fn a_third_differing_write_keeps_every_earlier_backup() {
        let root = tempfile::tempdir().unwrap();
        let path = root.path().join("version-1-10-163-0.bin");
        put(&path, b"first").unwrap();
        put(&path, b"second").unwrap();
        put(&path, b"third").unwrap();
        assert_eq!(fs::read(&path).unwrap(), b"third");
        assert_eq!(
            fs::read(root.path().join("version-1-10-163-0.bin.bak")).unwrap(),
            b"first"
        );
        assert_eq!(
            fs::read(root.path().join("version-1-10-163-0.bin.bak1")).unwrap(),
            b"second"
        );
    }

    #[test]
    fn file_names_and_allow_list_follow_the_runtime() {
        assert_eq!(file_name(Runtime::Og), "version-1-10-163-0.bin");
        assert_eq!(file_name(Runtime::Ng), "version-1-10-984-0.bin");
        assert_eq!(file_name(Runtime::Ae), "version-1-11-240-0.bin");
        assert_eq!(
            known_sha256(Runtime::Og),
            "d849ae8989c54a2ceebbc587c2a77f62a44f74d0c42a42e0bed30d65118d3395"
        );
        assert!(!matches_known(Runtime::Ng, OG_BYTES));
    }

    #[test]
    fn a_zip_yields_only_the_bin_for_the_detected_runtime() {
        let dir = tempfile::tempdir().unwrap();
        let zip = dir.path().join("address.zip");
        fs::write(
            &zip,
            zip_with(&[
                ("F4SE/Plugins/version-1-10-984-0.bin", b"ng"),
                ("Data/F4SE/Plugins/version-1-10-163-0.bin", OG_BYTES),
                ("readme.txt", b"hi"),
            ]),
        )
        .unwrap();
        assert_eq!(from_zip(&zip, Runtime::Og).unwrap(), OG_BYTES);
        assert_eq!(from_zip(&zip, Runtime::Ng).unwrap(), b"ng");
        let missing = from_zip(&zip, Runtime::Ae).unwrap_err().to_string();
        assert!(missing.contains("version-1-11-240-0.bin"), "{missing}");
    }

    #[test]
    fn import_checks_the_name_and_refuses_7z() {
        let dir = tempfile::tempdir().unwrap();
        let right = dir.path().join("version-1-10-163-0.bin");
        fs::write(&right, OG_BYTES).unwrap();
        assert_eq!(import(&right, Runtime::Og).unwrap(), OG_BYTES);
        let wrong = import(&right, Runtime::Ae).unwrap_err().to_string();
        assert!(wrong.contains("version-1-11-240-0.bin"), "{wrong}");
        let seven = dir.path().join("address.7z");
        fs::write(&seven, b"7z").unwrap();
        assert!(import(&seven, Runtime::Og)
            .unwrap_err()
            .to_string()
            .contains("7z archives are not supported"));
        let other = dir.path().join("notes.txt");
        fs::write(&other, b"x").unwrap();
        assert!(import(&other, Runtime::Og).is_err());
    }

    #[test]
    fn locate_reports_mo2_enabled_disabled_vortex_deployed_and_store() {
        let root = tempfile::tempdir().unwrap();
        let game = root.path().join("game");
        let instance = instance_with_list("+Library Mod\n-Old Copy\n");
        let name = file_name(Runtime::Og);
        write(
            &instance
                .path()
                .join("mods")
                .join("Library Mod")
                .join("F4SE")
                .join("Plugins")
                .join(&name),
            OG_BYTES,
        );
        write(
            &instance
                .path()
                .join("mods")
                .join("Old Copy")
                .join("Data")
                .join("F4SE")
                .join("Plugins")
                .join(&name),
            OG_BYTES,
        );
        let staging = root.path().join("staging");
        write(
            &staging
                .join("Vortex Copy")
                .join("F4SE")
                .join("Plugins")
                .join(&name),
            OG_BYTES,
        );
        write(
            &game.join("Data").join("F4SE").join("Plugins").join(&name),
            OG_BYTES,
        );
        let store = root.path().join("downloads");
        write(
            &store
                .join(STORE_DIR)
                .join("F4SE")
                .join("Plugins")
                .join(&name),
            OG_BYTES,
        );
        let deployed: HashSet<String> = ["vortex copy".to_owned()].into_iter().collect();
        let found = locate(&Search {
            runtime: Runtime::Og,
            game: Some(&game),
            mo2: Some((instance.path(), "Default")),
            vortex_staging: Some(&staging),
            vortex_deployed: &deployed,
            store: Some(&store),
        });
        let usable = |origin: Origin| found.iter().find(|f| f.origin == origin).map(|f| f.usable);
        assert_eq!(usable(Origin::Mo2Mod("Library Mod".into())), Some(true));
        assert_eq!(usable(Origin::Mo2Mod("Old Copy".into())), Some(false));
        assert_eq!(usable(Origin::VortexMod("Vortex Copy".into())), Some(true));
        assert_eq!(usable(Origin::GameData), Some(true));
        assert_eq!(usable(Origin::Store), Some(false));
        assert!(found.iter().all(|f| !f.known));
    }

    #[test]
    fn empty_address_library_is_not_reported_as_usable() {
        let root = tempfile::tempdir().unwrap();
        let file = root
            .path()
            .join("Data")
            .join("F4SE")
            .join("Plugins")
            .join(file_name(Runtime::Og));
        write(&file, b"");
        let found = locate(&Search {
            runtime: Runtime::Og,
            game: Some(root.path()),
            mo2: None,
            vortex_staging: None,
            vortex_deployed: &HashSet::new(),
            store: None,
        });
        assert!(found.is_empty());
    }

    #[test]
    fn locate_with_nothing_present_is_empty() {
        let root = tempfile::tempdir().unwrap();
        let deployed = HashSet::new();
        let found = locate(&Search {
            runtime: Runtime::Ae,
            game: Some(root.path()),
            mo2: None,
            vortex_staging: None,
            vortex_deployed: &deployed,
            store: None,
        });
        assert!(found.is_empty());
    }

    #[test]
    fn mo2_link_creates_enables_and_unlink_restores_the_modlist_exactly() {
        let instance = instance_with_list("+Main\r\n");
        let list = instance
            .path()
            .join("profiles")
            .join("Default")
            .join("modlist.txt");
        let link = link_mo2(instance.path(), "Default", Runtime::Og, OG_BYTES).unwrap();
        assert!(link.created);
        let plugin = instance
            .path()
            .join("mods")
            .join(ADDRLIB_MOD)
            .join("F4SE")
            .join("Plugins")
            .join(file_name(Runtime::Og));
        assert_eq!(fs::read(&plugin).unwrap(), OG_BYTES);
        assert!(instance
            .path()
            .join("mods")
            .join(ADDRLIB_MOD)
            .join("meta.ini")
            .is_file());
        let text = fs::read_to_string(&list).unwrap();
        assert!(text.contains(&format!("+{ADDRLIB_MOD}")), "{text}");
        let again = link_mo2(instance.path(), "Default", Runtime::Og, OG_BYTES).unwrap();
        assert!(!again.created);
        unlink_mo2(instance.path(), "Default").unwrap();
        assert!(!instance.path().join("mods").join(ADDRLIB_MOD).exists());
        assert_eq!(fs::read_to_string(&list).unwrap(), "+Main\r\n");
    }

    #[test]
    fn a_failed_mo2_link_removes_the_folder_it_made() {
        let instance = tempfile::tempdir().unwrap();
        fs::create_dir_all(instance.path().join("mods")).unwrap();
        assert!(link_mo2(instance.path(), "Default", Runtime::Og, OG_BYTES).is_err());
        assert!(!instance.path().join("mods").join(ADDRLIB_MOD).exists());
    }

    #[test]
    fn game_data_copy_keeps_a_backup_of_a_different_file() {
        let game = tempfile::tempdir().unwrap();
        fs::create_dir_all(game.path().join("Data")).unwrap();
        let name = file_name(Runtime::Ae);
        let target = game
            .path()
            .join("Data")
            .join("F4SE")
            .join("Plugins")
            .join(&name);
        let path = link_game_data(game.path(), Runtime::Ae, b"new").unwrap();
        assert_eq!(path, target);
        assert!(!target.with_extension("bin.bak").exists());
        link_game_data(game.path(), Runtime::Ae, b"newer").unwrap();
        assert_eq!(fs::read(&target).unwrap(), b"newer");
        assert_eq!(
            fs::read(PathBuf::from(format!("{}.bak", target.display()))).unwrap(),
            b"new"
        );
        link_game_data(game.path(), Runtime::Ae, b"newer").unwrap();
        assert_eq!(
            fs::read(PathBuf::from(format!("{}.bak", target.display()))).unwrap(),
            b"new"
        );
    }

    #[test]
    fn store_and_vortex_zip_write_the_expected_layout() {
        let root = tempfile::tempdir().unwrap();
        let stored = store(root.path(), Runtime::Og, OG_BYTES).unwrap();
        assert_eq!(fs::read(&stored).unwrap(), OG_BYTES);
        let zip_path = vortex_zip(root.path(), Runtime::Og, OG_BYTES).unwrap();
        let mut archive = ZipArchive::new(File::open(&zip_path).unwrap()).unwrap();
        let mut entry = archive
            .by_name("F4SE/Plugins/version-1-10-163-0.bin")
            .unwrap();
        let mut body = Vec::new();
        entry.read_to_end(&mut body).unwrap();
        assert_eq!(body, OG_BYTES);
    }

    #[test]
    fn removing_an_entry_keeps_every_other_line() {
        let text = format!("+Main\r\n-{ADDRLIB_MOD}\r\n+Other\r\n");
        assert_eq!(without_entry(&text, ADDRLIB_MOD), "+Main\r\n+Other\r\n");
        assert_eq!(without_entry("+Main\n", ADDRLIB_MOD), "+Main\n");
    }
}
