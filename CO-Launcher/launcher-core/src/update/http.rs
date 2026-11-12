use std::io::{self, Write};
use std::time::Duration;

use crate::error::{Error, Result};
use crate::update::running_version;

pub const REDIRECT_LIMIT: usize = 5;
pub const TOO_LARGE: &str = "the download is larger than expected";
const HOSTS: [&str; 4] = [
    "github.com",
    "api.github.com",
    "objects.githubusercontent.com",
    "release-assets.githubusercontent.com",
];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Reply {
    pub status: u16,
    pub etag: Option<String>,
}

pub trait Http {
    fn get(&self, url: &str, etag: Option<&str>, sink: &mut dyn Write) -> Result<Reply>;
}

pub struct Capped<W> {
    inner: W,
    left: u64,
}

impl<W> Capped<W> {
    pub fn new(inner: W, limit: u64) -> Self {
        Capped { inner, left: limit }
    }
}

impl<W: Write> Write for Capped<W> {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if buf.len() as u64 > self.left {
            return Err(io::Error::other(TOO_LARGE));
        }
        let written = self.inner.write(buf)?;
        self.left -= written as u64;
        Ok(written)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.inner.flush()
    }
}

pub fn host(url: &str) -> Option<&str> {
    let rest = url.strip_prefix("https://")?;
    let end = rest.find(['/', '?', '#']).unwrap_or(rest.len());
    let authority = &rest[..end];
    (!authority.is_empty() && !authority.contains(['@', ':', '\\', ' ', '%'])).then_some(authority)
}

pub fn check_url(url: &str) -> Result<()> {
    match host(url) {
        Some(host)
            if HOSTS
                .iter()
                .any(|allowed| allowed.eq_ignore_ascii_case(host)) =>
        {
            Ok(())
        }
        _ => Err(Error::Update(
            "refusing to fetch from a host that is not GitHub's release host".to_owned(),
        )),
    }
}

pub fn next_hop(current: &str, location: &str) -> Result<String> {
    let next = if location.starts_with("https://") {
        location.to_owned()
    } else if location.starts_with('/') {
        let origin = host(current).unwrap_or_default();
        format!("https://{origin}{location}")
    } else {
        return Err(Error::Update(
            "refusing a redirect that is not https".to_owned(),
        ));
    };
    check_url(&next)?;
    Ok(next)
}

fn accept(url: &str) -> &'static str {
    if host(url) == Some("api.github.com") {
        "application/vnd.github+json"
    } else {
        "application/octet-stream"
    }
}

pub struct UreqHttp {
    agent: ureq::Agent,
    user_agent: String,
}

impl UreqHttp {
    pub fn new() -> UreqHttp {
        let config = ureq::Agent::config_builder()
            .timeout_connect(Some(Duration::from_secs(10)))
            .timeout_recv_response(Some(Duration::from_secs(30)))
            .timeout_recv_body(Some(Duration::from_secs(120)))
            .max_redirects(0)
            .http_status_as_error(false)
            .build();
        UreqHttp {
            agent: ureq::Agent::new_with_config(config),
            user_agent: format!("CommonwealthOnlineLauncher/{}", running_version()),
        }
    }
}

impl Default for UreqHttp {
    fn default() -> Self {
        Self::new()
    }
}

impl Http for UreqHttp {
    fn get(&self, url: &str, etag: Option<&str>, sink: &mut dyn Write) -> Result<Reply> {
        let mut current = url.to_owned();
        for _ in 0..=REDIRECT_LIMIT {
            check_url(&current)?;
            let mut request = self
                .agent
                .get(&current)
                .header("User-Agent", &self.user_agent)
                .header("Accept", accept(&current));
            if let Some(tag) = etag {
                request = request.header("If-None-Match", tag);
            }
            let response = request
                .call()
                .map_err(|e| Error::Update(format!("could not reach GitHub: {e}")))?;
            let status = response.status().as_u16();
            let new_etag = response
                .headers()
                .get("etag")
                .and_then(|value| value.to_str().ok())
                .map(str::to_owned);
            if (300..400).contains(&status) && status != 304 {
                let location = response
                    .headers()
                    .get("location")
                    .and_then(|value| value.to_str().ok())
                    .ok_or_else(|| {
                        Error::Update("GitHub redirected without a location".to_owned())
                    })?;
                current = next_hop(&current, location)?;
                continue;
            }
            if (200..300).contains(&status) {
                let mut body = response.into_body().into_reader();
                io::copy(&mut body, sink)
                    .map_err(|e| Error::Update(format!("the download stopped: {e}")))?;
            }
            return Ok(Reply {
                status,
                etag: new_etag,
            });
        }
        Err(Error::Update("GitHub redirected too many times".to_owned()))
    }
}

#[cfg(test)]
pub(crate) mod fake {
    use std::cell::RefCell;
    use std::collections::HashMap;

    use super::*;

    #[derive(Debug, Clone)]
    pub struct Route {
        pub status: u16,
        pub etag: Option<String>,
        pub body: Vec<u8>,
    }

    #[derive(Default)]
    pub struct Fake {
        pub routes: RefCell<HashMap<String, Route>>,
        pub requests: RefCell<Vec<(String, Option<String>)>>,
    }

    impl Fake {
        pub fn serve(&self, url: &str, status: u16, etag: Option<&str>, body: &[u8]) {
            self.routes.borrow_mut().insert(
                url.to_owned(),
                Route {
                    status,
                    etag: etag.map(str::to_owned),
                    body: body.to_vec(),
                },
            );
        }
    }

    impl Http for Fake {
        fn get(&self, url: &str, etag: Option<&str>, sink: &mut dyn Write) -> Result<Reply> {
            check_url(url)?;
            self.requests
                .borrow_mut()
                .push((url.to_owned(), etag.map(str::to_owned)));
            let route = self
                .routes
                .borrow()
                .get(url)
                .cloned()
                .ok_or_else(|| Error::Update(format!("no route for {url}")))?;
            if route.status == 200 && etag.is_some() && etag == route.etag.as_deref() {
                return Ok(Reply {
                    status: 304,
                    etag: route.etag,
                });
            }
            if (200..300).contains(&route.status) {
                sink.write_all(&route.body)
                    .map_err(|e| Error::Update(e.to_string()))?;
            }
            Ok(Reply {
                status: route.status,
                etag: route.etag,
            })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn only_github_hosts_over_https_are_fetched() {
        for url in [
            "https://github.com/G-A-R-D-E-N/Commonwealth-Online/releases",
            "https://api.github.com/repos/x",
            "https://objects.githubusercontent.com/a",
            "https://release-assets.githubusercontent.com/a",
        ] {
            assert!(check_url(url).is_ok(), "{url}");
        }
        for url in [
            "http://github.com/a",
            "https://github.com.evil.com/a",
            "https://evil.com/github.com/a",
            "https://github.com@evil.com/a",
            "https://github.com:444/a",
            "https://github.com\\@evil.com/a",
            "https://user:pw@github.com/a",
            "https://github.com%2eevil.com/a",
            "https://",
            "ftp://github.com/a",
        ] {
            assert!(check_url(url).is_err(), "{url}");
        }
    }

    #[test]
    fn redirects_must_stay_on_github_over_https() {
        assert_eq!(
            next_hop(
                "https://github.com/a",
                "https://objects.githubusercontent.com/b"
            )
            .unwrap(),
            "https://objects.githubusercontent.com/b"
        );
        assert_eq!(
            next_hop("https://github.com/a", "/G-A-R-D-E-N/x").unwrap(),
            "https://github.com/G-A-R-D-E-N/x"
        );
        assert!(next_hop("https://github.com/a", "http://github.com/b").is_err());
        assert!(next_hop("https://github.com/a", "https://evil.com/b").is_err());
        assert!(next_hop("https://github.com/a", "relative/path").is_err());
    }

    #[test]
    fn capped_writer_stops_at_the_limit() {
        let mut body = Vec::new();
        let mut capped = Capped::new(&mut body, 4);
        assert!(capped.write_all(b"abcd").is_ok());
        assert!(capped.write_all(b"e").is_err());
        assert_eq!(body, b"abcd");
    }
}
