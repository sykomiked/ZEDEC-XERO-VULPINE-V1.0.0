// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// Where a reading falls against its safe range lo..hi (both ends inside),
// in Rust compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh):
// -1 below, 0 inside, 1 above, as wellspring_model.c wellspring_band says.
// A range whose low end is above its high end is no range: the module
// traps, which the runtime turns into a refusal (zxapp_wasm.c W3), never an
// answer. Comparisons only, so no input can overflow. Advice only: a
// reading outside its range is a reason to ask a clinician, never a
// diagnosis.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

#[no_mangle]
pub extern "C" fn run(reading: i64, lo: i64, hi: i64) -> i64 {
    if lo > hi {
        core::arch::wasm32::unreachable();
    }
    if reading < lo {
        -1
    } else if reading > hi {
        1
    } else {
        0
    }
}
