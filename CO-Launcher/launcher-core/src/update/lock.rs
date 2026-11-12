use std::fs::{self, File, OpenOptions, TryLockError};
use std::path::Path;
use std::sync::atomic::{AtomicBool, Ordering};

use crate::error::{Error, Result};

const LOCK_FILE: &str = "update.lock";
pub(super) const BUSY: &str = "another launcher is updating right now; try again when it finishes";
pub(super) const FINISHING: &str = "the previous update is still finishing; try again shortly";
static FINISHING_NOW: AtomicBool = AtomicBool::new(false);

pub(super) fn while_finishing<T>(run: impl FnOnce() -> T) -> T {
    FINISHING_NOW.store(true, Ordering::SeqCst);
    let out = run();
    FINISHING_NOW.store(false, Ordering::SeqCst);
    out
}

pub struct UpdateLock {
    _file: File,
}

impl UpdateLock {
    pub fn try_acquire(dir: &Path) -> Result<UpdateLock> {
        fs::create_dir_all(dir).map_err(Error::io(dir))?;
        let path = dir.join(LOCK_FILE);
        let file = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(&path)
            .map_err(Error::io(&path))?;
        match file.try_lock() {
            Ok(()) => Ok(UpdateLock { _file: file }),
            Err(TryLockError::WouldBlock) => {
                let message = if FINISHING_NOW.load(Ordering::SeqCst) {
                    FINISHING
                } else {
                    BUSY
                };
                Err(Error::Update(message.to_owned()))
            }
            Err(TryLockError::Error(source)) => Err(Error::Io {
                path: path.display().to_string(),
                source,
            }),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_held_lock_refuses_a_second_holder_until_it_is_dropped() {
        let dir = tempfile::tempdir().unwrap();
        let first = UpdateLock::try_acquire(dir.path()).unwrap();
        let refused = UpdateLock::try_acquire(dir.path()).err().unwrap();
        assert_eq!(refused.to_string(), BUSY);
        let during_finish = while_finishing(|| UpdateLock::try_acquire(dir.path()).err().unwrap());
        assert_eq!(during_finish.to_string(), FINISHING);
        drop(first);
        assert!(UpdateLock::try_acquire(dir.path()).is_ok());
        assert!(dir.path().join(LOCK_FILE).is_file());
    }
}
