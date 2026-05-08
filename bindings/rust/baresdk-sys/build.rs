use std::{env, path::PathBuf};

fn main() {
    let manifest = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let repo_root = manifest.parent().unwrap().parent().unwrap().parent().unwrap();

    // ── Include path ──────────────────────────────────────────────────────
    let include_dir = env::var("BARESDK_INCLUDE")
        .map(PathBuf::from)
        .unwrap_or_else(|_| repo_root.join("include"));

    // ── Library search path ───────────────────────────────────────────────
    if let Ok(lib_dir) = env::var("BARESDK_LIB_DIR") {
        println!("cargo:rustc-link-search=native={lib_dir}");
    } else {
        let target_os   = env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
        let target_arch = env::var("CARGO_CFG_TARGET_ARCH").unwrap_or_default();
        let dist_subdir = match target_os.as_str() {
            "macos"   => "macos/universal".to_string(),
            "windows" => "windows/x64".to_string(),
            _ => {
                let arch = match target_arch.as_str() {
                    "aarch64" => "arm64",
                    _         => "x86_64",
                };
                format!("linux/{arch}")
            }
        };
        let default = repo_root.join("dist").join(dist_subdir);
        println!("cargo:rustc-link-search=native={}", default.display());
    }

    println!("cargo:rustc-link-lib=static=baresdk");

    // ── Platform link flags ───────────────────────────────────────────────
    let target_os = env::var("CARGO_CFG_TARGET_OS").unwrap();
    match target_os.as_str() {
        "linux" => {
            println!("cargo:rustc-link-lib=pthread");
            println!("cargo:rustc-link-lib=ssl");
            println!("cargo:rustc-link-lib=crypto");
            println!("cargo:rustc-link-lib=z");
            println!("cargo:rustc-link-lib=m");
            println!("cargo:rustc-link-lib=dl");
            println!("cargo:rustc-link-lib=resolv");
        }
        "macos" => {
            println!("cargo:rustc-link-lib=framework=CoreFoundation");
            println!("cargo:rustc-link-lib=framework=Security");
            println!("cargo:rustc-link-lib=framework=CoreAudio");
            println!("cargo:rustc-link-lib=framework=AudioToolbox");
        }
        "android" => {
            println!("cargo:rustc-link-lib=log");
            println!("cargo:rustc-link-lib=OpenSLES");
            println!("cargo:rustc-link-lib=android");
        }
        "windows" => {
            println!("cargo:rustc-link-lib=ws2_32");
            println!("cargo:rustc-link-lib=iphlpapi");
            println!("cargo:rustc-link-lib=crypt32");
        }
        _ => {}
    }

    // ── Generate bindings ─────────────────────────────────────────────────
    println!("cargo:rerun-if-changed={}/baresdk.h", include_dir.display());

    let bindings = bindgen::Builder::default()
        .header(include_dir.join("baresdk.h").to_str().unwrap())
        .clang_arg(format!("-I{}", include_dir.display()))
        .allowlist_file(".*baresdk\\.h")
        .prepend_enum_name(false)
        .derive_debug(true)
        .derive_default(true)
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("Unable to generate baresdk bindings");

    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("Couldn't write bindings.rs");
}
