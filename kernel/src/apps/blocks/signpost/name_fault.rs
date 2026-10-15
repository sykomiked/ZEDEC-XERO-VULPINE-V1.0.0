// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Why one label of a name does not fit (signpost_rules.h SP2), in Rust
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh). label
// is the label's length in bytes, total the whole name's (a trailing dot
// aside), bad its bytes not allowed where they stand. The first fault
// that applies: 1 the label is empty or past 63 bytes, 2 the name is past
// 253 bytes or shorter than its label, 3 a byte not allowed, 0 none.
// A negative count is no measurement: the module traps, which the runtime
// turns into a refusal (zxapp_wasm.c W3), never an answer. Comparisons
// only, so no input can overflow.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(label: i64, total: i64, bad: i64) -> i64 {
    if label < 0 || total < 0 || bad < 0 {
        core::arch::wasm32::unreachable();
    }
    if label < 1 || label > 63 {
        1
    } else if total < label || total > 253 {
        2
    } else if bad > 0 {
        3
    } else {
        0
    }
}
