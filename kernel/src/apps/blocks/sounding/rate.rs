// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Bytes (or requests) a second between two readings of a counter, in Rust
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh). A
// counter that went backwards was reset, and a reading with no time
// between has no rate: both are -1, as sounding_model.c says. Checked
// arithmetic: a product too big for i64 is -1 too, never wrapped.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub extern "C" fn run(now: i64, was: i64, dt_ms: i64) -> i64 {
    if dt_ms <= 0 || now < was {
        return -1;
    }
    match (now - was).checked_mul(1000) {
        Some(v) => v / dt_ms,
        None => -1,
    }
}
