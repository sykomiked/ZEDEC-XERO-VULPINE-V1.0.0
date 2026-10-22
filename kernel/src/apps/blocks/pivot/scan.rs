// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The item a scan lights at a moment (pivot_rules.h PV5), in Rust compiled
// to WebAssembly (build: tools/apps/build_wasm_blocks.sh): the scan steps
// every `step` ms over `items` items from the first, so the item lit
// `elapsed` ms in is floor(elapsed / step) mod items. Elapsed 0 to
// 999999999 ms, step 300 to 10000 ms and 1 to 64 items; anything else
// traps, which the runtime turns into a refusal (zxapp_wasm.c W3), never
// an answer. KRON divides by any step but 0, so a step outside the limits
// is held.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(elapsed: i64, step: i64, items: i64) -> i64 {
    if elapsed < 0 || elapsed > 999999999 {
        core::arch::wasm32::unreachable();
    }
    if step < 300 || step > 10000 {
        core::arch::wasm32::unreachable();
    }
    if items < 1 || items > 64 {
        core::arch::wasm32::unreachable();
    }
    (elapsed / step) % items
}
