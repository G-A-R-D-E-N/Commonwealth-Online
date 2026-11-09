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
const APPEAL_LINK_KEYS: &[&str] = &["appeal_link", "appealLink"];
const MAX_PLAYERS_KEYS: &[&str] = &["max_players", "maxPlayers"];
const SERVER_PASSWORD_KEYS: &[&str] = &["server_password", "serverPassword"];
const REQUIRED_MODPACK_PATH_KEYS: &[&str] = &["required_modpack_path", "requiredModpackPath"];
const REQUIRED_MO2_INSTANCE_KEYS: &[&str] = &["required_mo2_instance", "requiredMo2Instance"];
const REQUIRED_MO2_PROFILE_KEYS: &[&str] = &["required_mo2_profile", "requiredMo2Profile"];
const PORT_KEYS: &[&str] = &["port"];
const ADMIN_PORT_KEYS: &[&str] = &["admin_port", "adminPort"];
const WORLD_MODE_KEYS: &[&str] = &["worldMode", "world_mode", "questMode", "quest_mode"];
const VATS_KEYS: &[&str] = &["vats", "vats_mode"];
const DOORS_SHARED_KEYS: &[&str] = &["doorsShared", "doors_shared"];
const SURVIVAL_MODE_KEYS: &[&str] = &["survival_mode", "survivalMode"];
const SURVIVAL_NEEDS_KEYS: &[&str] = &["survival_needs", "survivalNeeds"];
const FRIENDLY_FIRE_KEYS: &[&str] = &["friendly_fire", "friendlyFire"];
const FACTION_FRIENDLY_FIRE_KEYS: &[&str] = &["faction_friendly_fire", "factionFriendlyFire"];
const ENABLE_CHAT_KEYS: &[&str] = &["enable_chat", "enableChat"];
const PROFANITY_FILTER_KEYS: &[&str] = &["profanity_filter", "profanityFilter"];
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
    #[serde(default)]
    pub appeal_link: String,
    pub max_players: i64,
    pub password: String,
    #[serde(default)]
    pub required_modpack_path: String,
    #[serde(default)]
    pub required_mo2_instance: String,
    #[serde(default)]
    pub required_mo2_profile: String,
    pub port: i64,
    #[serde(default = "default_public_listing")]
    pub public_listing: bool,
    pub world_mode: String,
    pub vats: String,
    pub doors_shared: bool,
    #[serde(default = "default_survival_mode")]
    pub survival_mode: bool,
    #[serde(default = "default_survival_needs")]
    pub survival_needs: bool,
    pub friendly_fire: bool,
    pub faction_friendly_fire: bool,
    pub chat: bool,
    #[serde(default)]
    pub profanity_filter: bool,
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

const BOOLEAN_KEYS: [&[&str]; 12] = [
    PUBLIC_LISTING_KEYS.as_slice(),
    DOORS_SHARED_KEYS,
    SURVIVAL_MODE_KEYS,
    SURVIVAL_NEEDS_KEYS,
    FRIENDLY_FIRE_KEYS,
    FACTION_FRIENDLY_FIRE_KEYS,
    ENABLE_CHAT_KEYS,
    PROFANITY_FILTER_KEYS,
    ENABLE_FACTIONS_KEYS,
    ENABLE_PERSISTENCE_KEYS,
    STEAM_FRIENDS_ONLY_KEYS,
    LAN_DISCOVERY_ENABLED_KEYS,
];

fn trim_server_space(text: &str) -> &str {
    text.trim_matches(|c: char| matches!(c, ' ' | '\t' | '\n' | '\u{b}' | '\u{c}' | '\r'))
}

fn read_integer(value: &Value) -> Option<i64> {
    match value {
        Value::Number(number) => number.as_i64(),
        Value::String(text) => {
            let word = trim_server_space(text);
            if word.starts_with('+') {
                None
            } else {
                word.parse().ok()
            }
        }
        _ => None,
    }
}

pub(crate) fn read_port(value: &Value) -> Option<u16> {
    u16::try_from(read_integer(value)?)
        .ok()
        .filter(|port| *port >= 1)
}

fn read_bool(value: &Value) -> Option<bool> {
    match value {
        Value::Bool(flag) => Some(*flag),
        Value::Number(number) => match (number.as_i64(), number.as_u64()) {
            (Some(0), _) | (_, Some(0)) => Some(false),
            (Some(1), _) | (_, Some(1)) => Some(true),
            // constraint: the servers read JSON -0 as the integer 0, which serde_json stores as -0.0
            _ if number
                .as_f64()
                .is_some_and(|value| value == 0.0 && value.is_sign_negative()) =>
            {
                Some(false)
            }
            _ => None,
        },
        Value::String(text) => {
            let word = trim_server_space(text).to_ascii_lowercase();
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

fn bad_boolean(map: &Map<String, Value>) -> Option<(String, String)> {
    BOOLEAN_KEYS.iter().find_map(|keys| {
        let (key, value) = keys.iter().find_map(|key| {
            map.get(*key)
                .filter(|value| !value.is_null())
                .map(|value| (*key, value))
        })?;
        read_bool(value)
            .is_none()
            .then(|| (key.to_owned(), value.to_string()))
    })
}

fn refused(field: &str, rule: &str) -> String {
    format!("The server settings file was not changed: {field} {rule}.")
}

fn read_map(dir: &Path) -> Result<Map<String, Value>, String> {
    let text = fs::read_to_string(dir.join(FILE)).map_err(|_| UNREADABLE.to_owned())?;
    let map = match serde_json::from_str(text.trim_start_matches('\u{feff}')) {
        Ok(Value::Object(map)) => map,
        _ => return Err(UNREADABLE.to_owned()),
    };
    for keys in [
        REQUIRED_MODPACK_PATH_KEYS,
        REQUIRED_MO2_INSTANCE_KEYS,
        REQUIRED_MO2_PROFILE_KEYS,
        SERVER_NAME_KEYS,
        SERVER_DESCRIPTION_KEYS,
        APPEAL_LINK_KEYS,
        SERVER_PASSWORD_KEYS,
    ] {
        if pick(&map, keys).is_some_and(|value| !value.is_string()) {
            return Err(refused(keys[0], "must be a string"));
        }
    }
    for keys in [
        MAX_PLAYERS_KEYS,
        PORT_KEYS,
        ADMIN_PORT_KEYS,
        LAN_DISCOVERY_PORT_KEYS,
    ] {
        if pick(&map, keys).is_some_and(|value| read_integer(value).is_none()) {
            return Err(refused(keys[0], "must be a whole number"));
        }
    }
    if map
        .get("downed")
        .is_some_and(|downed| !downed.is_null() && !downed.is_object())
    {
        return Err(refused("downed", "must be an object"));
    }
    for (nested, keys) in [
        ("seconds", DOWNED_SECONDS_KEYS),
        ("revive_channel_seconds", REVIVE_SECONDS_KEYS),
    ] {
        if downed_value(&map, nested, keys).is_some_and(|value| read_integer(value).is_none()) {
            return Err(refused(
                &format!("downed.{nested}"),
                "must be a whole number",
            ));
        }
    }
    match bad_boolean(&map) {
        Some((key, value)) => Err(format!(
            "The server settings file was not changed: \"{key}\" is {value}, which is not a boolean. Use true or false, yes/no, on/off, 1/0, or fix that line by hand."
        )),
        None => Ok(map),
    }
}

pub fn read_object(dir: &Path) -> Option<Map<String, Value>> {
    read_map(dir).ok()
}

pub fn load_and_problem(dir: &Path) -> (Option<Settings>, Option<String>) {
    if !dir.join(FILE).exists() {
        return (Some(load_map(&Map::new())), None);
    }
    match read_map(dir) {
        Ok(map) => (Some(load_map(&map)), None),
        Err(problem) => (None, Some(problem)),
    }
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

fn default_survival_mode() -> bool {
    true
}

fn default_survival_needs() -> bool {
    true
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
        .and_then(read_port)
        .unwrap_or(LAN_DISCOVERY_PORT);
    enabled.then_some(port)
}

pub fn load(dir: &Path) -> Settings {
    load_map(&read_object(dir).unwrap_or_default())
}

fn load_map(map: &Map<String, Value>) -> Settings {
    let text = |keys: &[&str], default: &str| {
        pick(map, keys)
            .and_then(Value::as_str)
            .unwrap_or(default)
            .to_owned()
    };
    let number =
        |keys: &[&str], default: i64| pick(map, keys).and_then(read_integer).unwrap_or(default);
    let flag =
        |keys: &[&str], default: bool| pick(map, keys).and_then(read_bool).unwrap_or(default);
    Settings {
        name: text(SERVER_NAME_KEYS, "Commonwealth Online Server"),
        description: text(SERVER_DESCRIPTION_KEYS, ""),
        appeal_link: text(APPEAL_LINK_KEYS, ""),
        max_players: number(MAX_PLAYERS_KEYS, 100),
        password: text(SERVER_PASSWORD_KEYS, ""),
        required_modpack_path: text(REQUIRED_MODPACK_PATH_KEYS, ""),
        required_mo2_instance: text(REQUIRED_MO2_INSTANCE_KEYS, ""),
        required_mo2_profile: text(REQUIRED_MO2_PROFILE_KEYS, ""),
        port: number(PORT_KEYS, 7777),
        public_listing: flag(&PUBLIC_LISTING_KEYS, DEFAULT_PUBLIC_LISTING),
        world_mode: text(WORLD_MODE_KEYS, "coop-story"),
        vats: text(VATS_KEYS, "disabled"),
        doors_shared: flag(DOORS_SHARED_KEYS, true),
        survival_mode: flag(SURVIVAL_MODE_KEYS, true),
        survival_needs: flag(SURVIVAL_NEEDS_KEYS, true),
        friendly_fire: flag(FRIENDLY_FIRE_KEYS, true),
        faction_friendly_fire: flag(FACTION_FRIENDLY_FIRE_KEYS, false),
        chat: flag(ENABLE_CHAT_KEYS, true),
        profanity_filter: flag(PROFANITY_FILTER_KEYS, false),
        factions: flag(ENABLE_FACTIONS_KEYS, true),
        persistence: flag(ENABLE_PERSISTENCE_KEYS, true),
        steam_friends_only: flag(STEAM_FRIENDS_ONLY_KEYS, true),
        downed_seconds: downed_number(map, "seconds", DOWNED_SECONDS_KEYS, 60),
        revive_seconds: downed_number(map, "revive_channel_seconds", REVIVE_SECONDS_KEYS, 3),
    }
}

fn downed_value<'a>(map: &'a Map<String, Value>, nested: &str, flat: &[&str]) -> Option<&'a Value> {
    map.get("downed")
        .and_then(|downed| downed.get(nested))
        .filter(|value| !value.is_null())
        .or_else(|| pick(map, flat))
}

fn downed_number(map: &Map<String, Value>, nested: &str, flat: &[&str], default: i64) -> i64 {
    downed_value(map, nested, flat)
        .and_then(read_integer)
        .unwrap_or(default)
}

fn set_nested_downed(map: &mut Map<String, Value>, nested: &str, value: i64) {
    if let Some(slot) = map.get_mut("downed").and_then(|d| d.get_mut(nested)) {
        *slot = json!(value);
    }
}

fn appeal_link_allowed(link: &str) -> bool {
    if link.is_empty() {
        return true;
    }
    let Some((scheme, rest)) = link.split_once("://") else {
        return false;
    };
    let authority = rest.split(['/', '?', '#']).next().unwrap_or("");
    link.len() <= 200
        && link.bytes().all(|b| (0x21..=0x7e).contains(&b))
        && matches!(scheme, "http" | "https")
        && !authority.is_empty()
        && !authority.contains('@')
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
    if !appeal_link_allowed(&settings.appeal_link) {
        return fail("The appeal link must be empty, or an http:// or https:// link of at most 200 characters with no spaces, accents or '@'.");
    }
    if !(1..=100).contains(&settings.max_players) {
        return fail("The number of players must be between 1 and 100.");
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
        read_map(dir)?
    } else {
        Map::new()
    };
    validate(settings, admin_port, discovery_port(&map))?;
    let has_mo2_instance = !settings.required_mo2_instance.trim().is_empty();
    let has_mo2_profile = !settings.required_mo2_profile.trim().is_empty();
    if listing.is_some() && has_mo2_instance != has_mo2_profile {
        return Err("Choose both the MO2 instance and profile, or leave both empty.".to_owned());
    }
    if listing.is_some() && has_mo2_instance && !settings.required_modpack_path.is_empty() {
        return Err("Choose an MO2 profile or a manifest file, not both.".to_owned());
    }
    if listing.is_some()
        && settings.required_modpack_path.is_empty()
        && !has_mo2_instance
        && (pick(&map, &["modpack_name", "modpackName"])
            .is_some_and(|value| value.as_str().is_none_or(|name| !name.trim().is_empty()))
            || pick(&map, &["allow_modpack_downloads", "allowModpackDownloads"])
                .and_then(read_bool)
                == Some(true))
    {
        return Err("A modpack manifest is required while the server has a modpack name or modpack downloads enabled. Remove those settings first.".to_owned());
    }
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
    set(APPEAL_LINK_KEYS, json!(settings.appeal_link));
    set(MAX_PLAYERS_KEYS, json!(settings.max_players));
    set(PORT_KEYS, json!(settings.port));
    if let Some(listing) = listing {
        set(&PUBLIC_LISTING_KEYS, json!(listing));
    }
    set(WORLD_MODE_KEYS, json!(settings.world_mode));
    set(VATS_KEYS, json!(settings.vats));
    set(DOORS_SHARED_KEYS, json!(settings.doors_shared));
    set(SURVIVAL_MODE_KEYS, json!(settings.survival_mode));
    set(SURVIVAL_NEEDS_KEYS, json!(settings.survival_needs));
    set(FRIENDLY_FIRE_KEYS, json!(settings.friendly_fire));
    set(
        FACTION_FRIENDLY_FIRE_KEYS,
        json!(settings.faction_friendly_fire),
    );
    set(ENABLE_CHAT_KEYS, json!(settings.chat));
    set(PROFANITY_FILTER_KEYS, json!(settings.profanity_filter));
    set(ENABLE_FACTIONS_KEYS, json!(settings.factions));
    set(ENABLE_PERSISTENCE_KEYS, json!(settings.persistence));
    set(STEAM_FRIENDS_ONLY_KEYS, json!(settings.steam_friends_only));
    set(DOWNED_SECONDS_KEYS, json!(settings.downed_seconds));
    set(REVIVE_SECONDS_KEYS, json!(settings.revive_seconds));
    if listing.is_some() {
        set(
            REQUIRED_MODPACK_PATH_KEYS,
            json!(settings.required_modpack_path),
        );
        set(
            REQUIRED_MO2_INSTANCE_KEYS,
            json!(settings.required_mo2_instance),
        );
        set(
            REQUIRED_MO2_PROFILE_KEYS,
            json!(settings.required_mo2_profile),
        );
    }
    if settings.password.is_empty() {
        SERVER_PASSWORD_KEYS.iter().for_each(|alias| {
            map.shift_remove(*alias);
        });
    } else {
        set(SERVER_PASSWORD_KEYS, json!(settings.password));
    }
    set_nested_downed(&mut map, "seconds", settings.downed_seconds);
    set_nested_downed(&mut map, "revive_channel_seconds", settings.revive_seconds);
    if listing.is_some() && settings.required_modpack_path.is_empty() {
        map.shift_remove(REQUIRED_MODPACK_PATH_KEYS[0]);
    }
    if listing.is_some() && !has_mo2_instance {
        map.shift_remove(REQUIRED_MO2_INSTANCE_KEYS[0]);
        map.shift_remove(REQUIRED_MO2_PROFILE_KEYS[0]);
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
mod tests;
