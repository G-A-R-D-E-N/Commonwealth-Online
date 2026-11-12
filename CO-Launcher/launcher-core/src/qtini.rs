pub(crate) fn ini_section(text: &str, section: &str) -> Vec<(String, String)> {
    let mut inside = false;
    let mut pairs = Vec::new();
    for line in text.lines().map(str::trim) {
        if let Some(header) = line.strip_prefix('[') {
            inside = header.trim_end_matches(']').eq_ignore_ascii_case(section);
        } else if inside && !line.starts_with([';', '#']) {
            if let Some((key, value)) = line.split_once('=') {
                pairs.push((key.trim().to_ascii_lowercase(), ini_value(value.trim())));
            }
        }
    }
    pairs
}

pub(crate) fn ini_get<'a>(pairs: &'a [(String, String)], key: &str) -> Option<&'a str> {
    pairs
        .iter()
        .find(|(k, _)| k == key)
        .map(|(_, v)| v.as_str())
}

fn ini_value(raw: &str) -> String {
    let raw = raw
        .strip_prefix('"')
        .and_then(|v| v.strip_suffix('"'))
        .unwrap_or(raw);
    let raw = raw
        .strip_prefix("@ByteArray(")
        .and_then(|v| v.strip_suffix(')'))
        .unwrap_or(raw);
    unescape(raw)
}

fn unescape(text: &str) -> String {
    let bytes = text.as_bytes();
    let mut out = String::with_capacity(text.len());
    let mut at = 0;
    while at < bytes.len() {
        if let Some((ch, len)) = escape(bytes, at) {
            out.push(ch);
            at += len;
            continue;
        }
        let ch = text[at..].chars().next().expect("at is a char boundary");
        out.push(ch);
        at += ch.len_utf8();
    }
    out
}

fn escape(bytes: &[u8], at: usize) -> Option<(char, usize)> {
    if bytes.get(at) != Some(&b'\\') {
        return None;
    }
    match bytes.get(at + 1) {
        Some(b'\\') => Some(('\\', 2)),
        Some(b'x') => hex_escaped_char(bytes, at),
        _ => None,
    }
}

fn hex_escaped_char(bytes: &[u8], at: usize) -> Option<(char, usize)> {
    let first = hex_unit(bytes, at + 2)?;
    if !(0xD800..0xDC00).contains(&first) {
        return Some((char::from_u32(first as u32)?, 6));
    }
    let low = (bytes.get(at + 6) == Some(&b'\\') && bytes.get(at + 7) == Some(&b'x'))
        .then(|| hex_unit(bytes, at + 8))
        .flatten()
        .filter(|low| (0xDC00..0xE000).contains(low))?;
    let code = 0x10000 + ((first as u32 - 0xD800) << 10) + (low as u32 - 0xDC00);
    Some((char::from_u32(code)?, 12))
}

fn hex_unit(bytes: &[u8], at: usize) -> Option<u16> {
    let digits = bytes.get(at..at + 4)?;
    digits.iter().try_fold(0u16, |value, &b| {
        Some(value.checked_mul(16)? + (b as char).to_digit(16)? as u16)
    })
}

#[cfg(test)]
mod tests {
    use super::{ini_get, ini_section};

    #[test]
    fn qt_hex_escapes_decode_to_unicode() {
        let text = "[General]\ngamePath=@ByteArray(C:\\\\Spiele\\\\Fallout\\x00204)\nname=@ByteArray(Caf\\x00e9)\npaired=@ByteArray(\\xD83D\\xDE00)\nplain=C:\\\\Games\n";
        let general = ini_section(text, "general");
        assert_eq!(ini_get(&general, "gamepath"), Some("C:\\Spiele\\Fallout 4"));
        assert_eq!(ini_get(&general, "name"), Some("Café"));
        assert_eq!(ini_get(&general, "paired"), Some("😀"));
        assert_eq!(ini_get(&general, "plain"), Some("C:\\Games"));
    }

    #[test]
    fn malformed_escapes_are_kept_as_they_are() {
        let text = "[General]\nbad=C:\\Games\\xZZZZ\nlone=C:\\Games\n";
        let general = ini_section(text, "general");
        assert_eq!(ini_get(&general, "bad"), Some("C:\\Games\\xZZZZ"));
        assert_eq!(ini_get(&general, "lone"), Some("C:\\Games"));
    }

    #[test]
    fn only_the_named_section_is_read() {
        let text = "[General]\ngameName=Fallout 4\n[Other]\ngameName=Skyrim\n";
        let general = ini_section(text, "GENERAL");
        assert_eq!(ini_get(&general, "gamename"), Some("Fallout 4"));
        assert_eq!(ini_get(&general, "missing"), None);
    }
}
