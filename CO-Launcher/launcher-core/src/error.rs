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
    #[error("{0} is in use by another program, so it cannot be replaced")]
    InUse(String),
    #[error("{0}")]
    Update(String),
    #[error("no Fallout 4 log files were found; start the game once first")]
    NoLogs,
    #[error("none of the Fallout 4 log files could be read: {0}")]
    Unreadable(String),
}

impl Error {
    pub fn io(path: impl AsRef<Path>) -> impl FnOnce(io::Error) -> Error {
        let path = path.as_ref().display().to_string();
        move |source| Error::Io { path, source }
    }

    pub fn user_message(&self) -> String {
        match self {
            Error::Io { path, source } => match source.kind() {
                io::ErrorKind::PermissionDenied => format!(
                    "Windows blocked access to {path}. Close any program using that file (Fallout 4, Mod Organizer 2, Vortex or a file viewer) and try again."
                ),
                io::ErrorKind::NotFound => format!(
                    "{path} was not found. It may have been moved or deleted. Choose the folder again and retry."
                ),
                _ if matches!(source.raw_os_error(), Some(112 | 39)) => {
                    format!("The drive is full at {path}. Free some space, then run setup again.")
                }
                _ => self.to_string(),
            },
            _ => self.to_string(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn io_failures_get_a_plain_next_step() {
        let denied = Error::io("C:/Game/Data")(io::Error::from(io::ErrorKind::PermissionDenied));
        assert!(denied
            .user_message()
            .starts_with("Windows blocked access to C:/Game/Data"));
        let full = Error::io("D:/dl")(io::Error::from_raw_os_error(112));
        assert!(full
            .user_message()
            .starts_with("The drive is full at D:/dl"));
        let missing = Error::io("E:/x")(io::Error::from(io::ErrorKind::NotFound));
        assert!(missing.user_message().contains("was not found"));
        let other = Error::Update("could not reach GitHub".to_owned());
        assert_eq!(other.user_message(), "could not reach GitHub");
    }
}
