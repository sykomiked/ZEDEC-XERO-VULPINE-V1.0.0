// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The slowest run so far (retort_rules.h RT2), in Rust compiled to
// WebAssembly (build: tools/apps/build_wasm_blocks.sh): one fold step,
// max(slowest, ms). Each value is 0..3600000 milliseconds, the longest run
// Retort's limits allow (an hour); anything else is no latency: the module
// traps, which the runtime turns into a refusal (zxapp_wasm.c W3), where
// slowest.kron answers: held.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

fn latency(x: i64) -> bool {
    x >= 0 && x <= 3600000
}

#[no_mangle]
pub extern "C" fn run(slowest: i64, ms: i64) -> i64 {
    if !(latency(slowest) && latency(ms)) {
        core::arch::wasm32::unreachable();
    }
    if ms > slowest {
        ms
    } else {
        slowest
    }
}
