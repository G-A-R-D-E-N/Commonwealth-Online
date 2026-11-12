pub mod addrlib;
pub mod appdata;
pub mod channel;
pub mod discover;
pub mod error;
pub mod fsutil;
pub mod logs;
pub mod modinstall;
pub mod modlist;
pub mod modmgr;
mod qtini;
pub mod redact;
pub mod runtime;
pub mod server;
pub mod setup;
pub mod update;
pub mod versions;
pub mod vortex;

#[cfg(test)]
pub(crate) mod testutil;

pub use error::{Error, Result};
pub use runtime::{Detected, ExeInfo, Runtime};
