const REDACTED: &str = "<redacted>";
const QUOTED: &str = "\"<redacted>\"";
const ESCAPED_QUOTED: &str = "\\\"<redacted>\\\"";
const SINGLE_QUOTED: &str = "'<redacted>'";
const USER: &str = "<user>";
const IP: &str = "<ip>";
const STEAM_ID: &str = "<steamid>";
#[allow(dead_code)]
const REDACT_LEDGER_TAG: &str = "Gc4Wo9Xp1Ta7Hu3Kz5Ly0Iq";
const _: () = assert!(!REDACT_LEDGER_TAG.is_empty());

const KEYS: [&str; 38] = [
    "resume_token_protected",
    "resumeTokenProtected",
    "server_password_sha256",
    "serverPasswordSha256",
    "previous_token_hash",
    "previousTokenHash",
    "resume_token_hash",
    "resume_token_ack",
    "password_sha256",
    "password_hash",
    "resumeTokenHash",
    "server_password",
    "resumeTokenAck",
    "serverPassword",
    "refresh_token",
    "session_nonce",
    "refreshToken",
    "access_token",
    "sessionNonce",
    "passwordHash",
    "resume_token",
    "accessToken",
    "resumeToken",
    "credentials",
    "auth_token",
    "session_id",
    "credential",
    "authToken",
    "sessionId",
    "token_hash",
    "tokenHash",
    "password",
    "api_key",
    "apiKey",
    "secret",
    "bearer",
    "token",
    "ticket",
];

const PROSE_WORDS: [&str; 51] = [
    "protected",
    "required",
    "set",
    "unset",
    "missing",
    "is",
    "was",
    "were",
    "be",
    "not",
    "changed",
    "reset",
    "expired",
    "invalid",
    "valid",
    "enabled",
    "disabled",
    "verification",
    "mismatch",
    "failed",
    "success",
    "error",
    "prompt",
    "field",
    "value",
    "token",
    "empty",
    "null",
    "none",
    "the",
    "a",
    "an",
    "of",
    "to",
    "for",
    "with",
    "and",
    "or",
    "has",
    "have",
    "had",
    "must",
    "should",
    "would",
    "will",
    "cannot",
    "can",
    "may",
    "does",
    "did",
    "do",
];

const VERSION_LABELS: [&str; 4] = ["version", "ver", "build", "v"];

type Hit = (usize, usize, &'static str);

pub fn redact(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let (mut at, mut copied) = (0, 0);
    while at < bytes.len() {
        let hit = key_value(bytes, at)
            .or_else(|| bearer(bytes, at))
            .or_else(|| user_path(bytes, at))
            .or_else(|| steam_id(bytes, at))
            .or_else(|| ipv4(bytes, at))
            .or_else(|| ipv6(bytes, at));
        match hit {
            Some((start, end, marker)) => {
                out.push_str(&text[copied..start]);
                out.push_str(marker);
                at = end.max(at + 1);
                copied = end;
            }
            None => at += 1,
        }
    }
    out.push_str(&text[copied..]);
    out
}

fn ident(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

fn slash(b: u8) -> bool {
    b == b'\\' || b == b'/'
}

fn delimiter(b: u8) -> bool {
    b" \t\r\n,;)]}&".contains(&b)
}

fn starts_ci(bytes: &[u8], at: usize, word: &str) -> bool {
    bytes
        .get(at..at + word.len())
        .is_some_and(|s| s.eq_ignore_ascii_case(word.as_bytes()))
}

fn skip_blanks(bytes: &[u8], mut at: usize) -> usize {
    while matches!(bytes.get(at), Some(b' ' | b'\t')) {
        at += 1;
    }
    at
}

fn before_ident(bytes: &[u8], at: usize) -> bool {
    at > 0 && ident(bytes[at - 1])
}

fn key_boundary(bytes: &[u8], at: usize) -> bool {
    let Some(&prev) = at.checked_sub(1).and_then(|i| bytes.get(i)) else {
        return true;
    };
    !prev.is_ascii_alphanumeric()
        || (prev.is_ascii_alphabetic() && bytes.get(at).is_some_and(u8::is_ascii_alphabetic))
}

fn quoted_at(bytes: &[u8], at: usize) -> Option<(usize, usize)> {
    let mut quote = at;
    while bytes.get(quote) == Some(&b'\\') {
        quote += 1;
    }
    match bytes.get(quote) {
        Some(b'"' | b'\'') => Some((quote, quote - at)),
        _ => None,
    }
}

fn key_value(bytes: &[u8], at: usize) -> Option<Hit> {
    if !key_boundary(bytes, at) {
        return None;
    }
    let key = KEYS.iter().find(|k| starts_ci(bytes, at, k))?;
    let mut cursor = at + key.len();
    if let Some((quote, _)) = quoted_at(bytes, cursor) {
        cursor = quote + 1;
    }
    cursor = skip_blanks(bytes, cursor);
    if !matches!(bytes.get(cursor), Some(b'=' | b':')) {
        return None;
    }
    let start = skip_blanks(bytes, cursor + 1);
    if let Some((quote_at, slashes)) = quoted_at(bytes, start) {
        let quote = bytes[quote_at];
        let escaped = slashes % 2 == 1;
        let from = if escaped { start } else { quote_at };
        let mut end = quote_at + 1;
        let mut run = 0;
        while end < bytes.len() && bytes[end] != b'\n' {
            if bytes[end] == b'\\' {
                run += 1;
                end += 1;
                continue;
            }
            if bytes[end] == quote {
                let close = if escaped {
                    run == slashes
                } else {
                    run % 2 == 0
                };
                if close {
                    end += 1;
                    let marker = match (escaped, quote) {
                        (true, b'"') => ESCAPED_QUOTED,
                        (false, b'"') => QUOTED,
                        _ => SINGLE_QUOTED,
                    };
                    return Some((from, end, marker));
                }
            }
            run = 0;
            end += 1;
        }
        let end = end.min(bytes.len());
        return Some((from, end, REDACTED));
    }
    let end = (start..bytes.len())
        .find(|&i| delimiter(bytes[i]))
        .unwrap_or(bytes.len());
    (end > start).then_some((start, end, REDACTED))
}

fn prose(value: &[u8]) -> bool {
    PROSE_WORDS
        .iter()
        .any(|word| value.eq_ignore_ascii_case(word.as_bytes()))
}

fn bearer(bytes: &[u8], at: usize) -> Option<Hit> {
    if before_ident(bytes, at)
        || !starts_ci(bytes, at, "bearer")
        || !matches!(bytes.get(at + 6), Some(b' ' | b'\t'))
    {
        return None;
    }
    let start = skip_blanks(bytes, at + 6);
    let end = (start..bytes.len())
        .find(|&i| delimiter(bytes[i]))
        .unwrap_or(bytes.len());
    let value = bytes.get(start..end)?;
    (end > start && (value.len() >= 16 || !prose(value))).then_some((start, end, REDACTED))
}

fn user_path(bytes: &[u8], at: usize) -> Option<Hit> {
    if !starts_ci(bytes, at, "users") || !bytes.get(at + 5).is_some_and(|&b| slash(b)) {
        return None;
    }
    let mut colon_end = at;
    while colon_end > 0 && slash(bytes[colon_end - 1]) {
        colon_end -= 1;
    }
    let drive = (1..=2).contains(&(at - colon_end))
        && colon_end >= 2
        && bytes[colon_end - 1] == b':'
        && bytes[colon_end - 2].is_ascii_alphabetic();
    if !drive {
        return None;
    }
    let mut start = at + 6;
    while bytes.get(start).is_some_and(|&b| slash(b)) {
        start += 1;
    }
    let end = (start..bytes.len())
        .find(|&i| slash(bytes[i]) || b"\r\n\"<>|\t".contains(&bytes[i]))
        .unwrap_or(bytes.len());
    (end > start && !starts_ci(bytes, start, USER)).then_some((start, end, USER))
}

fn steam_id(bytes: &[u8], at: usize) -> Option<Hit> {
    let token = bytes.get(at..at + 17)?;
    let alone = !before_ident(bytes, at) && !bytes.get(at + 17).is_some_and(|&b| ident(b));
    (alone && token.starts_with(b"7656119") && token.iter().all(u8::is_ascii_digit)).then_some((
        at,
        at + 17,
        STEAM_ID,
    ))
}

fn digits(bytes: &[u8], at: usize) -> usize {
    bytes.get(at..).map_or(0, |rest| {
        rest.iter().take_while(|b| b.is_ascii_digit()).count()
    })
}

fn ipv4(bytes: &[u8], at: usize) -> Option<Hit> {
    if !bytes[at].is_ascii_digit() || (at > 0 && (ident(bytes[at - 1]) || bytes[at - 1] == b'.')) {
        return None;
    }
    let mut octets = [0u32; 4];
    let mut cursor = at;
    for (i, octet) in octets.iter_mut().enumerate() {
        let n = digits(bytes, cursor);
        if n == 0 || n > 3 || (i > 0 && bytes.get(cursor - 1) != Some(&b'.')) {
            return None;
        }
        *octet = std::str::from_utf8(&bytes[cursor..cursor + n])
            .ok()?
            .parse()
            .ok()?;
        cursor += n;
        if i < 3 {
            cursor += 1;
        }
    }
    if octets.iter().any(|&o| o > 255) {
        return None;
    }
    let after = bytes.get(cursor).copied();
    let dotted = after == Some(b'.') && bytes.get(cursor + 1).is_some_and(u8::is_ascii_digit);
    if after.is_some_and(ident) || dotted {
        return None;
    }
    let port = if after == Some(b':') {
        digits(bytes, cursor + 1)
    } else {
        0
    };
    let ported =
        (1..=5).contains(&port) && !bytes.get(cursor + 1 + port).is_some_and(|&b| ident(b));
    if ported {
        Some((at, cursor + 1 + port, IP))
    } else if version_label(bytes, at) {
        None
    } else {
        Some((at, cursor, IP))
    }
}

fn version_label(bytes: &[u8], at: usize) -> bool {
    let mut end = at;
    while end > 0 && matches!(bytes[end - 1], b' ' | b'\t' | b':' | b'=') {
        end -= 1;
    }
    VERSION_LABELS.iter().any(|label| {
        let start = end.saturating_sub(label.len());
        end - start == label.len() && starts_ci(bytes, start, label) && !before_ident(bytes, start)
    })
}

fn hex_groups(part: &str, groups: &mut usize) -> bool {
    if part.is_empty() {
        return true;
    }
    part.split(':').all(|g| {
        *groups += 1;
        (1..=4).contains(&g.len()) && g.bytes().all(|b| b.is_ascii_hexdigit())
    })
}

fn ipv6(bytes: &[u8], at: usize) -> Option<Hit> {
    let prev = at.checked_sub(1).map(|i| bytes[i]);
    if prev.is_some_and(|b| ident(b) || b == b':' || b == b'.') {
        return None;
    }
    let len = bytes[at..]
        .iter()
        .take_while(|b| b.is_ascii_hexdigit() || **b == b':')
        .count();
    let end = at + len;
    if !(3..=45).contains(&len) || bytes.get(end).is_some_and(|&b| ident(b) || b == b'.') {
        return None;
    }
    let token = std::str::from_utf8(&bytes[at..end]).ok()?;
    let mut groups = 0;
    let valid = match token.split_once("::") {
        Some((head, tail)) => {
            !tail.contains("::")
                && hex_groups(head, &mut groups)
                && hex_groups(tail, &mut groups)
                && (1..=7).contains(&groups)
        }
        None => token.matches(':').count() == 7 && hex_groups(token, &mut groups),
    };
    valid.then_some((at, end, IP))
}

#[cfg(test)]
mod tests {
    use super::redact;

    #[test]
    fn secrets_paths_and_ids_are_masked() {
        let line = r#"login password=hunter2 ok {"access_token":"abc\"def","x":1} Bearer abcdefghijklmnop1234 C:\Users\Jane Doe\Documents\x steam 76561198000000000 from 203.0.113.9:27015 and [2001:db8::1]"#;
        let out = redact(line);
        for secret in [
            "hunter2",
            "abc\\\"def",
            "abcdefghij",
            "Jane",
            "7656119",
            "203.0",
            "2001",
        ] {
            assert!(!out.contains(secret), "{secret} leaked: {out}");
        }
        for marker in [
            "password=<redacted>",
            "\"access_token\":\"<redacted>\"",
            "C:\\Users\\<user>\\Documents",
            "<steamid>",
            "<ip>",
        ] {
            assert!(out.contains(marker), "{marker} missing: {out}");
        }
    }

    #[test]
    fn escaped_and_plain_profile_paths_are_masked() {
        let line = r#"sCrashLogDirectory = "C:\\Users\\Dana Example\\Documents\\F4SE" Working Directory: C:\Users\Dana Example\Games"#;
        let out = redact(line);
        assert!(!out.contains("Dana"), "user name leaked: {out}");
        assert!(
            out.contains(r#""C:\\Users\\<user>\\Documents\\F4SE""#),
            "escaped path missing: {out}"
        );
        assert!(
            out.contains(r"C:\Users\<user>\Games"),
            "plain path missing: {out}"
        );
    }

    #[test]
    fn prefixed_single_quoted_and_unclosed_values() {
        let out = redact("client_secret=abc {'password': 'x y'} password=\"oops\nnext line ok");
        assert_eq!(
            out,
            "client_secret=<redacted> {'password': '<redacted>'} password=<redacted>\nnext line ok"
        );
    }

    #[test]
    fn compound_camel_case_and_escaped_keys_are_masked() {
        let line = r#"{"adminToken":"abc123","adminPassword":"hunter2","serverSecret":"s3cr3t"} admin_token=snake1 admin_password=snake2 {"refreshToken":"rt0k"} {\"password\":\"escaped\"}"#;
        let out = redact(line);
        for secret in [
            "abc123", "hunter2", "s3cr3t", "snake1", "snake2", "rt0k", "escaped",
        ] {
            assert!(!out.contains(secret), "{secret} leaked: {out}");
        }
        for marker in [
            r#""adminToken":"<redacted>""#,
            r#""adminPassword":"<redacted>""#,
            r#""serverSecret":"<redacted>""#,
            "admin_token=<redacted>",
            "admin_password=<redacted>",
            r#""refreshToken":"<redacted>""#,
            r#"\"password\":\"<redacted>\""#,
        ] {
            assert!(out.contains(marker), "{marker} missing: {out}");
        }
    }

    #[test]
    fn hash_and_caps_keys_are_masked() {
        let line = "password_hash=aaaa1 password_sha256=bbbb1 server_password_sha256=cccc1 user/password_hash=dddd1 ADMINTOKEN=eeee1 ADMIN_PASSWORD=ffff1 SERVERPASSWORD=gggg1";
        let out = redact(line);
        for secret in [
            "aaaa1", "bbbb1", "cccc1", "dddd1", "eeee1", "ffff1", "gggg1",
        ] {
            assert!(!out.contains(secret), "{secret} leaked: {out}");
        }
        for marker in [
            "password_hash=<redacted>",
            "password_sha256=<redacted>",
            "server_password_sha256=<redacted>",
            "user/password_hash=<redacted>",
            "ADMINTOKEN=<redacted>",
            "ADMIN_PASSWORD=<redacted>",
            "SERVERPASSWORD=<redacted>",
        ] {
            assert!(out.contains(marker), "{marker} missing: {out}");
        }
    }

    #[test]
    fn double_escaped_json_is_masked() {
        let line = r#"{"payload":"{\\\"password\\\":\\\"hunter2\\\",\\\"mode\\\":\\\"free\\\"}"}"#;
        let out = redact(line);
        assert!(!out.contains("hunter2"), "{out}");
        assert!(out.contains("\\\"<redacted>\\\""), "{out}");
        assert!(out.contains("free"), "{out}");
    }

    #[test]
    fn suffix_style_secret_keys_are_masked() {
        let line = "steamTicket=t1 auth_ticket=t2 clientSecret=s1 client_secret=s2 api_password=p1 apiPassword=p2 accessToken=a1 access_token=a2 session_ticket: t3 SESSIONTOKEN=t4";
        let out = redact(line);
        for secret in ["t1", "t2", "s1", "s2", "p1", "p2", "a1", "a2", "t3", "t4"] {
            assert!(!out.contains(secret), "{secret} leaked: {out}");
        }
        for marker in [
            "steamTicket=<redacted>",
            "auth_ticket=<redacted>",
            "clientSecret=<redacted>",
            "client_secret=<redacted>",
            "api_password=<redacted>",
            "apiPassword=<redacted>",
            "accessToken=<redacted>",
            "access_token=<redacted>",
            "session_ticket: <redacted>",
            "SESSIONTOKEN=<redacted>",
        ] {
            assert!(out.contains(marker), "{marker} missing: {out}");
        }
    }

    #[test]
    fn token_hash_keys_are_masked() {
        let line = r#"{"tokenHash":"aaaa1","previousTokenHash":"bbbb1","resumeTokenHash":"cccc1"} tokenHash=dddd1 previousTokenHash=eeee1 resumeTokenHash=ffff1"#;
        let out = redact(line);
        for secret in ["aaaa1", "bbbb1", "cccc1", "dddd1", "eeee1", "ffff1"] {
            assert!(!out.contains(secret), "{secret} leaked: {out}");
        }
        for marker in [
            r#""tokenHash":"<redacted>""#,
            r#""previousTokenHash":"<redacted>""#,
            r#""resumeTokenHash":"<redacted>""#,
            "tokenHash=<redacted>",
            "previousTokenHash=<redacted>",
            "resumeTokenHash=<redacted>",
        ] {
            assert!(out.contains(marker), "{marker} missing: {out}");
        }
    }

    #[test]
    fn token_like_diagnostics_survive() {
        let line = "ticket_count=5 passwordless=1 secrets_dir=cache tokens_seen=9 secretary: none tokenizer=10.0";
        assert_eq!(redact(line), line);
    }

    #[test]
    fn snake_case_token_hash_keys_are_masked() {
        let line = r#"{"token_hash":"aaaa1","previous_token_hash":"bbbb1","resume_token_hash":"cccc1"} token_hash=dddd1 previous_token_hash=eeee1 resume_token_hash=ffff1 token_hash: gggg1 previous_token_hash: hhhh1 resume_token_hash: iiii1 {\"resume_token_hash\":\"jjjj1\"}"#;
        let out = redact(line);
        for secret in [
            "aaaa1", "bbbb1", "cccc1", "dddd1", "eeee1", "ffff1", "gggg1", "hhhh1", "iiii1",
            "jjjj1",
        ] {
            assert!(!out.contains(secret), "{secret} leaked: {out}");
        }
        for marker in [
            r#""token_hash":"<redacted>""#,
            r#""previous_token_hash":"<redacted>""#,
            r#""resume_token_hash":"<redacted>""#,
            "token_hash=<redacted>",
            "previous_token_hash=<redacted>",
            "resume_token_hash=<redacted>",
            "token_hash: <redacted>",
            "previous_token_hash: <redacted>",
            "resume_token_hash: <redacted>",
            r#"\"resume_token_hash\":\"<redacted>\""#,
        ] {
            assert!(out.contains(marker), "{marker} missing: {out}");
        }
    }

    #[test]
    fn no_key_precedes_a_longer_key_that_it_would_shadow() {
        for (index, short) in super::KEYS.iter().enumerate() {
            for (other, long) in super::KEYS.iter().enumerate() {
                if other != index
                    && long.len() > short.len()
                    && long.as_bytes()[..short.len()].eq_ignore_ascii_case(short.as_bytes())
                {
                    assert!(
                        other < index,
                        "{short} at {index} would shadow {long} at {other}"
                    );
                }
            }
        }
    }

    #[test]
    fn short_bearer_values_are_masked_and_bearer_in_a_sentence_is_not() {
        assert_eq!(
            redact("Authorization: Bearer abc123"),
            "Authorization: Bearer <redacted>"
        );
        assert_eq!(redact("Bearer x7"), "Bearer <redacted>");
        assert_eq!(redact("Bearer abcdefghijklmnop"), "Bearer <redacted>");
        assert_eq!(
            redact("Authorization: Bearer\tabc123"),
            "Authorization: Bearer\t<redacted>"
        );
        assert_eq!(redact("Bearer\ttoken"), "Bearer\ttoken");
        assert_eq!(redact("Bearer\t\tabcdefghijklmnop"), "Bearer\t\t<redacted>");
        assert_eq!(redact("bearer=abc"), "bearer=<redacted>");
        assert_eq!(redact("Bearer token"), "Bearer token");
        assert_eq!(redact("the bearer of bad news"), "the bearer of bad news");
        assert_eq!(
            redact("a bearer token was required"),
            "a bearer token was required"
        );
    }

    #[test]
    fn ambiguous_addresses_are_masked_with_or_without_a_port() {
        assert_eq!(
            redact("connected endpoint=1.2.3.0:27015"),
            "connected endpoint=<ip>"
        );
        assert_eq!(
            redact("connected endpoint=1.2.3.4:27015"),
            "connected endpoint=<ip>"
        );
        assert_eq!(redact("peer 1.2.3.0 joined"), "peer <ip> joined");
        assert_eq!(redact("0.0.0.0:27015"), "<ip>");
        assert_eq!(redact("version 1.2.3.4:27015"), "version <ip>");
    }

    #[test]
    fn labelled_version_numbers_survive() {
        for line in [
            "Game version: 1.10.163.0",
            "Unsupported version: 2.1.0.7",
            "Fallout 4 v1.10.163.0 Fallout4.exe+0141358",
            "Fallout 4 v 1.10.984.0",
            "build 1.11.240.0",
            "ver=1.10.163.0",
        ] {
            assert_eq!(redact(line), line);
        }
    }

    #[test]
    fn unlabelled_version_like_addresses_are_masked() {
        assert_eq!(redact("Fallout 1.10.163.0 loaded"), "Fallout <ip> loaded");
        assert_eq!(redact("server 1.2.3.0 up"), "server <ip> up");
    }

    #[test]
    fn diagnostics_survive() {
        let line = "[11:17:45.123] Fallout 4 v1.10.163.0 Fallout4.exe+0141358 TESObjectREFR::HasKeyword frame 05 0x7FF70B7B1358 token_count=5 tokenizer 10.0.26200.0";
        assert_eq!(redact(line), line);
        assert_eq!(redact("C:\\Users\\<user>\\x"), "C:\\Users\\<user>\\x");
        assert_eq!(redact("FrameTokenCount=5"), "FrameTokenCount=5");
    }
}
