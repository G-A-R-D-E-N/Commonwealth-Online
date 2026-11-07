use std::cmp::Ordering;

pub fn parse(text: &str) -> Option<Vec<u32>> {
    text.split('.')
        .map(|part| {
            (!part.is_empty() && part.bytes().all(|b| b.is_ascii_digit()))
                .then(|| part.parse().ok())
                .flatten()
        })
        .collect()
}

pub fn compare(a: &[u32], b: &[u32]) -> Ordering {
    let len = a.len().max(b.len());
    (0..len)
        .map(|i| a.get(i).unwrap_or(&0).cmp(b.get(i).unwrap_or(&0)))
        .find(|order| order.is_ne())
        .unwrap_or(Ordering::Equal)
}

pub fn newer(candidate: &str, than: &str) -> bool {
    match (parse(candidate), parse(than)) {
        (Some(a), Some(b)) => compare(&a, &b).is_gt(),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn numeric_components_compare_as_numbers() {
        assert!(newer("0.0.0.10", "0.0.0.9"));
        assert!(!newer("0.0.0.9", "0.0.0.10"));
        assert!(newer("0.0.1", "0.0.0.99"));
    }

    #[test]
    fn missing_components_count_as_zero() {
        assert!(!newer("1.2.3", "1.2.3.0"));
        assert!(!newer("1.2.3.0", "1.2.3"));
        assert!(newer("1.2.3.1", "1.2.3"));
    }

    #[test]
    fn junk_tags_are_never_newer_or_parsed() {
        for junk in [
            "",
            "v0.0.0.9",
            "0.0.0.9-rc1",
            "0..1",
            "1.x",
            "-1",
            " 1.0",
            "99999999999",
        ] {
            assert!(parse(junk).is_none(), "{junk}");
            assert!(!newer(junk, "0.0.0.1"), "{junk}");
            assert!(!newer("0.0.0.9", junk), "{junk}");
        }
    }
}
