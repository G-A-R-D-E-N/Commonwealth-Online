use super::*;
use crate::channel::installed_version;
use std::io::{Cursor, Write};
use zip::write::SimpleFileOptions;
use zip::CompressionMethod;

struct Guards {
    game: bool,
    mo2: bool,
}

impl InstallGuards for Guards {
    fn game_or_f4se_running(&self) -> bool {
        self.game
    }
    fn mo2_running(&self) -> bool {
        self.mo2
    }
}

fn free() -> Guards {
    Guards {
        game: false,
        mo2: false,
    }
}

const OLD_LIST: &str = "+Main\r\n+Commonwealth Online OG 1.0\r\n-Commonwealth Online NG 2.0\r\n+Commonwealth Online Maps\r\n+CommonwealthOnlineUI\r\n";

fn put(path: &Path, bytes: impl AsRef<[u8]>) {
    fs::create_dir_all(path.parent().unwrap()).unwrap();
    fs::write(path, bytes).unwrap();
}

fn marker_text(version: &str, runtime: &str) -> String {
    format!(
        r#"{{"schema":1,"channel":"standalone","installed_version":"{version}","runtime":"{runtime}","installed_unix":1700000000}}"#
    )
}

fn profile_list(dir: &Path) -> PathBuf {
    dir.join("profiles").join("Default").join(MODLIST)
}

fn fixture(list: &str) -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    let old = dir.path().join("mods").join("Commonwealth Online OG 1.0");
    put(&old.join(STANDALONE_MARKER), marker_text("1.0", "OG"));
    put(&old.join("CommonwealthOnline.esp"), b"old");
    put(&dir.path().join("mods/Some Armor/armor.esp"), b"armor");
    put(&profile_list(dir.path()), list);
    dir
}

fn modlist(dir: &Path) -> String {
    fs::read_to_string(profile_list(dir)).unwrap()
}

fn zip_file(dir: &Path, entries: &[(&str, &[u8])]) -> PathBuf {
    let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
    for (name, bytes) in entries {
        zip.start_file(*name, SimpleFileOptions::default()).unwrap();
        zip.write_all(bytes).unwrap();
    }
    let path = dir.join("release.zip");
    fs::write(&path, zip.finish().unwrap().into_inner()).unwrap();
    path
}

fn tree(root: &Path) -> Vec<String> {
    fn walk(dir: &Path, base: &Path, out: &mut Vec<String>) {
        for entry in fs::read_dir(dir).unwrap().flatten() {
            let path = entry.path();
            out.push(path.strip_prefix(base).unwrap().display().to_string());
            if path.is_dir() {
                walk(&path, base, out);
            }
        }
    }
    let mut out = Vec::new();
    walk(root, root, &mut out);
    out.sort();
    out
}

fn og(dir: &Path) -> Target<'_> {
    Target {
        instance: dir,
        profile: "Default",
        runtime: Runtime::Og,
    }
}

fn install(dir: &Path, version: &str, zip: &Path) -> Result<InstallReport> {
    install_mod_release(og(dir), version, zip, false, &free())
}

#[test]
fn bundled_prisma_is_installed_in_the_commonwealth_mod() {
    let dir = fixture("+Commonwealth Online OG 1.0\n");
    let zip = zip_file(
        dir.path(),
        &[
            ("Data/CommonwealthOnline.esp", b"esp"),
            ("Data/F4SE/Plugins/CommonwealthOnline.dll", b"plugin"),
            ("Data/F4SE/Plugins/PrismaUI_F4.dll", b"prisma"),
            ("Data/PrismaUI_F4/views/index.html", b"view"),
        ],
    );
    install(dir.path(), "1.1", &zip).unwrap();
    let installed = dir.path().join("mods/Commonwealth Online OG 1.1");
    assert_eq!(
        fs::read(installed.join("CommonwealthOnline.esp")).unwrap(),
        b"esp"
    );
    assert_eq!(
        fs::read(installed.join("F4SE/Plugins/CommonwealthOnline.dll")).unwrap(),
        b"plugin"
    );
    assert_eq!(
        fs::read(installed.join("F4SE/Plugins/PrismaUI_F4.dll")).unwrap(),
        b"prisma"
    );
    assert!(installed.join("PrismaUI_F4/views/index.html").is_file());
    assert!(!dir.path().join("mods/PrismaUI").exists());
    assert!(modlist(dir.path()).contains("+Commonwealth Online OG 1.1"));
    assert_eq!(
        installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
        Some("1.1")
    );
}

#[test]
fn a_release_becomes_a_versioned_folder_and_the_old_build_is_disabled() {
    let dir = fixture(OLD_LIST);
    let zip = zip_file(
        dir.path(),
        &[
            ("Data/F4SE/Plugins/CommonwealthOnline.dll", b"dll"),
            ("Data/CommonwealthOnline.esp", b"esp"),
            ("Data/Scripts/Co.pex", b"pex"),
        ],
    );
    let report = install(dir.path(), "1.1", &zip).unwrap();
    assert_eq!(
        report,
        InstallReport {
            folder: "Commonwealth Online OG 1.1".to_owned(),
            already_installed: false,
            files: 3,
            disabled: vec!["Commonwealth Online OG 1.0".to_owned()],
            retired: Vec::new(),
        }
    );
    let mods = dir.path().join("mods");
    let folder = mods.join("Commonwealth Online OG 1.1");
    assert_eq!(
        fs::read(folder.join("F4SE/Plugins/CommonwealthOnline.dll")).unwrap(),
        b"dll"
    );
    assert!(folder.join("CommonwealthOnline.esp").is_file());
    assert!(folder.join("Scripts/Co.pex").is_file());
    assert!(!folder.join("Data").exists());
    let marker: serde_json::Value =
        serde_json::from_slice(&fs::read(folder.join(STANDALONE_MARKER)).unwrap()).unwrap();
    assert_eq!(marker["schema"], 1);
    assert_eq!(marker["channel"], "standalone");
    assert_eq!(marker["installed_version"], "1.1");
    assert_eq!(marker["runtime"], "OG");
    assert!(marker["installed_unix"].as_u64().is_some_and(|t| t > 0));
    assert!(!mods.join(".co-install-1.1.tmp").exists());
    assert_eq!(
        modlist(dir.path()),
        "+Main\r\n+Commonwealth Online OG 1.1\r\n-Commonwealth Online OG 1.0\r\n-Commonwealth Online NG 2.0\r\n+Commonwealth Online Maps\r\n+CommonwealthOnlineUI\r\n"
    );
    assert!(mods.join("Commonwealth Online OG 1.0").is_dir());
    assert!(mods.join("Some Armor/armor.esp").is_file());
    assert_eq!(
        installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
        Some("1.1")
    );
}

#[test]
fn an_lf_modlist_stays_lf_and_a_build_with_no_old_one_is_appended() {
    let dir = fixture("+Main\n");
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    let ng = Target {
        runtime: Runtime::Ng,
        ..og(dir.path())
    };
    let report = install_mod_release(ng, "2.0", &zip, false, &free()).unwrap();
    assert!(report.disabled.is_empty());
    assert_eq!(modlist(dir.path()), "+Main\n+Commonwealth Online NG 2.0\n");
}

#[test]
fn rollback_and_prune_follow_the_enabled_build_and_touch_only_launcher_builds() {
    let dir = fixture("+Main\r\n+Commonwealth Online OG 1.0\r\n+Some Armor\r\n");
    let manual = dir.path().join("mods/Commonwealth Online OG 0.9");
    put(&manual.join("manual.esp"), b"manual");
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    install(dir.path(), "1.1", &zip).unwrap();
    install(dir.path(), "1.2", &zip).unwrap();
    assert_eq!(
        modlist(dir.path()),
        "+Main\r\n+Commonwealth Online OG 1.2\r\n-Commonwealth Online OG 1.1\r\n-Commonwealth Online OG 1.0\r\n+Some Armor\r\n"
    );
    let mo2 = Guards {
        game: false,
        mo2: true,
    };
    assert!(rollback(og(dir.path()), &mo2).is_err());
    let back = rollback(og(dir.path()), &free()).unwrap();
    assert_eq!(back.restored, "Commonwealth Online OG 1.1");
    assert_eq!(back.disabled, "Commonwealth Online OG 1.2");
    assert!(modlist(dir.path()).contains("+Commonwealth Online OG 1.1\r\n"));
    rollback(og(dir.path()), &free()).unwrap();
    assert!(modlist(dir.path()).starts_with(
        "+Main\r\n-Commonwealth Online OG 1.2\r\n-Commonwealth Online OG 1.1\r\n+Commonwealth Online OG 1.0\r\n"
    ));
    assert!(rollback(og(dir.path()), &free()).is_err());
    assert!(prune_old(og(dir.path()), 0, &mo2).is_err());
    assert_eq!(
        prune_old(og(dir.path()), 1, &free()).unwrap(),
        vec!["Commonwealth Online OG 1.1".to_owned()]
    );
    assert!(!dir.path().join("mods/Commonwealth Online OG 1.1").exists());
    assert_eq!(
        modlist(dir.path()),
        "+Main\r\n-Commonwealth Online OG 1.2\r\n+Commonwealth Online OG 1.0\r\n+Some Armor\r\n"
    );
    assert!(dir.path().join("mods/Commonwealth Online OG 1.0").is_dir());
    assert!(dir.path().join("mods/Commonwealth Online OG 1.2").is_dir());
    assert!(prune_old(og(dir.path()), KEEP, &free()).unwrap().is_empty());
    assert!(manual.join("manual.esp").is_file());
}

#[test]
fn a_running_game_or_mo2_refuses_before_anything_changes() {
    let dir = fixture(OLD_LIST);
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    let before = tree(dir.path());
    for guards in [
        Guards {
            game: true,
            mo2: false,
        },
        Guards {
            game: false,
            mo2: true,
        },
    ] {
        let result = install_mod_release(og(dir.path()), "1.1", &zip, false, &guards);
        assert!(result.is_err());
    }
    assert_eq!(tree(dir.path()), before);
    assert_eq!(modlist(dir.path()), OLD_LIST);
}

#[test]
fn only_a_standalone_install_gets_release_files() {
    let wabbajack = tempfile::tempdir().unwrap();
    put(
        &wabbajack
            .path()
            .join("mods/Commonwealth Online OG 1.0/co-channel.json"),
        r#"{"schema":1,"channel":"wabbajack"}"#,
    );
    put(&profile_list(wabbajack.path()), OLD_LIST);
    let unknown = fixture(OLD_LIST);
    fs::remove_dir_all(unknown.path().join("mods/Commonwealth Online OG 1.0")).unwrap();
    for dir in [&wabbajack, &unknown] {
        let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
        let before = tree(dir.path());
        assert!(install(dir.path(), "1.1", &zip).is_err());
        assert_eq!(tree(dir.path()), before);
    }
}

#[test]
fn an_existing_folder_is_an_idempotent_no_op() {
    let dir = fixture(OLD_LIST);
    put(
        &dir.path().join("mods/Commonwealth Online OG 1.1/keep.esp"),
        b"k",
    );
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    let before = tree(dir.path());
    let report = install(dir.path(), "1.1", &zip).unwrap();
    assert!(report.already_installed && report.files == 0 && report.disabled.is_empty());
    assert_eq!(tree(dir.path()), before);
    assert_eq!(modlist(dir.path()), OLD_LIST);
}

#[test]
fn unsafe_entry_names_and_symlinks_are_refused_and_leave_nothing_behind() {
    let names = [
        "Data/../x.esp",
        "Data/..\\x.esp",
        "Data\\..\\x.esp",
        "Data/F4SE/../../x.esp",
        "/Data/x.esp",
        "Data//x.esp",
        "Data/C:x.esp",
        "C:\\x.esp",
        "\\\\server\\share\\x.esp",
        "x.esp",
        "Data/CON.txt",
        "Data/x.co-tmp",
        "Data/.co-launcher.json",
        "Data/co-channel.json",
    ];
    for name in names {
        let dir = fixture(OLD_LIST);
        let zip = zip_file(dir.path(), &[("Data/ok.esp", b"x"), (name, b"x")]);
        let before = tree(dir.path());
        assert!(install(dir.path(), "1.1", &zip).is_err(), "{name}");
        assert_eq!(tree(dir.path()), before, "{name}");
    }
    let dir = fixture(OLD_LIST);
    let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
    zip.start_file("Data/ok.esp", SimpleFileOptions::default())
        .unwrap();
    zip.write_all(b"x").unwrap();
    zip.add_symlink("Data/link.esp", "/etc/passwd", SimpleFileOptions::default())
        .unwrap();
    let path = dir.path().join("release.zip");
    fs::write(&path, zip.finish().unwrap().into_inner()).unwrap();
    let before = tree(dir.path());
    assert!(install(dir.path(), "1.1", &path).is_err());
    assert_eq!(tree(dir.path()), before);
}

#[test]
fn each_cap_refuses_the_release() {
    let dir = fixture(OLD_LIST);
    let zips = tempfile::tempdir().unwrap();
    let before = tree(dir.path());
    let run = |limits: Limits, entries: &[(&str, &[u8])]| {
        let zip = zip_file(zips.path(), entries);
        install_with(og(dir.path()), "1.1", &zip, false, &free(), &limits)
    };
    let big = vec![0u8; 1 << 20];
    assert!(run(
        Limits {
            per_file: 4,
            ..LIMITS
        },
        &[("Data/a.esp", b"12345")]
    )
    .is_err());
    assert!(run(
        Limits { total: 9, ..LIMITS },
        &[("Data/a.esp", b"12345"), ("Data/b.esp", b"12345")]
    )
    .is_err());
    assert!(run(
        Limits {
            entries: 1,
            ..LIMITS
        },
        &[("Data/a.esp", b"1"), ("Data/b.esp", b"2")]
    )
    .is_err());
    assert!(run(LIMITS, &[("Data/zeros.esp", big.as_slice())]).is_err());
    assert_eq!(tree(dir.path()), before);
}

#[test]
fn a_corrupt_entry_in_the_middle_removes_the_staging_folder() {
    let dir = fixture(OLD_LIST);
    let stored = SimpleFileOptions::default().compression_method(CompressionMethod::Stored);
    let mut zip = zip::ZipWriter::new(Cursor::new(Vec::new()));
    zip.start_file("Data/a.esp", stored).unwrap();
    zip.write_all(b"AAAA").unwrap();
    zip.start_file("Data/b.esp", stored).unwrap();
    zip.write_all(b"BBBB").unwrap();
    let mut bytes = zip.finish().unwrap().into_inner();
    let at = bytes.windows(4).position(|w| w == b"BBBB").unwrap();
    bytes[at] = b'X';
    let path = dir.path().join("release.zip");
    fs::write(&path, &bytes).unwrap();
    let before = tree(dir.path());
    assert!(install(dir.path(), "1.1", &path).is_err());
    assert_eq!(tree(dir.path()), before);
    assert_eq!(modlist(dir.path()), OLD_LIST);
}

#[test]
fn bad_versions_and_profile_names_are_refused() {
    let dir = fixture(OLD_LIST);
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    let before = tree(dir.path());
    assert!(install(dir.path(), "1.1/../x", &zip).is_err());
    assert!(install(dir.path(), "", &zip).is_err());
    for profile in ["..\\Default", "Missing"] {
        let target = Target {
            profile,
            ..og(dir.path())
        };
        let result = install_mod_release(target, "1.1", &zip, false, &free());
        assert!(result.is_err(), "{profile}");
    }
    assert_eq!(tree(dir.path()), before);
}

#[test]
fn an_unknown_install_needs_an_explicit_adoption_and_a_wabbajack_one_never_does() {
    let unknown = fixture(OLD_LIST);
    fs::remove_dir_all(unknown.path().join("mods/Commonwealth Online OG 1.0")).unwrap();
    let zip = zip_file(unknown.path(), &[("Data/a.esp", b"x")]);
    let before = tree(unknown.path());
    assert!(install_mod_release(og(unknown.path()), "1.1", &zip, false, &free()).is_err());
    assert_eq!(tree(unknown.path()), before);
    let report = install_mod_release(og(unknown.path()), "1.1", &zip, true, &free()).unwrap();
    assert_eq!(report.folder, "Commonwealth Online OG 1.1");
    assert_eq!(install_channel(unknown.path()), InstallChannel::Standalone);
    let wabbajack = tempfile::tempdir().unwrap();
    put(
        &wabbajack
            .path()
            .join("mods/Commonwealth Online OG 1.0/co-channel.json"),
        r#"{"schema":1,"channel":"wabbajack"}"#,
    );
    put(&profile_list(wabbajack.path()), OLD_LIST);
    let zip = zip_file(wabbajack.path(), &[("Data/a.esp", b"x")]);
    let before = tree(wabbajack.path());
    for adopt in [false, true] {
        let error =
            install_mod_release(og(wabbajack.path()), "1.1", &zip, adopt, &free()).unwrap_err();
        assert!(error.to_string().contains("Wabbajack"));
    }
    assert!(rollback(og(wabbajack.path()), &free()).is_err());
    assert!(prune_old(og(wabbajack.path()), 1, &free()).is_err());
    assert_eq!(tree(wabbajack.path()), before);
    assert_eq!(modlist(wabbajack.path()), OLD_LIST);
    let hinted = fixture(OLD_LIST);
    fs::remove_dir_all(hinted.path().join("mods/Commonwealth Online OG 1.0")).unwrap();
    put(&hinted.path().join("portable.txt"), "Created by Wabbajack");
    assert!(install_mod_release(og(hinted.path()), "1.1", &zip, true, &free()).is_err());
}

#[test]
fn installed_version_follows_the_enabled_build_after_a_rollback() {
    let dir = fixture("+Main\r\n+Commonwealth Online OG 1.0\r\n");
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    install(dir.path(), "1.1", &zip).unwrap();
    assert_eq!(
        installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
        Some("1.1")
    );
    rollback(og(dir.path()), &free()).unwrap();
    assert_eq!(
        installed_version(dir.path(), "Default", Runtime::Og).as_deref(),
        Some("1.0")
    );
}

#[test]
fn prune_removes_only_the_disabled_lines_of_the_folders_it_deletes() {
    let dir = fixture(
        "+Main\r\n+Commonwealth Online OG 1.0\r\n-Commonwealth Online OG 1.00\r\n+Some Armor\r\n",
    );
    let zip = zip_file(dir.path(), &[("Data/a.esp", b"x")]);
    install(dir.path(), "1.1", &zip).unwrap();
    install(dir.path(), "1.2", &zip).unwrap();
    let text = modlist(dir.path());
    assert!(
        text.contains("-Commonwealth Online OG 1.1\r\n")
            && text.contains("-Commonwealth Online OG 1.0\r\n")
    );
    let removed = prune_old(og(dir.path()), 1, &free()).unwrap();
    assert_eq!(
        removed,
        ["Commonwealth Online OG 1.1", "Commonwealth Online OG 1.0"]
    );
    let text = modlist(dir.path());
    assert!(!text.contains("-Commonwealth Online OG 1.1\r\n"));
    assert!(!text.contains("-Commonwealth Online OG 1.0\r\n"));
    assert!(text.contains("-Commonwealth Online OG 1.00\r\n"));
    assert!(text.contains("+Commonwealth Online OG 1.2\r\n"));
    assert!(text.contains("+Some Armor\r\n"));
}

#[test]
fn an_enabled_mod_still_holding_a_pre_merge_dll_has_it_set_aside() {
    let dir = fixture(OLD_LIST);
    let plugins = dir.path().join("mods/CommonwealthOnlineUI/F4SE/Plugins");
    put(&plugins.join("CommonwealthOnlineUI.dll"), b"old ui");
    put(&plugins.join("Other.dll"), b"other");
    let off = dir
        .path()
        .join("mods/Commonwealth Online NG 2.0/F4SE/Plugins");
    put(&off.join("CommonwealthOnline-NG.dll"), b"ng");
    let zip = zip_file(
        dir.path(),
        &[("Data/F4SE/Plugins/CommonwealthOnline.dll", b"dll")],
    );
    let report = install(dir.path(), "1.1", &zip).unwrap();
    assert_eq!(
        report.retired,
        vec!["CommonwealthOnlineUI/CommonwealthOnlineUI.dll".to_owned()]
    );
    assert!(!plugins.join("CommonwealthOnlineUI.dll").exists());
    assert_eq!(
        fs::read(plugins.join("CommonwealthOnlineUI.dll.legacy")).unwrap(),
        b"old ui"
    );
    assert!(plugins.join("Other.dll").is_file());
    assert!(off.join("CommonwealthOnline-NG.dll").is_file());
}

#[test]
fn a_pre_merge_dll_is_set_aside_in_any_enabled_mod_folder() {
    let dir = fixture(&format!("{OLD_LIST}+Merged Pack\n"));
    let plugins = dir.path().join("mods/Merged Pack/F4SE/Plugins");
    put(&plugins.join("CommonwealthOnlineUI.dll"), b"old ui");
    let zip = zip_file(
        dir.path(),
        &[("Data/F4SE/Plugins/CommonwealthOnline.dll", b"dll")],
    );
    let report = install(dir.path(), "1.1", &zip).unwrap();
    assert_eq!(
        report.retired,
        vec!["Merged Pack/CommonwealthOnlineUI.dll".to_owned()]
    );
    assert!(plugins.join("CommonwealthOnlineUI.dll.legacy").is_file());
}
