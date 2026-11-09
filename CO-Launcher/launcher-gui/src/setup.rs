use std::collections::HashSet;
use std::fs;
use std::path::{Path, PathBuf};

use launcher_core::addrlib::{self, Found, Origin, Search};
use launcher_core::channel::{self, InstallChannel};
use launcher_core::discover::{find_game, is_gog_install, Env};
use launcher_core::modinstall::Target;
use launcher_core::modmgr::{self, Roots};
use launcher_core::runtime::{detect, Detected, Runtime};
use launcher_core::setup::{self as flow, ManagerKind, Mo2Plan, State, MANAGER_NONE};
use launcher_core::update::{self, http::UreqHttp, Paths};
use launcher_core::vortex;

use crate::locations::Locations;
use crate::mods::{self, Pin, MODS_WARNING};
use crate::protocol::{
    AddressPage, Event, RuntimeView, SetupManager, SetupManagerView, SetupRequest, SetupView,
};
use crate::update::Guards;

mod findings;

use findings::{dependency_notes, f4se_version, has_f4se, scan_mod_files};

pub const NEXUS_OG: &str = "https://www.nexusmods.com/fallout4/mods/47327";
pub const NEXUS_SEARCH: &str = "https://www.nexusmods.com/fallout4/mods/?keyword=Address+Library";
const STEAM_VERIFY: &str = "steam://validate/377160";
const NO_FOLDER: &str = "The launcher data folder is not available.";
const WRITES_GAME_RUNNING: &str = "Close Fallout 4 before setup changes its files.";
const MO2_RUNNING: &str = "Close Mod Organizer 2 before setup changes its instance.";
const NOT_MANAGED: &str = "That Mod Organizer 2 instance does not manage this Fallout 4 folder.";
const CHECKS_NOTE: &str = "The launcher lists file names in the game folder to find common mod files. It does not check what they contain, so an empty list does not prove there are no problems.";
const VERIFY_STEAM: &str = "Steam can check Fallout 4's game files and replace any that are missing or changed. Use it if the game will not start. Replacing game files can undo mod changes to them.";
const VERIFY_GOG: &str = "This is a GOG install. In GOG Galaxy, use the verify or repair option for Fallout 4 to check its game files. That can replace changed game files, so mod changes to them can be undone.";

// Constraint: the probe is the only place the scan reads the machine; tests pass a quiet one.
pub struct Probe {
    pub roots: Roots,
    pub env: Env,
    pub discover_game: bool,
    pub game_running: bool,
    pub mo2_running: bool,
    pub vortex_running: bool,
    pub vortex: Option<vortex::Install>,
}

impl Probe {
    pub fn system() -> Probe {
        let roots = Roots::from_env();
        let vortex = vortex::detect(&roots);
        Probe {
            roots,
            env: Env::detect(),
            discover_game: true,
            game_running: mods::game_running(),
            mo2_running: mods::mo2_running(),
            vortex_running: mods::process_running("Vortex.exe"),
            vortex,
        }
    }

    #[cfg(test)]
    fn quiet() -> Probe {
        Probe {
            roots: Roots::default(),
            env: Env {
                home: None,
                registry_steam_paths: Vec::new(),
                registry_gog_paths: Vec::new(),
            },
            discover_game: false,
            game_running: false,
            mo2_running: false,
            vortex_running: false,
            vortex: None,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Source {
    Imported,
    Existing,
    Stored,
}

pub fn address_url(page: AddressPage) -> &'static str {
    match page {
        AddressPage::Og => NEXUS_OG,
        AddressPage::Search => NEXUS_SEARCH,
        AddressPage::F4se => "https://f4se.silverlock.org/",
        AddressPage::Mo2 => "https://www.modorganizer.org/",
        AddressPage::SteamVerify => STEAM_VERIFY,
    }
}

fn manager_label(saved: &str) -> &'static str {
    [ManagerKind::Mo2, ManagerKind::Vortex]
        .into_iter()
        .map(ManagerKind::label)
        .find(|label| *label == saved)
        .unwrap_or(MANAGER_NONE)
}

fn manager_key(manager: SetupManager) -> &'static str {
    match manager {
        SetupManager::Mo2 => ManagerKind::Mo2.label(),
        SetupManager::Vortex => ManagerKind::Vortex.label(),
        SetupManager::Neither => MANAGER_NONE,
    }
}

fn text(path: &Path) -> String {
    path.to_string_lossy().into_owned()
}

fn game_path(
    configured: &str,
    pin: Option<&Pin>,
    env: &Env,
    discover: bool,
) -> Result<PathBuf, String> {
    let configured = configured.trim();
    if !configured.is_empty() {
        return find_game(Some(Path::new(configured)), env).map_err(|e| e.to_string());
    }
    if let Some(pin) = pin {
        return Ok(pin.game.clone());
    }
    if !discover {
        return Err("Fallout 4 was not searched for in this scan.".to_owned());
    }
    find_game(None, env).map_err(|e| e.to_string())
}

fn runtime_of(game: &Path) -> Result<Runtime, String> {
    match detect(game).map_err(|e| e.to_string())?.detected {
        Detected::Supported(runtime) => Ok(runtime),
        Detected::Unsupported(found) => Err(format!(
            "Fallout 4 {found} is not a supported runtime. Supported: 1.10.163 (OG), 1.10.984 (NG), 1.11.240 (AE)."
        )),
    }
}

fn channel_label(dir: &Path) -> &'static str {
    match channel::install_channel(dir) {
        InstallChannel::Wabbajack { .. } => "wabbajack",
        InstallChannel::Standalone => "standalone",
        InstallChannel::Unknown => "unknown",
    }
}

fn selected_locations(previous: &Locations, game: &Path, instance: Option<&Path>) -> Locations {
    Locations {
        game: text(game),
        mo2: instance.map(text).unwrap_or_default(),
        vortex: previous.vortex.clone(),
    }
}

fn configured_vortex(locations: &Locations) -> Option<PathBuf> {
    let value = locations.vortex.trim();
    (!value.is_empty()).then(|| PathBuf::from(value))
}

fn deployed_set(listing: &vortex::Listing) -> HashSet<String> {
    listing
        .mods
        .iter()
        .filter(|m| m.enabled)
        .map(|m| m.id.to_ascii_lowercase())
        .collect()
}

fn saved_state() -> State {
    flow::user_file()
        .map(|path| flow::load(&path))
        .unwrap_or_default()
}

pub fn saved_manager() -> String {
    saved_state().manager
}

fn source_mo2<'a>(
    chosen: Option<(&'a Path, &'a str)>,
    pin: Option<&'a Pin>,
) -> Option<(&'a Path, &'a str)> {
    chosen.or_else(|| pin.and_then(|p| Some((p.instance.as_path(), p.profile.as_deref()?))))
}

pub fn scan(
    pin: Option<&Pin>,
    locations: &Locations,
    state: &State,
    game_hint: &str,
    probe: &Probe,
) -> SetupView {
    let root = flow::default_download_root();
    let root_text = root.as_deref().map(text);
    let download_dir = if state.download_dir.trim().is_empty() {
        root_text.clone().unwrap_or_default()
    } else {
        state.download_dir.clone()
    };
    let mut view = SetupView {
        setup_complete: state.setup_complete,
        ack_experimental: state.ack_experimental,
        download_dir,
        default_download_dir: root_text,
        warning: MODS_WARNING,
        prerelease_note: flow::PRERELEASE_NOTE,
        game_dir: None,
        game_error: None,
        runtime: None,
        unsupported: None,
        f4se_present: false,
        f4se_version: None,
        findings: Vec::new(),
        dependency_notes: Vec::new(),
        checks_note: CHECKS_NOTE,
        gog_install: false,
        verify_note: VERIFY_STEAM,
        saved_manager: manager_label(&state.manager),
        address_file: None,
        address_status: "missing",
        address_path: None,
        address_known: false,
        managers: Vec::new(),
        game_running: probe.game_running,
        mo2_running: probe.mo2_running,
        vortex_running: probe.vortex_running,
    };
    let configured = if game_hint.trim().is_empty() {
        locations.game.as_str()
    } else {
        game_hint.trim()
    };
    let game = game_path(configured, pin, &probe.env, probe.discover_game);
    let game_ref = game.as_ref().ok().map(PathBuf::as_path);
    let runtime = match &game {
        Ok(game) => {
            view.game_dir = Some(text(game));
            view.f4se_present = has_f4se(game);
            view.f4se_version = f4se_version(game);
            view.findings = scan_mod_files(game);
            view.dependency_notes = dependency_notes(game);
            view.gog_install = is_gog_install(game);
            view.verify_note = if view.gog_install {
                VERIFY_GOG
            } else {
                VERIFY_STEAM
            };
            match detect(game) {
                Ok(info) => match info.detected {
                    Detected::Supported(runtime) => {
                        view.runtime = Some(RuntimeView::of(runtime));
                        Some(runtime)
                    }
                    Detected::Unsupported(found) => {
                        view.unsupported = Some(found);
                        None
                    }
                },
                Err(error) => {
                    view.game_error = Some(error.to_string());
                    None
                }
            }
        }
        Err(error) => {
            view.game_error = Some(error.clone());
            None
        }
    };
    for install in modmgr::detect_in(&probe.roots, game_ref) {
        view.managers.push(SetupManagerView {
            kind: ManagerKind::Mo2.label(),
            name: install.name.clone(),
            dir: text(&install.dir),
            exe: install.exe.as_deref().map(text),
            manages_this_game: install.manages_this_game,
            channel: channel_label(&install.dir),
            active_profile: install
                .profiles
                .iter()
                .find(|p| p.active)
                .map(|p| p.name.clone()),
        });
    }
    let fallback = vortex::default_staging(&probe.roots);
    let listing = vortex::listing(
        game_ref,
        configured_vortex(locations).as_deref(),
        fallback.as_deref(),
    );
    if probe.vortex.is_some() || listing.manages_game {
        view.managers.push(SetupManagerView {
            kind: ManagerKind::Vortex.label(),
            name: "Vortex".to_owned(),
            dir: listing.staging.as_deref().map(text).unwrap_or_default(),
            exe: probe
                .vortex
                .as_ref()
                .and_then(|i| i.exe.as_deref().map(text)),
            manages_this_game: listing.manages_game,
            channel: "vortex",
            active_profile: None,
        });
    }
    if let Some(runtime) = runtime {
        view.address_file = Some(addrlib::file_name(runtime));
        let deployed = deployed_set(&listing);
        let mo2 = source_mo2(None, pin);
        let found = addrlib::locate(&Search {
            runtime,
            game: game_ref,
            mo2,
            vortex_staging: listing.staging.as_deref(),
            vortex_deployed: &deployed,
            store: root.as_deref(),
        });
        if let Some(found) = found.iter().find(|f| f.usable) {
            view.address_status = "present";
            view.address_path = Some(text(&found.path));
            view.address_known = found.known;
        } else if let Some(found) = found.iter().find(|f| f.origin == Origin::Store) {
            view.address_status = "stored";
            view.address_path = Some(text(&found.path));
            view.address_known = found.known;
        }
    }
    view
}

fn setup_is_complete(state: &State, pin: Option<&Pin>) -> bool {
    state.setup_complete
        || pin.is_some_and(|pin| {
            matches!(
                channel::install_channel(&pin.instance),
                InstallChannel::Wabbajack { .. }
            )
        })
}

pub fn status(pin: Option<&Pin>) -> Event {
    Event::SetupStatus {
        setup_complete: setup_is_complete(&saved_state(), pin),
    }
}

pub fn get(pin: Option<&Pin>, locations: &Locations, game_hint: &str) -> Event {
    Event::Setup(Box::new(scan(
        pin,
        locations,
        &saved_state(),
        game_hint,
        &Probe::system(),
    )))
}

pub fn validate(request: &SetupRequest) -> Result<(), String> {
    if !request.acknowledge {
        return Err(
            "Read the warning and tick the box to confirm you understand it before setup runs."
                .to_owned(),
        );
    }
    if !Path::new(request.download_dir.trim()).is_absolute() {
        return Err(
            "Choose a download folder with a full path, for example C:\\Users\\you\\AppData\\Local\\CommonwealthOnlineLauncher\\downloads."
                .to_owned(),
        );
    }
    if request.manager == SetupManager::Mo2 && request.instance.trim().is_empty() {
        return Err("Choose a Mod Organizer 2 instance first.".to_owned());
    }
    Ok(())
}

pub fn choose_source(found: &[Found]) -> Option<(Source, &Found)> {
    if let Some(hit) = found.iter().find(|f| f.usable) {
        return Some((Source::Existing, hit));
    }
    found
        .iter()
        .find(|f| f.origin == Origin::Store)
        .map(|hit| (Source::Stored, hit))
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Links {
    pub mo2: bool,
    pub game_copy: bool,
}

pub fn plan_links(manager: SetupManager, found: &[Found], vortex_direct: bool) -> Links {
    let usable_present = found.iter().any(|f| f.usable);
    let game_data_has = found.iter().any(|f| f.origin == Origin::GameData);
    Links {
        mo2: manager == SetupManager::Mo2 && !usable_present,
        game_copy: match manager {
            SetupManager::Neither => !game_data_has,
            SetupManager::Vortex => vortex_direct && !game_data_has,
            SetupManager::Mo2 => false,
        },
    }
}

pub fn address_notes(source: Source, known: bool, found: &[Found]) -> Vec<String> {
    let mut notes = Vec::new();
    let usable_present = found.iter().any(|f| f.usable);
    use Source::{Existing, Imported, Stored};
    match source {
        Imported => {
            if !known {
                notes.push(
                    "Warning: the Address Library file you chose is not on the known list for this Fallout 4 version. It was used anyway; check that it came from a trusted source."
                        .to_owned(),
                );
            }
            if usable_present {
                notes.push(
                    "A working copy of the Address Library is already in place, so the file you chose was not linked; it is kept in your download folder."
                        .to_owned(),
                );
            }
        }
        Existing if !known => notes.push(
            "The Address Library already in place is not on the known list, so it was left as it is."
                .to_owned(),
        ),
        Stored if !known => notes.push(
            "Warning: the stored Address Library copy is not on the known list; it was used anyway."
                .to_owned(),
        ),
        _ => {}
    }
    notes
}

pub fn running_refusal(
    manager: SetupManager,
    writes_game_data: bool,
    game_running: bool,
    mo2_running: bool,
) -> Option<&'static str> {
    if (writes_game_data || manager == SetupManager::Mo2) && game_running {
        return Some(WRITES_GAME_RUNNING);
    }
    if manager == SetupManager::Mo2 && mo2_running {
        return Some(MO2_RUNNING);
    }
    None
}

fn writes_game_data(request: &SetupRequest) -> bool {
    request.manager == SetupManager::Neither
        || (request.manager == SetupManager::Vortex && request.vortex_direct)
}

fn ensure_game_closed() -> Result<(), String> {
    if mods::game_running() {
        Err(WRITES_GAME_RUNNING.to_owned())
    } else {
        Ok(())
    }
}

fn step(emit: &dyn Fn(Event), name: &'static str, message: impl Into<String>) {
    emit(Event::SetupProgress {
        step: name,
        message: message.into(),
        done: 0,
        total: 0,
    });
}

pub fn run(
    request: &SetupRequest,
    pin: Option<&Pin>,
    locations: &Locations,
    emit: &dyn Fn(Event),
) -> Event {
    let probe = Probe::system();
    let (ok, message) = match install(request, pin, locations, emit, &probe) {
        Ok(text) => (true, text),
        Err(text) => (false, text),
    };
    emit(get(pin, locations, ""));
    Event::SetupResult { ok, message }
}

fn install(
    request: &SetupRequest,
    pin: Option<&Pin>,
    locations: &Locations,
    emit: &dyn Fn(Event),
    probe: &Probe,
) -> Result<String, String> {
    validate(request)?;
    let root = PathBuf::from(request.download_dir.trim());
    let configured = if request.game.trim().is_empty() {
        locations.game.as_str()
    } else {
        request.game.trim()
    };
    let game = game_path(configured, pin, &probe.env, true)?;
    let runtime = runtime_of(&game)?;
    let label = runtime.label();
    let file = addrlib::file_name(runtime);
    step(
        emit,
        "detect",
        format!("Found Fallout 4 {label} at {}.", game.display()),
    );

    if let Some(problem) = running_refusal(
        request.manager,
        writes_game_data(request),
        probe.game_running,
        probe.mo2_running,
    ) {
        return Err(problem.to_owned());
    }
    let mut instance: Option<(PathBuf, String)> = None;
    if request.manager == SetupManager::Mo2 {
        let dir = PathBuf::from(request.instance.trim());
        let found =
            modmgr::selected_instance(&dir, Some(&game)).ok_or_else(|| NOT_MANAGED.to_owned())?;
        let chan = channel::install_channel(&found.dir);
        let allowed = channel::may_install_mod_files(&chan, false)
            || (request.adopt_unknown && channel::may_adopt(&found.dir));
        if !allowed {
            return Err(match chan {
                InstallChannel::Wabbajack { .. } => {
                    "This install is managed by Wabbajack: update the modlist in Wabbajack."
                        .to_owned()
                }
                _ => "Tick the box to let the launcher manage this instance, then run setup again."
                    .to_owned(),
            });
        }
        let profile = found
            .profiles
            .iter()
            .find(|p| p.active)
            .map(|p| p.name.clone())
            .ok_or_else(|| "The Mod Organizer 2 instance has no active profile.".to_owned())?;
        instance = Some((found.dir.clone(), profile));
    }

    let fallback = vortex::default_staging(&probe.roots);
    let listing = vortex::listing(
        Some(game.as_path()),
        configured_vortex(locations).as_deref(),
        fallback.as_deref(),
    );
    let deployed = deployed_set(&listing);
    let chosen_mo2 = instance
        .as_ref()
        .map(|(dir, profile)| (dir.as_path(), profile.as_str()));
    let mo2 = source_mo2(chosen_mo2, pin);
    let found = addrlib::locate(&Search {
        runtime,
        game: Some(game.as_path()),
        mo2,
        vortex_staging: listing.staging.as_deref(),
        vortex_deployed: &deployed,
        store: Some(root.as_path()),
    });

    let explicit = request
        .address_library_path
        .as_deref()
        .map(str::trim)
        .filter(|p| !p.is_empty());
    let (bytes, source) = match explicit {
        Some(path) => (
            addrlib::import(Path::new(path), runtime).map_err(|e| e.to_string())?,
            Source::Imported,
        ),
        None => match choose_source(&found) {
            Some((source, hit)) => (fs::read(&hit.path).map_err(|e| e.to_string())?, source),
            None => {
                return Err(format!(
                    "Address Library for Fallout 4 {label} was not found. Download {file} from Nexus Mods (the launcher cannot fetch it, because Nexus needs a login), choose it in the Address Library step, and run setup again."
                ));
            }
        },
    };
    let known = addrlib::matches_known(runtime, &bytes);
    let mut notes = address_notes(source, known, &found);
    let links = plan_links(request.manager, &found, request.vortex_direct);

    let paths = Paths::user().ok_or_else(|| NO_FOLDER.to_owned())?;
    step(
        emit,
        "download",
        format!("Downloading the newest Commonwealth Online build for {label}."),
    );
    let mut on_bytes = |done: u64, total: u64| {
        emit(Event::SetupProgress {
            step: "download",
            message: String::new(),
            done,
            total,
        })
    };
    let key = update::UPDATE_PUBLIC_KEY;
    let fetched = flow::fetch_release(
        &UreqHttp::new(),
        &root,
        key.as_ref(),
        update::running_version(),
        true,
        runtime,
        &mut on_bytes,
    )
    .map_err(|e| e.to_string())?;
    flow::enable_prereleases(&paths).map_err(|e| e.to_string())?;
    step(
        emit,
        "verify",
        format!(
            "{} matches its SHA-256 in the signed release manifest.",
            fetched.name
        ),
    );

    step(emit, "address_library", format!("Using {file}."));
    if source == Source::Imported {
        addrlib::store(&root, runtime, &bytes).map_err(|e| e.to_string())?;
    }

    step(emit, "link", "Linking the files for your setup.");
    let tag = fetched.tag.as_str();
    let summary = match request.manager {
        SetupManager::Mo2 => {
            let (dir, profile) = instance.as_ref().ok_or_else(|| NOT_MANAGED.to_owned())?;
            let plan = Mo2Plan {
                target: Target {
                    instance: dir.as_path(),
                    profile: profile.as_str(),
                    runtime,
                },
                version: tag,
                zip: &fetched.zip,
                address: if links.mo2 {
                    Some(bytes.as_slice())
                } else {
                    None
                },
                adopt_unknown: request.adopt_unknown,
            };
            let done = flow::install_mo2(&plan, &Guards)
                .map_err(|e| format!("Commonwealth Online was not installed: {e}"))?;
            format!(
                "Commonwealth Online {tag} ({label}) is installed in {} and turned on. {}",
                done.report.folder,
                if done.address_linked {
                    "Address Library was linked into that instance."
                } else {
                    "Address Library was already in place."
                }
            )
        }
        SetupManager::Vortex => {
            let dir = root.join(tag);
            let package = flow::vortex_package(&fetched.zip, runtime, tag, &dir)
                .map_err(|e| e.to_string())?;
            let address = addrlib::vortex_zip(&dir, runtime, &bytes).map_err(|e| e.to_string())?;
            let mut summary = format!(
                "Commonwealth Online {tag} ({label}) is packaged for Vortex: {}. Address Library for Vortex: {}. In Vortex, use Install From File with each zip, then Deploy.",
                package.display(),
                address.display()
            );
            if links.game_copy {
                ensure_game_closed()?;
                let copied =
                    addrlib::link_game_data(&game, runtime, &bytes).map_err(|e| e.to_string())?;
                summary.push_str(&format!(
                    " The Address Library was also copied to {}.",
                    copied.display()
                ));
            }
            if mods::process_running("Vortex.exe") {
                summary.push_str(" Vortex is running, so the launcher did not touch its state; deploy from Vortex after setup.");
            }
            summary
        }
        SetupManager::Neither => {
            let record = flow::loose_record_file().ok_or_else(|| NO_FOLDER.to_owned())?;
            let installed =
                flow::install_loose(&fetched.zip, &game, &record).map_err(|e| e.user_message())?;
            let mut summary = format!(
                "Commonwealth Online {tag} ({label}) was copied into the game's Data folder ({installed} files)."
            );
            if links.game_copy {
                ensure_game_closed()?;
                let copied =
                    addrlib::link_game_data(&game, runtime, &bytes).map_err(|e| e.to_string())?;
                summary.push_str(&format!(
                    " The Address Library was copied to {}.",
                    copied.display()
                ));
            }
            summary
        }
    };

    let location_path = crate::locations::user_file().ok_or_else(|| NO_FOLDER.to_owned())?;
    let selected = selected_locations(
        locations,
        &game,
        instance.as_ref().map(|(dir, _)| dir.as_path()),
    );
    crate::locations::save(&location_path, &selected).map_err(|error| {
        format!("The files were prepared, but the selected locations could not be saved: {error}")
    })?;

    if let Some(path) = flow::user_file() {
        let mut state = flow::load(&path);
        state.setup_complete = true;
        state.ack_experimental = true;
        state.download_dir = text(&root);
        state.manager = manager_key(request.manager).to_owned();
        if let Err(error) = flow::save(&path, &state) {
            notes.push(format!(
                "Setup finished, but the setup state could not be saved ({error}); the wizard may open again next time."
            ));
        }
    }
    notes.insert(0, summary);
    Ok(notes.join(" "))
}

pub fn reset(pin: Option<&Pin>, locations: &Locations) -> Event {
    let Some(path) = flow::user_file() else {
        return Event::SetupResult {
            ok: false,
            message: NO_FOLDER.to_owned(),
        };
    };
    let mut state = flow::load(&path);
    state.setup_complete = false;
    state.ack_experimental = false;
    match flow::save(&path, &state) {
        Ok(()) => get(pin, locations, ""),
        Err(error) => Event::SetupResult {
            ok: false,
            message: error.to_string(),
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn request(acknowledge: bool) -> SetupRequest {
        SetupRequest {
            manager: SetupManager::Neither,
            download_dir: "C:\\dl".to_owned(),
            game: String::new(),
            instance: String::new(),
            address_library_path: None,
            adopt_unknown: false,
            vortex_direct: false,
            acknowledge,
        }
    }

    fn found(origin: Origin, usable: bool, known: bool) -> Found {
        Found {
            path: PathBuf::from("version-1-10-163-0.bin"),
            origin,
            usable,
            known,
        }
    }

    #[test]
    fn first_run_skips_wabbajack_but_not_new_standalone_installs() {
        let root = tempfile::tempdir().unwrap();
        let pin = Pin {
            game: root.path().to_path_buf(),
            exe: None,
            instance: root.path().to_path_buf(),
            profile: None,
        };
        assert!(!setup_is_complete(&State::default(), Some(&pin)));
        fs::write(root.path().join("portable.txt"), "Created by Wabbajack").unwrap();
        assert!(setup_is_complete(&State::default(), Some(&pin)));
        assert!(!setup_is_complete(&State::default(), None));
        assert!(setup_is_complete(
            &State {
                setup_complete: true,
                ..State::default()
            },
            None
        ));
    }

    #[test]
    fn setup_run_is_refused_without_the_acknowledgement() {
        assert!(validate(&request(false))
            .unwrap_err()
            .contains("tick the box"));
        assert!(validate(&request(true)).is_ok());
    }

    #[test]
    fn a_relative_download_folder_or_a_missing_instance_is_refused() {
        let mut relative = request(true);
        relative.download_dir = "downloads".to_owned();
        assert!(validate(&relative).unwrap_err().contains("full path"));
        let mut mo2 = request(true);
        mo2.manager = SetupManager::Mo2;
        assert!(validate(&mo2).unwrap_err().contains("instance"));
        mo2.instance = "C:\\MO2".to_owned();
        assert!(validate(&mo2).is_ok());
    }

    #[test]
    fn setup_remembers_the_game_and_selected_manager() {
        let previous = Locations {
            game: "old game".into(),
            mo2: "old instance".into(),
            vortex: "existing staging".into(),
        };
        let game = Path::new("C:/Fallout 4");
        let selected = selected_locations(&previous, game, Some(Path::new("C:/MO2")));
        assert_eq!(selected.game, text(game));
        assert_eq!(selected.mo2, text(Path::new("C:/MO2")));
        assert_eq!(selected.vortex, "existing staging");
        assert!(selected_locations(&previous, game, None).mo2.is_empty());
    }

    #[test]
    fn the_address_pages_are_fixed_urls() {
        assert_eq!(address_url(AddressPage::Og), NEXUS_OG);
        assert!(address_url(AddressPage::Search).starts_with("https://www.nexusmods.com/"));
        assert_eq!(
            address_url(AddressPage::F4se),
            "https://f4se.silverlock.org/"
        );
        assert_eq!(
            address_url(AddressPage::Mo2),
            "https://www.modorganizer.org/"
        );
        assert_eq!(
            address_url(AddressPage::SteamVerify),
            "steam://validate/377160"
        );
    }

    #[test]
    fn the_saved_manager_maps_to_a_known_label() {
        assert_eq!(manager_label(manager_key(SetupManager::Vortex)), "vortex");
        assert_eq!(manager_label(manager_key(SetupManager::Mo2)), "mo2");
        assert_eq!(manager_label("garbage"), MANAGER_NONE);
        assert_eq!(manager_key(SetupManager::Neither), MANAGER_NONE);
    }

    #[test]
    fn the_mods_warning_is_the_required_wording_here_too() {
        assert_eq!(
            MODS_WARNING,
            "Playing with mods is highly experimental and results may vary."
        );
    }

    #[test]
    fn a_quiet_scan_with_nothing_installed_reports_missing_setup() {
        let state = State::default();
        let view = scan(None, &Locations::default(), &state, "", &Probe::quiet());
        assert!(!view.setup_complete && !view.ack_experimental);
        assert_eq!(view.warning, MODS_WARNING);
        assert_eq!(view.address_status, "missing");
        assert!(view.managers.is_empty());
        assert!(view.game_error.is_some());
        assert!(!view.game_running && !view.mo2_running && !view.vortex_running);
    }

    #[test]
    fn a_running_game_or_manager_is_reported_from_the_probe() {
        let mut probe = Probe::quiet();
        probe.game_running = true;
        probe.vortex_running = true;
        let view = scan(None, &Locations::default(), &State::default(), "", &probe);
        assert!(view.game_running && view.vortex_running && !view.mo2_running);
    }

    #[test]
    fn a_copy_in_the_pinned_mo2_is_found_when_the_chosen_target_is_vortex_or_none() {
        let root = tempfile::tempdir().unwrap();
        let instance = root.path().join("mo2");
        let plugin = instance
            .join("mods")
            .join("Address Mod")
            .join("F4SE")
            .join("Plugins")
            .join(addrlib::file_name(Runtime::Og));
        std::fs::create_dir_all(plugin.parent().unwrap()).unwrap();
        std::fs::write(&plugin, b"address bytes").unwrap();
        let profile = instance.join("profiles").join("Default");
        std::fs::create_dir_all(&profile).unwrap();
        std::fs::write(profile.join("modlist.txt"), "+Address Mod\n").unwrap();
        let pin = Pin {
            game: root.path().join("game"),
            exe: None,
            instance: instance.clone(),
            profile: Some("Default".to_owned()),
        };
        let deployed = HashSet::new();
        let found = addrlib::locate(&Search {
            runtime: Runtime::Og,
            game: None,
            mo2: source_mo2(None, Some(&pin)),
            vortex_staging: None,
            vortex_deployed: &deployed,
            store: None,
        });
        let (source, hit) = choose_source(&found).expect("the pinned copy is found");
        assert_eq!(source, Source::Existing);
        assert_eq!(std::fs::read(&hit.path).unwrap(), b"address bytes");
        let chosen = root.path().join("other");
        assert_eq!(
            source_mo2(Some((chosen.as_path(), "Default")), Some(&pin)).map(|(dir, _)| dir),
            Some(chosen.as_path())
        );
    }

    #[test]
    fn the_first_usable_copy_is_the_source_and_the_store_is_the_fallback() {
        let usable = [
            found(Origin::Store, false, false),
            found(Origin::GameData, true, true),
        ];
        assert_eq!(choose_source(&usable).unwrap().0, Source::Existing);
        let stored = [found(Origin::Store, false, true)];
        assert_eq!(choose_source(&stored).unwrap().0, Source::Stored);
        assert!(choose_source(&[]).is_none());
    }

    #[test]
    fn each_manager_gets_only_the_links_it_needs() {
        let none: [Found; 0] = [];
        let plan = plan_links(SetupManager::Mo2, &none, false);
        assert!(plan.mo2 && !plan.game_copy);
        let linked = [found(Origin::Mo2Mod("Address".into()), true, true)];
        assert!(!plan_links(SetupManager::Mo2, &linked, false).mo2);
        assert!(plan_links(SetupManager::Neither, &none, false).game_copy);
        assert!(!plan_links(SetupManager::Vortex, &none, false).game_copy);
        assert!(plan_links(SetupManager::Vortex, &none, true).game_copy);
        let in_data = [found(Origin::GameData, true, true)];
        assert!(!plan_links(SetupManager::Neither, &in_data, false).game_copy);
    }

    #[test]
    fn an_unknown_hash_is_a_warning_and_never_a_refusal() {
        let notes = address_notes(Source::Imported, false, &[]);
        assert_eq!(notes.len(), 1);
        assert!(notes[0].starts_with("Warning"));
        assert!(address_notes(Source::Imported, true, &[]).is_empty());
        let usable = [found(Origin::GameData, true, true)];
        let kept = address_notes(Source::Imported, true, &usable);
        assert!(kept.iter().any(|n| n.contains("was not linked")));
    }

    #[test]
    fn running_programs_refuse_the_writes_they_would_conflict_with() {
        assert_eq!(
            running_refusal(SetupManager::Neither, true, true, false),
            Some(WRITES_GAME_RUNNING)
        );
        assert_eq!(
            running_refusal(SetupManager::Vortex, false, true, false),
            None
        );
        assert_eq!(
            running_refusal(SetupManager::Mo2, false, false, true),
            Some(MO2_RUNNING)
        );
        assert_eq!(
            running_refusal(SetupManager::Mo2, false, false, false),
            None
        );
    }
}
