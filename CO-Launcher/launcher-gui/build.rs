use std::{env, fs, path::PathBuf};

fn main() {
    let bundled = PathBuf::from("bundle/CommonwealthOnline-server.zip");
    let out = PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("server-bundle.zip");
    fs::write(&out, fs::read(&bundled).unwrap_or_default()).unwrap();
    println!("cargo:rerun-if-changed={}", bundled.display());
}
