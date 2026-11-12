use std::path::Path;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use launcher_core::channel::{self, ChannelDecision, InstallChannel};
use launcher_core::modinstall::{self, InstallGuards, Target};
use launcher_core::runtime::Runtime;
use launcher_core::update::{self, http::UreqHttp, mods as installer, Offer, Paths};
use launcher_core::Result;

use crate::mods::{self, Pin};
use crate::protocol::{Event, ModChannel};

const FIRST_CHECK: Duration = Duration::from_secs(5);
const EVERY: Duration = Duration::from_secs(6 * 60 * 60);
const NO_NEWER: &str = "No newer version is available.";
const NO_FOLDER: &str = "The launcher data folder is not available.";
const NO_INSTANCE: &str =
    "Mod Organizer 2 was not found, so the mod is not managed by the launcher.";
const NO_TARGET: &str =
    "The mod cannot be managed: the game version or the active profile is not known.";
const NOTIFY_ONLY: &str = "Updates are set to notify only";

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Update {
    Check {
        manual: bool,
    },
    Install {
        version: String,
    },
    InstallMod {
        adopt_unknown: bool,
        version: String,
    },
    RollbackMod,
    Skip(String),
    Settings {
        include_prereleases: bool,
        auto_install: bool,
        notify_only: bool,
    },
}

pub fn spawn_timer(send: impl Fn() -> bool + Send + 'static) {
    std::thread::spawn(move || {
        std::thread::sleep(FIRST_CHECK);
        while send() {
            std::thread::sleep(EVERY);
        }
    });
}

pub fn finish_previous(args: &[String]) {
    let old_pid = update::finish_arg(args);
    std::thread::spawn(move || update::settle_previous(old_pid));
}

pub fn prefs_event(pin: Option<&Pin>) -> Option<Event> {
    Paths::user().map(|paths| prefs(&paths, pin))
}

fn prefs(paths: &Paths, pin: Option<&Pin>) -> Event {
    let state = update::load_state(paths);
    Event::UpdatePrefs {
        include_prereleases: state.include_prereleases,
        auto_install: state.auto_install,
        notify_only: state.notify_only,
        launcher_version: update::running_version().to_owned(),
        signing_configured: update::UPDATE_PUBLIC_KEY.is_some(),
        channel: mod_channel(pin, paths),
    }
}

pub fn run(update: &Update, pin: Option<&Pin>, emit: &dyn Fn(Event)) -> Option<Event> {
    let Some(paths) = Paths::user() else {
        return Some(result(false, NO_FOLDER, &ModChannel::none()));
    };
    let info = mod_channel(pin, &paths);
    match update {
        Update::Check { manual } => check(*manual, &paths, &info),
        Update::Install { version } => install(&paths, emit, &info, version),
        Update::InstallMod {
            adopt_unknown,
            version,
        } => Some(install_mod(&paths, pin, *adopt_unknown, version, emit)),
        Update::RollbackMod => Some(rollback_mod(&paths, pin)),
        Update::Skip(version) => Some(outcome(
            update::skip(&paths, version),
            "This version is skipped.",
            &info,
        )),
        Update::Settings {
            include_prereleases,
            auto_install,
            notify_only,
        } => Some(settings(
            &paths,
            pin,
            *include_prereleases,
            *auto_install,
            *notify_only,
            emit,
        )),
    }
}

fn settings(
    paths: &Paths,
    pin: Option<&Pin>,
    include_prereleases: bool,
    auto_install: bool,
    notify_only: bool,
    emit: &dyn Fn(Event),
) -> Event {
    let changed = update::load_state(paths).include_prereleases != include_prereleases;
    let saved = update::set_prefs(paths, include_prereleases, auto_install, notify_only);
    emit(prefs(paths, pin));
    let info = mod_channel(pin, paths);
    match saved {
        Ok(()) => {
            if changed {
                check(false, paths, &info).into_iter().for_each(emit);
            }
            result(true, "Settings saved.", &info)
        }
        Err(error) => result(false, error.to_string(), &info),
    }
}

fn now() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|age| age.as_secs())
        .unwrap_or(0)
}

fn result(ok: bool, message: impl Into<String>, channel: &ModChannel) -> Event {
    Event::UpdateResult {
        ok,
        message: message.into(),
        channel: channel.clone(),
    }
}

fn outcome(done: Result<()>, ok_text: &str, channel: &ModChannel) -> Event {
    match done {
        Ok(()) => result(true, ok_text, channel),
        Err(error) => result(false, error.to_string(), channel),
    }
}

fn available(offer: Offer, channel: ModChannel) -> Event {
    Event::UpdateAvailable {
        version: offer.version,
        prerelease: offer.prerelease,
        launcher_version: offer.launcher_version,
        mod_version: offer.mod_version,
        notes: offer.notes,
        signing_configured: offer.signing_configured,
        channel,
    }
}

pub fn mod_channel(pin: Option<&Pin>, paths: &Paths) -> ModChannel {
    let state = update::load_state(paths);
    let notify_only = state.notify_only;
    let Some(pin) = pin else {
        let decision = ChannelDecision {
            channel: InstallChannel::Unknown,
            reasons: vec![NO_INSTANCE.to_owned()],
        };
        return describe(decision, false, notify_only, None, false);
    };
    let decision = channel::decide(&pin.instance);
    let runtime = mods::detected_runtime(&pin.game);
    let installed = match (runtime, pin.profile.as_deref()) {
        (Some(runtime), Some(profile)) => modinstall::enabled_version(Target {
            instance: &pin.instance,
            profile,
            runtime,
        }),
        _ => None,
    };
    let adoptable = channel::may_adopt(&pin.instance);
    let usable = runtime.is_some() && pin.profile.is_some();
    let mut info = describe(decision, usable, notify_only, installed, adoptable);
    if info.needs_adopt_confirm {
        info.adopt_version = state.seen_mod.map(|seen| seen.version);
    }
    info
}

fn describe(
    decision: ChannelDecision,
    usable: bool,
    notify_only: bool,
    installed: Option<String>,
    adoptable: bool,
) -> ModChannel {
    let (label, can_install, needs_adopt) = match decision.channel {
        InstallChannel::Wabbajack { .. } => ("wabbajack", false, false),
        InstallChannel::Standalone => (
            "standalone",
            usable && channel::may_install_mod_files(&decision.channel, notify_only),
            false,
        ),
        InstallChannel::Unknown => ("unknown", false, usable && !notify_only && adoptable),
    };
    ModChannel {
        channel: label,
        channel_reasons: decision.reasons,
        installed_version: installed,
        can_install_mod: can_install,
        needs_adopt_confirm: needs_adopt,
        adopt_version: None,
    }
}

pub struct Guards;

impl InstallGuards for Guards {
    fn game_or_f4se_running(&self) -> bool {
        mods::game_running()
    }

    fn mo2_running(&self) -> bool {
        mods::mo2_running()
    }
}

fn mod_target(pin: Option<&Pin>) -> Option<(&Pin, &str, Runtime)> {
    let pin = pin?;
    Some((
        pin,
        pin.profile.as_deref()?,
        mods::detected_runtime(&pin.game)?,
    ))
}

fn check(manual: bool, paths: &Paths, info: &ModChannel) -> Option<Event> {
    match update::check(
        &UreqHttp::new(),
        paths,
        info.installed_version.as_deref(),
        now(),
    ) {
        Ok(Some(offer)) => Some(available(offer, info.clone())),
        Ok(None) => manual.then(|| result(true, NO_NEWER, info)),
        Err(error) => {
            update::log(paths, &format!("update check failed: {error}"));
            manual.then(|| result(false, error.to_string(), info))
        }
    }
}

fn install(paths: &Paths, emit: &dyn Fn(Event), info: &ModChannel, version: &str) -> Option<Event> {
    let Some(exe) = update::installed_exe() else {
        return Some(result(false, "the launcher path is not known", info));
    };
    let mut progress = |done: u64, total: u64| {
        emit(Event::UpdateProgress {
            what: "launcher",
            done,
            total,
        })
    };
    match update::install_launcher(&UreqHttp::new(), paths, &exe, version, &mut progress) {
        Ok(()) => match restart(&exe) {
            Ok(()) => std::process::exit(0),
            Err(error) => Some(result(
                false,
                format!("The launcher was updated but could not start again ({error}). Start it yourself."),
                info,
            )),
        },
        Err(error) => {
            update::log(paths, &format!("launcher update failed: {error}"));
            Some(result(false, error.to_string(), info))
        }
    }
}

fn notify_only(paths: &Paths) -> bool {
    update::load_state(paths).notify_only
}

fn install_mod(
    paths: &Paths,
    pin: Option<&Pin>,
    adopt_unknown: bool,
    version: &str,
    emit: &dyn Fn(Event),
) -> Event {
    let info = mod_channel(pin, paths);
    if notify_only(paths) {
        return result(false, NOTIFY_ONLY, &info);
    }
    let Some((pin, profile, runtime)) = mod_target(pin) else {
        return result(false, NO_TARGET, &info);
    };
    let target = Target {
        instance: &pin.instance,
        profile,
        runtime,
    };
    let mut progress = |done: u64, total: u64| {
        emit(Event::UpdateProgress {
            what: "mod",
            done,
            total,
        })
    };
    let key = update::UPDATE_PUBLIC_KEY;
    let signing = installer::Signing {
        key: key.as_ref(),
        running: update::running_version(),
        tag: version,
    };
    let done = installer::install_mod(
        &UreqHttp::new(),
        paths,
        target,
        adopt_unknown,
        &Guards,
        signing,
        &mut progress,
    );
    match done {
        Ok(installed) => {
            let mut text = format!("{} is installed and enabled.", installed.report.folder);
            if !installed.report.retired.is_empty() {
                text.push_str(&format!(
                    " Old DLLs set aside (renamed to .legacy): {}.",
                    installed.report.retired.join(", ")
                ));
            }
            result(true, text, &mod_channel(Some(pin), paths))
        }
        Err(error) => {
            update::log(paths, &format!("mod update failed: {error}"));
            result(false, error.to_string(), &info)
        }
    }
}

fn rollback_mod(paths: &Paths, pin: Option<&Pin>) -> Event {
    let info = mod_channel(pin, paths);
    if notify_only(paths) {
        return result(false, NOTIFY_ONLY, &info);
    }
    let Some((pin, profile, runtime)) = mod_target(pin) else {
        return result(false, NO_TARGET, &info);
    };
    let target = Target {
        instance: &pin.instance,
        profile,
        runtime,
    };
    match installer::roll_back(target, &Guards) {
        Ok(back) => result(
            true,
            format!(
                "{} is enabled again; {} is disabled. The launcher keeps this choice until you turn {} back on in Mod Organizer 2 or install a newer build.",
                back.restored, back.disabled, back.disabled
            ),
            &mod_channel(Some(pin), paths),
        ),
        Err(error) => result(false, error.to_string(), &info),
    }
}

fn restart(exe: &Path) -> std::io::Result<()> {
    std::process::Command::new(exe)
        .arg(update::FINISH_FLAG)
        .arg(std::process::id().to_string())
        .spawn()
        .map(|_| ())
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    fn decision(channel: InstallChannel) -> ChannelDecision {
        ChannelDecision {
            channel,
            reasons: vec!["why".to_owned()],
        }
    }

    fn wabbajack() -> InstallChannel {
        InstallChannel::Wabbajack {
            modlist: None,
            version: None,
        }
    }

    #[test]
    fn a_failed_settings_write_is_reported_not_panicked() {
        let event = outcome(
            Err(launcher_core::Error::Update("disk full".to_owned())),
            "ok",
            &ModChannel::none(),
        );
        assert!(matches!(
            event,
            Event::UpdateResult { ok: false, ref message, .. } if message == "disk full"
        ));
    }

    #[test]
    fn a_wabbajack_install_is_never_installed_into_or_adopted() {
        let channel = describe(
            decision(wabbajack()),
            true,
            false,
            Some("1.0".to_owned()),
            true,
        );
        assert_eq!(channel.channel, "wabbajack");
        assert!(!channel.can_install_mod && !channel.needs_adopt_confirm);
        assert_eq!(channel.installed_version.as_deref(), Some("1.0"));
        assert_eq!(channel.channel_reasons, vec!["why".to_owned()]);
    }

    #[test]
    fn a_standalone_install_updates_unless_the_user_said_notify_only() {
        let standalone = || decision(InstallChannel::Standalone);
        assert!(describe(standalone(), true, false, None, false).can_install_mod);
        assert!(!describe(standalone(), true, true, None, false).can_install_mod);
        assert!(!describe(standalone(), false, false, None, false).can_install_mod);
    }

    #[test]
    fn an_unknown_install_is_adopted_only_on_request_and_never_under_notify_only() {
        let unknown = || decision(InstallChannel::Unknown);
        let offered = describe(unknown(), true, false, None, true);
        assert!(offered.needs_adopt_confirm && !offered.can_install_mod);
        assert!(!describe(unknown(), true, true, None, true).needs_adopt_confirm);
        assert!(!describe(unknown(), true, false, None, false).needs_adopt_confirm);
        assert!(!describe(unknown(), false, false, None, true).needs_adopt_confirm);
    }

    #[test]
    fn a_missing_instance_offers_nothing_to_install() {
        let channel = mod_channel(None, &Paths::new(PathBuf::from("no-such-state-dir")));
        assert_eq!(channel.channel, "unknown");
        assert!(!channel.can_install_mod && !channel.needs_adopt_confirm);
        assert_eq!(channel.channel_reasons, vec![NO_INSTANCE.to_owned()]);
    }

    fn put(path: &Path, text: &str) {
        std::fs::create_dir_all(path.parent().unwrap()).unwrap();
        std::fs::write(path, text).unwrap();
    }

    fn instance_pin(root: &Path) -> (Pin, PathBuf) {
        let instance = root.join("instance");
        put(&instance.join("mods").join("placeholder.txt"), "");
        let list = instance
            .join("profiles")
            .join("Default")
            .join("modlist.txt");
        put(&list, "+Main\n");
        let pin = Pin {
            game: root.join("game"),
            exe: None,
            instance,
            profile: Some("Default".to_owned()),
        };
        (pin, list)
    }

    fn tree(path: &Path) -> Vec<PathBuf> {
        let mut found = Vec::new();
        let mut pending = vec![path.to_path_buf()];
        while let Some(dir) = pending.pop() {
            for entry in std::fs::read_dir(&dir).unwrap().flatten() {
                if entry.path().is_dir() {
                    pending.push(entry.path());
                }
                found.push(entry.path());
            }
        }
        found.sort();
        found
    }

    #[test]
    fn notify_only_refuses_a_mod_install_before_any_download_or_write() {
        let root = tempfile::tempdir().unwrap();
        let (pin, list) = instance_pin(root.path());
        let paths = Paths::new(root.path().join("state"));
        update::set_prefs(&paths, true, false, true).unwrap();
        let state_file = root.path().join("state").join("launcher-state.json");
        let before = (
            std::fs::read(&state_file).unwrap(),
            tree(root.path()),
            std::fs::read_to_string(&list).unwrap(),
        );
        let emitted = std::cell::RefCell::new(Vec::new());
        let emit = |event: Event| emitted.borrow_mut().push(event);
        let event = install_mod(&paths, Some(&pin), true, "0.0.0.8", &emit);
        assert!(matches!(
            event,
            Event::UpdateResult { ok: false, ref message, .. } if message == NOTIFY_ONLY
        ));
        assert!(emitted.borrow().is_empty());
        let after = (
            std::fs::read(&state_file).unwrap(),
            tree(root.path()),
            std::fs::read_to_string(&list).unwrap(),
        );
        assert_eq!(before, after);
    }

    #[test]
    fn notify_only_refuses_a_rollback_and_leaves_the_modlist_alone() {
        let root = tempfile::tempdir().unwrap();
        let (pin, list) = instance_pin(root.path());
        let paths = Paths::new(root.path().join("state"));
        update::set_prefs(&paths, true, false, true).unwrap();
        let event = rollback_mod(&paths, Some(&pin));
        assert!(matches!(
            event,
            Event::UpdateResult { ok: false, ref message, .. } if message == NOTIFY_ONLY
        ));
        assert_eq!(std::fs::read_to_string(&list).unwrap(), "+Main\n");
    }

    #[test]
    fn without_notify_only_the_same_request_is_not_refused_by_the_gate() {
        let root = tempfile::tempdir().unwrap();
        let (pin, _) = instance_pin(root.path());
        let paths = Paths::new(root.path().join("state"));
        update::set_prefs(&paths, true, false, false).unwrap();
        let emit = |_: Event| {};
        let event = install_mod(&paths, Some(&pin), true, "0.0.0.8", &emit);
        assert!(matches!(
            event,
            Event::UpdateResult { ok: false, ref message, .. } if message == NO_TARGET
        ));
    }

    #[test]
    fn an_offer_keeps_every_field_for_the_page() {
        let event = available(
            Offer {
                version: "0.0.0.8".to_owned(),
                prerelease: true,
                launcher_version: Some("0.0.0.8".to_owned()),
                mod_version: None,
                notes: Some("notes".to_owned()),
                signing_configured: false,
            },
            ModChannel::none(),
        );
        assert!(matches!(
            event,
            Event::UpdateAvailable {
                ref version,
                ref launcher_version,
                mod_version: None,
                signing_configured: false,
                ..
            }
                if version == "0.0.0.8" && launcher_version.as_deref() == Some("0.0.0.8")
        ));
    }

    #[test]
    fn saving_settings_emits_fresh_prefs_without_a_check_when_the_channel_is_unchanged() {
        let root = tempfile::tempdir().unwrap();
        let paths = Paths::new(root.path().join("state"));
        let emitted = std::cell::RefCell::new(Vec::new());
        let emit = |event: Event| emitted.borrow_mut().push(event);
        let event = settings(&paths, None, true, true, false, &emit);
        assert!(matches!(event, Event::UpdateResult { ok: true, .. }));
        let sent = emitted.borrow();
        assert_eq!(sent.len(), 1);
        assert!(matches!(
            sent[0],
            Event::UpdatePrefs {
                include_prereleases: true,
                auto_install: true,
                notify_only: false,
                ..
            }
        ));
    }
}
