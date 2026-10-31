pub fn lookup(path: &str) -> Option<(&'static str, &'static [u8])> {
    match path {
        "/index.html" => Some((
            "text/html; charset=utf-8",
            include_bytes!("../ui/index.html"),
        )),
        "/launcher.css" => Some(("text/css", include_bytes!("../ui/launcher.css"))),
        "/launcher.js" => Some(("text/javascript", include_bytes!("../ui/launcher.js"))),
        "/mods.js" => Some(("text/javascript", include_bytes!("../ui/mods.js"))),
        "/logs.js" => Some(("text/javascript", include_bytes!("../ui/logs.js"))),
        "/server.js" => Some(("text/javascript", include_bytes!("../ui/server.js"))),
        "/server-settings.js" => Some((
            "text/javascript",
            include_bytes!("../ui/server-settings.js"),
        )),
        "/server-admin.js" => Some(("text/javascript", include_bytes!("../ui/server-admin.js"))),
        "/server-mock.js" => Some(("text/javascript", include_bytes!("../ui/server-mock.js"))),
        "/logo.svg" => Some((
            "image/svg+xml",
            include_bytes!(
                "../../../CommonwealthOnlineUI/ui/views/CommonwealthOnline/shared/logo.svg"
            ),
        )),
        "/futura-pt-demibold.otf" => Some((
            "font/otf",
            include_bytes!("../../../CommonwealthOnlineUI/ui/fonts/futura-pt-demibold.otf"),
        )),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn serves_the_contract_files_with_their_mime_types() {
        assert_eq!(lookup("/index.html").unwrap().0, "text/html; charset=utf-8");
        assert_eq!(lookup("/launcher.css").unwrap().0, "text/css");
        assert_eq!(lookup("/launcher.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/mods.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/logs.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/server.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/server-settings.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/server-admin.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/server-mock.js").unwrap().0, "text/javascript");
        assert_eq!(lookup("/logo.svg").unwrap().0, "image/svg+xml");
        assert_eq!(lookup("/futura-pt-demibold.otf").unwrap().0, "font/otf");
        assert!(!lookup("/logo.svg").unwrap().1.is_empty());
    }

    #[test]
    fn anything_else_is_not_found() {
        assert!(lookup("/").is_none());
        assert!(lookup("/settings.json").is_none());
        assert!(lookup("/index.html/extra").is_none());
    }
}
