use std::collections::HashSet;
use std::fs::{self, File};
use std::io::{self, Read};
use std::path::{Path, PathBuf};
use std::time::{SystemTime, UNIX_EPOCH};

use zip::ZipArchive;

use crate::channel::{
    co_mod_dirs, compare_versions, install_channel, is_version, may_adopt, may_install_mod_files,
    read_standalone, InstallChannel, StandaloneMarker, SCHEMA, STANDALONE_MARKER, WABBAJACK_MARKER,
};
use crate::error::{Error, Result};
use crate::fsutil::{find_ci, resolve_ci, write_atomic};
use crate::modlist::{enabled_names, profile_modlist};
use crate::modmgr::{read_bytes_capped, write_replacing, MODLIST};
use crate::runtime::Runtime;
use crate::versions::is_co_build;

const PREFIX: &str = "Commonwealth Online";
const DATA: &str = "Data/";
const MODS: &str = "mods";
const STAGING: &str = ".co-tmp";
const STANDALONE: &str = "standalone";
pub const KEEP: usize = 2;
const LEGACY_DLLS: [&str; 3] = [
    "CommonwealthOnline-NG.dll",
    "CommonwealthOnline-OG.dll",
    "CommonwealthOnlineUI.dll",
];
const LEGACY_SUFFIX: &str = ".legacy";

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Limits {
    pub per_file: u64,
    pub total: u64,
    pub entries: usize,
    pub ratio: u64,
}

pub const LIMITS: Limits = Limits {
    per_file: 256 << 20,
    total: 1 << 30,
    entries: 20_000,
    ratio: 200,
};

pub trait InstallGuards {
    fn game_or_f4se_running(&self) -> bool;
    fn mo2_running(&self) -> bool;
}

#[derive(Debug, Clone, Copy)]
pub struct Target<'a> {
    pub instance: &'a Path,
    pub profile: &'a str,
    pub runtime: Runtime,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct InstallReport {
    pub folder: String,
    pub already_installed: bool,
    pub files: usize,
    pub disabled: Vec<String>,
    pub retired: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct RollbackReport {
    pub restored: String,
    pub disabled: String,
}

struct Build {
    name: String,
    version: String,
    dir: PathBuf,
}

struct Entry {
    index: usize,
    rel: PathBuf,
    size: u64,
}

fn refused(message: &str) -> Error {
    Error::Update(message.to_owned())
}

fn bad_zip(message: impl Into<String>) -> Error {
    Error::Archive(message.into())
}

fn archive_error(error: impl ToString) -> Error {
    Error::Archive(error.to_string())
}

pub fn install_mod_release(
    target: Target,
    version: &str,
    zip_path: &Path,
    adopt_unknown: bool,
    guards: &dyn InstallGuards,
) -> Result<InstallReport> {
    install_with(target, version, zip_path, adopt_unknown, guards, &LIMITS)
}

pub(crate) fn install_with(
    target: Target,
    version: &str,
    zip_path: &Path,
    adopt_unknown: bool,
    guards: &dyn InstallGuards,
    limits: &Limits,
) -> Result<InstallReport> {
    preflight(target, adopt_unknown, guards)?;
    if !is_version(version) {
        return Err(refused("the release version must be dotted numbers"));
    }
    let mods = mods_dir(target.instance)?;
    let folder = format!("{PREFIX} {} {version}", target.runtime.label());
    let dest = mods.join(&folder);
    if dest.exists() {
        return Ok(InstallReport {
            folder,
            already_installed: true,
            files: 0,
            disabled: Vec::new(),
            retired: Vec::new(),
        });
    }
    let file = File::open(zip_path).map_err(Error::io(zip_path))?;
    let mut archive = ZipArchive::new(file).map_err(archive_error)?;
    let plans = plan(&mut archive, limits)?;
    if plans.is_empty() {
        return Err(bad_zip("the release has no files under Data/"));
    }
    let temp = mods.join(format!(".co-install-{version}.tmp"));
    let _ = fs::remove_dir_all(&temp);
    let marker = StandaloneMarker {
        schema: SCHEMA,
        channel: STANDALONE.to_owned(),
        installed_version: version.to_owned(),
        runtime: target.runtime.label().to_owned(),
        installed_unix: SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map_or(0, |d| d.as_secs()),
    };
    stage(&mut archive, &plans, &temp, &marker, limits)
        .and_then(|files| {
            commit(&temp, &dest, target, &folder, guards).map(|disabled| InstallReport {
                retired: retire_legacy_dlls(target, &folder),
                folder: folder.clone(),
                already_installed: false,
                files,
                disabled,
            })
        })
        .inspect_err(|_| {
            let _ = fs::remove_dir_all(&temp);
        })
}

pub(crate) fn preflight(
    target: Target,
    adopt_unknown: bool,
    guards: &dyn InstallGuards,
) -> Result<()> {
    refuse_if_running(guards)?;
    may_write_mods(target.instance, adopt_unknown)?;
    read_modlist(target.instance, target.profile).map(|_| ())
}

pub fn rollback(target: Target, guards: &dyn InstallGuards) -> Result<RollbackReport> {
    refuse_if_running(guards)?;
    may_write_mods(target.instance, false)?;
    let (path, text) = read_modlist(target.instance, target.profile)?;
    let builds = launcher_builds(target.instance, target.runtime);
    let on = enabled_names(&text);
    let at = builds
        .iter()
        .position(|b| on.contains(&b.name.to_ascii_lowercase()))
        .ok_or_else(|| refused("no launcher build is enabled in this profile"))?;
    let previous = builds
        .get(at + 1)
        .ok_or_else(|| refused("there is no older launcher build to roll back to"))?;
    let current = builds[at].name.clone();
    let (next, _) = switch(&text, &previous.name, |n| n.eq_ignore_ascii_case(&current));
    if !write_replacing(&path, &next) {
        return Err(refused(
            "could not write modlist.txt, so nothing was changed",
        ));
    }
    Ok(RollbackReport {
        restored: previous.name.clone(),
        disabled: current,
    })
}

pub fn prune_old(target: Target, keep: usize, guards: &dyn InstallGuards) -> Result<Vec<String>> {
    refuse_if_running(guards)?;
    may_write_mods(target.instance, false)?;
    let (path, text) = read_modlist(target.instance, target.profile)?;
    let on = enabled_names(&text);
    let stale: Vec<Build> = launcher_builds(target.instance, target.runtime)
        .into_iter()
        .skip(keep.max(1))
        .filter(|build| !on.contains(&build.name.to_ascii_lowercase()))
        .collect();
    if stale.is_empty() {
        return Ok(Vec::new());
    }
    let names: Vec<String> = stale.iter().map(|build| build.name.clone()).collect();
    let next = drop_disabled(&text, &names);
    if next != text && !write_replacing(&path, &next) {
        return Err(refused(
            "could not write modlist.txt, so nothing was removed",
        ));
    }
    for build in &stale {
        fs::remove_dir_all(&build.dir).map_err(Error::io(&build.dir))?;
    }
    Ok(names)
}

fn may_write_mods(instance: &Path, adopt_unknown: bool) -> Result<()> {
    let channel = install_channel(instance);
    if may_install_mod_files(&channel, false) || (adopt_unknown && may_adopt(instance)) {
        return Ok(());
    }
    Err(refused(match channel {
        InstallChannel::Wabbajack { .. } => {
            "this install is managed by Wabbajack: update the modlist in Wabbajack"
        }
        _ if adopt_unknown => {
            "this install has a Wabbajack trace or a conflicting marker, so the launcher will not manage it"
        }
        _ => "this install is not known to the launcher; confirm before the launcher manages it",
    }))
}

fn refuse_if_running(guards: &dyn InstallGuards) -> Result<()> {
    if guards.game_or_f4se_running() {
        return Err(refused(
            "Fallout 4 or F4SE is running, or the process list could not be read; close the game and try again",
        ));
    }
    if guards.mo2_running() {
        return Err(refused(
            "Mod Organizer 2 is running, or the process list could not be read; close it and try again",
        ));
    }
    Ok(())
}

fn mods_dir(instance: &Path) -> Result<PathBuf> {
    find_ci(instance, MODS)
        .filter(|p| p.is_dir())
        .ok_or_else(|| refused("the instance has no mods folder"))
}

fn read_modlist(instance: &Path, profile: &str) -> Result<(PathBuf, String)> {
    let unreadable = || {
        refused(&format!(
            "the profile's {MODLIST} cannot be read, so nothing was changed"
        ))
    };
    let path = profile_modlist(instance, profile).ok_or_else(unreadable)?;
    let text = read_bytes_capped(&path)
        .and_then(|bytes| String::from_utf8(bytes).ok())
        .ok_or_else(unreadable)?;
    Ok((path, text))
}

fn entry_path(name: &str) -> Result<Option<PathBuf>> {
    let unsafe_name = || bad_zip(format!("unsafe file name in the release: {name:?}"));
    if name.is_empty()
        || name.contains(['\\', ':', '\0'])
        || name.starts_with('/')
        || name.chars().any(char::is_control)
    {
        return Err(unsafe_name());
    }
    let Some(rest) = name
        .get(..DATA.len())
        .filter(|head| head.eq_ignore_ascii_case(DATA))
        .map(|_| &name[DATA.len()..])
    else {
        return Err(bad_zip(format!("{name:?} is not under Data/")));
    };
    let rest = rest.trim_end_matches('/');
    if rest.is_empty() {
        return Ok(None);
    }
    let parts: Vec<&str> = rest.split('/').collect();
    let bad_part = parts.iter().any(|p| {
        p.is_empty() || *p == "." || *p == ".." || p.ends_with(['.', ' ']) || is_device_name(p)
    });
    let reserved = rest.to_ascii_lowercase().ends_with(STAGING)
        || (parts.len() == 1
            && [STANDALONE_MARKER, WABBAJACK_MARKER]
                .iter()
                .any(|m| m.eq_ignore_ascii_case(rest)));
    if bad_part || reserved {
        return Err(unsafe_name());
    }
    Ok(Some(parts.iter().collect()))
}

fn is_device_name(part: &str) -> bool {
    let stem = part
        .split('.')
        .next()
        .unwrap_or_default()
        .to_ascii_uppercase();
    let numbered = stem.len() == 4
        && matches!(stem.get(..3), Some("COM" | "LPT"))
        && matches!(stem.as_bytes()[3], b'1'..=b'9');
    numbered || matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
}

fn plan(archive: &mut ZipArchive<File>, limits: &Limits) -> Result<Vec<Entry>> {
    if archive.len() > limits.entries {
        return Err(bad_zip("the release has too many files"));
    }
    let mut seen = HashSet::new();
    let (mut entries, mut total) = (Vec::new(), 0u64);
    for index in 0..archive.len() {
        let entry = archive.by_index(index).map_err(archive_error)?;
        let name = entry.name().map_err(archive_error)?;
        let Some(rel) = entry_path(&name)? else {
            continue;
        };
        if entry.is_symlink() || entry.unix_mode().is_some_and(|m| m & 0o170000 == 0o120000) {
            return Err(bad_zip("the release holds a symbolic link"));
        }
        if entry.is_dir() {
            continue;
        }
        let size = entry.size();
        let shown = rel.display();
        if !entry.is_file() {
            return Err(bad_zip(format!("{shown} is not a regular file")));
        }
        if size > limits.per_file {
            return Err(bad_zip(format!("{shown} is too large")));
        }
        if size > entry.compressed_size().saturating_mul(limits.ratio) {
            return Err(bad_zip(format!("{shown} compresses too well to be real")));
        }
        total = total.saturating_add(size);
        if total > limits.total {
            return Err(bad_zip("the release is too large when unpacked"));
        }
        if !seen.insert(rel.to_string_lossy().to_ascii_lowercase()) {
            return Err(bad_zip(format!("{shown} is listed twice")));
        }
        entries.push(Entry { index, rel, size });
    }
    Ok(entries)
}

fn stage(
    archive: &mut ZipArchive<File>,
    plans: &[Entry],
    temp: &Path,
    marker: &StandaloneMarker,
    limits: &Limits,
) -> Result<usize> {
    fs::create_dir(temp).map_err(Error::io(temp))?;
    let mut total = 0u64;
    for entry in plans {
        let target = temp.join(&entry.rel);
        if let Some(parent) = target.parent() {
            fs::create_dir_all(parent).map_err(Error::io(parent))?;
        }
        let mut zipped = archive.by_index(entry.index).map_err(archive_error)?;
        let written = write_file(&target, &mut zipped, limits.per_file)?;
        total = total.saturating_add(written);
        if total > limits.total || written != entry.size {
            return Err(bad_zip(format!(
                "{} does not match its listed size",
                entry.rel.display()
            )));
        }
    }
    let mut bytes = serde_json::to_vec_pretty(marker)
        .map_err(|e| refused(&format!("could not encode the install marker: {e}")))?;
    bytes.push(b'\n');
    write_atomic(&temp.join(STANDALONE_MARKER), &bytes)?;
    Ok(plans.len())
}

fn write_file(target: &Path, reader: &mut impl Read, limit: u64) -> Result<u64> {
    let mut staging = target.as_os_str().to_owned();
    staging.push(STAGING);
    let staging = PathBuf::from(staging);
    let result = (|| {
        let mut file = File::create(&staging).map_err(Error::io(&staging))?;
        let written =
            io::copy(&mut reader.by_ref().take(limit + 1), &mut file).map_err(archive_error)?;
        if written > limit {
            return Err(bad_zip(format!("{} is too large", target.display())));
        }
        drop(file);
        fs::rename(&staging, target).map_err(Error::io(target))?;
        Ok(written)
    })();
    if result.is_err() {
        let _ = fs::remove_file(&staging);
    }
    result
}

fn commit(
    temp: &Path,
    dest: &Path,
    target: Target,
    folder: &str,
    guards: &dyn InstallGuards,
) -> Result<Vec<String>> {
    refuse_if_running(guards)?;
    if dest.exists() {
        return Err(refused(&format!(
            "{folder} appeared while installing, so nothing was changed"
        )));
    }
    fs::rename(temp, dest).map_err(Error::io(dest))?;
    read_modlist(target.instance, target.profile)
        .and_then(|(path, text)| {
            let (next, disabled) = switch(&text, folder, |n| is_build_for(n, target.runtime));
            if write_replacing(&path, &next) {
                Ok(disabled)
            } else {
                Err(refused(
                    "could not write modlist.txt, so the new build was removed again",
                ))
            }
        })
        .inspect_err(|_| {
            let _ = fs::remove_dir_all(dest);
        })
}

fn retire_legacy_dlls(target: Target, keep: &str) -> Vec<String> {
    let on = read_modlist(target.instance, target.profile)
        .map(|(_, text)| enabled_names(&text))
        .unwrap_or_default();
    let mut retired = Vec::new();
    let mut dirs: Vec<PathBuf> = mods_dir(target.instance)
        .and_then(|mods| fs::read_dir(&mods).map_err(Error::io(&mods)))
        .map(|entries| {
            entries
                .flatten()
                .filter(|e| e.file_type().is_ok_and(|t| t.is_dir()))
                .map(|e| e.path())
                .collect()
        })
        .unwrap_or_default();
    dirs.sort();
    for dir in dirs {
        let Some(name) = dir.file_name().and_then(|n| n.to_str()).map(str::to_owned) else {
            continue;
        };
        if name.eq_ignore_ascii_case(keep) || !on.contains(&name.to_ascii_lowercase()) {
            continue;
        }
        let plugins = resolve_ci(&dir, &["F4SE", "Plugins"]);
        for dll in LEGACY_DLLS {
            let Some(path) = find_ci(&plugins, dll) else {
                continue;
            };
            let moved = PathBuf::from(format!("{}{LEGACY_SUFFIX}", path.display()));
            if fs::rename(&path, &moved).is_ok() {
                retired.push(format!("{name}/{dll}"));
            }
        }
    }
    retired
}

fn is_build_for(name: &str, runtime: Runtime) -> bool {
    is_co_build(name)
        && name
            .split(' ')
            .any(|t| t.eq_ignore_ascii_case(runtime.label()))
}

fn launcher_builds(instance: &Path, runtime: Runtime) -> Vec<Build> {
    let mut builds: Vec<Build> = co_mod_dirs(instance)
        .into_iter()
        .filter_map(|dir| {
            let name = dir.file_name()?.to_str()?.to_owned();
            let marker = read_standalone(&dir)?;
            (marker.runtime == runtime && is_build_for(&name, runtime)).then_some(Build {
                name,
                version: marker.version,
                dir,
            })
        })
        .collect();
    builds
        .sort_by(|a, b| compare_versions(&b.version, &a.version).then_with(|| a.name.cmp(&b.name)));
    builds
}

fn drop_disabled(text: &str, names: &[String]) -> String {
    let eol = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let kept: Vec<&str> = text
        .lines()
        .filter(|line| {
            !line
                .strip_prefix('-')
                .is_some_and(|name| names.iter().any(|n| n.eq_ignore_ascii_case(name)))
        })
        .collect();
    kept.join(eol) + eol
}

fn switch(text: &str, enable: &str, disable: impl Fn(&str) -> bool) -> (String, Vec<String>) {
    let eol = if text.contains("\r\n") { "\r\n" } else { "\n" };
    let (mut lines, mut disabled, mut found, mut slot) = (Vec::new(), Vec::new(), false, None);
    for line in text.lines() {
        match line.strip_prefix('+').or_else(|| line.strip_prefix('-')) {
            Some(name) if name.eq_ignore_ascii_case(enable) => {
                found = true;
                lines.push(format!("+{name}"));
            }
            Some(name) if disable(name) => {
                slot.get_or_insert(lines.len());
                if line.starts_with('+') {
                    disabled.push(name.to_owned());
                }
                lines.push(format!("-{name}"));
            }
            _ => lines.push(line.to_owned()),
        }
    }
    if !found {
        lines.insert(slot.unwrap_or(lines.len()), format!("+{enable}"));
    }
    (lines.join(eol) + eol, disabled)
}

#[cfg(test)]
mod tests;
