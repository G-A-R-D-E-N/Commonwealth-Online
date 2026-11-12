use std::{env, fs, path::PathBuf};

fn main() {
    let bundled = PathBuf::from("bundle/CommonwealthOnline-server.zip");
    let version = PathBuf::from("bundle/CommonwealthOnline-server.version");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let zip = fs::read(&bundled).unwrap_or_default();
    let text = fs::read_to_string(&version).unwrap_or_default();
    let text = if zip.is_empty() { "" } else { text.trim() };
    fs::write(out.join("server-bundle.zip"), zip).unwrap();
    fs::write(out.join("server-bundle.version"), text).unwrap();
    println!("cargo:rerun-if-changed={}", bundled.display());
    println!("cargo:rerun-if-changed={}", version.display());
}
