use super::*;

fn with_file(text: &str) -> (tempfile::TempDir, Settings) {
    let dir = tempfile::tempdir().unwrap();
    fs::write(dir.path().join(FILE), text).unwrap();
    let settings = load(dir.path());
    (dir, settings)
}

#[test]
fn nested_downed_settings_are_read_first_and_kept_in_step_on_save() {
    let (dir, mut s) =
        with_file(r#"{"downed_seconds":60,"downed":{"seconds":90,"revive_channel_seconds":5}}"#);
    assert_eq!((s.downed_seconds, s.revive_seconds), (90, 5));
    (s.downed_seconds, s.revive_seconds) = (120, 7);
    save(dir.path(), &s, 7779, Some(true)).unwrap();
    let r = load(dir.path());
    assert_eq!((r.downed_seconds, r.revive_seconds), (120, 7));
    assert_eq!(read_object(dir.path()).unwrap()["downed"]["seconds"], 120);
}

#[test]
fn a_missing_file_gives_the_server_defaults() {
    let dir = tempfile::tempdir().unwrap();
    let settings = load(dir.path());
    assert_eq!((settings.port, settings.max_players), (7777, 100));
    assert!(settings.friendly_fire && !settings.faction_friendly_fire);
    assert_eq!(validate(&settings, 7779, Some(7778)), Ok(()));
}

#[test]
fn modpack_path_round_trips_and_can_be_cleared() {
    let (dir, mut settings) = with_file(r#"{"requiredModpackPath":"modpack.json"}"#);
    assert_eq!(settings.required_modpack_path, "modpack.json");
    save(dir.path(), &settings, 7779, Some(true)).unwrap();
    let map = read_object(dir.path()).unwrap();
    assert_eq!(map["required_modpack_path"], "modpack.json");
    assert!(!map.contains_key("requiredModpackPath"));
    settings.required_modpack_path.clear();
    save(dir.path(), &settings, 7779, Some(true)).unwrap();
    assert!(!read_object(dir.path())
        .unwrap()
        .contains_key("required_modpack_path"));
}

#[test]
fn modpack_dependencies_block_clearing_only_for_dedicated_saves() {
    for field in [
        r#""modpack_name":"Example""#,
        r#""allow_modpack_downloads":true"#,
    ] {
        let (dir, mut settings) = with_file(&format!(
            r#"{{{field},"required_modpack_path":"pack.json"}}"#
        ));
        settings.required_modpack_path.clear();
        assert!(save(dir.path(), &settings, 7779, Some(true)).is_err());
        save(dir.path(), &settings, 7779, None).unwrap();
        assert_eq!(
            read_object(dir.path()).unwrap()["required_modpack_path"],
            "pack.json"
        );
    }
}

#[test]
fn malformed_modpack_path_does_not_get_erased() {
    let text = r#"{"required_modpack_path":7}"#;
    let (dir, settings) = with_file(text);
    assert!(save(dir.path(), &settings, 7779, Some(true)).is_err());
    assert_eq!(fs::read_to_string(dir.path().join(FILE)).unwrap(), text);
}

#[test]
fn mo2_profile_round_trips_and_is_exclusive_with_manifest() {
    let (dir, mut settings) =
        with_file(r#"{"requiredMo2Instance":"C:/MO2","requiredMo2Profile":"Default"}"#);
    assert_eq!(settings.required_mo2_instance, "C:/MO2");
    assert_eq!(settings.required_mo2_profile, "Default");
    save(dir.path(), &settings, 7779, Some(true)).unwrap();
    let map = read_object(dir.path()).unwrap();
    assert_eq!(map["required_mo2_instance"], "C:/MO2");
    assert_eq!(map["required_mo2_profile"], "Default");
    assert!(!map.contains_key("requiredMo2Instance"));
    settings.required_modpack_path = "manifest.json".into();
    assert!(save(dir.path(), &settings, 7779, Some(true)).is_err());
    settings.required_modpack_path.clear();
    settings.required_mo2_profile.clear();
    assert!(save(dir.path(), &settings, 7779, Some(true)).is_err());
    save(dir.path(), &settings, 7779, None).unwrap();
    assert_eq!(
        read_object(dir.path()).unwrap()["required_mo2_profile"],
        "Default"
    );
    settings.required_mo2_instance.clear();
    save(dir.path(), &settings, 7779, Some(true)).unwrap();
    let map = read_object(dir.path()).unwrap();
    assert!(!map.contains_key("required_mo2_instance"));
    assert!(!map.contains_key("required_mo2_profile"));
}

#[test]
fn empty_modpack_name_does_not_block_clearing() {
    let (dir, mut settings) =
        with_file(r#"{"modpack_name":"  ","required_modpack_path":"pack.json"}"#);
    settings.required_modpack_path.clear();
    save(dir.path(), &settings, 7779, Some(true)).unwrap();
    assert!(!read_object(dir.path())
        .unwrap()
        .contains_key("required_modpack_path"));
}

#[test]
fn saving_changes_only_the_managed_keys_and_keeps_the_rest_in_order() {
    let (dir, mut settings) = with_file(
        r#"{"host":"0.0.0.0","serverName":"Old","admin_profiles":["abc"],"port":7777,"zzz":1}"#,
    );
    assert_eq!(settings.name, "Old");
    settings.name = "  Wasteland Crew ".into();
    settings.password = "hunter22".into();
    settings.friendly_fire = false;
    settings.appeal_link = "https://example.test/appeal".into();
    save(dir.path(), &settings, 7779, Some(settings.public_listing)).unwrap();
    let text = fs::read_to_string(dir.path().join(FILE)).unwrap();
    let map: Map<String, Value> = serde_json::from_str(&text).unwrap();
    let keys: Vec<_> = map.keys().take(4).map(String::as_str).collect();
    assert_eq!(keys, ["host", "admin_profiles", "port", "zzz"]);
    assert_eq!(map["server_name"], json!("Wasteland Crew"));
    assert!(!map.contains_key("serverName"));
    assert_eq!(map["admin_profiles"], json!(["abc"]));
    assert_eq!(map["friendly_fire"], json!(false));
    assert_eq!(map["appeal_link"], json!("https://example.test/appeal"));
    assert_eq!(
        load(dir.path()),
        Settings {
            name: "Wasteland Crew".into(),
            ..settings.clone()
        }
    );
    settings.password.clear();
    save(dir.path(), &settings, 7779, Some(settings.public_listing)).unwrap();
    assert!(!fs::read_to_string(dir.path().join(FILE))
        .unwrap()
        .contains("password"));
}

#[test]
fn values_the_server_would_refuse_are_refused_here_first() {
    let (dir, good) = with_file("{}");
    let bad = |change: fn(&mut Settings)| {
        let mut settings = good.clone();
        change(&mut settings);
        validate(&settings, 7779, Some(7778)).is_err()
    };
    assert!(bad(|s| s.name = " ".into()));
    assert!(bad(|s| s.max_players = 101));
    assert!(bad(|s| s.password = "abc".into()));
    assert!(bad(|s| s.password = "pässword".into()));
    assert!(bad(|s| s.port = 7779));
    assert!(bad(|s| s.port = 7778));
    assert!(bad(|s| s.port = 70000));
    assert!(bad(|s| s.world_mode = "anything".into()));
    assert!(bad(|s| s.vats = "on".into()));
    assert!(bad(|s| s.downed_seconds = 4));
    assert!(bad(|s| s.revive_seconds = 16));
    let mut clash = good;
    clash.port = 7000;
    assert!(save(dir.path(), &clash, 7000, Some(clash.public_listing)).is_err());
}

#[test]
fn an_appeal_link_must_be_empty_or_a_plain_http_link() {
    let (_dir, good) = with_file("{}");
    let with_link = |link: &str| {
        let mut settings = good.clone();
        settings.appeal_link = link.into();
        validate(&settings, 7779, Some(7778))
    };
    assert_eq!(with_link(""), Ok(()));
    assert_eq!(with_link("https://example.test/appeal"), Ok(()));
    assert_eq!(with_link("http://127.0.0.1:8080/appeal?case=1#top"), Ok(()));
    assert!(with_link("ftp://example.test").is_err());
    assert!(with_link("https://").is_err());
    assert!(with_link("https://user@example.test").is_err());
    assert!(with_link("https://example.test/appeal here").is_err());
    assert_eq!(with_link(&format!("https://{}", "a".repeat(192))), Ok(()));
    assert!(with_link(&format!("https://{}", "a".repeat(193))).is_err());
}

#[test]
fn an_unreadable_file_or_a_starting_inventory_blocks_the_save() {
    let (dir, mut settings) = with_file("not json");
    assert!(save(dir.path(), &settings, 7779, Some(settings.public_listing)).is_err());
    assert_eq!(
        fs::read_to_string(dir.path().join(FILE)).unwrap(),
        "not json"
    );
    fs::write(dir.path().join(FILE), r#"{"starting_inventory":[]}"#).unwrap();
    settings.persistence = false;
    assert!(save(dir.path(), &settings, 7779, Some(settings.public_listing)).is_err());
}

#[test]
fn the_discovery_port_follows_the_file_and_its_switch() {
    let (dir, mut settings) = with_file(r#"{"lanDiscoveryPort": 7900}"#);
    settings.port = 7900;
    assert!(save(dir.path(), &settings, 7779, Some(settings.public_listing)).is_err());
    settings.port = 7778;
    assert!(save(dir.path(), &settings, 7779, Some(settings.public_listing)).is_ok());
    fs::write(
        dir.path().join(FILE),
        r#"{"lan_discovery_enabled": false, "startingInventory": []}"#,
    )
    .unwrap();
    assert!(save(dir.path(), &settings, 7779, Some(settings.public_listing)).is_ok());
    settings.persistence = false;
    assert!(save(dir.path(), &settings, 7779, Some(settings.public_listing)).is_err());
    assert_eq!(raw(dir.path(), &PUBLIC_LISTING_KEYS), Some(json!(true)));
}

#[test]
fn a_missing_public_listing_key_means_the_server_lists_itself() {
    let dir = tempfile::tempdir().unwrap();
    assert!(listed(dir.path()));
    assert!(load(dir.path()).public_listing);
    fs::write(dir.path().join(FILE), r#"{"publicListing": false}"#).unwrap();
    assert!(!listed(dir.path()));
    assert!(!load(dir.path()).public_listing);
    fs::write(dir.path().join(FILE), r#"{"public_listing": true}"#).unwrap();
    assert!(listed(dir.path()));
    assert!(load(dir.path()).public_listing);
    fs::write(dir.path().join(FILE), r#"{"public_listing": "yes"}"#).unwrap();
    assert!(listed(dir.path()) && load(dir.path()).public_listing);
}

#[test]
fn saving_keeps_one_public_listing_key_and_the_unknown_ones() {
    let (dir, mut settings) = with_file(r#"{"publicListing": false,"host":"0.0.0.0"}"#);
    assert!(!settings.public_listing);
    settings.public_listing = true;
    save(dir.path(), &settings, 7779, Some(settings.public_listing)).unwrap();
    let map: Map<String, Value> =
        serde_json::from_str(&fs::read_to_string(dir.path().join(FILE)).unwrap()).unwrap();
    assert_eq!(map["public_listing"], json!(true));
    assert!(!map.contains_key("publicListing"));
    assert_eq!(map["host"], json!("0.0.0.0"));
    assert!(load(dir.path()).public_listing);
    settings.public_listing = false;
    save(dir.path(), &settings, 7779, Some(settings.public_listing)).unwrap();
    assert!(!listed(dir.path()));
}

#[test]
fn every_boolean_spelling_the_server_reads_is_read_the_same_way() {
    let dir = tempfile::tempdir().unwrap();
    let write = |value: &str| {
        fs::write(
            dir.path().join(FILE),
            format!(r#"{{"public_listing": {value}}}"#),
        )
        .unwrap();
    };
    for word in TRUE_WORDS {
        write(&format!("\"{word}\""));
        assert!(listed(dir.path()), "{word}");
        assert!(load(dir.path()).public_listing, "{word}");
        write(&format!("\"  {}  \"", word.to_uppercase()));
        assert!(listed(dir.path()), "{word} in capitals with spaces");
    }
    for word in FALSE_WORDS.iter().filter(|word| !word.is_empty()) {
        write(&format!("\"{word}\""));
        assert!(!listed(dir.path()), "{word}");
        assert!(!load(dir.path()).public_listing, "{word}");
        write(&format!("\" {} \"", word));
        assert!(!listed(dir.path()), "{word} with spaces");
    }
    for (raw, expected) in [
        ("true", true),
        ("false", false),
        ("1", true),
        ("0", false),
        ("-0", false),
    ] {
        write(raw);
        assert_eq!(listed(dir.path()), expected, "{raw}");
        assert_eq!(load(dir.path()).public_listing, expected, "{raw}");
    }
    write(r#""\ttrue\n""#);
    assert!(listed(dir.path()) && load(dir.path()).public_listing);
}

#[test]
fn doors_shared_and_friendly_fire_use_the_same_reader() {
    let (_dir, settings) = with_file(r#"{"doors_shared": "off", "friendlyFire": "YES"}"#);
    assert!(!settings.doors_shared && settings.friendly_fire);
    let (dir, settings) = with_file(r#"{"doorsShared": 0, "friendly_fire": 1}"#);
    assert!(!settings.doors_shared && settings.friendly_fire);
    assert!(read_object(dir.path()).is_some());
}

#[test]
fn survival_defaults_on_and_round_trips_off() {
    let (dir, mut settings) = with_file("{}");
    assert!(settings.survival_mode && settings.survival_needs);
    settings.survival_mode = false;
    settings.survival_needs = false;
    save(dir.path(), &settings, 7779, Some(settings.public_listing)).unwrap();
    assert!(!load(dir.path()).survival_mode && !load(dir.path()).survival_needs);
    fs::write(dir.path().join(FILE), r#"{"survivalMode": "off"}"#).unwrap();
    assert!(!load(dir.path()).survival_mode);
    fs::write(dir.path().join(FILE), r#"{"survival_mode": "invalid"}"#).unwrap();
    assert!(load_and_problem(dir.path()).1.is_some());
}

#[test]
fn an_unrecognised_boolean_makes_the_file_unusable_and_never_lists() {
    let dir = tempfile::tempdir().unwrap();
    for (text, key, value) in [
        (
            r#"{"public_listing": "maybe"}"#,
            "public_listing",
            "\"maybe\"",
        ),
        (r#"{"public_listing": 2}"#, "public_listing", "2"),
        (r#"{"public_listing": 1.5}"#, "public_listing", "1.5"),
        (r#"{"public_listing": 1.0}"#, "public_listing", "1.0"),
        (r#"{"public_listing": 0.0}"#, "public_listing", "0.0"),
        (r#"{"public_listing": []}"#, "public_listing", "[]"),
        (r#"{"doorsShared": "sure"}"#, "doorsShared", "\"sure\""),
        (
            r#"{"friendly_fire": "enable"}"#,
            "friendly_fire",
            "\"enable\"",
        ),
        (
            r#"{"lan_discovery_enabled": "sometimes"}"#,
            "lan_discovery_enabled",
            "\"sometimes\"",
        ),
    ] {
        fs::write(dir.path().join(FILE), text).unwrap();
        assert!(read_object(dir.path()).is_none(), "{text}");
        assert!(!listed(dir.path()), "{text}");
        let (form, problem) = load_and_problem(dir.path());
        assert!(form.is_none(), "{text}");
        let problem = problem.expect("the reason must be reported");
        assert!(
            problem.contains(key) && problem.contains(value),
            "{problem}"
        );
        assert_eq!(
            save(dir.path(), &load(dir.path()), 7779, Some(true)),
            Err(problem),
            "{text}"
        );
        assert_eq!(fs::read_to_string(dir.path().join(FILE)).unwrap(), text);
    }
}

#[test]
fn a_p2p_save_leaves_the_public_listing_key_exactly_as_it_was() {
    let (dir, mut settings) = with_file(r#"{"publicListing": "off","host":"0.0.0.0"}"#);
    settings.name = "Crew".into();
    save(dir.path(), &settings, 7779, None).unwrap();
    let map: Map<String, Value> =
        serde_json::from_str(&fs::read_to_string(dir.path().join(FILE)).unwrap()).unwrap();
    assert_eq!(map["publicListing"], json!("off"));
    assert!(!map.contains_key("public_listing"));
    assert_eq!(map["server_name"], json!("Crew"));
    assert!(!load(dir.path()).public_listing);
    let bare = tempfile::tempdir().unwrap();
    save(bare.path(), &load(bare.path()), 7779, None).unwrap();
    assert!(!fs::read_to_string(bare.path().join(FILE))
        .unwrap()
        .contains("public_listing"));
    let listed = load(bare.path());
    save(bare.path(), &listed, 7779, Some(listed.public_listing)).unwrap();
    assert!(fs::read_to_string(bare.path().join(FILE))
        .unwrap()
        .contains("\"public_listing\": true"));
}

fn server_public_listing() -> (bool, Vec<String>) {
    let repo = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let header = fs::read_to_string(repo.join("co-core/include/F4TServerConfiguration.h"))
        .expect("the server configuration header must be in the repository");
    let source = fs::read_to_string(repo.join("co-core/src/config/F4TServerConfiguration.cpp"))
        .expect("the server configuration source must be in the repository");
    let field = header
        .lines()
        .find(|line| line.contains("bool publicListing"))
        .expect("Options::publicListing must be in the header")
        .replace(' ', "");
    assert!(field.contains("boolpublicListing{true};"), "{field}");
    let line = source
        .lines()
        .find(|line| line.contains("options.publicListing = ReadBoolean("))
        .expect("the public_listing parse must be in the source");
    let open = line
        .find("Find(")
        .expect("the parse must look both spellings up")
        + 5;
    let close = line[open..].find(')').expect("the lookup must close") + open;
    let keys = line[open..close]
        .split('"')
        .skip(1)
        .step_by(2)
        .map(str::to_owned)
        .collect();
    let rest = &line[close + 1..];
    let fallback = rest
        .split(',')
        .nth(1)
        .expect("the parse must have a fallback");
    assert_eq!(fallback.trim(), "true", "{line}");
    (true, keys)
}

#[test]
fn the_public_listing_key_and_default_follow_the_server() {
    let (server_default, server_keys) = server_public_listing();
    assert_eq!(server_keys, PUBLIC_LISTING_KEYS.to_vec());
    assert_eq!(server_default, DEFAULT_PUBLIC_LISTING);
}

fn boolean_words(block: &str, marker: &str) -> Vec<String> {
    block
        .lines()
        .find(|line| line.contains(marker))
        .unwrap_or_else(|| panic!("{marker} must be in the boolean reader"))
        .split('"')
        .skip(1)
        .step_by(2)
        .map(str::to_owned)
        .collect()
}

fn server_boolean_words() -> Vec<(Vec<String>, Vec<String>)> {
    let repo = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let source = fs::read_to_string(repo.join("co-core/src/config/F4TServerConfigJson.cpp"))
        .expect("the JSON reader must be in the repository");
    let start = source
        .find("bool ReadBoolean(")
        .expect("ReadBoolean must be in the reader");
    let block = &source[start..];
    let end = block
        .find("\n\t}")
        .expect("ReadBoolean must close at one tab");
    let core = &block[..end];
    let mut core_falsy = boolean_words(core, "value.empty()");
    core_falsy.insert(0, String::new());
    let host = fs::read_to_string(repo.join("CO-SERVER/host/Services/ServerConfig.cs"))
        .expect("the Host reader must be in the repository");
    let start = host
        .find("private static bool? ReadBool(")
        .expect("ReadBool must be in the Host reader");
    let block = &host[start..];
    let end = block
        .find("\n    }")
        .expect("ReadBool must close at four spaces");
    let host = &block[..end];
    vec![
        (boolean_words(core, "== \"1\""), core_falsy),
        (
            boolean_words(host, "=> true"),
            boolean_words(host, "=> false"),
        ),
    ]
}

fn sorted(words: &[String]) -> Vec<String> {
    let mut words = words.to_vec();
    words.sort();
    words
}

#[test]
fn the_booleans_are_read_the_way_both_servers_read_them() {
    let readers = server_boolean_words();
    let (truthy, falsy) = readers[0].clone();
    assert_eq!(truthy, TRUE_WORDS.map(str::to_owned).to_vec());
    assert_eq!(falsy, FALSE_WORDS.map(str::to_owned).to_vec());
    for (host_truthy, host_falsy) in &readers[1..] {
        assert_eq!(
            sorted(host_truthy),
            sorted(&truthy),
            "the Host must accept the same true words"
        );
        assert_eq!(
            sorted(host_falsy),
            sorted(&falsy),
            "the Host must accept the same false words"
        );
    }
    for word in &truthy {
        assert_eq!(read_bool(&json!(word)), Some(true), "{word}");
    }
    for word in &falsy {
        assert_eq!(read_bool(&json!(word)), Some(false), "{word}");
    }
    assert_eq!(read_bool(&json!(" OFF ")), Some(false));
    assert_eq!(read_bool(&json!("\tOn\n")), Some(true));
    for (raw, expected) in [("0", false), ("1", true), ("-0", false)] {
        assert_eq!(
            read_bool(&serde_json::from_str(raw).unwrap()),
            Some(expected),
            "{raw}"
        );
    }
    assert_eq!(read_bool(&json!(-0.0)), Some(false));
    for rejected in [
        json!("maybe"),
        json!("enable"),
        json!(2),
        json!(1.5),
        json!(0.0),
        json!(1.0),
        json!([]),
        json!({}),
    ] {
        assert_eq!(read_bool(&rejected), None, "{rejected}");
    }
}

#[test]
fn debug_output_hides_the_password() {
    let (_dir, mut settings) = with_file("{}");
    settings.password = "hunter22".into();
    assert!(!format!("{settings:?}").contains("hunter22"));
}

#[test]
fn numeric_strings_and_both_aliases_load_the_way_the_server_reads_them() {
    let (dir, settings) = with_file(
        r#"{"maxPlayers":" 12 ","port":"7000","lanDiscoveryPort":"7100","downedSeconds":"70","downed":{"revive_channel_seconds":"5"}}"#,
    );
    assert_eq!((settings.max_players, settings.port), (12, 7000));
    assert_eq!((settings.downed_seconds, settings.revive_seconds), (70, 5));
    assert_eq!(
        discovery_port(&read_object(dir.path()).unwrap()),
        Some(7100)
    );
    let (_dir, settings) = with_file(r#"{"max_players": null, "maxPlayers": "12"}"#);
    assert_eq!(settings.max_players, 12);
    let (_dir, settings) = with_file(r#"{"downedSeconds": 90, "downed_seconds": "80"}"#);
    assert_eq!(settings.downed_seconds, 80);
    let (_dir, settings) = with_file(r#"{"downed": {"seconds": null}, "downedSeconds": "70"}"#);
    assert_eq!(settings.downed_seconds, 70);
}

#[test]
fn numbers_the_server_refuses_are_refused_and_the_field_is_named() {
    let dir = tempfile::tempdir().unwrap();
    for (text, key) in [
        (r#"{"max_players": "twelve"}"#, "max_players"),
        (r#"{"maxPlayers": "+12"}"#, "max_players"),
        (r#"{"port": 12.5}"#, "port"),
        (r#"{"port": true}"#, "port"),
        (r#"{"admin_port": "8.0"}"#, "admin_port"),
        (r#"{"lanDiscoveryPort": []}"#, "lan_discovery_port"),
        (r#"{"downed": []}"#, "downed"),
        (r#"{"downed": {"seconds": "soon"}}"#, "downed.seconds"),
        (
            r#"{"downed": {"revive_channel_seconds": 1.5}}"#,
            "downed.revive_channel_seconds",
        ),
    ] {
        fs::write(dir.path().join(FILE), text).unwrap();
        assert!(read_object(dir.path()).is_none(), "{text}");
        let (form, problem) = load_and_problem(dir.path());
        assert!(form.is_none(), "{text}");
        let problem = problem.expect("the reason must be reported");
        assert!(problem.contains(key), "{problem}");
        assert_eq!(
            save(dir.path(), &load(dir.path()), 7779, Some(true)),
            Err(problem),
            "{text}"
        );
        assert_eq!(fs::read_to_string(dir.path().join(FILE)).unwrap(), text);
    }
}

#[test]
fn a_malformed_password_under_either_alias_is_refused_and_never_removed() {
    let dir = tempfile::tempdir().unwrap();
    for text in [
        r#"{"server_password": 1234}"#,
        r#"{"serverPassword": ["hunter22"]}"#,
        r#"{"serverPassword": "hunter22", "server_password": false}"#,
    ] {
        fs::write(dir.path().join(FILE), text).unwrap();
        let (form, problem) = load_and_problem(dir.path());
        assert!(form.is_none(), "{text}");
        let problem = problem.expect("the reason must be reported");
        assert!(
            problem.contains("server_password") && !problem.contains("hunter22"),
            "{problem}"
        );
        assert_eq!(
            save(dir.path(), &load(dir.path()), 7779, Some(true)),
            Err(problem),
            "{text}"
        );
        assert_eq!(fs::read_to_string(dir.path().join(FILE)).unwrap(), text);
    }
}

#[test]
fn survival_needs_reads_both_aliases_and_refuses_a_bad_value() {
    let dir = tempfile::tempdir().unwrap();
    for (text, expected) in [
        (r#"{"survivalNeeds": "off"}"#, false),
        (r#"{"survival_needs": 0}"#, false),
        (r#"{"survivalNeeds": "yes"}"#, true),
        (r#"{"survival_needs": null, "survivalNeeds": "off"}"#, false),
    ] {
        fs::write(dir.path().join(FILE), text).unwrap();
        assert_eq!(load(dir.path()).survival_needs, expected, "{text}");
    }
    for (text, key, value) in [
        (
            r#"{"survivalNeeds": "maybe"}"#,
            "survivalNeeds",
            "\"maybe\"",
        ),
        (r#"{"survival_needs": 2}"#, "survival_needs", "2"),
    ] {
        fs::write(dir.path().join(FILE), text).unwrap();
        let (form, problem) = load_and_problem(dir.path());
        assert!(form.is_none(), "{text}");
        let problem = problem.expect("the reason must be reported");
        assert!(
            problem.contains(key) && problem.contains(value),
            "{problem}"
        );
        assert_eq!(
            save(dir.path(), &load(dir.path()), 7779, Some(true)),
            Err(problem),
            "{text}"
        );
        assert_eq!(fs::read_to_string(dir.path().join(FILE)).unwrap(), text);
    }
}

#[test]
fn an_unrelated_save_keeps_numeric_strings_survival_and_a_valid_password() {
    for password_key in ["serverPassword", "server_password"] {
        let text = format!(
            r#"{{"maxPlayers":" 12 ","port":"7000","downed":{{"seconds":"90"}},"survivalNeeds":"off","{password_key}":"hunter22"}}"#
        );
        let (dir, mut settings) = with_file(&text);
        assert_eq!(
            (settings.max_players, settings.port, settings.downed_seconds),
            (12, 7000, 90)
        );
        assert!(!settings.survival_needs);
        assert_eq!(settings.password, "hunter22");
        settings.name = "Crew".into();
        save(dir.path(), &settings, 7779, Some(true)).unwrap();
        let reloaded = load(dir.path());
        assert_eq!(
            (
                reloaded.name.as_str(),
                reloaded.max_players,
                reloaded.port,
                reloaded.downed_seconds,
                reloaded.survival_needs,
                reloaded.password.as_str(),
            ),
            ("Crew", 12, 7000, 90, false, "hunter22"),
            "{password_key}"
        );
        assert_eq!(
            read_object(dir.path()).unwrap()["server_password"],
            json!("hunter22")
        );
    }
}
