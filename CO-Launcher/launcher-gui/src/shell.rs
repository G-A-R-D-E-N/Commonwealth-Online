use std::panic::{self, AssertUnwindSafe};
use std::path::PathBuf;

use crate::admin::{self, Request};
use crate::diagnose;
use crate::locations::{self, Locations};
use crate::logs;
use crate::mods::{self, Pin};
use crate::ops;
use crate::protocol::{Command, Event, HostMode, Task};
use crate::server;
use crate::settings::Settings;
use crate::state::{self, NO_MO2, OS_NOTE};
use crate::troubleshoot;
use crate::update::{self, Update};

const BUSY: &str = "another task is running";
const BUSY_QUIT: &str = "the launcher is busy; wait for it to finish";
const STOPPED: &str = "the task stopped unexpectedly";

#[derive(Debug)]
pub enum Step {
    Emit(Event),
    Work(Job),
    Reveal(PathBuf),
    OpenFolder(PathBuf),
    Quit,
}

#[derive(Debug)]
pub struct Job {
    task: Task,
    pin: Option<Pin>,
    mode: HostMode,
    request: Option<Request>,
    settings: Option<Box<Settings>>,
    restart: bool,
    update: Option<Update>,
}

impl Job {
    pub fn run(self, emit: &dyn Fn(Event)) {
        let task = self.task;
        run_guarded(task, emit, |emit| self.execute(emit));
    }

    fn execute(&self, emit: &dyn Fn(Event)) {
        let pin = self.pin.as_ref();
        let finished = match self.task {
            Task::Check => {
                mods::sync_versions(pin);
                update::prefs_event(pin).into_iter().for_each(emit);
                Some(state::snapshot(pin))
            }
            Task::Play => ops::play(pin),
            Task::OpenMo2 => ops::open_mo2(pin),
            Task::Mods => Some(mods::list(pin)),
            Task::Crashes => Some(logs::crashes(pin)),
            Task::Share => Some(logs::share(pin)),
            Task::EndServers => Some(troubleshoot::fix(self.task, troubleshoot::end_servers())),
            Task::ClearLinks => Some(troubleshoot::fix(self.task, troubleshoot::clear_links())),
            Task::RepairServer => Some(troubleshoot::fix(self.task, troubleshoot::repair(pin))),
            Task::ResetFolder => Some(troubleshoot::fix(self.task, troubleshoot::reset_folder())),
            Task::CheckServerFiles => Some(troubleshoot::server_files(pin)),
            Task::CheckSetup => Some(diagnose::setup(pin)),
            Task::Server => Some(server::overview(pin, true)),
            Task::Network => Some(server::network(pin, self.mode)),
            Task::Start => {
                server::start(pin).into_iter().for_each(emit);
                Some(server::overview(pin, false))
            }
            Task::Admin => self
                .request
                .as_ref()
                .map(|request| server::run(pin, request, emit)),
            Task::Save => self
                .settings
                .as_ref()
                .map(|settings| server::save(pin, settings, self.restart, self.mode, emit)),
            Task::Restart => Some(server::restart(pin, emit)),
            Task::UpdateCheck
            | Task::UpdateInstall
            | Task::UpdateInstallMod
            | Task::UpdateRollbackMod
            | Task::UpdateSkip
            | Task::UpdateSettings => self
                .update
                .as_ref()
                .and_then(|update| update::run(update, pin, emit)),
        };
        finished.into_iter().for_each(emit);
    }
}

pub fn run_guarded(task: Task, emit: &dyn Fn(Event), body: impl FnOnce(&dyn Fn(Event))) {
    if panic::catch_unwind(AssertUnwindSafe(|| body(emit))).is_err() {
        emit(Event::Error {
            message: STOPPED.to_owned(),
        });
    }
    emit(Event::Busy {
        on: false,
        what: task,
    });
}

fn interruptible(task: Task) -> bool {
    matches!(
        task,
        Task::Crashes
            | Task::OpenMo2
            | Task::Play
            | Task::Network
            | Task::Start
            | Task::UpdateCheck
            | Task::CheckServerFiles
            | Task::CheckSetup
    )
}

#[derive(Debug)]
pub struct Shell {
    busy: Option<Task>,
    shared: Option<PathBuf>,
    pin: Option<Pin>,
    locations_file: Option<PathBuf>,
}

impl Shell {
    pub fn new(pin: Option<Pin>) -> Shell {
        Shell {
            busy: None,
            shared: None,
            pin,
            locations_file: locations::user_file(),
        }
    }

    pub fn handle(&mut self, command: Command) -> Vec<Step> {
        match command {
            Command::Init => match self.busy {
                Some(what) => vec![Step::Emit(Event::Busy { on: true, what })],
                None => self.start(Task::Check),
            },
            Command::Play => self.launch(Task::Play),
            Command::OpenMo2 => self.launch(Task::OpenMo2),
            Command::Mods => self.start(Task::Mods),
            Command::Crashes => self.start(Task::Crashes),
            Command::ShareLogs => self.start(Task::Share),
            Command::RevealLogs => self.shared.clone().map(Step::Reveal).into_iter().collect(),
            Command::EndServers => self.start(Task::EndServers),
            Command::ClearLinks => self.start(Task::ClearLinks),
            Command::RepairServer => self.start(Task::RepairServer),
            Command::ResetFolder => self.start(Task::ResetFolder),
            Command::CheckServerFiles => self.start(Task::CheckServerFiles),
            Command::CheckSetup => self.start(Task::CheckSetup),
            Command::Server => self.start(Task::Server),
            Command::ServerNetwork { mode } => self.begin(Task::Network, mode, None, None),
            Command::ServerStart => self.start(Task::Start),
            Command::ServerRestart => self.start(Task::Restart),
            Command::ServerSave {
                settings,
                restart,
                mode,
            } => {
                let mut steps = self.begin(Task::Save, mode, None, Some(Box::new(settings)));
                if let Some(Step::Work(job)) = steps.last_mut() {
                    job.restart = restart;
                }
                steps
            }
            Command::ServerAdmin { command, args } => self.admin(admin::checked(&command, args)),
            Command::UpdateCheck => {
                self.update_job(Task::UpdateCheck, Update::Check { manual: true })
            }
            Command::UpdateInstallLauncher => self.update_job(Task::UpdateInstall, Update::Install),
            Command::UpdateInstallMod { adopt_unknown } => {
                self.update_job(Task::UpdateInstallMod, Update::InstallMod { adopt_unknown })
            }
            Command::UpdateRollbackMod => {
                self.update_job(Task::UpdateRollbackMod, Update::RollbackMod)
            }
            Command::LocationsGet => {
                let result = self
                    .locations_file
                    .as_ref()
                    .ok_or_else(|| "The launcher data folder is not available.".to_owned())
                    .and_then(|path| locations::load(path));
                match result {
                    Ok(locations) => vec![Step::Emit(Event::Locations {
                        locations,
                        message: String::new(),
                    })],
                    Err(message) => vec![Step::Emit(Event::Locations {
                        locations: Locations::default(),
                        message,
                    })],
                }
            }
            Command::LocationsSave { locations } => self.save_locations(locations),
            Command::LocationsRevealVortex => {
                let path = self
                    .locations_file
                    .as_ref()
                    .and_then(|path| locations::load(path).ok())
                    .map(|value| PathBuf::from(value.vortex))
                    .filter(|path| path.is_dir());
                match path {
                    Some(path) => vec![Step::OpenFolder(path)],
                    None => vec![Step::Emit(Event::LocationsNotice {
                        message: "Save a valid Vortex staging folder first.".into(),
                    })],
                }
            }
            Command::UpdateSkip { version } => {
                self.update_job(Task::UpdateSkip, Update::Skip(version))
            }
            Command::UpdateSettings {
                include_prereleases,
                auto_install,
                notify_only,
            } => self.update_job(
                Task::UpdateSettings,
                Update::Settings {
                    include_prereleases,
                    auto_install,
                    notify_only,
                },
            ),
            Command::Quit => self.request_close(),
        }
    }

    pub fn auto_update_check(&mut self) -> Vec<Step> {
        if self.busy.is_some() {
            return Vec::new();
        }
        self.update_job(Task::UpdateCheck, Update::Check { manual: false })
    }

    fn save_locations(&mut self, locations: Locations) -> Vec<Step> {
        if self.busy.is_some() {
            return vec![error(BUSY)];
        }
        if server::startup_in_progress() {
            return vec![error(
                "Wait for the server to finish starting or stopping before changing locations.",
            )];
        }
        let result = self
            .locations_file
            .as_ref()
            .ok_or_else(|| "The launcher data folder is not available.".to_owned())
            .and_then(|path| locations::save(path, &locations));
        match result {
            Ok(()) => {
                self.pin = mods::pin_with_locations(&locations);
                let message = if self.pin.is_none()
                    && (!locations.game.is_empty() || !locations.mo2.is_empty())
                {
                    "Saved. No matching Fallout 4 Mod Organizer instance was found."
                } else {
                    "Locations saved."
                };
                vec![
                    Step::Emit(Event::Locations {
                        locations,
                        message: message.to_owned(),
                    }),
                    Step::Emit(state::snapshot(self.pin.as_ref())),
                ]
            }
            Err(message) => vec![Step::Emit(Event::Locations { locations, message })],
        }
    }

    fn update_job(&mut self, task: Task, update: Update) -> Vec<Step> {
        let mut steps = self.begin(task, HostMode::P2p, None, None);
        if let Some(Step::Work(job)) = steps.last_mut() {
            job.update = Some(update);
        }
        steps
    }

    fn admin(&mut self, request: Result<Request, String>) -> Vec<Step> {
        match request {
            Ok(request) => self.begin(Task::Admin, HostMode::P2p, Some(request), None),
            Err(text) => vec![Step::Emit(Event::ServerReply {
                ok: false,
                text,
                data: None,
            })],
        }
    }

    pub fn request_close(&self) -> Vec<Step> {
        match self.busy {
            Some(task) if !interruptible(task) => vec![error(BUSY_QUIT)],
            _ => vec![Step::Quit],
        }
    }

    pub fn observe(&mut self, event: &Event) {
        match event {
            Event::Busy { on, what } => self.busy = on.then_some(*what),
            Event::LogsShared { path, .. } => self.shared = Some(PathBuf::from(path)),
            _ => {}
        }
    }

    fn start(&mut self, task: Task) -> Vec<Step> {
        self.begin(task, HostMode::P2p, None, None)
    }

    fn begin(
        &mut self,
        task: Task,
        mode: HostMode,
        request: Option<Request>,
        settings: Option<Box<Settings>>,
    ) -> Vec<Step> {
        if self.busy.is_some() {
            return vec![error(BUSY)];
        }
        self.busy = Some(task);
        vec![
            Step::Emit(Event::Busy {
                on: true,
                what: task,
            }),
            Step::Work(Job {
                task,
                pin: self.pin.clone(),
                mode,
                request,
                settings,
                restart: false,
                update: None,
            }),
        ]
    }

    fn launch(&mut self, task: Task) -> Vec<Step> {
        if !state::is_windows() {
            return vec![error(OS_NOTE)];
        }
        if !self.pin.as_ref().is_some_and(|pin| pin.exe.is_some()) {
            return vec![error(NO_MO2)];
        }
        self.start(task)
    }
}

fn error(message: impl Into<String>) -> Step {
    Step::Emit(Event::Error {
        message: message.into(),
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cell::RefCell;
    use std::path::Path;

    fn fresh() -> Shell {
        Shell::new(None)
    }

    #[test]
    fn location_commands_can_replace_a_broken_file() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("locations.json");
        std::fs::write(&path, b"broken").unwrap();
        let mut shell = fresh();
        shell.locations_file = Some(path.clone());
        assert!(
            matches!(shell.handle(Command::LocationsGet).as_slice(), [Step::Emit(Event::Locations { message, .. })] if !message.is_empty())
        );
        let value = Locations {
            vortex: temp.path().to_string_lossy().into_owned(),
            ..Locations::default()
        };
        assert!(
            matches!(shell.handle(Command::LocationsSave { locations: value.clone() }).first(), Some(Step::Emit(Event::Locations { locations, .. })) if locations == &value)
        );
        assert_eq!(locations::load(&path).unwrap(), value);
        assert!(
            matches!(shell.handle(Command::LocationsRevealVortex).as_slice(), [Step::OpenFolder(folder)] if folder == temp.path())
        );
        assert!(
            matches!(shell.handle(Command::LocationsGet).as_slice(), [Step::Emit(Event::Locations { locations, .. })] if locations == &value)
        );
        locations::save(&path, &Locations::default()).unwrap();
        assert!(matches!(
            shell.handle(Command::LocationsRevealVortex).as_slice(),
            [Step::Emit(Event::LocationsNotice { .. })]
        ));
        std::fs::remove_file(&path).unwrap();
        assert!(matches!(
            shell.handle(Command::LocationsRevealVortex).as_slice(),
            [Step::Emit(Event::LocationsNotice { .. })]
        ));
    }

    fn launchable() -> Shell {
        Shell::new(Some(Pin {
            game: PathBuf::from("D:/Fallout 4"),
            exe: Some(PathBuf::from("D:/MO2/ModOrganizer.exe")),
            instance: PathBuf::from("D:/MO2"),
            profile: Some("Default".to_owned()),
        }))
    }

    fn busy_on(task: Task) -> Event {
        Event::Busy {
            on: true,
            what: task,
        }
    }

    #[test]
    fn init_starts_a_check_job() {
        let steps = fresh().handle(Command::Init);
        assert!(matches!(
            steps.as_slice(),
            [
                Step::Emit(Event::Busy {
                    on: true,
                    what: Task::Check
                }),
                Step::Work(_)
            ]
        ));
    }

    #[test]
    fn init_while_busy_reports_the_running_task() {
        let mut shell = fresh();
        shell.handle(Command::Mods);
        assert!(matches!(
            shell.handle(Command::Init).as_slice(),
            [Step::Emit(Event::Busy {
                on: true,
                what: Task::Mods
            })]
        ));
    }

    #[test]
    fn second_job_is_refused_until_the_first_reports_busy_off() {
        let mut shell = fresh();
        shell.handle(Command::Init);
        assert!(matches!(
            shell.handle(Command::Mods).as_slice(),
            [Step::Emit(Event::Error { message })] if message == BUSY
        ));
        shell.observe(&Event::Busy {
            on: false,
            what: Task::Check,
        });
        assert!(matches!(
            shell.handle(Command::Mods).as_slice(),
            [Step::Emit(Event::Busy { on: true, .. }), Step::Work(_)]
        ));
    }

    #[test]
    fn play_and_open_mo2_need_a_pinned_mod_organizer() {
        for command in [Command::Play, Command::OpenMo2] {
            assert!(matches!(
                fresh().handle(command).as_slice(),
                [Step::Emit(Event::Error { message })] if message == NO_MO2 || message == OS_NOTE
            ));
        }
    }

    #[test]
    fn open_mo2_is_one_busy_job_when_the_exe_is_known() {
        if !state::is_windows() {
            return;
        }
        let mut shell = launchable();
        assert!(matches!(
            shell.handle(Command::OpenMo2).as_slice(),
            [
                Step::Emit(Event::Busy {
                    on: true,
                    what: Task::OpenMo2
                }),
                Step::Work(_)
            ]
        ));
        shell.observe(&busy_on(Task::OpenMo2));
        assert!(matches!(
            shell.handle(Command::Play).as_slice(),
            [Step::Emit(Event::Error { message })] if message == BUSY
        ));
    }

    #[test]
    fn quit_waits_for_jobs_that_may_write_but_not_for_read_only_ones() {
        let mut shell = fresh();
        assert!(matches!(
            shell.handle(Command::Quit).as_slice(),
            [Step::Quit]
        ));
        for (task, quits) in [
            (Task::Mods, false),
            (Task::Crashes, true),
            (Task::OpenMo2, true),
            (Task::Play, true),
            (Task::Check, false),
            (Task::Share, false),
            (Task::EndServers, false),
            (Task::ClearLinks, false),
            (Task::RepairServer, false),
            (Task::ResetFolder, false),
            (Task::CheckServerFiles, true),
            (Task::CheckSetup, true),
            (Task::Server, false),
            (Task::Admin, false),
            (Task::Network, true),
            (Task::Start, true),
            (Task::Save, false),
            (Task::Restart, false),
            (Task::UpdateCheck, true),
            (Task::UpdateInstall, false),
            (Task::UpdateInstallMod, false),
            (Task::UpdateRollbackMod, false),
            (Task::UpdateSkip, false),
            (Task::UpdateSettings, false),
        ] {
            shell.observe(&busy_on(task));
            let steps = shell.request_close();
            assert_eq!(matches!(steps.as_slice(), [Step::Quit]), quits, "{task:?}");
            shell.observe(&Event::Busy {
                on: false,
                what: task,
            });
        }
        shell.observe(&busy_on(Task::Check));
        assert!(matches!(
            shell.handle(Command::Quit).as_slice(),
            [Step::Emit(Event::Error { message })] if message == BUSY_QUIT
        ));
    }

    #[test]
    fn share_logs_is_a_busy_job_and_reveal_only_opens_the_zip_it_made() {
        let mut shell = fresh();
        assert!(shell.handle(Command::RevealLogs).is_empty());
        let steps = shell.handle(Command::ShareLogs);
        assert!(matches!(steps.as_slice(), [Step::Emit(_), Step::Work(_)]));
        shell.observe(&Event::LogsShared {
            path: "C:\\Desktop\\logs.zip".to_owned(),
            files: 3,
        });
        assert!(matches!(
            shell.handle(Command::RevealLogs).as_slice(),
            [Step::Reveal(path)] if path == Path::new("C:\\Desktop\\logs.zip")
        ));
    }

    #[test]
    fn troubleshoot_fixes_are_busy_jobs() {
        for (command, task) in [
            (Command::EndServers, Task::EndServers),
            (Command::ClearLinks, Task::ClearLinks),
            (Command::RepairServer, Task::RepairServer),
            (Command::ResetFolder, Task::ResetFolder),
            (Command::CheckServerFiles, Task::CheckServerFiles),
            (Command::CheckSetup, Task::CheckSetup),
        ] {
            let steps = fresh().handle(command);
            assert!(matches!(
                steps.first(),
                Some(Step::Emit(Event::Busy { on: true, what })) if *what == task
            ));
            assert!(matches!(steps.last(), Some(Step::Work(job)) if job.task == task));
        }
    }

    #[test]
    fn server_commands_are_jobs_and_bad_admin_input_is_answered_without_one() {
        let mut shell = fresh();
        for command in [
            Command::Server,
            Command::ServerStart,
            Command::ServerNetwork {
                mode: HostMode::Dedicated,
            },
        ] {
            assert!(matches!(
                shell.handle(command).as_slice(),
                [Step::Emit(Event::Busy { on: true, .. }), Step::Work(_)]
            ));
            shell.busy = None;
        }
        let bad = |command: &str, args: serde_json::Value| Command::ServerAdmin {
            command: command.to_owned(),
            args: args.as_object().cloned().unwrap_or_default(),
        };
        let reply = shell.handle(bad("ki!ck", serde_json::json!({})));
        assert!(matches!(
            reply.as_slice(),
            [Step::Emit(Event::ServerReply { ok: false, .. })]
        ));
        assert!(shell.busy.is_none());
        let good = shell.handle(bad("kick", serde_json::json!({"playerId": 3})));
        assert!(matches!(good.as_slice(), [Step::Emit(_), Step::Work(_)]));
    }

    #[test]
    fn saving_and_restarting_the_server_are_busy_jobs() {
        let mut shell = fresh();
        let settings = crate::settings::load(std::path::Path::new("no-such-folder"));
        let steps = shell.handle(Command::ServerSave {
            settings: settings.clone(),
            restart: true,
            mode: HostMode::Dedicated,
        });
        assert!(matches!(
            steps.as_slice(),
            [Step::Emit(Event::Busy { on: true, .. }), Step::Work(_)]
        ));
        let [.., Step::Work(job)] = steps.as_slice() else {
            panic!("no job");
        };
        assert_eq!(
            (job.task, job.mode, job.restart),
            (Task::Save, HostMode::Dedicated, true)
        );
        shell.busy = None;
        for command in [
            Command::ServerSave {
                settings,
                restart: true,
                mode: HostMode::P2p,
            },
            Command::ServerRestart,
        ] {
            assert!(matches!(
                shell.handle(command).as_slice(),
                [Step::Emit(Event::Busy { on: true, .. }), Step::Work(_)]
            ));
            shell.busy = None;
        }
    }

    #[test]
    fn update_commands_are_jobs_and_the_timer_check_waits_its_turn() {
        let mut shell = fresh();
        assert!(matches!(
            shell.handle(Command::UpdateInstallLauncher).as_slice(),
            [
                Step::Emit(Event::Busy {
                    on: true,
                    what: Task::UpdateInstall
                }),
                Step::Work(_)
            ]
        ));
        shell.busy = None;
        assert!(matches!(
            shell
                .handle(Command::UpdateInstallMod {
                    adopt_unknown: true
                })
                .as_slice(),
            [
                Step::Emit(Event::Busy {
                    on: true,
                    what: Task::UpdateInstallMod
                }),
                Step::Work(_)
            ]
        ));
        shell.busy = None;
        assert!(matches!(
            shell.handle(Command::UpdateRollbackMod).as_slice(),
            [
                Step::Emit(Event::Busy {
                    on: true,
                    what: Task::UpdateRollbackMod
                }),
                Step::Work(_)
            ]
        ));
        shell.busy = None;
        assert!(matches!(
            shell
                .handle(Command::UpdateSkip {
                    version: "0.0.0.8".to_owned()
                })
                .as_slice(),
            [
                Step::Emit(Event::Busy {
                    on: true,
                    what: Task::UpdateSkip
                }),
                Step::Work(_)
            ]
        ));
        shell.busy = None;
        assert_eq!(shell.auto_update_check().len(), 2);
        assert!(shell.auto_update_check().is_empty());
    }

    #[test]
    fn a_panicking_job_reports_an_error_and_ends_busy() {
        let events = RefCell::new(Vec::new());
        let record = |event: Event| events.borrow_mut().push(event);
        run_guarded(Task::Mods, &record, |_| panic!("boom"));
        let events = events.into_inner();
        assert_eq!(events.len(), 2);
        assert!(matches!(&events[0], Event::Error { message } if message == STOPPED));
        assert_eq!(
            events[1],
            Event::Busy {
                on: false,
                what: Task::Mods
            }
        );
    }
}
