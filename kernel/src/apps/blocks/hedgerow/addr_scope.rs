// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// The scope of an address from its octets (hedgerow_rules.h HR4), in Rust
// compiled to WebAssembly (build: tools/apps/build_wasm_blocks.sh). fam 4
// is IPv4 a.b.c.d; fam 6 is IPv6, a and b its first two octets, c and d
// its last two. 0 public, 1 loopback (127/8, ::1), 2 private (10/8,
// 172.16/12, 192.168/16), 3 link-local (169.254/16, fe80::/10),
// 4 unique-local (fc00::/7), 5 no peer address (0/8, 224/3, ff00::/8, and
// ::/16 other than ::1). Any other family, or an octet outside 0..255, is
// no address: the module traps, which the runtime turns into a refusal
// (zxapp_wasm.c W3), never an answer.
#![no_std]

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    core::arch::wasm32::unreachable()
}

fn octet(x: i64) -> bool {
    x >= 0 && x <= 255
}

fn scope4(a: i64, b: i64) -> i64 {
    if a == 127 {
        1
    } else if a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) {
        2
    } else if a == 169 && b == 254 {
        3
    } else if a == 0 || a >= 224 {
        5
    } else {
        0
    }
}

fn scope6(a: i64, b: i64, c: i64, d: i64) -> i64 {
    if a == 0 && b == 0 {
        if c == 0 && d == 1 {
            1
        } else {
            5
        }
    } else if a == 254 && b >= 128 && b <= 191 {
        3
    } else if a == 252 || a == 253 {
        4
    } else if a == 255 {
        5
    } else {
        0
    }
}

#[no_mangle]
pub extern "C" fn run(fam: i64, a: i64, b: i64, c: i64, d: i64) -> i64 {
    if !(octet(a) && octet(b) && octet(c) && octet(d)) {
        core::arch::wasm32::unreachable();
    }
    if fam == 4 {
        scope4(a, b)
    } else if fam == 6 {
        scope6(a, b, c, d)
    } else {
        core::arch::wasm32::unreachable()
    }
}
