/* Embed the product icon (shell/assets/inv.ico) into inv.exe via windres.
   Regenerates only when the icon or this script changes. */
use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let assets = manifest.join("assets");
    let ico = assets.join("inv.ico");
    if !ico.is_file() {
        panic!("missing icon: {}", ico.display());
    }
    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-changed=assets/inv.ico");

    let out = PathBuf::from(env::var("OUT_DIR").unwrap());
    let rc = out.join("inv.rc");
    let o = out.join("inv_res.o");

    let windres = env::var("WINDRES").unwrap_or_else(|_| "windres.exe".to_string());
    /* windres treats backslashes as escape sequences: always forward slashes. */
    let ico_fwd = ico.display().to_string().replace('\\', "/");
    std::fs::write(&rc, format!("1 ICON \"{}\"", ico_fwd)).unwrap();

    let st = Command::new(&windres)
        .args(["-O", "coff"])
        .arg(&rc)
        .arg(&o)
        .status()
        .expect("failed to run windres (set WINDRES or add it to PATH)");
    if !st.success() {
        panic!("windres failed");
    }
    println!("cargo:rustc-link-arg-bins={}", o.display());
}
