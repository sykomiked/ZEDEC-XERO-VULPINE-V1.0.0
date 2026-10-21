// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Minutes of quiet hours left at minute `now` of the day (halloo_rules.h
// HR1), in Rust compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh). The quiet hours run from minute
// `opens` up to minute `closes`, crossing midnight when opens is after
// closes, the whole day when they are equal. Inside them the answer is
// the minutes until they close, 1..1440; outside it is 0. A minute
// outside 0..1439 is no minute of the day: the module traps, which the
// runtime turns into a refusal (zxapp_wasm.c W3), while quiet.kron
// answers it (held).
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(opens: i64, closes: i64, now: i64) -> i64 {
    if !(0..1440).contains(&opens) || !(0..1440).contains(&closes) || !(0..1440).contains(&now) {
        core::arch::wasm32::unreachable();
    }
    let span = 1440 - (opens - closes).rem_euclid(1440);
    let past = (now - opens).rem_euclid(1440);
    if past < span {
        span - past
    } else {
        0
    }
}
