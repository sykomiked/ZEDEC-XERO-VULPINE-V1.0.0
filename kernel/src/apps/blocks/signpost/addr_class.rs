// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The class of an IPv4 address a.b.c.d (signpost_rules.h SP4, after RFC
// 6890), in Rust compiled to WebAssembly (build:
// tools/apps/build_wasm_blocks.sh): 0 public, 1 this network 0/8,
// 2 loopback 127/8, 3 private 10/8, 172.16/12 and 192.168/16, 4 link-local
// 169.254/16, 5 shared 100.64/10, 6 multicast 224/4, 7 reserved 240/4,
// 8 the broadcast 255.255.255.255. An octet outside 0..255 is no address:
// the module traps, which the runtime turns into a refusal (zxapp_wasm.c
// W3), never an answer.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

fn octet(x: i64) -> bool {
    x >= 0 && x <= 255
}

#[no_mangle]
pub extern "C" fn run(a: i64, b: i64, c: i64, d: i64) -> i64 {
    if !(octet(a) && octet(b) && octet(c) && octet(d)) {
        core::arch::wasm32::unreachable();
    }
    if a == 0 {
        1
    } else if a == 127 {
        2
    } else if a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) {
        3
    } else if a == 169 && b == 254 {
        4
    } else if a == 100 && b >= 64 && b <= 127 {
        5
    } else if a >= 224 && a <= 239 {
        6
    } else if a == 255 && b == 255 && c == 255 && d == 255 {
        8
    } else if a >= 240 {
        7
    } else {
        0
    }
}
