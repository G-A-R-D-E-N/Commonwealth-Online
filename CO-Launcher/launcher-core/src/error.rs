use std::io;
use std::path::Path;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("Fallout 4 was not found; tried: {}", .tried.join(", "))]
    GameNotFound { tried: Vec<String> },
    #[error("I/O error at {path}: {source}")]
    Io { path: String, source: io::Error },
    #[error("cannot read the PE version of {path}: {reason}")]
    Pe { path: String, reason: String },
    #[error("archive error: {0}")]
    Archive(String),
    #[error("no Fallout 4 log files were found; start the game once first")]
    NoLogs,
}

impl Error {
    pub fn io(path: impl AsRef<Path>) -> impl FnOnce(io::Error) -> Error {
        let path = path.as_ref().display().to_string();
        move |source| Error::Io { path, source }
    }
}
