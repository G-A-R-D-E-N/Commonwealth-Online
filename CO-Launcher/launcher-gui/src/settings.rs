use std::fs;
use std::path::Path;

use serde::{Deserialize, Serialize};
use serde_json::{json, Map, Value};

pub const FILE: &str = "commonwealth-server.json";
pub const PUBLIC_LISTING_KEYS: [&str; 2] = ["public_listing", "publicListing"];
pub const DEFAULT_PUBLIC_LISTING: bool = true;
const TRUE_WORDS: [&str; 4] = ["1", "true", "yes", "on"];
const FALSE_WORDS: [&str; 5] = ["", "0", "false", "no", "off"];
pub const WORLD_MODES: [&str; 2] = ["coop-story", "free-roam"];
pub const VATS: [&str; 3] = ["disabled", "real-time", "vanilla"];
const LAN_DISCOVERY_PORT: u16 = 7778;
pub const UNREADABLE: &str = "The server settings file could not be read, so it was left as it is.";

const SERVER_NAME_KEYS: &[&str] = &["server_name", "serverName"];
const SERVER_DESCRIPTION_KEYS: &[&str] = &["server_description", "serverDescription"];
const MAX_PLAYERS_KEYS: &[&str] = &["max_players", "maxPlayers"];
const SERVER_PASSWORD_KEYS: &[&str] = &["server_password", "serverPassword"];
const PORT_KEYS: &[&str] = &["port"];
const WORLD_MODE_KEYS: &[&str] = &["worldMode", "world_mode", "questMode", "quest_mode"];
const VATS_KEYS: &[&str] = &["vats", "vats_mode"];
const DOORS_SHARED_KEYS: &[&str] = &["doorsShared", "doors_shared"];
const FRIENDLY_FIRE_KEYS: &[&str] = &["friendly_fire", "friendlyFire"];
const FACTION_FRIENDLY_FIRE_KEYS: &[&str] = &["faction_friendly_fire", "factionFriendlyFire"];
const ENABLE_CHAT_KEYS: &[&str] = &["enable_chat", "enableChat"];
const ENABLE_FACTIONS_KEYS: &[&str] = &["enable_factions", "enableFactions"];
const ENABLE_PERSISTENCE_KEYS: &[&str] = &["enable_persistence", "enablePersistence"];
const STEAM_FRIENDS_ONLY_KEYS: &[&str] = &["steam_friends_only", "steamFriendsOnly"];
const DOWNED_SECONDS_KEYS: &[&str] = &["downed_seconds", "downedSeconds"];
const REVIVE_SECONDS_KEYS: &[&str] = &["revive_channel_seconds", "reviveChannelSeconds"];
const STARTING_INVENTORY_KEYS: &[&str] = &["starting_inventory", "startingInventory"];
const LAN_DISCOVERY_ENABLED_KEYS: &[&str] = &["lan_discovery_enabled", "lanDiscoveryEnabled"];
const LAN_DISCOVERY_PORT_KEYS: &[&str] = &["lan_discovery_port", "lanDiscoveryPort"];

#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Settings {
    pub name: String,
    pub description: String,
    pub max_players: i64,
    pub password: String,
    pub port: i64,
    #[serde(default = "default_public_listing")]
    pub public_listing: bool,
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

const BOOLEAN_KEYS: [&[&str]; 9] = [
    PUBLIC_LISTING_KEYS.as_slice(),
    DOORS_SHARED_KEYS,
    FRIENDLY_FIRE_KEYS,
    FACTION_FRIENDLY_FIRE_KEYS,
    ENABLE_CHAT_KEYS,
    ENABLE_FACTIONS_KEYS,
    ENABLE_PERSISTENCE_KEYS,
    STEAM_FRIENDS_ONLY_KEYS,
    LAN_DISCOVERY_ENABLED_KEYS,
];

fn is_server_space(byte: u8) -> bool {
    matches!(byte, b' ' | b'\t' | b'\n' | 0x0b | 0x0c | b'\r')
}

fn read_bool(value: &Value) -> Option<bool> {
    match value {
        Value::Bool(flag) => Some(*flag),
        Value::Number(number) => match (number.as_i64(), number.as_u64()) {
            (Some(0), _) | (_, Some(0)) => Some(false),
            (Some(1), _) | (_, Some(1)) => Some(true),
            _ => None,
        },
        Value::String(text) => {
            let bytes = text.as_bytes();
            let start = bytes
                .iter()
                .position(|byte| !is_server_space(*byte))
                .unwrap_or(bytes.len());
            let end = bytes
                .iter()
                .rposition(|byte| !is_server_space(*byte))
                .map_or(start, |at| at + 1);
            let word = text[start..end].to_ascii_lowercase();
            if TRUE_WORDS.contains(&word.as_str()) {
                Some(true)
            } else if FALSE_WORDS.contains(&word.as_str()) {
                Some(false)
            } else {
                None
            }
        }
        _ => None,
    }
}

pub fn read_object(dir: &Path) -> Option<Map<String, Value>> {
    let text = fs::read_to_string(dir.join(FILE)).ok()?;
    let map = match serde_json::from_str(text.trim_start_matches('\u{feff}')).ok()? {
        Value::Object(map) => map,
        _ => return None,
    };
    BOOLEAN_KEYS
        .iter()
        .filter_map(|keys| pick(&map, keys))
        .all(|value| read_bool(value).is_some())
        .then_some(map)
}

pub(crate) fn pick<'a>(map: &'a Map<String, Value>, keys: &[&str]) -> Option<&'a Value> {
    keys.iter()
        .find_map(|key| map.get(*key).filter(|value| !value.is_null()))
}

pub fn raw(dir: &Path, keys: &[&str]) -> Option<Value> {
    pick(&read_object(dir)?, keys).cloned()
}

fn default_public_listing() -> bool {
    DEFAULT_PUBLIC_LISTING
}

pub fn listed(dir: &Path) -> bool {
    if !dir.join(FILE).exists() {
        return DEFAULT_PUBLIC_LISTING;
    }
    let Some(map) = read_object(dir) else {
        return false;
    };
    pick(&map, &PUBLIC_LISTING_KEYS)
        .and_then(read_bool)
        .unwrap_or(DEFAULT_PUBLIC_LISTING)
}

fn discovery_port(map: &Map<String, Value>) -> Option<u16> {
    let enabled = pick(map, LAN_DISCOVERY_ENABLED_KEYS)
        .and_then(read_bool)
        .unwrap_or(true);
    let port = pick(map, LAN_DISCOVERY_PORT_KEYS)
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
        |keys: &[&str], default: bool| pick(&map, keys).and_then(read_bool).unwrap_or(default);
    Settings {
        name: text(SERVER_NAME_KEYS, "Commonwealth Online Server"),
        description: text(SERVER_DESCRIPTION_KEYS, ""),
        max_players: number(MAX_PLAYERS_KEYS, 16),
        password: text(SERVER_PASSWORD_KEYS, ""),
        port: number(PORT_KEYS, 7777),
        public_listing: flag(&PUBLIC_LISTING_KEYS, DEFAULT_PUBLIC_LISTING),
        world_mode: text(WORLD_MODE_KEYS, "coop-story"),
        vats: text(VATS_KEYS, "disabled"),
        doors_shared: flag(DOORS_SHARED_KEYS, true),
        friendly_fire: flag(FRIENDLY_FIRE_KEYS, true),
        faction_friendly_fire: flag(FACTION_FRIENDLY_FIRE_KEYS, false),
        chat: flag(ENABLE_CHAT_KEYS, true),
        factions: flag(ENABLE_FACTIONS_KEYS, true),
        persistence: flag(ENABLE_PERSISTENCE_KEYS, true),
        steam_friends_only: flag(STEAM_FRIENDS_ONLY_KEYS, true),
        downed_seconds: number(DOWNED_SECONDS_KEYS, 60),
        revive_seconds: number(REVIVE_SECONDS_KEYS, 3),
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

pub fn save(
    dir: &Path,
    settings: &Settings,
    admin_port: u16,
    listing: Option<bool>,
) -> Result<(), String> {
    let path = dir.join(FILE);
    let mut map = if path.exists() {
        read_object(dir).ok_or(UNREADABLE)?
    } else {
        Map::new()
    };
    validate(settings, admin_port, discovery_port(&map))?;
    if !settings.persistence && pick(&map, STARTING_INVENTORY_KEYS).is_some() {
        return Err("Saving progress cannot be turned off while the settings file hands out a starting inventory.".to_owned());
    }
    let mut set = |keys: &[&str], value: Value| {
        keys.iter().skip(1).for_each(|alias| {
            map.shift_remove(*alias);
        });
        map.insert(keys[0].to_owned(), value);
    };
    set(SERVER_NAME_KEYS, json!(settings.name.trim()));
    set(SERVER_DESCRIPTION_KEYS, json!(settings.description));
    set(MAX_PLAYERS_KEYS, json!(settings.max_players));
    set(PORT_KEYS, json!(settings.port));
    if let Some(listing) = listing {
        set(&PUBLIC_LISTING_KEYS, json!(listing));
    }
    set(WORLD_MODE_KEYS, json!(settings.world_mode));
    set(VATS_KEYS, json!(settings.vats));
    set(DOORS_SHARED_KEYS, json!(settings.doors_shared));
    set(FRIENDLY_FIRE_KEYS, json!(settings.friendly_fire));
    set(
        FACTION_FRIENDLY_FIRE_KEYS,
        json!(settings.faction_friendly_fire),
    );
    set(ENABLE_CHAT_KEYS, json!(settings.chat));
    set(ENABLE_FACTIONS_KEYS, json!(settings.factions));
    set(ENABLE_PERSISTENCE_KEYS, json!(settings.persistence));
    set(STEAM_FRIENDS_ONLY_KEYS, json!(settings.steam_friends_only));
    set(DOWNED_SECONDS_KEYS, json!(settings.downed_seconds));
    set(REVIVE_SECONDS_KEYS, json!(settings.revive_seconds));
    if settings.password.is_empty() {
        SERVER_PASSWORD_KEYS.iter().for_each(|alias| {
            map.shift_remove(*alias);
        });
    } else {
        set(SERVER_PASSWORD_KEYS, json!(settings.password));
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
        save(dir.path(), &settings, 7779, Some(settings.public_listing)).unwrap();
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
        assert!(save(dir.path(), &clash, 7000, Some(clash.public_listing)).is_err());
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
        for (raw, expected) in [("true", true), ("false", false), ("1", true), ("0", false)] {
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
    fn an_unrecognised_boolean_makes_the_file_unusable_and_never_lists() {
        let dir = tempfile::tempdir().unwrap();
        for text in [
            r#"{"public_listing": "maybe"}"#,
            r#"{"public_listing": 2}"#,
            r#"{"public_listing": 1.5}"#,
            r#"{"public_listing": []}"#,
            r#"{"doorsShared": "sure"}"#,
            r#"{"friendly_fire": "enable"}"#,
            r#"{"lan_discovery_enabled": "sometimes"}"#,
        ] {
            fs::write(dir.path().join(FILE), text).unwrap();
            assert!(read_object(dir.path()).is_none(), "{text}");
            assert!(!listed(dir.path()), "{text}");
            let form = load(dir.path());
            assert_eq!(
                save(dir.path(), &form, 7779, Some(true)),
                Err(UNREADABLE.to_owned()),
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

    fn server_boolean_words() -> (Vec<String>, Vec<String>) {
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
        let block = &block[..end];
        let words = |marker: &str| -> Vec<String> {
            block
                .lines()
                .find(|line| line.contains(marker))
                .unwrap_or_else(|| panic!("{marker} must be in ReadBoolean"))
                .split('"')
                .skip(1)
                .step_by(2)
                .map(str::to_owned)
                .collect()
        };
        let truthy = words("== \"1\"");
        let mut falsy = words("value.empty()");
        falsy.insert(0, String::new());
        (truthy, falsy)
    }

    #[test]
    fn the_booleans_are_read_the_way_the_server_reads_them() {
        let (truthy, falsy) = server_boolean_words();
        assert_eq!(truthy, TRUE_WORDS.map(str::to_owned).to_vec());
        assert_eq!(falsy, FALSE_WORDS.map(str::to_owned).to_vec());
        for word in &truthy {
            assert_eq!(read_bool(&json!(word)), Some(true), "{word}");
        }
        for word in &falsy {
            assert_eq!(read_bool(&json!(word)), Some(false), "{word}");
        }
        assert_eq!(read_bool(&json!(" OFF ")), Some(false));
        assert_eq!(read_bool(&json!("\tOn\n")), Some(true));
        for rejected in [
            json!("maybe"),
            json!("enable"),
            json!(2),
            json!(1.5),
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
}
