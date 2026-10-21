// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0
//
// zt_wgpu.wgsl — the WebGPU compute kernels (zt_wgpu.c): zt_vk.comp's
// ZV_I32 build in WGSL, line for line, so every logit is bit for bit the
// CPU's (rule R1: integers only; test_zt_wgpu checks it).
//
// WGSL has no 64-bit integers and no 8- or 16-bit storage, so this is the
// Vulkan port's design for GPUs without shaderInt64: every 64-bit value is a
// pair of 32-bit words (zl signed, zw unsigned: v.x the low word, v.y the
// high word, two's complement) and every 64-bit step is done exactly on the
// pair; buffers are 32-bit words and a byte or 16-bit store is an atomic and
// then or on its word. WGSL has no 32 x 32 -> 64 multiply either (GLSL's
// umulExtended): x_mulw builds it from 16-bit halves.
//
// What differs from zt_vk.comp is only how arguments reach memory, because
// WebGPU forbids two writable bindings of one buffer in a dispatch and a
// buffer bound both read-only and writable:
//   - zt_wgpu.c binds each distinct buffer an op touches once: read-only in
//     r0..r2, or writable in w0, w1 when the kernel writes any of its
//     arguments there (then every argument in it is read with atomicLoad);
//   - argument n (0..5, zt_gpu.h's b[n]) names its binding in sel(n) (0..2:
//     r0..r2, 3: w0, 4: w1) and its byte offset in that binding in off(n);
//   - the op's numbers come in a uniform block (WebGPU has no push
//     constants): pu(0..15) zt_gpu.h's u, pu(16..21) off, pu(22..27) sel,
//     pu(28) the workgroups per grid row (grids past 65535 go 2-D);
//   - the saturation counter is its own binding.
// One source, two modules: zt_wgpu.c puts four constants in front of it,
//     const ZW_WG: u32 = 256u;  threads per layer-kernel workgroup
//     const ZW_R:  u32 = 4u;    rows per product workgroup
//     const ZW_L:  u32 = 32u;   threads per row (ZW_T <= ZW_L)
//     const ZW_T:  u32 = 8u;    tokens per tile (1 for one token)
// chosen from the adapter's limits, so phones get smaller blocks. Integer
// sums are exact, so the logits do not depend on the sizes (test_zt_wgpu
// runs two). Entry points: k0..k14 (zt_gpu.h's ZG_K_*, 3 excepted: products
// use mm_q4k, mm_q6k, mm_q80).

struct Params {
    v: array<vec4<u32>, 8>,
}

@group(0) @binding(0) var<uniform> prm: Params;
@group(0) @binding(1) var<storage, read> r0: array<u32>;
@group(0) @binding(2) var<storage, read> r1: array<u32>;
@group(0) @binding(3) var<storage, read> r2: array<u32>;
@group(0) @binding(4) var<storage, read_write> w0: array<atomic<u32>>;
@group(0) @binding(5) var<storage, read_write> w1: array<atomic<u32>>;
@group(0) @binding(6) var<storage, read_write> ctr: atomic<u32>;

fn pu(k: u32) -> u32 {
    return prm.v[k >> 2u][k & 3u];
}

// ---- 64-bit integers as two 32-bit words: x low, y high ----

fn x_add(a: vec2<u32>, b: vec2<u32>) -> vec2<u32> {
    let lo = a.x + b.x;
    return vec2<u32>(lo, a.y + b.y + select(0u, 1u, lo < a.x));
}
fn x_sub(a: vec2<u32>, b: vec2<u32>) -> vec2<u32> {
    return vec2<u32>(a.x - b.x, a.y - b.y - select(0u, 1u, a.x < b.x));
}
// a * b as 64 bits (umulExtended), from 16-bit halves
fn x_mulw(a: u32, b: u32) -> vec2<u32> {
    let a0 = a & 0xFFFFu;
    let a1 = a >> 16u;
    let b0 = b & 0xFFFFu;
    let b1 = b >> 16u;
    let p00 = a0 * b0;
    let p01 = a0 * b1;
    let p10 = a1 * b0;
    let p11 = a1 * b1;
    let mid = (p00 >> 16u) + (p01 & 0xFFFFu) + (p10 & 0xFFFFu); // < 2^18
    return vec2<u32>((mid << 16u) | (p00 & 0xFFFFu), p11 + (p01 >> 16u) + (p10 >> 16u) + (mid >> 16u));
}
fn x_mul(a: vec2<u32>, b: vec2<u32>) -> vec2<u32> { // the low 64 bits, as C's wrapping product
    let p = x_mulw(a.x, b.x);
    return vec2<u32>(p.x, p.y + a.x * b.y + a.y * b.x);
}
fn x_shl(a: vec2<u32>, n: u32) -> vec2<u32> {
    if (n == 0u) { return a; }
    if (n < 32u) { return vec2<u32>(a.x << n, (a.y << n) | (a.x >> (32u - n))); }
    return vec2<u32>(0u, a.x << (n - 32u));
}
fn x_shr(a: vec2<u32>, n: u32) -> vec2<u32> { // logical
    if (n == 0u) { return a; }
    if (n < 32u) { return vec2<u32>((a.x >> n) | (a.y << (32u - n)), a.y >> n); }
    return vec2<u32>(a.y >> (n - 32u), 0u);
}
fn x_sar(a: vec2<u32>, n: u32) -> vec2<u32> { // arithmetic
    let hs = bitcast<i32>(a.y);
    if (n == 0u) { return a; }
    if (n < 32u) { return vec2<u32>((a.x >> n) | (a.y << (32u - n)), bitcast<u32>(hs >> n)); }
    return vec2<u32>(bitcast<u32>(hs >> (n - 32u)), bitcast<u32>(hs >> 31u));
}
fn x_ult(a: vec2<u32>, b: vec2<u32>) -> bool {
    return a.y < b.y || (a.y == b.y && a.x < b.x);
}
fn x_slt(a: vec2<u32>, b: vec2<u32>) -> bool {
    return bitcast<i32>(a.y) < bitcast<i32>(b.y) || (a.y == b.y && a.x < b.x);
}

// distinct types, so a signed value is never used as an unsigned one unawares
struct zl {
    v: vec2<u32>,
}
struct zw {
    v: vec2<u32>,
}

fn l_i(a: i32) -> zl { return zl(vec2<u32>(bitcast<u32>(a), bitcast<u32>(a >> 31u))); }
fn l_p2(n: u32) -> zl { return zl(x_shl(vec2<u32>(1u, 0u), n)); }
fn l_add(a: zl, b: zl) -> zl { return zl(x_add(a.v, b.v)); }
fn l_sub(a: zl, b: zl) -> zl { return zl(x_sub(a.v, b.v)); }
fn l_neg(a: zl) -> zl { return zl(x_sub(vec2<u32>(0u, 0u), a.v)); }
fn l_mul(a: zl, b: zl) -> zl { return zl(x_mul(a.v, b.v)); }
fn l_mul32(a: i32, b: i32) -> zl { return l_mul(l_i(a), l_i(b)); }
fn l_sar(a: zl, n: u32) -> zl { return zl(x_sar(a.v, n)); }
fn l_shl(a: zl, n: u32) -> zl { return zl(x_shl(a.v, n)); }
fn l_lt(a: zl, b: zl) -> bool { return x_slt(a.v, b.v); }
fn l_gt(a: zl, b: zl) -> bool { return x_slt(b.v, a.v); }
fn l_le(a: zl, b: zl) -> bool { return !x_slt(b.v, a.v); }
fn l_ge(a: zl, b: zl) -> bool { return !x_slt(a.v, b.v); }
fn l_eq(a: zl, b: zl) -> bool { return all(a.v == b.v); }
fn l_lo(a: zl) -> i32 { return bitcast<i32>(a.v.x); }
fn l_sel(f: zl, t: zl, c: bool) -> zl { return zl(select(f.v, t.v, c)); }

fn w_u(a: u32) -> zw { return zw(vec2<u32>(a, 0u)); }
fn w_make(lo: u32, hi: u32) -> zw { return zw(vec2<u32>(lo, hi)); }
fn w_p2(n: u32) -> zw { return zw(x_shl(vec2<u32>(1u, 0u), n)); }
fn w_add(a: zw, b: zw) -> zw { return zw(x_add(a.v, b.v)); }
fn w_sub(a: zw, b: zw) -> zw { return zw(x_sub(a.v, b.v)); }
fn w_mul32(a: u32, b: u32) -> zw { return zw(x_mulw(a, b)); }
fn w_shr(a: zw, n: u32) -> zw { return zw(x_shr(a.v, n)); }
fn w_shl(a: zw, n: u32) -> zw { return zw(x_shl(a.v, n)); }
fn w_lt(a: zw, b: zw) -> bool { return x_ult(a.v, b.v); }
fn w_gt(a: zw, b: zw) -> bool { return x_ult(b.v, a.v); }
fn w_ge(a: zw, b: zw) -> bool { return !x_ult(a.v, b.v); }
fn w_nz(a: zw) -> bool { return any(a.v != vec2<u32>(0u, 0u)); }
fn w_lo(a: zw) -> u32 { return a.v.x; }
fn w_hi(a: zw) -> u32 { return a.v.y; }

fn w_l(a: zl) -> zw { return zw(a.v); }
fn l_w(a: zw) -> zl { return zl(a.v); }

fn zm_bitlen(v: zw) -> u32 {
    if (v.v.y != 0u) { return 33u + firstLeadingBit(v.v.y); }
    if (v.v.x == 0u) { return 0u; }
    return firstLeadingBit(v.v.x) + 1u;
}

// zt_metal_math.h's shift-subtract division, keeping the bit a shift of the
// remainder carries out, so it is n / d for every d
fn zm_udiv(n: zw, d: zw) -> zw {
    if (!w_nz(d)) { return w_u(0u); }
    if (n.v.y == 0u && d.v.y == 0u) { return w_u(n.v.x / d.v.x); }
    var q = vec2<u32>(0u, 0u);
    var r = vec2<u32>(0u, 0u);
    for (var i: i32 = 63; i >= 0; i--) {
        let carry = (r.y >> 31u) != 0u;
        r = x_shl(r, 1u);
        if (i >= 32) {
            r.x |= (n.v.y >> u32(i - 32)) & 1u;
        } else {
            r.x |= (n.v.x >> u32(i)) & 1u;
        }
        if (carry || !x_ult(r, d.v)) {
            r = x_sub(r, d.v);
            if (i >= 32) {
                q.y |= 1u << u32(i - 32);
            } else {
                q.x |= 1u << u32(i);
            }
        }
    }
    return zw(q);
}

// ---- memory: argument n's bytes, through the binding sel(n) names ----

fn wd(n: u32, wi: u32) -> u32 {
    switch (pu(22u + n)) {
        case 0u: { return r0[wi]; }
        case 1u: { return r1[wi]; }
        case 2u: { return r2[wi]; }
        case 3u: { return atomicLoad(&w0[wi]); }
        default: { return atomicLoad(&w1[wi]); }
    }
}
fn wand(n: u32, wi: u32, m: u32) {
    if (pu(22u + n) == 3u) {
        atomicAnd(&w0[wi], m);
    } else {
        atomicAnd(&w1[wi], m);
    }
}
fn wor(n: u32, wi: u32, m: u32) {
    if (pu(22u + n) == 3u) {
        atomicOr(&w0[wi], m);
    } else {
        atomicOr(&w1[wi], m);
    }
}
fn wst(n: u32, wi: u32, v: u32) {
    if (pu(22u + n) == 3u) {
        atomicStore(&w0[wi], v);
    } else {
        atomicStore(&w1[wi], v);
    }
}

fn ld8(n: u32, o: u32) -> u32 {
    let a = o + pu(16u + n);
    return (wd(n, a >> 2u) >> ((a & 3u) << 3u)) & 255u;
}
fn ldu32(n: u32, o: u32) -> u32 {
    let a = o + pu(16u + n);
    if ((a & 3u) == 0u) { return wd(n, a >> 2u); }
    return ld8(n, o) | (ld8(n, o + 1u) << 8u) | (ld8(n, o + 2u) << 16u) | (ld8(n, o + 3u) << 24u);
}
fn ld16(n: u32, o: u32) -> i32 {
    return bitcast<i32>((ld8(n, o) | (ld8(n, o + 1u) << 8u)) << 16u) >> 16u;
}
fn ld32(n: u32, o: u32) -> i32 { return bitcast<i32>(ldu32(n, o)); }
fn ld64(n: u32, o: u32) -> zw { return w_make(ldu32(n, o), ldu32(n, o + 4u)); }
fn st8(n: u32, o: u32, v: u32) {
    let a = o + pu(16u + n);
    let s = (a & 3u) << 3u;
    wand(n, a >> 2u, ~(255u << s));
    wor(n, a >> 2u, (v & 255u) << s);
}
fn st16(n: u32, o: u32, v: u32) {
    st8(n, o, v);
    st8(n, o + 1u, v >> 8u);
}
fn st32(n: u32, o: u32, v: u32) {
    let a = o + pu(16u + n);
    if ((a & 3u) == 0u) {
        wst(n, a >> 2u, v);
        return;
    }
    st16(n, o, v);
    st16(n, o + 2u, v >> 16u);
}
fn st64(n: u32, o: u32, v: zw) {
    st32(n, o, w_lo(v));
    st32(n, o + 4u, w_hi(v));
}

const ZV_I32_MAX: i32 = 2147483647;
const ZV_I32_MIN: i32 = -2147483647 - 1;

fn zm_sat32(v: zl) -> i32 {
    if (l_gt(v, l_i(ZV_I32_MAX))) { return ZV_I32_MAX; }
    if (l_lt(v, l_i(ZV_I32_MIN))) { return ZV_I32_MIN; }
    return l_lo(v);
}

fn zm_term52(v0: zl, e: i32) -> zl {
    var v = v0;
    let lim = l_p2(52u);
    let nlim = l_neg(lim);
    let z = l_i(0);
    if (l_eq(v, z)) { return z; }
    if (e <= 0) {
        if (e < -62) { return l_i(select(0, -1, l_lt(v, z))); }
        v = l_sar(v, u32(-e));
    } else {
        if (e > 52) { return l_sel(lim, nlim, l_lt(v, z)); }
        let m = l_sar(lim, u32(e));
        if (l_gt(v, m) || l_lt(v, l_neg(m))) { return l_sel(lim, nlim, l_lt(v, z)); }
        v = l_shl(v, u32(e)); // v * 2^e: in range, as checked
    }
    if (l_gt(v, lim)) { return lim; }
    if (l_lt(v, nlim)) { return nlim; }
    return v;
}

fn zm_round(acc: zl) -> i32 { return zm_sat32(l_sar(l_add(acc, l_i(32768)), 16u)); }

fn zm_f16_m(d: u32) -> i32 {
    let ex = (d >> 10u) & 31u;
    var m = i32(d & 0x3FFu);
    if (ex == 31u) { return 0; }
    if (ex != 0u) { m |= 0x400; }
    return select(m, -m, (d & 0x8000u) != 0u);
}

fn zm_f16_e(d: u32) -> i32 {
    let ex = (d >> 10u) & 31u;
    if (ex == 31u) { return 0; }
    return select(-24, i32(ex) - 25, ex != 0u);
}

// ---- products: weights in argument 0 (byte offsets), activations in 1 ----

fn zm_u16_w(o: u32) -> u32 { return ld8(0u, o) | (ld8(0u, o + 1u) << 8u); }
fn sx8(b: u32) -> i32 { return bitcast<i32>(b << 24u) >> 24u; }

fn zm_dot32_q4(q: u32, hi: u32, x: u32) -> i32 {
    var s = 0;
    for (var i = 0u; i < 32u; i++) {
        s += i32((ld8(0u, q + i) >> hi) & 15u) * ld16(1u, x + 2u * i);
    }
    return s;
}

fn zm_q4k_sub(blk: u32, j: u32, x: u32, sh: i32, asum: i32) -> zl {
    let d = zm_u16_w(blk);
    let dmin = zm_u16_w(blk + 2u);
    let p = blk + 4u;
    var sc: u32;
    var mn: u32;
    if (j < 4u) {
        sc = ld8(0u, p + j) & 63u;
        mn = ld8(0u, p + j + 4u) & 63u;
    } else {
        sc = (ld8(0u, p + j + 4u) & 15u) | ((ld8(0u, p + j - 4u) >> 6u) << 4u);
        mn = (ld8(0u, p + j + 4u) >> 4u) | ((ld8(0u, p + j) >> 6u) << 4u);
    }
    let s = zm_dot32_q4(blk + 16u + (j / 2u) * 32u, (j & 1u) * 4u, x);
    return l_sub(zm_term52(l_mul(l_mul32(s, i32(sc)), l_i(zm_f16_m(d))), zm_f16_e(d) + 16 + sh),
                 zm_term52(l_mul(l_mul32(asum, i32(mn)), l_i(zm_f16_m(dmin))), zm_f16_e(dmin) + 16 + sh));
}

fn zm_q6k_sub(blk: u32, j: u32, x: u32, sh: i32) -> zl {
    let h = j / 4u;
    let quarter = j % 4u;
    let ql = blk + h * 64u + (quarter & 1u) * 32u;
    let qh = blk + 128u + h * 32u;
    let s4 = select(0u, 4u, (quarter & 2u) != 0u);
    let s2 = 2u * quarter;
    var s0 = 0;
    var s1 = 0;
    for (var l = 0u; l < 16u; l++) {
        s0 += (i32(((ld8(0u, ql + l) >> s4) & 15u) | (((ld8(0u, qh + l) >> s2) & 3u) << 4u)) - 32) *
              ld16(1u, x + 2u * l);
        s1 += (i32(((ld8(0u, ql + l + 16u) >> s4) & 15u) | (((ld8(0u, qh + l + 16u) >> s2) & 3u) << 4u)) - 32) *
              ld16(1u, x + 2u * (l + 16u));
    }
    let d = zm_u16_w(blk + 208u);
    let sc0 = sx8(ld8(0u, blk + 192u + 2u * j));
    let sc1 = sx8(ld8(0u, blk + 193u + 2u * j));
    let v = l_add(l_mul32(sc0, s0), l_mul32(sc1, s1));
    return zm_term52(l_mul(v, l_i(zm_f16_m(d))), zm_f16_e(d) + 16 + sh);
}

fn zm_q80_sub(blk: u32, x: u32, sh: i32) -> zl {
    var s = 0;
    for (var i = 0u; i < 32u; i++) {
        s += sx8(ld8(0u, blk + 2u + i)) * ld16(1u, x + 2u * i);
    }
    let d = zm_u16_w(blk);
    return zm_term52(l_mul32(s, zm_f16_m(d)), zm_f16_e(d) + 16 + sh);
}

fn zm_sub(qt: u32, row: u32, b: u32, x: u32, sh: i32, asum: i32) -> zl {
    if (qt == 12u) { return zm_q4k_sub(row + (b / 8u) * 144u, b % 8u, x, sh, asum); }
    if (qt == 14u) { return zm_q6k_sub(row + (b / 8u) * 210u, b % 8u, x, sh); }
    return zm_q80_sub(row + b * 34u, x, sh);
}

// ---- the layer kernels' helpers ----

fn zm_uabs(v: i32) -> u32 { return select(u32(v), 0u - u32(v), v < 0); }

fn zm_isqrt(v0: zw) -> zw {
    var v = v0;
    var r = w_u(0u);
    var bit = w_p2(62u);
    while (w_gt(bit, v)) { bit = w_shr(bit, 2u); }
    while (w_nz(bit)) {
        let rb = w_add(r, bit);
        if (w_ge(v, rb)) {
            v = w_sub(v, rb);
            r = w_add(w_shr(r, 1u), bit);
        } else {
            r = w_shr(r, 1u);
        }
        bit = w_shr(bit, 2u);
    }
    return w_u(w_lo(r));
}

fn zm_w64(k: u32) -> zw { return w_make(pu(k), pu(k + 1u)); }

// the exp2 table is argument 2 in both kernels that use it
fn zm_exp(x0: i32) -> i32 {
    var x = x0;
    if (x > 0) { x = 0; }
    let t = l_sar(l_mul32(x, 94548), 16u);
    let ip = l_sar(t, 16u);
    let f = u32(l_lo(l_sub(t, l_shl(ip, 16u)))); // t - ip * 65536
    let hi = f >> 8u;
    let lo = f & 255u;
    let e0 = ldu32(2u, 4u * hi);
    let e1 = ldu32(2u, 4u * (hi + 1u));
    let m = e0 + (((e1 - e0) * lo + 128u) >> 8u);
    let sh = l_neg(ip);
    if (l_ge(sh, l_i(32))) { return 0; }
    if (l_le(sh, l_i(0))) { return i32(m); }
    let s = u32(l_lo(sh));
    return i32((m + (1u << (s - 1u))) >> s);
}

fn zm_mul_shr(a: zl, b: u32, sh: u32) -> zl {
    let lim = w_p2(58u);
    var r: zw;
    let neg = l_lt(a, l_i(0));
    let u = w_l(l_sel(a, l_neg(a), neg));
    let hi = w_mul32(w_hi(u), b);
    let lo = w_mul32(w_lo(u), b);
    if (sh >= 32u) {
        let t = w_add(hi, w_shr(lo, 32u));
        if (sh - 32u >= 64u) {
            r = w_u(0u);
        } else {
            r = w_shr(t, sh - 32u);
        }
    } else if (w_nz(w_shr(hi, 26u + sh)) || w_gt(w_shr(lo, sh), lim)) {
        r = lim;
    } else {
        r = w_add(w_shl(hi, 32u - sh), w_shr(lo, sh));
    }
    if (w_gt(r, lim)) { r = lim; }
    return l_sel(l_w(r), l_neg(l_w(r)), neg);
}

fn zm_round_div(n: zl, d: zw) -> zl {
    let neg = l_lt(n, l_i(0));
    let u = w_l(l_sel(n, l_neg(n), neg));
    let q = l_w(zm_udiv(w_add(u, w_shr(d, 1u)), d));
    return l_sel(q, l_neg(q), neg);
}

fn zm_count(n: u32) {
    if (n != 0u) { atomicAdd(&ctr, n); }
}

// ---- the layer kernels (zt_metal_math.h zk_*), one output per thread ----

fn zk0(i: u32) { // rms_inv
    let x = 4u * (i * pu(2u));
    let n = pu(1u);
    let eps = zm_w64(3u);
    var amax = 0u;
    for (var j = 0u; j < n; j++) { amax = max(amax, zm_uabs(ld32(0u, x + 4u * j))); }
    let bb = 2u * zm_bitlen(w_u(amax)) + zm_bitlen(w_u(n));
    let sh = select(0u, bb - 60u, bb > 60u);
    var ss = w_u(0u);
    for (var j = 0u; j < n; j++) {
        let v = ld32(0u, x + 4u * j);
        ss = w_add(ss, w_shr(w_l(l_mul32(v, v)), sh));
    }
    var M = zm_udiv(ss, w_u(n));
    let F = 32 - i32(sh);
    M = w_add(M, w_shr(eps, u32(48 - F)));
    if (!w_nz(M)) {
        st64(1u, 8u * i, w_u(0u));
        return;
    }
    var k = 62 - i32(zm_bitlen(M));
    if (((k + F) & 1) != 0) { k--; }
    let r = zm_isqrt(w_shl(M, u32(k)));
    let N = 32 + (k + F) / 2;
    let q0 = zm_udiv(w_p2(63u), r);
    var inv: zw;
    if (N >= 63) {
        let up = u32(N - 63);
        if (up >= 63u || w_nz(w_shr(q0, 62u - up))) {
            inv = w_p2(62u);
        } else {
            inv = w_shl(q0, up);
        }
    } else {
        inv = w_shr(q0, u32(63 - N));
    }
    st64(1u, 8u * i, inv);
}

fn zk1(i: u32) { // rms_apply
    let n = pu(1u);
    let row = i / n;
    let col = i % n;
    let frac = pu(3u);
    let xv = ld32(0u, 4u * (row * pu(2u) + col));
    let inv = l_w(ld64(1u, 8u * row));
    var t = l_sar(l_add(l_mul(l_i(xv), inv), l_p2(31u)), 32u);
    let lim = l_p2(32u);
    if (l_gt(t, lim)) { t = lim; }
    if (l_lt(t, l_neg(lim))) { t = l_neg(lim); }
    var hw = l_i(0);
    if (frac != 0u) { hw = l_p2(frac - 1u); }
    let g = l_i(ld32(2u, 4u * col));
    st32(3u, 4u * (row * pu(4u) + col), bitcast<u32>(zm_sat32(l_sar(l_add(l_mul(t, g), hw), frac))));
}

fn zk2(i: u32) { // act: 32 values to int16 with a shift
    let n = pu(1u);
    let nb = n / 32u;
    let row = i / nb;
    let blk = i % nb;
    let v = 4u * (row * pu(2u) + blk * 32u);
    let q = 2u * (i * 32u);
    var amax = 0u;
    for (var j = 0u; j < 32u; j++) { amax = max(amax, zm_uabs(ld32(0u, v + 4u * j))); }
    var s = 0u;
    while (s < 17u && w_gt(w_shr(w_add(w_u(amax), w_shr(w_p2(s), 1u)), s), w_u(32767u))) { s++; }
    let hw = (1u << s) >> 1u;
    var t = 0;
    for (var j = 0u; j < 32u; j++) {
        let vj = ld32(0u, v + 4u * j);
        let r = w_lo(w_shr(w_add(w_u(zm_uabs(vj)), w_u(hw)), s));
        let qv = (select(i32(r), -i32(r), vj < 0) << 16u) >> 16u; // (zs) truncation
        st16(1u, q + 2u * j, bitcast<u32>(qv));
        t += qv;
    }
    st8(2u, i, s);
    st32(3u, 4u * i, bitcast<u32>(t));
}

fn zk4(i: u32) { // bias, and the saturation count
    var bv = l_i(0);
    if (pu(2u) != 0u) { bv = l_i(ld32(1u, 4u * (i % pu(1u)))); }
    let v = l_add(l_i(ld32(0u, 4u * i)), bv);
    zm_count(select(0u, 1u, l_ge(v, l_i(ZV_I32_MAX)) || l_le(v, l_i(ZV_I32_MIN))));
    st32(0u, 4u * i, bitcast<u32>(zm_sat32(v)));
}

fn zk5(i: u32) { // rope
    let nh = pu(1u);
    let hd = pu(2u);
    let hw = pu(3u);
    let b = i / (nh * hw);
    let h = (i / hw) % nh;
    let k = i % hw;
    let x = 4u * ((b * nh + h) * hd);
    let sc = 4u * ((b * hw + k) * 2u);
    let ia = select(2u * k, k, pu(4u) != 0u);
    let ib = select(2u * k + 1u, k + hw, pu(4u) != 0u);
    let x0 = ld32(0u, x + 4u * ia);
    let x1 = ld32(0u, x + 4u * ib);
    let s = ld32(1u, sc);
    let c = ld32(1u, sc + 4u);
    let rnd = l_p2(29u);
    st32(0u, x + 4u * ia, bitcast<u32>(zm_sat32(l_sar(l_add(l_sub(l_mul32(x0, c), l_mul32(x1, s)), rnd), 30u))));
    st32(0u, x + 4u * ib, bitcast<u32>(zm_sat32(l_sar(l_add(l_add(l_mul32(x0, s), l_mul32(x1, c)), rnd), 30u))));
}

fn zk6(i: u32) { // kv store
    let kvd = pu(1u);
    let b = i / kvd;
    let j = i % kvd;
    let cell = (pu(2u) + b) * kvd + j;
    st32(2u, 4u * cell, ldu32(0u, 4u * i));
    st32(3u, 4u * cell, ldu32(1u, 4u * i));
}

fn zk7(i: u32) { // att_q: scale the query to under 2^22
    let hd = pu(2u);
    let qh = 4u * (i * hd);
    let qs = 4u * (i * hd);
    var qmax = 0u;
    var sq = 0u;
    for (var j = 0u; j < hd; j++) { qmax = max(qmax, zm_uabs(ld32(0u, qh + 4u * j))); }
    while ((qmax >> sq) >= (1u << 22u)) { sq++; }
    for (var j = 0u; j < hd; j++) {
        let v = ld32(0u, qh + 4u * j);
        var o = v;
        if (sq != 0u) { o = l_lo(l_sar(l_add(l_i(v), l_p2(sq - 1u)), sq)); }
        st32(1u, qs + 4u * j, bitcast<u32>(o));
    }
    st32(2u, 4u * i, sq);
}

fn zk8(i: u32) { // att_score
    let nh = pu(1u);
    let hd = pu(2u);
    let P = pu(5u);
    let item = i / P;
    let pos = i % P;
    let b = item / nh;
    let h = item % nh;
    if (pos > pu(6u) + b) { return; }
    let qs = 4u * (item * hd);
    let kr = 4u * ((pu(7u) + pos) * pu(4u) + (h / pu(3u)) * hd);
    var S = l_i(0);
    for (var j = 0u; j < hd; j++) { S = l_add(S, l_mul32(ld32(0u, qs + 4u * j), ld32(3u, kr + 4u * j))); }
    let sq = ldu32(1u, 4u * item);
    st32(2u, 4u * i, bitcast<u32>(zm_sat32(zm_mul_shr(S, pu(8u), 46u - sq))));
}

fn zk9(i: u32) { // att_max
    let nh = pu(1u);
    let P = pu(5u);
    let n = pu(6u) + i / nh + 1u;
    let sc = 4u * (i * P);
    var mx = ZV_I32_MIN;
    for (var j = 0u; j < n; j++) { mx = max(mx, ld32(0u, sc + 4u * j)); }
    st32(1u, 4u * i, bitcast<u32>(mx));
}

fn zk10(i: u32) { // att_exp
    let nh = pu(1u);
    let P = pu(5u);
    let item = i / P;
    let pos = i % P;
    if (pos > pu(6u) + item / nh) { return; }
    let d = l_sub(l_i(ld32(0u, 4u * i)), l_i(ld32(1u, 4u * item)));
    st32(0u, 4u * i, bitcast<u32>(zm_exp(select(l_lo(d), ZV_I32_MIN, l_lt(d, l_i(ZV_I32_MIN))))));
}

fn zk11(i: u32) { // att_sum
    let nh = pu(1u);
    let P = pu(5u);
    let n = pu(6u) + i / nh + 1u;
    let w = 4u * (i * P);
    var sum = w_u(0u);
    for (var j = 0u; j < n; j++) { sum = w_add(sum, w_u(ldu32(0u, w + 4u * j))); }
    st64(1u, 8u * i, sum);
}

fn zk12(i: u32) { // att_wv
    let nh = pu(1u);
    let hd = pu(2u);
    let P = pu(5u);
    let item = i / hd;
    let j = i % hd;
    let b = item / nh;
    let h = item % nh;
    let n = pu(6u) + b + 1u;
    let w = 4u * (item * P);
    let v = 4u * (pu(7u) * pu(4u) + (h / pu(3u)) * hd + j);
    var acc = l_i(0);
    for (var k = 0u; k < n; k++) {
        let wk = ld32(0u, w + 4u * k);
        if (wk != 0) { acc = l_add(acc, l_mul32(wk, ld32(3u, v + 4u * (k * pu(4u))))); }
    }
    st32(2u, 4u * i, bitcast<u32>(zm_sat32(zm_round_div(acc, ld64(1u, 8u * item)))));
}

fn zk13(i: u32) { // residual, and the saturation count
    let v = l_add(l_i(ld32(0u, 4u * i)), l_i(ld32(1u, 4u * i)));
    zm_count(select(0u, 1u, l_gt(v, l_i(ZV_I32_MAX)) || l_lt(v, l_i(ZV_I32_MIN))));
    st32(0u, 4u * i, bitcast<u32>(zm_sat32(v)));
}

fn zk14(i: u32) { // silu(gate) * up
    let g = ld32(0u, 4u * i);
    let u = ld32(1u, 4u * i);
    if (g == 0) {
        st32(0u, 4u * i, 0u);
        return;
    }
    let e = u32(zm_exp(select(-g, g, g < 0)));
    var sig = 0xFFFFFFFFu / (65536u + e); // at most 65536: a non-negative int
    if (g < 0) { sig = 65536u - sig; }
    let s = l_sar(l_add(l_mul32(g, i32(sig)), l_i(32768)), 16u);
    st32(0u, 4u * i, bitcast<u32>(zm_sat32(l_sar(l_add(l_mul(s, l_i(u)), l_i(32768)), 16u))));
}

// thread i of a layer kernel (a 2-D grid past the per-dimension limit)
fn tid(wg: vec3<u32>, li: vec3<u32>) -> u32 {
    return (wg.y * pu(28u) + wg.x) * ZW_WG + li.x;
}

@compute @workgroup_size(ZW_WG)
fn k0(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk0(i); }
}
@compute @workgroup_size(ZW_WG)
fn k1(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk1(i); }
}
@compute @workgroup_size(ZW_WG)
fn k2(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk2(i); }
}
@compute @workgroup_size(ZW_WG)
fn k4(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk4(i); }
}
@compute @workgroup_size(ZW_WG)
fn k5(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk5(i); }
}
@compute @workgroup_size(ZW_WG)
fn k6(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk6(i); }
}
@compute @workgroup_size(ZW_WG)
fn k7(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk7(i); }
}
@compute @workgroup_size(ZW_WG)
fn k8(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk8(i); }
}
@compute @workgroup_size(ZW_WG)
fn k9(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk9(i); }
}
@compute @workgroup_size(ZW_WG)
fn k10(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk10(i); }
}
@compute @workgroup_size(ZW_WG)
fn k11(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk11(i); }
}
@compute @workgroup_size(ZW_WG)
fn k12(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk12(i); }
}
@compute @workgroup_size(ZW_WG)
fn k13(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk13(i); }
}
@compute @workgroup_size(ZW_WG)
fn k14(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    let i = tid(wg, li);
    if (i < pu(0u)) { zk14(i); }
}

// ---- the products: ZW_R rows a workgroup, ZW_L threads a row, tiles of
// ZW_T tokens. pu: 0 rows (this dispatch), 1 nb, 2 astride, 3 B, 4 rowbytes,
// 5 ystride (rows of the whole product), 6 row0 (this dispatch's first row
// in y). Arguments: 0 weights (from row0), 1 a (int16), 2 shifts, 3 sums,
// 4 y. Each thread sums its blocks, then thread t of each row adds the row's
// ZW_L partial sums of token t: exact, so any sizes give the same bits. ----

const ZW_MW: u32 = ZW_R * ZW_L;
var<workgroup> red: array<vec2<u32>, ZW_T * ZW_MW>; // zl values, as their two words

fn mm(qt: u32, wg: vec3<u32>, li: vec3<u32>) {
    let lid = li.x;
    let sg = lid / ZW_L;
    let lane = lid % ZW_L;
    let r = wg.x * ZW_R + sg;
    let rows = pu(0u);
    let nb = pu(1u);
    let as_ = pu(2u);
    let B = pu(3u);
    let sb = as_ / 32u;
    let t0 = wg.y * ZW_T;
    let nt = min(ZW_T, B - t0);
    let live = r < rows;
    var acc: array<zl, ZW_T>;
    for (var t = 0u; t < ZW_T; t++) { acc[t] = l_i(0); }
    if (live) {
        let row = r * pu(4u);
        for (var b = lane; b < nb; b += ZW_L) {
            for (var t = 0u; t < ZW_T; t++) {
                if (t < nt) {
                    let tok = t0 + t;
                    acc[t] = l_add(acc[t], zm_sub(qt, row, b, 2u * (tok * as_ + b * 32u),
                                                  i32(ld8(2u, tok * sb + b)), ld32(3u, 4u * (tok * sb + b))));
                }
            }
        }
    }
    for (var t = 0u; t < ZW_T; t++) { red[t * ZW_MW + lid] = acc[t].v; }
    workgroupBarrier();
    if (live && lane < nt) {
        var s = l_i(0);
        for (var l = 0u; l < ZW_L; l++) { s = l_add(s, zl(red[lane * ZW_MW + sg * ZW_L + l])); }
        st32(4u, 4u * ((t0 + lane) * pu(5u) + pu(6u) + r), bitcast<u32>(zm_round(s)));
    }
}

@compute @workgroup_size(ZW_MW)
fn mm_q4k(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    mm(12u, wg, li);
}
@compute @workgroup_size(ZW_MW)
fn mm_q6k(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    mm(14u, wg, li);
}
@compute @workgroup_size(ZW_MW)
fn mm_q80(@builtin(workgroup_id) wg: vec3<u32>, @builtin(local_invocation_id) li: vec3<u32>) {
    mm(8u, wg, li);
}
