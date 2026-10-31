use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};

pub const FILE: &str = "commonwealth-server.json";
pub const WORLD_MODES: [&str; 2] = ["coop-story", "free-roam"];
pub const VATS: [&str; 3] = ["disabled", "real-time", "vanilla"];
const LAN_DISCOVERY_PORT: u16 = 7778;
pub const UNREADABLE: &str = "The server settings file could not be read, so it was left as it is.";

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub name: String,
    pub description: String,
    pub max_players: i64,
    pub password: String,
    pub port: i64,
    pub world_mode: String,
    pub vats: String,
    pub doors_shared: bool,
    pub friendly_fire: bool,
    pub faction_friendly_fire: bool,
    pub chat: bool,
    pub factions: bool,
    pub persistence: bool,
    pub steam_friends_only: bool,
    pub downed_seconds: i64,
    pub revive_seconds: i64,
}

impl std::fmt::Debug for Settings {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Settings")
            .field("name", &self.name)
            .field("port", &self.port)
            .field("password", &(!self.password.is_empty()).then_some("***"))
            .finish_non_exhaustive()
    }
}

pub fn read_object(dir: &Path) -> Option<Map<String, Value>> {
    let text = fs::read_to_string(dir.join(FILE)).ok()?;
    match serde_json::from_str(text.trim_start_matches('\u{feff}')).ok()? {
        Value::Object(map) => Some(map),
        _ => None,
    }
}

pub(crate) fn pick<'a>(map: &'a Map<String, Value>, keys: &[&str]) -> Option<&'a Value> {
    keys.iter()
        .find_map(|key| map.get(*key).filter(|value| !value.is_null()))
}

pub fn raw(dir: &Path, keys: &[&str]) -> Option<Value> {
    pick(&read_object(dir)?, keys).cloned()
}

fn discovery_port(map: &Map<String, Value>) -> Option<u16> {
    let enabled = pick(map, &["lan_discovery_enabled", "lanDiscoveryEnabled"])
        .and_then(Value::as_bool)
        .unwrap_or(true);
    let port = pick(map, &["lan_discovery_port", "lanDiscoveryPort"])
        .and_then(Value::as_u64)
        .and_then(|port| u16::try_from(port).ok())
        .unwrap_or(LAN_DISCOVERY_PORT);
    enabled.then_some(port)
}

pub fn load(dir: &Path) -> Settings {
    let map = read_object(dir).unwrap_or_default();
    let text = |keys: &[&str], default: &str| {
        pick(&map, keys)
            .and_then(Value::as_str)
            .unwrap_or(default)
            .to_owned()
    };
    let number =
        |keys: &[&str], default: i64| pick(&map, keys).and_then(Value::as_i64).unwrap_or(default);
    let flag =
        |keys: &[&str], default: bool| pick(&map, keys).and_then(Value::as_bool).unwrap_or(default);
    Settings {
        name: text(&["server_name", "serverName"], "Commonwealth Online Server"),
        description: text(&["server_description", "serverDescription"], ""),
        max_players: number(&["max_players", "maxPlayers"], 16),
        password: text(&["server_password", "serverPassword"], ""),
        port: number(&["port"], 7777),
        world_mode: text(
            &["worldMode", "world_mode", "questMode", "quest_mode"],
            "coop-story",
        ),
        vats: text(&["vats", "vats_mode"], "disabled"),
        doors_shared: flag(&["doorsShared", "doors_shared"], true),
        friendly_fire: flag(&["friendly_fire", "friendlyFire"], true),
        faction_friendly_fire: flag(&["faction_friendly_fire", "factionFriendlyFire"], false),
        chat: flag(&["enable_chat", "enableChat"], true),
        factions: flag(&["enable_factions", "enableFactions"], true),
        persistence: flag(&["enable_persistence", "enablePersistence"], true),
        steam_friends_only: flag(&["steam_friends_only", "steamFriendsOnly"], true),
        downed_seconds: number(&["downed_seconds", "downedSeconds"], 60),
        revive_seconds: number(&["revive_channel_seconds", "reviveChannelSeconds"], 3),
    }
}

pub fn validate(
    settings: &Settings,
    admin_port: u16,
    discovery_port: Option<u16>,
) -> Result<(), String> {
    let printable = |text: &str| text.bytes().all(|b| (0x20..=0x7e).contains(&b));
    let fail = |text: &str| Err(text.to_owned());
    if settings.name.trim().is_empty() || settings.name.len() > 64 {
        return fail("The server name must be 1 to 64 characters long (accented letters count as more than one).");
    }
    if settings.description.len() > 256 {
        return fail("The description can be at most 256 characters long (accented letters count as more than one).");
    }
    if !(1..=16).contains(&settings.max_players) {
        return fail("The number of players must be between 1 and 16.");
    }
    let password = settings.password.len();
    if password != 0 && (!(4..=64).contains(&password) || !printable(&settings.password)) {
        return fail("The password must be 4 to 64 letters, numbers or symbols with no accents, or empty for no password.");
    }
    if !(1..=65535).contains(&settings.port) {
        return fail("The game port must be a number from 1 to 65535.");
    }
    if settings.port == i64::from(admin_port)
        || discovery_port.is_some_and(|d| settings.port == i64::from(d))
    {
        return Err(format!(
            "Port {} is already used by the server itself. Pick a different game port.",
            settings.port
        ));
    }
    if !WORLD_MODES.contains(&settings.world_mode.as_str()) {
        return fail("Pick a game mode from the list.");
    }
    if !VATS.contains(&settings.vats.as_str()) {
        return fail("Pick a VATS option from the list.");
    }
    if !(5..=600).contains(&settings.downed_seconds) {
        return fail("The time before a downed player dies must be between 5 and 600 seconds.");
    }
    if !(0..=15).contains(&settings.revive_seconds) {
        return fail("The revive time must be between 0 and 15 seconds.");
    }
    Ok(())
}

pub fn save(dir: &Path, settings: &Settings, admin_port: u16) -> Result<(), String> {
    let path = dir.join(FILE);
    let mut map = if path.exists() {
        read_object(dir).ok_or(UNREADABLE)?
    } else {
        Map::new()
    };
    validate(settings, admin_port, discovery_port(&map))?;
    if !settings.persistence && pick(&map, &["starting_inventory", "startingInventory"]).is_some() {
        return Err("Saving progress cannot be turned off while the settings file hands out a starting inventory.".to_owned());
    }
    let mut set = |keys: &[&str], value: Value| {
        keys.iter().skip(1).for_each(|alias| {
            map.shift_remove(*alias);
        });
        map.insert(keys[0].to_owned(), value);
    };
    set(&["server_name", "serverName"], json!(settings.name.trim()));
    set(
        &["server_description", "serverDescription"],
        json!(settings.description),
    );
    set(&["max_players", "maxPlayers"], json!(settings.max_players));
    set(&["port"], json!(settings.port));
    set(
        &["worldMode", "world_mode", "questMode", "quest_mode"],
        json!(settings.world_mode),
    );
    set(&["vats", "vats_mode"], json!(settings.vats));
    set(
        &["doorsShared", "doors_shared"],
        json!(settings.doors_shared),
    );
    set(
        &["friendly_fire", "friendlyFire"],
        json!(settings.friendly_fire),
    );
    set(
        &["faction_friendly_fire", "factionFriendlyFire"],
        json!(settings.faction_friendly_fire),
    );
    set(&["enable_chat", "enableChat"], json!(settings.chat));
    set(
        &["enable_factions", "enableFactions"],
        json!(settings.factions),
    );
    set(
        &["enable_persistence", "enablePersistence"],
        json!(settings.persistence),
    );
    set(
        &["steam_friends_only", "steamFriendsOnly"],
        json!(settings.steam_friends_only),
    );
    set(
        &["downed_seconds", "downedSeconds"],
        json!(settings.downed_seconds),
    );
    set(
        &["revive_channel_seconds", "reviveChannelSeconds"],
        json!(settings.revive_seconds),
    );
    if settings.password.is_empty() {
        map.shift_remove("server_password");
        map.shift_remove("serverPassword");
    } else {
        set(
            &["server_password", "serverPassword"],
            json!(settings.password),
        );
    }
    let text = serde_json::to_string_pretty(&Value::Object(map)).map_err(|e| e.to_string())? + "\n";
    let temp = path.with_extension("json.co-tmp");
    fs::write(&temp, text)
        .and_then(|()| fs::rename(&temp, &path))
        .map_err(|error| {
            let _ = fs::remove_file(&temp);
            format!("The settings could not be saved: {error}. Stop the server and try again.")
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn with_file(text: &str) -> (tempfile::TempDir, Settings) {
        let dir = tempfile::tempdir().unwrap();
        fs::write(dir.path().join(FILE), text).unwrap();
        let settings = load(dir.path());
        (dir, settings)
    }

    #[test]
    fn a_missing_file_gives_the_server_defaults() {
        let dir = tempfile::tempdir().unwrap();
        let settings = load(dir.path());
        assert_eq!((settings.port, settings.max_players), (7777, 16));
        assert!(settings.friendly_fire && !settings.faction_friendly_fire);
        assert_eq!(validate(&settings, 7779, Some(7778)), Ok(()));
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
        save(dir.path(), &settings, 7779).unwrap();
        let text = fs::read_to_string(dir.path().join(FILE)).unwrap();
        let map: Map<String, Value> = serde_json::from_str(&text).unwrap();
        let keys: Vec<_> = map.keys().take(4).map(String::as_str).collect();
        assert_eq!(keys, ["host", "admin_profiles", "port", "zzz"]);
        assert_eq!(map["server_name"], json!("Wasteland Crew"));
        assert!(!map.contains_key("serverName"));
        assert_eq!(map["admin_profiles"], json!(["abc"]));
        assert_eq!(map["friendly_fire"], json!(false));
        assert_eq!(
            load(dir.path()),
            Settings {
                name: "Wasteland Crew".into(),
                ..settings.clone()
            }
        );
        settings.password.clear();
        save(dir.path(), &settings, 7779).unwrap();
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
        assert!(bad(|s| s.max_players = 17));
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
        assert!(save(dir.path(), &clash, 7000).is_err());
    }

    #[test]
    fn an_unreadable_file_or_a_starting_inventory_blocks_the_save() {
        let (dir, mut settings) = with_file("not json");
        assert!(save(dir.path(), &settings, 7779).is_err());
        assert_eq!(
            fs::read_to_string(dir.path().join(FILE)).unwrap(),
            "not json"
        );
        fs::write(dir.path().join(FILE), r#"{"starting_inventory":[]}"#).unwrap();
        settings.persistence = false;
        assert!(save(dir.path(), &settings, 7779).is_err());
    }

    #[test]
    fn the_discovery_port_follows_the_file_and_its_switch() {
        let (dir, mut settings) = with_file(r#"{"lanDiscoveryPort": 7900}"#);
        settings.port = 7900;
        assert!(save(dir.path(), &settings, 7779).is_err());
        settings.port = 7778;
        assert!(save(dir.path(), &settings, 7779).is_ok());
        fs::write(
            dir.path().join(FILE),
            r#"{"lan_discovery_enabled": false, "startingInventory": []}"#,
        )
        .unwrap();
        assert!(save(dir.path(), &settings, 7779).is_ok());
        settings.persistence = false;
        assert!(save(dir.path(), &settings, 7779).is_err());
        assert_eq!(raw(dir.path(), &["public_listing", "publicListing"]), None);
    }

    #[test]
    fn debug_output_hides_the_password() {
        let (_dir, mut settings) = with_file("{}");
        settings.password = "hunter22".into();
        assert!(!format!("{settings:?}").contains("hunter22"));
    }
}
