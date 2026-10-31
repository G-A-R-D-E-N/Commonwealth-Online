use std::path::Path;

use launcher_core::server as files;
use serde::Serialize;
use serde_json::Value;

use crate::protocol::CheckView;
use crate::settings;

pub const NOT_LISTED: &str = "Not listed";
const LISTED_WARNING: &str = "This server is in the public server list, so strangers can find it. Turn 'List this server publicly' off in SETTINGS if you do not want that.";

#[derive(Debug, Clone, Default, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ListingView {
    pub state: String,
    pub public_address: String,
    pub error: String,
    pub text: String,
}

impl ListingView {
    pub fn from_status(data: &Value) -> Option<Self> {
        let listing = data.get("public_listing")?.as_object()?;
        let field = |key: &str| {
            listing
                .get(key)
                .and_then(Value::as_str)
                .unwrap_or_default()
                .to_owned()
        };
        let mut state = field("state");
        if state.is_empty() {
            state = "disabled".to_owned();
        }
        let public_address = field("publicAddress");
        let error = field("error");
        let text = describe(&state, &public_address, &error);
        Some(Self {
            state,
            public_address,
            error,
            text,
        })
    }

    pub fn listed(&self) -> bool {
        self.state != "disabled"
    }
}

pub fn describe(state: &str, address: &str, error: &str) -> String {
    match state {
        "registering" => "Registering...".to_owned(),
        "registered" if !address.is_empty() => format!("Registered as {address}"),
        "registered" => "Registered".to_owned(),
        "error" => format!("Error: {}", plain_error(error)),
        _ => NOT_LISTED.to_owned(),
    }
}

pub fn plain_error(error: &str) -> String {
    if error.is_empty() {
        return "the directory reported an unspecified error.".to_owned();
    }
    let known = [
        (
            "non_public_address",
            "the directory cannot use a public address for this server, so it cannot be listed. A server behind CGNAT, or behind a home router without a public address, cannot be listed. Forward the game UDP port on the router, or use STEAM P2P or direct connect instead.",
        ),
        (
            "password_listing_unsupported",
            "password-protected servers cannot be listed publicly yet. Clear the server password or turn public listing off.",
        ),
        (
            "ipv6_not_listening",
            "the directory reached this server over IPv6, but the server does not listen on IPv6. Bind the server to :: or register over IPv4.",
        ),
        (
            "invalid_metadata",
            "the directory rejected the server name or description. Use a name of 1 to 64 printable UTF-8 bytes and a description of at most 256, with no control characters.",
        ),
        (
            "unsupported_address",
            "public listings need a public IPv4 or IPv6 address.",
        ),
        (
            "directory_full",
            "the public directory is full right now; the server keeps retrying.",
        ),
        (
            "too_many_servers",
            "too many servers are already listed from this address.",
        ),
        (
            "address_not_allowed",
            "the directory takes the public address from the request itself, so the server must not send its own address.",
        ),
        (
            "Directory unreachable",
            "the directory cannot be reached right now; the server keeps retrying.",
        ),
    ];
    known
        .iter()
        .find(|(code, _)| error.contains(code))
        .map_or_else(|| error.to_owned(), |(_, text)| (*text).to_owned())
}

fn check(level: &'static str, text: impl Into<String>) -> CheckView {
    CheckView {
        level,
        text: text.into(),
    }
}

pub fn dedicated_checks(
    listing: Option<&ListingView>,
    file_listed: bool,
    port: u16,
) -> Vec<CheckView> {
    if let Some(view) = listing.filter(|view| view.state == "error") {
        return vec![check(
            "fail",
            format!("The server is not listed: {}", plain_error(&view.error)),
        )];
    }
    let listed = listing.map_or(file_listed, ListingView::listed);
    if !listed {
        return vec![check(
            "info",
            "This server is not in the public server list, so players can only join it when they know its address.",
        )];
    }
    vec![
        check("warn", LISTED_WARNING),
        check(
            "warn",
            format!("Players from the internet reach the listed server only if UDP port {port} is forwarded to this PC on your router. The launcher never changes your router or firewall."),
        ),
    ]
}

pub fn checks(dir: Option<&Path>, listing: Option<&ListingView>, port: u16) -> Vec<CheckView> {
    let Some(dir) = dir.filter(|dir| files::installed(dir)) else {
        return Vec::new();
    };
    dedicated_checks(listing, settings::listed(dir), port)
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;
    use std::fs;

    fn registered() -> ListingView {
        ListingView {
            state: "registered".into(),
            public_address: "203.0.113.7:7777".into(),
            error: String::new(),
            text: "Registered as 203.0.113.7:7777".into(),
        }
    }

    #[test]
    fn the_status_object_becomes_the_wording_the_host_uses() {
        let disabled = ListingView::from_status(&json!({"public_listing": {"state": "disabled"}}));
        assert_eq!(disabled.as_ref().map(|l| l.text.as_str()), Some(NOT_LISTED));
        assert!(!disabled.unwrap().listed());
        let registering =
            ListingView::from_status(&json!({"public_listing": {"state": "registering"}}));
        assert_eq!(registering.unwrap().text, "Registering...");
        let registered = ListingView::from_status(&json!({
            "public_listing": {"state": "registered", "publicAddress": "203.0.113.7:7777"}
        }))
        .unwrap();
        assert_eq!(registered.text, "Registered as 203.0.113.7:7777");
        assert!(registered.listed());
        let unclear = ListingView::from_status(&json!({"public_listing": {"state": "registered"}}));
        assert_eq!(unclear.unwrap().text, "Registered");
        let failed = ListingView::from_status(&json!({
            "public_listing": {"state": "error", "error": "non_public_address"}
        }))
        .unwrap();
        assert!(failed.text.starts_with("Error: ") && failed.listed());
        let unspecified =
            ListingView::from_status(&json!({"public_listing": {"state": "error"}})).unwrap();
        assert!(unspecified.text.contains("unspecified error"));
        let empty = ListingView::from_status(&json!({"public_listing": {}})).unwrap();
        assert_eq!(empty.text, NOT_LISTED);
        assert!(ListingView::from_status(&json!({"server_name": "S"})).is_none());
        assert!(ListingView::from_status(&json!({"public_listing": "on"})).is_none());
    }

    #[test]
    fn the_errors_the_directory_can_send_are_explained_in_plain_words() {
        let listed: [(&str, &[&str]); 9] = [
            (
                "non_public_address",
                &["CGNAT", "home router", "STEAM P2P", "direct connect"],
            ),
            ("password_listing_unsupported", &["password"]),
            ("ipv6_not_listening", &["IPv6"]),
            ("invalid_metadata", &["name", "description"]),
            ("unsupported_address", &["public IPv4 or IPv6"]),
            ("directory_full", &["full"]),
            ("too_many_servers", &["too many servers"]),
            ("address_not_allowed", &["address"]),
            ("Directory unreachable", &["cannot be reached"]),
        ];
        for (code, words) in listed {
            let text = plain_error(&format!("registration failed: {code}"));
            for word in words {
                assert!(text.contains(word), "{code} -> {text}");
            }
        }
        assert_eq!(
            plain_error(""),
            "the directory reported an unspecified error."
        );
        assert_eq!(plain_error("weird"), "weird");
    }

    #[test]
    fn the_listed_checks_name_the_game_port_and_never_touch_the_router() {
        let stopped = dedicated_checks(None, false, 7777);
        assert_eq!(stopped.len(), 1);
        assert_eq!(stopped[0].level, "info");
        let listed = dedicated_checks(None, true, 7000);
        assert!(listed
            .iter()
            .any(|check| check.text.contains(LISTED_WARNING)));
        assert!(listed
            .iter()
            .any(|check| check.level == "warn" && check.text.contains("UDP port 7000")));
        assert!(listed
            .iter()
            .all(|check| !check.text.contains("netsh") && !check.text.contains("firewall rule")));
        let failed = dedicated_checks(
            Some(&ListingView {
                state: "error".into(),
                error: "non_public_address".into(),
                ..registered()
            }),
            true,
            7777,
        );
        let error = failed
            .iter()
            .find(|check| check.level == "fail")
            .expect("an error listing must show");
        assert!(error.text.contains("CGNAT") && error.text.contains("not listed"));
    }

    #[test]
    fn a_missing_public_listing_key_counts_as_listed_in_the_checks() {
        let dir = tempfile::tempdir().unwrap();
        let at = dir.path().to_path_buf();
        assert!(checks(Some(&at), None, 7777).is_empty());
        fs::write(dir.path().join(files::EXE), b"x").unwrap();
        let missing = checks(Some(&at), None, 7777);
        assert!(missing
            .iter()
            .any(|check| check.text.contains(LISTED_WARNING)));
        fs::write(
            dir.path().join(settings::FILE),
            r#"{"public_listing": false}"#,
        )
        .unwrap();
        let off = checks(Some(&at), None, 7777);
        assert!(off.iter().all(|check| !check.text.contains(LISTED_WARNING)));
        let running = checks(Some(&at), Some(&registered()), 7777);
        assert!(running
            .iter()
            .any(|check| check.text.contains(LISTED_WARNING)));
    }
}
