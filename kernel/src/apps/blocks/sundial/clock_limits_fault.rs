// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Why a clock setting is past Sundial's limits (sundial_rules.h SD3), in
// Rust compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh).
// alarms is how many alarms there would be, snooze the snooze in minutes
// and timer a timer's length in minutes. The first fault that applies: 1
// more than 64 alarms, 2 a snooze outside 1..60 minutes, 3 a timer past
// 5999 minutes, 0 none. A count below zero is no measurement (a timer is
// never below zero): the module traps, which the runtime turns into a
// refusal (zxapp_wasm.c W3), never an answer. Comparisons only, so no
// input can overflow.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(alarms: i64, snooze: i64, timer: i64) -> i64 {
    if alarms < 0 || snooze < 0 || timer < 0 {
        core::arch::wasm32::unreachable();
    }
    if alarms > 64 {
        1
    } else if snooze < 1 || snooze > 60 {
        2
    } else if timer > 5999 {
        3
    } else {
        0
    }
}
