// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//! Adds an rpath to libzxv for this crate's own tests and examples, so
//! `cargo test` and `cargo run --example` work without LD_LIBRARY_PATH.
fn main() {
    if let Ok(dir) = std::env::var("DEP_ZXV_LIB_DIR") {
        let os = std::env::var("CARGO_CFG_TARGET_OS").unwrap_or_default();
        if os == "linux" || os == "macos" || os.ends_with("bsd") {
            println!("cargo:rustc-link-arg=-Wl,-rpath,{dir}");
        }
    }
}
