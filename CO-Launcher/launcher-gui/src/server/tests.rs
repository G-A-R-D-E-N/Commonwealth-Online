use super::*;
use serde_json::json;

static TEST_LOCK: Mutex<()> = Mutex::new(());

fn sleeping_child() -> Child {
    std::process::Command::new(std::env::current_exe().unwrap())
        .args(["--exact", "server::tests::startup_child", "--ignored"])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap()
}

#[test]
#[ignore]
fn startup_child() {
    std::thread::sleep(Duration::from_secs(30));
}

#[test]
fn stop_requested_during_startup_waits_for_graceful_shutdown() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|error| error.into_inner());
    let dir = tempfile::tempdir().unwrap();
    let child = sleeping_child();
    *STARTING.lock().unwrap() = Some(StartingServer {
        dir: dir.path().to_owned(),
        child,
        stop_requested_at: None,
    });
    assert!(startup_in_progress());
    assert!(starting(dir.path(), false).0);
    assert!(!request_startup_stop(&dir.path().join("other")).unwrap());
    assert!(request_startup_stop(dir.path()).unwrap());
    let (active, stopping, _) = starting(dir.path(), true);
    assert!(active && stopping);
    let mut server = STARTING.lock().unwrap().take().unwrap();
    server.child.kill().unwrap();
    server.child.wait().unwrap();
    assert!(!startup_in_progress());
}

#[test]
fn a_stop_requested_long_ago_ends_a_startup_that_has_not_answered() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|error| error.into_inner());
    let dir = tempfile::tempdir().unwrap();
    let child = sleeping_child();
    *STARTING.lock().unwrap() = Some(StartingServer {
        dir: dir.path().to_owned(),
        child,
        stop_requested_at: Some(Instant::now().checked_sub(Duration::from_secs(30)).unwrap()),
    });
    assert_eq!(starting(dir.path(), false), (false, false, None));
    assert!(!startup_in_progress());
}

#[test]
fn an_answering_server_restarts_the_stop_grace() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|error| error.into_inner());
    let dir = tempfile::tempdir().unwrap();
    *STARTING.lock().unwrap() = Some(StartingServer {
        dir: dir.path().to_owned(),
        child: sleeping_child(),
        stop_requested_at: Some(Instant::now().checked_sub(Duration::from_secs(30)).unwrap()),
    });
    assert_eq!(starting(dir.path(), true), (true, true, None));
    let mut server = STARTING.lock().unwrap().take().unwrap();
    assert!(server.stop_requested_at.unwrap().elapsed() < Duration::from_secs(5));
    server.child.kill().unwrap();
    server.child.wait().unwrap();
}

#[test]
fn a_running_server_that_differs_from_the_saved_file_was_started_elsewhere() {
    let saved = settings::load(std::path::Path::new("no-such-folder"));
    let same = json!({"port": saved.port, "server_name": saved.name});
    assert!(!differs_from_saved(&same, &saved));
    assert!(differs_from_saved(
        &json!({"port": 9000, "server_name": saved.name}),
        &saved
    ));
    assert!(differs_from_saved(
        &json!({"port": saved.port, "server_name": "Mine"}),
        &saved
    ));
    assert!(!differs_from_saved(&json!({}), &saved));
}

#[test]
fn replies_become_player_ban_and_status_rows() {
    let clients = json!({"clients": [
        {"player_id": 7, "address": "10.0.0.2", "steam_id": "7656"},
        {"address": "no id"}
    ]});
    assert_eq!(
        players(&clients),
        [PlayerView {
            id: 7,
            address: "10.0.0.2".into(),
            steam_id: Some("7656".into())
        }]
    );
    let listed = bans(&json!({
        "bans": [{"ip": "1.2.3.4", "profileId": "", "reason": "x"}, {"ip": "", "profileId": "p1", "reason": "y"}],
        "steamBans": [{"steamId": "7656", "reason": "z"}]
    }));
    let kinds: Vec<_> = listed.iter().map(|b| (b.kind, b.id.as_str())).collect();
    assert_eq!(
        kinds,
        [("ip", "1.2.3.4"), ("profile", "p1"), ("steam", "7656")]
    );
    let status = describe(
        &json!({"server_name": "S", "connected_clients": 2, "max_players": 16, "world_mode": "coop-story", "port": 7777}),
    );
    assert_eq!(
        (
            status.name.as_str(),
            status.players,
            status.max_players,
            status.port
        ),
        ("S", 2, 16, 7777)
    );
    let Event::ServerReply { data, .. } =
        reply_event(Ok(json!({"ok": true, "data": {"pong": true}})))
    else {
        panic!("not a reply");
    };
    assert_eq!(data, Some(json!({"pong": true})));
    assert_eq!(reply_text(&json!({"ok": true})), "Done.");
    assert_eq!(
        reply_text(&json!({"message": "Kicked player 7."})),
        "Kicked player 7."
    );
}

#[test]
fn the_port_check_only_claims_what_the_bind_shows() {
    let levels: Vec<_> = [
        (true, PortState::Free),
        (true, PortState::InUse),
        (true, PortState::Unknown),
        (false, PortState::InUse),
        (false, PortState::Unknown),
        (false, PortState::Free),
    ]
    .map(|(running, state)| port_check(running, state, 7777).level)
    .into();
    assert_eq!(levels, ["warn", "info", "info", "fail", "warn", "ok"]);
}

#[test]
fn only_an_address_in_use_error_means_the_port_is_taken() {
    use std::io::{Error, ErrorKind};
    assert_eq!(classify_bind(Ok(())), PortState::Free);
    assert_eq!(
        classify_bind(Err(Error::from(ErrorKind::AddrInUse))),
        PortState::InUse
    );
    assert_eq!(
        classify_bind(Err(Error::from(ErrorKind::PermissionDenied))),
        PortState::Unknown
    );
}

#[test]
fn a_failed_admin_probe_is_not_reported_as_proof() {
    let ip: IpAddr = "192.168.1.20".parse().unwrap();
    let silent = exposure_check(ip, 7779, false);
    assert_eq!(silent.level, "info");
    assert!(silent.text.contains("does not prove"));
    assert_eq!(exposure_check(ip, 7779, true).level, "fail");
}

#[test]
fn play_stops_when_the_profile_cannot_be_read() {
    assert_eq!(switch_problem(ModSwitch::Already), None);
    assert_eq!(switch_problem(ModSwitch::Done), None);
    assert_eq!(
        switch_problem(ModSwitch::Pending).as_deref(),
        Some(NEEDS_MO2_CLOSED)
    );
    let unreadable = switch_problem(ModSwitch::Unreadable).unwrap_or_default();
    assert!(unreadable.contains("cannot read the Mod Organizer 2 profile"));
}

#[test]
fn only_the_server_this_launcher_started_is_ours() {
    let ours = tempfile::tempdir().unwrap();
    let other = tempfile::tempdir().unwrap();
    for dir in [ours.path(), other.path()] {
        fs::write(dir.join(files::EXE), b"x").unwrap();
    }
    let reported = ours.path().join(files::EXE).display().to_string();
    assert!(same_program(&reported, ours.path()));
    let elsewhere = other.path().join(files::EXE).display().to_string();
    assert!(!same_program(&elsewhere, ours.path()));
    assert!(!same_program("", ours.path()));
}

#[test]
fn the_server_lives_in_a_mod_folder_that_the_profile_turns_on() {
    let instance = tempfile::tempdir().unwrap();
    let modlist = instance.path().join("profiles").join("Default");
    fs::create_dir_all(&modlist).unwrap();
    let modlist = modlist.join("modlist.txt");
    fs::write(&modlist, "+Main\n").unwrap();
    let dir = instance.path().join("mods").join(files::MOD).join("server");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join(files::EXE), b"x").unwrap();
    let pin = Pin {
        game: "G".into(),
        exe: None,
        instance: instance.path().into(),
        profile: Some("Default".into()),
    };
    assert_eq!(folders(&pin)[0], dir);
    let mut view = ServerView::default();
    setup(&Site::Mo2(pin.clone()), false, &mut view);
    assert!(view.problem.is_none() && view.note.contains(NEEDS_MO2_CLOSED));
    assert_eq!(fs::read_to_string(&modlist).unwrap(), "+Main\n");
    let mut view = ServerView::default();
    setup(&Site::Mo2(pin.clone()), true, &mut view);
    assert!(view.problem.is_none() && view.note.contains("Turned on"));
    assert_eq!(
        fs::read_to_string(&modlist).unwrap(),
        "+Main\n+Commonwealth Online Server\n"
    );
}

#[test]
fn saving_needs_an_installed_server_and_reports_bad_values_in_words() {
    let instance = tempfile::tempdir().unwrap();
    let pin = Pin {
        game: "G".into(),
        exe: None,
        instance: instance.path().into(),
        profile: None,
    };
    let site = Site::Mo2(pin.clone());
    let dir = files::dir(&pin.instance);
    let replies = std::cell::RefCell::new(Vec::new());
    let record = |event: Event| replies.borrow_mut().push(event);
    let mut form = settings::load(&dir);
    save(Some(&site), &form, false, HostMode::Dedicated, &record);
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join(files::EXE), b"x").unwrap();
    form.max_players = 101;
    save(Some(&site), &form, false, HostMode::Dedicated, &record);
    form.max_players = 4;
    form.name = "Crew".into();
    let Event::Server(view) = save(Some(&site), &form, false, HostMode::Dedicated, &record) else {
        panic!("not a server event");
    };
    let texts: Vec<_> = replies
        .borrow()
        .iter()
        .filter_map(|event| match event {
            Event::ServerReply { ok, text, .. } => Some((*ok, text.clone())),
            _ => None,
        })
        .collect();
    let oks: Vec<_> = texts.iter().map(|reply| reply.0).collect();
    assert_eq!(oks, [false, false, true]);
    assert!(texts[0].1.contains("not installed") && texts[1].1.contains("between 1 and 100"));
    let saved = view.settings.map(|s| (s.name, s.max_players));
    assert_eq!(saved, Some(("Crew".to_owned(), 4)));
    fs::write(dir.join(settings::FILE), r#"{"publicListing": "off"}"#).unwrap();
    form.public_listing = true;
    save(Some(&site), &form, false, HostMode::P2p, &record);
    let kept = fs::read_to_string(dir.join(settings::FILE)).unwrap();
    assert!(kept.contains(r#""publicListing": "off""#) && !kept.contains("public_listing"));
    save(Some(&site), &form, false, HostMode::Dedicated, &record);
    assert!(fs::read_to_string(dir.join(settings::FILE))
        .unwrap()
        .contains("\"public_listing\": true"));
    fs::write(dir.join(settings::FILE), r#"{"doorsShared": "sometimes"}"#).unwrap();
    let Event::Server(view) = overview(Some(&site), false) else {
        panic!()
    };
    assert!(view.settings.is_none() && view.problem.unwrap().contains("doorsShared"));
}

#[test]
fn the_dedicated_checks_warn_about_the_router_and_a_missing_password() {
    let mut form = settings::load(Path::new("no-such-folder"));
    let open = lan_checks(7777, Some(&form));
    assert!(open
        .iter()
        .any(|c| c.level == "warn" && c.text.contains("No password")));
    assert!(open.iter().any(|c| c.text.contains("port forwarding")));
    assert!(open.iter().all(|c| !c.text.contains("netsh")));
    form.password = "hunter22".into();
    let closed = lan_checks(7777, Some(&form));
    assert!(closed.iter().all(|c| !c.text.contains("No password")));
    form.steam_friends_only = false;
    let anyone = steam_checks(Some(&form));
    assert!(anyone.iter().any(|c| c.text.contains("Anyone on Steam")));
}

#[test]
fn a_standalone_server_is_set_up_without_a_mod_manager() {
    let root = tempfile::tempdir().unwrap();
    let dir = root.path().join("CommonwealthOnlineServer");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join(files::EXE), b"x").unwrap();
    let site = Site::Standalone(dir.clone());
    assert_eq!(site.dirs(), vec![dir]);
    let mut view = ServerView::default();
    setup(&site, true, &mut view);
    assert!(view.problem.is_none() && !view.note.contains("Mod Organizer"));
    assert!(site_for(None, ManagerKind::Mo2.label()).is_none());
    assert!(matches!(
        site_for(None, MANAGER_NONE),
        Some(Site::Standalone(_))
    ));
}

#[test]
fn without_a_pinned_instance_the_page_says_so_and_commands_fail() {
    let Event::Server(view) = overview(None, true) else {
        panic!("not a server event");
    };
    assert_eq!(view.problem.as_deref(), Some(no_site()));
    assert!(matches!(
        start(None),
        Some(Event::Error { message }) if message == no_site()
    ));
}

#[test]
fn a_second_start_is_refused_while_a_server_is_starting() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|error| error.into_inner());
    let instance = tempfile::tempdir().unwrap();
    let pin = Pin {
        game: "G".into(),
        exe: None,
        instance: instance.path().into(),
        profile: None,
    };
    *STARTING.lock().unwrap() = Some(StartingServer {
        dir: folders(&pin)[0].clone(),
        child: sleeping_child(),
        stop_requested_at: None,
    });
    let event = start(Some(&Site::Mo2(pin)));
    let mut server = STARTING.lock().unwrap().take().unwrap();
    server.child.kill().unwrap();
    server.child.wait().unwrap();
    let Some(Event::Error { message }) = event else {
        panic!("not an error event");
    };
    assert_eq!(
        message,
        "A server is already starting. Wait for it or stop it first."
    );
}

#[test]
fn a_restart_waits_for_a_starting_server_to_finish() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|error| error.into_inner());
    let instance = tempfile::tempdir().unwrap();
    let pin = Pin {
        game: "G".into(),
        exe: None,
        instance: instance.path().into(),
        profile: None,
    };
    *STARTING.lock().unwrap() = Some(StartingServer {
        dir: folders(&pin)[0].clone(),
        child: sleeping_child(),
        stop_requested_at: None,
    });
    let replies = std::cell::RefCell::new(Vec::new());
    let record = |event: Event| replies.borrow_mut().push(event);
    let outcome = restart(Some(&Site::Mo2(pin)), &record);
    let mut server = STARTING.lock().unwrap().take().unwrap();
    server.child.kill().unwrap();
    server.child.wait().unwrap();
    let events = replies.borrow();
    let [Event::Error { message }] = events.as_slice() else {
        panic!("expected one error event");
    };
    assert_eq!(
        message.as_str(),
        "Wait for startup to finish or stop the server before restarting."
    );
    let Event::Server(view) = outcome else {
        panic!("not a server event");
    };
    assert!(view.starting);
}

#[test]
fn a_restart_without_a_pinned_instance_reports_it() {
    let replies = std::cell::RefCell::new(Vec::new());
    let record = |event: Event| replies.borrow_mut().push(event);
    let Event::Server(view) = restart(None, &record) else {
        panic!("not a server event");
    };
    assert_eq!(view.problem.as_deref(), Some(no_site()));
    let events = replies.borrow();
    let [Event::Error { message }] = events.as_slice() else {
        panic!("expected one error event");
    };
    assert_eq!(message.as_str(), no_site());
}

#[test]
fn play_leaves_a_running_server_alone() {
    let _guard = TEST_LOCK.lock().unwrap_or_else(|error| error.into_inner());
    let instance = tempfile::tempdir().unwrap();
    let pin = Pin {
        game: PathBuf::new(),
        exe: None,
        instance: instance.path().to_path_buf(),
        profile: None,
    };
    let dir = folders(&pin)[0].clone();
    fs::create_dir_all(&dir).unwrap();
    fs::copy(std::env::current_exe().unwrap(), dir.join(files::EXE)).unwrap();
    let mut child = std::process::Command::new(dir.join(files::EXE))
        .args(["--exact", "server::tests::startup_child", "--ignored"])
        .stdin(Stdio::null())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();
    files::record_host(&dir, child.id()).unwrap();
    let problem = ensure(&Site::Mo2(pin));
    let still_running = child.try_wait().unwrap().is_none();
    let recorded = files::recorded_host(&dir);
    child.kill().unwrap();
    child.wait().unwrap();
    assert!(still_running);
    assert_eq!(recorded, Some(child.id()));
    assert!(problem.is_some());
}

#[test]
fn setup_note_explains_a_kept_copy() {
    assert_eq!(
        install_note(&["CommonwealthOnline.Server.exe".to_owned()]),
        "Installed CommonwealthOnline.Server.exe."
    );
    let note = install_note(&["workshop-zones.json.new".to_owned()]);
    assert!(note.starts_with("Wrote workshop-zones.json.new."));
    assert!(note.contains("Your copy of workshop-zones.json was kept"));
}
