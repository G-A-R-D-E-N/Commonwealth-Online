const REDACTED: &str = "<redacted>";
const QUOTED: &str = "\"<redacted>\"";
const ESCAPED_QUOTED: &str = "\\\"<redacted>\\\"";
const SINGLE_QUOTED: &str = "'<redacted>'";
const USER: &str = "<user>";
const IP: &str = "<ip>";
const STEAM_ID: &str = "<steamid>";

const KEYS: [&str; 31] = [
    "resume_token_protected",
    "resumeTokenProtected",
    "server_password_sha256",
    "serverPasswordSha256",
    "resume_token_ack",
    "password_sha256",
    "password_hash",
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
    "password",
    "api_key",
    "apiKey",
    "secret",
    "bearer",
    "token",
];

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

fn bearer(bytes: &[u8], at: usize) -> Option<Hit> {
    if before_ident(bytes, at) || !starts_ci(bytes, at, "bearer ") {
        return None;
    }
    let start = skip_blanks(bytes, at + 7);
    let end = (start..bytes.len())
        .find(|&i| delimiter(bytes[i]))
        .unwrap_or(bytes.len());
    (end - start >= 16).then_some((start, end, REDACTED))
}

fn user_path(bytes: &[u8], at: usize) -> Option<Hit> {
    let drive = at >= 3
        && slash(bytes[at - 1])
        && bytes[at - 2] == b':'
        && bytes[at - 3].is_ascii_alphabetic();
    if !drive || !starts_ci(bytes, at, "users") || !bytes.get(at + 5).is_some_and(|&b| slash(b)) {
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
    let version = octets[0] == 0 || (octets[0] == 1 && octets[3] == 0);
    if version {
        return None;
    }
    if after == Some(b':') {
        let port = digits(bytes, cursor + 1);
        if (1..=5).contains(&port) && !bytes.get(cursor + 1 + port).is_some_and(|&b| ident(b)) {
            cursor += 1 + port;
        }
    }
    Some((at, cursor, IP))
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
    fn diagnostics_survive() {
        let line = "[11:17:45.123] Fallout 4 v1.10.163.0 Fallout4.exe+0141358 TESObjectREFR::HasKeyword frame 05 0x7FF70B7B1358 token_count=5 tokenizer 10.0.26200.0";
        assert_eq!(redact(line), line);
        assert_eq!(redact("C:\\Users\\<user>\\x"), "C:\\Users\\<user>\\x");
        assert_eq!(redact("FrameTokenCount=5"), "FrameTokenCount=5");
    }
}
