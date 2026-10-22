// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Why a mail address does not fit (rookery_rules.h KR3), in Rust compiled
// to WebAssembly (build: tools/apps/build_wasm_blocks.sh), from counts of
// its bytes: size, ats ('@' bytes), local (before the first '@'), domain
// (after the last '@'), longest and shortest (its labels) and bad (bytes
// not allowed where they stand). The first fault that applies: 1 shorter
// than 3 bytes or past 254, 2 not exactly one '@', 3 the local part empty
// or past 64, 4 the domain empty or past 253, 5 a label empty or past 63,
// 6 a byte not allowed, 0 none. Counts no one address can give (a
// negative count, more parts than bytes, a shortest label longer than the
// longest or the longest longer than the domain) are no measurement: the
// module traps, which the runtime turns into a refusal (zxapp_wasm.c W3),
// never an answer. Every count is checked against size first, so the sum
// below cannot overflow.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(
    size: i64,
    ats: i64,
    local: i64,
    domain: i64,
    longest: i64,
    shortest: i64,
    bad: i64,
) -> i64 {
    if size < 0 || ats < 0 || local < 0 || domain < 0 || longest < 0 || shortest < 0 || bad < 0 {
        core::arch::wasm32::unreachable();
    }
    if ats > size
        || local > size
        || domain > size
        || bad > size
        || local + domain > size - ats
        || shortest > longest
        || longest > domain
    {
        core::arch::wasm32::unreachable();
    }
    if size < 3 || size > 254 {
        1
    } else if ats != 1 {
        2
    } else if local < 1 || local > 64 {
        3
    } else if domain < 1 || domain > 253 {
        4
    } else if shortest < 1 || longest > 63 {
        5
    } else if bad > 0 {
        6
    } else {
        0
    }
}
