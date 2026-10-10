/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_adapter_house.c — the adapter-house spec's four tests over all five
 * adapters, plus each sub-adapter's own rules (zt_adapters.h). */
#include "zt_htest.h"
#include "zt_adapters.h"
#include "zt_phi.h"
#include "../swarm/swarm_enochian.h"

_Static_assert(sizeof(zt_ubh168_frame_t) == 21, "T1 frame is 21 octets on every target");

#define MAXF 1024u
static zt_ubh168_frame_t fr[MAXF];
static uint8_t inb[4096], outb[8192];

static uint32_t lcg(uint32_t *s)
{
    *s = *s * 1664525u + 1013904223u;
    return *s >> 8;
}

static bool same(const uint8_t *a, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static bool str_has(const char *s, const char *sub)
{
    for (; *s; s++) {
        size_t k = 0;
        while (sub[k] && s[k] == sub[k]) k++;
        if (!sub[k]) return true;
    }
    return false;
}

/* T2: lossless pump_in -> drain_out(TRUE) on the byte path. */
static void t2_lossless(void)
{
    zt_adapter_t a;
    zt_adapter_bytes_init(&a, 3);
    uint32_t seed = 1;
    bool ok = true;
    for (size_t len = 0; len <= 301u; len++) {
        for (size_t i = 0; i < len; i++) inb[i] = (uint8_t) lcg(&seed);
        int nf = a.pump_in(&a, inb, len, fr, MAXF);
        ok = ok && nf == (int) zt_adapter_frames_for(len);
        int nb = a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, outb, sizeof outb);
        ok = ok && nb == (int) len && same(inb, outb, len);
    }
    HT_CHECK(ok, "T2 bytes: 302 lengths round trip bit-exact");
    /* a flipped bit is caught */
    a.pump_in(&a, inb, 100, fr, MAXF);
    uint8_t o[21];
    zt_wire_to_octets(&fr[3], o);
    o[10] ^= 0x10u;
    zt_wire_from_octets(o, &fr[3]);
    HT_CHECK(a.drain_out(&a, fr, 6, ZT_TRUTH_TRUE, outb, sizeof outb) == ZT_ADAPTER_EFORMAT,
             "T2 corrupted frame rejected by the checksum");
    HT_CHECK(a.pump_in(&a, inb, 100, fr, 3) == ZT_ADAPTER_ESPACE, "T2 frame budget enforced");
    a.pump_in(&a, inb, 100, fr, MAXF);
    HT_CHECK(a.drain_out(&a, fr, 6, ZT_TRUTH_GLUT, outb, sizeof outb) == ZT_ADAPTER_EHELD &&
                 a.drain_out(&a, fr, 6, ZT_TRUTH_FALSE, outb, sizeof outb) == 0,
             "A3 byte path: GLUT refused, FALSE drains nothing");
}

/* T3: A = 1 and not-A = 1 on every adapter. */
static void t3_explosion(void)
{
    zt_adapter_t ad[5];
    zt_tensor_adapter_state_t ts = {ZT_DTYPE_FP32, 1, 1, 0, 0, false, 0, 0};
    zt_graphics_adapter_state_t gs = {4, 4, 0, 0};
    zt_audio_adapter_state_t as;
    zt_text_adapter_state_t xs;
    zt_adapter_bytes_init(&ad[0], 0);
    zt_adapter_tensor_init(&ad[1], &ts, 1);
    zt_adapter_graphics_init(&ad[2], &gs, 2);
    zt_adapter_audio_init(&ad[3], &as, 3);
    HT_CHECK(zt_adapter_text_init(&ad[4], &xs, 4, "ABC", 3), "text init");
    zt_ubh168_frame_t A, notA;
    zt_adapter_claim_frame(7, 42, 0x10000, &A);
    zt_adapter_claim_frame(7, 42, 0x10000, &notA);
    bool ok = true, para = true;
    for (int i = 0; i < 5; i++) {
        zt_truth_state_t s1 = ad[i].evaluate_interference(&ad[i], &A, &notA);
        zt_truth_state_t s2 = ad[i].evaluate_interference(&ad[i], &A, &notA);
        zt_truth_state_t s3 = ad[i].evaluate_interference(&ad[i], &A, &notA);
        ok = ok && s1 == ZT_TRUTH_GLUT && s2 == ZT_TRUTH_GLUT;
        para = para && s3 == ZT_TRUTH_PARADOX;
    }
    HT_CHECK(ok, "T3 A=1 and not-A=1 -> GLUT on all five adapters");
    HT_CHECK(para, "T3 the third consecutive collision -> PARADOX");
    zt_adapter_claim_frame(7, 42, 0, &notA);
    HT_CHECK(ad[0].evaluate_interference(&ad[0], &A, &notA) == ZT_TRUTH_TRUE, "A only -> TRUE");
    zt_adapter_claim_frame(7, 43, 0x10000, &notA);
    HT_CHECK(ad[0].evaluate_interference(&ad[0], &A, &notA) == ZT_TRUTH_UNKNOWN,
             "claims on different propositions -> UNKNOWN");
    HT_CHECK(ad[0].evaluate_interference(&ad[0], 0, &A) == ZT_TRUTH_UNKNOWN, "NULL is safe");
}

/* T4: words[0] little-endian, words[1] big-endian, on any host. */
static void t4_commutation(void)
{
    zt_adapter_t a;
    zt_adapter_bytes_init(&a, 5);
    for (uint32_t i = 0; i < 20; i++) inb[i] = (uint8_t) (0x10 + i);
    a.pump_in(&a, inb, 20, fr, MAXF);
    uint8_t o[21];
    zt_wire_to_octets(&fr[1], o);
    static const uint8_t want[21] = {5,    0x10, 0x11, 0x12, 0x13, 0x17, 0x16,
                                     0x15, 0x14, 0x18, 0x19, 0x1A, 0x1B, 0x1F,
                                     0x1E, 0x1D, 0x1C, 0x20, 0x21, 0x22, 0x23};
    HT_CHECK(same(o, want, 21), "T4 data frame octets: L-B-L-B-L, identical on every host");
}

static void tensor(void)
{
    uint32_t fl = 0;
    HT_CHECK(zt_fp32_bits_to_q16(0x3F800000u, &fl) == 0x10000, "1.0f -> 0x10000");
    HT_CHECK(zt_fp32_bits_to_q16(0xC0200000u, &fl) == -0x28000, "-2.5f");
    HT_CHECK(zt_fp32_bits_to_q16(0x37000000u, &fl) == 1 &&
                 zt_fp32_bits_to_q16(0xB7000000u, &fl) == -1,
             "2^-17 (half an LSB) rounds away from zero");
    HT_CHECK(zt_fp32_bits_to_q16(0x36800000u, &fl) == 0, "2^-18 -> 0");
    fl = 0;
    HT_CHECK(zt_fp32_bits_to_q16(0x471C4000u, &fl) == INT32_MAX && (fl & ZT_Q16_SAT),
             "40000.0f saturates");
    fl = 0;
    HT_CHECK(zt_fp32_bits_to_q16(0x7FC00000u, &fl) == 0 && (fl & ZT_Q16_NAN), "NaN -> 0, flagged");
    HT_CHECK(zt_fp32_bits_to_q16(0xFF800000u, &fl) == INT32_MIN, "-inf saturates");
    HT_CHECK(zt_fp32_bits_to_q16(0x00000001u, &fl) == 0, "subnormal -> 0");
    HT_CHECK(zt_fp32_bits_to_q16(0xC7000000u, &fl) == INT32_MIN, "-32768.0f is exact");
    HT_CHECK(zt_bf16_bits_to_q16(0x3FC0, &fl) == 0x18000, "bf16 1.5");
    HT_CHECK(zt_fp16_bits_to_q16(0x3C00, &fl) == 0x10000 &&
                 zt_fp16_bits_to_q16(0xC500, &fl) == -0x50000,
             "fp16 1.0 and -5.0");
    HT_CHECK(zt_fp16_bits_to_q16(0x0001, &fl) == 0 && zt_fp16_bits_to_q16(0x0080, &fl) == 1,
             "fp16 subnormals 2^-24 -> 0, 2^-17 -> 1");
    /* quantisation error on a sweep: |q - x * 65536| <= 1/2 */
    bool err_ok = true;
    for (uint32_t k = 1; k < 200000u; k += 7u) {
        /* x = k / 2^20, exactly representable: bits from the integer */
        uint32_t e = 0, m = k;
        while (m >= (1u << 24)) m >>= 1, e++;
        while (m < (1u << 23)) m <<= 1, e--;
        uint32_t bits = (uint32_t) ((int32_t) 127 + 23 - 20 + (int32_t) e) << 23 | (m & 0x7FFFFFu);
        int32_t q = zt_fp32_bits_to_q16(bits, &fl);
        int64_t twice = (int64_t) q * 32 - (int64_t) k * 2; /* 2 * (q - k/16) * 16 */
        err_ok = err_ok && twice <= 16 && twice >= -16;
    }
    HT_CHECK(err_ok, "FP32 -> Q16.16 error <= 2^-17 (half an LSB) over a sweep");
    /* phi powers and snapping */
    HT_CHECK(zt_phi_pow_q16(0) == 0x10000 && zt_phi_pow_q16(1) == 106039 &&
                 zt_phi_pow_q16(-1) == 40503 && zt_phi_pow_q16(21) == 1604059139 &&
                 zt_phi_pow_q16(-24) == 1,
             "phi^n = F(n) phi + F(n-1) in Q16");
    HT_CHECK(zt_fib(-6) == -8 && zt_fib(-5) == 5 && zt_fib(10) == 55, "negafibonacci");
    int32_t n;
    int32_t s = zt_phi_snap_q16(6554, &n); /* 0.1 */
    HT_CHECK(n == -5 && s == zt_phi_pow_q16(-5), "0.1 snaps to phi^-5 = 0.0902");
    ht_puts("  phi snap: scale 0.1000 -> ");
    ht_putd((int32_t) (((uint32_t) s * 10000u) >> 16));
    ht_puts("e-4 (");
    ht_putd((s - 6554) * 1000 / 6554);
    ht_puts(" per mille): snapping is lossy\n");
    /* tiling */
    zt_tile_t t[64];
    uint32_t nt = zt_tensor_tile_plan(34, 34, t, 64);
    HT_CHECK(nt == 4 && t[0].nr == 21 && t[0].nc == 21 && t[3].nr == 13 && t[3].nc == 13,
             "34 x 34 tiles as 21+13 by 21+13");
    static uint8_t cover[200 * 150];
    for (uint32_t i = 0; i < 200u * 150u; i++) cover[i] = 0;
    static zt_tile_t big[4096];
    nt = zt_tensor_tile_plan(200, 150, big, 4096);
    bool fit = nt <= 4096;
    for (uint32_t i = 0; i < nt && i < 4096; i++) {
        fit = fit && big[i].nr <= 21 && big[i].nc <= 21 && big[i].nr && big[i].nc;
        for (uint32_t r = 0; r < big[i].nr; r++)
            for (uint32_t c = 0; c < big[i].nc; c++)
                cover[(big[i].r0 + r) * 150u + big[i].c0 + c]++;
    }
    bool once = true;
    for (uint32_t i = 0; i < 200u * 150u; i++) once = once && cover[i] == 1;
    HT_CHECK(fit && once, "200 x 150: every tile <= 21 x 21, every element exactly once");
    /* matrix round trip: values that are exact in Q16 come back exactly */
    zt_tensor_adapter_state_t st = {ZT_DTYPE_FP32, 40, 50, 0, 0, false, 0, 0};
    zt_adapter_t a;
    zt_adapter_tensor_init(&a, &st, 2);
    static uint8_t w[40 * 50 * 4];
    static int32_t q[40 * 50];
    uint32_t seed = 9;
    for (uint32_t i = 0; i < 2000u; i++) {
        int32_t v = (int32_t) (lcg(&seed) & 0xFFFFFu) - 0x80000; /* +-8.0 in 2^-16 steps */
        /* build the float bits of v / 65536 exactly */
        uint32_t sign = v < 0 ? 0x80000000u : 0u, mag = (uint32_t) (v < 0 ? -v : v), e = 0;
        uint32_t bits = 0;
        if (mag) {
            uint32_t m = mag;
            int32_t ex = 0;
            while (m >= (1u << 24)) m >>= 1, ex++;
            while (m < (1u << 23)) m <<= 1, ex--;
            e = (uint32_t) (127 + 23 - 16 + ex);
            bits = sign | e << 23 | (m & 0x7FFFFFu);
        }
        zt_st_le32(w + 4u * i, bits);
        q[i] = v;
    }
    int nf = a.pump_in(&a, w, sizeof w, fr, MAXF);
    HT_CHECK(nf == (int) zt_tensor_frames_for(2000), "tensor frames: header + ceil(n/3)");
    static int32_t back[40 * 50];
    int nb = a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, back, sizeof back);
    bool eq = nb == 8000;
    for (uint32_t i = 0; i < 2000u; i++) eq = eq && back[i] == q[i];
    HT_CHECK(eq, "40 x 50 FP32 matrix: tiled out, row-major back, exact for Q16 values");
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(&fr[5], &r);
    HT_CHECK(r.s_plus[0] + r.s_plus[1] + r.s_plus[2] + r.s_minus[1] == 0u,
             "each tensor frame sums to zero (T4 witness)");
    uint8_t o[21];
    zt_wire_to_octets(&fr[5], o);
    o[2] ^= 1u;
    zt_wire_from_octets(o, &fr[5]);
    HT_CHECK(a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, back, sizeof back) ==
                 ZT_ADAPTER_EFORMAT,
             "damaged tensor frame caught by its witness");
    /* INT8 with phi-snapped scales */
    static const int32_t scales[2] = {6554, 0x10000};
    zt_tensor_adapter_state_t s8 = {ZT_DTYPE_INT8, 1, 64, scales, 32, true, 0, 0};
    zt_adapter_tensor_init(&a, &s8, 2);
    for (uint32_t i = 0; i < 64; i++) inb[i] = (uint8_t) (int8_t) (i < 32 ? 10 : -3);
    nf = a.pump_in(&a, inb, 64, fr, MAXF);
    a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, back, sizeof back);
    HT_CHECK(back[0] == 10 * zt_phi_pow_q16(-5) && back[40] == -3 * 0x10000,
             "INT8 x snapped scale (0.1 -> phi^-5, 1.0 stays 1.0)");
}

static void graphics(void)
{
    static uint8_t img[8 * 8 * 4], prev[8 * 8 * 4], depth[64];
    for (uint32_t i = 0; i < sizeof img; i++) {
        img[i] = (uint8_t) (i * 7u + 1u);
        prev[i] = (uint8_t) (255u - i);
    }
    for (int i = 0; i < 64; i++) depth[i] = 9;
    zt_graphics_adapter_state_t st = {8, 8, prev, depth};
    zt_adapter_t a;
    zt_adapter_graphics_init(&a, &st, 6);
    int nf = a.pump_in(&a, img, sizeof img, fr, MAXF);
    int nb = a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, outb, sizeof outb);
    HT_CHECK(nb == (int) sizeof img && same(outb, img, sizeof img), "graphics TRUE: exact image");
    a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_FALSE, outb, sizeof outb);
    HT_CHECK(same(outb, prev, sizeof prev), "graphics FALSE: residual undoes exactly");
    a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_GLUT, outb, sizeof outb);
    bool board = true;
    for (uint32_t y = 0; y < 8; y++)
        for (uint32_t x = 0; x < 8; x++) {
            const uint8_t *want = ((x + y) & 1u) ? prev : img;
            board = board && same(outb + 4u * (y * 8u + x), want + 4u * (y * 8u + x), 4);
        }
    bool d0 = true;
    for (int i = 0; i < 64; i++) d0 = d0 && depth[i] == 0;
    HT_CHECK(board && d0, "graphics GLUT: checkerboard of both, S0 depth plane 0");
    /* opaque white against fully transparent: never grey */
    static uint8_t canvas[16 * 16 * 4];
    zt_gfx_render_conflict(canvas, 16, 0, 0, 16, 16, 0xFFFFFFFFu, 0x00000000u, ZT_TRUTH_GLUT);
    uint32_t na = 0, nb0 = 0, other = 0;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = zt_ld_le32(canvas + 4u * i);
        if (c == 0xFFFFFFFFu)
            na++;
        else if (c == 0)
            nb0++;
        else
            other++;
    }
    HT_CHECK(na == 128 && nb0 == 128 && other == 0, "conflicting alpha: 128/128 checkerboard, no "
                                                    "averaged pixel");
}

static void audio(void)
{
    static int16_t a1[2 * 300], b1[2 * 300], mix[2 * 300];
    for (int i = 0; i < 300; i++) {
        int32_t v = (i * 997 % 20000) - 10000;
        a1[2 * i] = (int16_t) v;
        a1[2 * i + 1] = (int16_t) (v / 2);
        b1[2 * i] = (int16_t) -v;
        b1[2 * i + 1] = (int16_t) (-v / 2);
    }
    uint32_t folds = 0;
    HT_CHECK(zt_audio_mix(a1, b1, 300, mix, &folds) == ZT_TRUTH_GLUT, "a + (-a): held, GLUT");
    bool kept = true;
    for (int i = 0; i < 300; i++)
        kept = kept && mix[2 * i] == (int16_t) (((int32_t) a1[2 * i] + a1[2 * i + 1]) >> 1) &&
               mix[2 * i + 1] == (int16_t) (((int32_t) b1[2 * i] + b1[2 * i + 1]) >> 1);
    HT_CHECK(kept, "cancelling pair kept apart (a on L, b on R), not summed to silence");
    /* full-scale overflow folds instead of clipping */
    for (int i = 0; i < 300; i++) {
        a1[2 * i] = a1[2 * i + 1] = (int16_t) (20000 + i * 40);
        b1[2 * i] = b1[2 * i + 1] = 20000;
    }
    folds = 0;
    HT_CHECK(zt_audio_mix(a1, b1, 300, mix, &folds) == ZT_TRUTH_TRUE, "same-sign sum is TRUE");
    bool no_plateau = true, monotone_fold = true;
    for (int i = 0; i < 300; i++) {
        no_plateau = no_plateau && mix[2 * i] < 32767;
        if (i) monotone_fold = monotone_fold && mix[2 * i] <= mix[2 * i - 2];
    }
    HT_CHECK(folds == 600 && no_plateau && monotone_fold,
             "overflow folds back below full scale (no clipped plateau)");
    HT_CHECK(zt_audio_soft_fold(40000, 0) == 32767 - (int16_t) ((7233 * 40503) >> 16),
             "fold: 40000 -> 32767 - 7233/phi");
    /* lossless PCM round trip and the side channel */
    zt_audio_adapter_state_t st;
    zt_adapter_t a;
    zt_adapter_audio_init(&a, &st, 4);
    uint32_t seed = 5;
    for (uint32_t i = 0; i < 1000; i++) inb[i] = (uint8_t) lcg(&seed);
    int nf = a.pump_in(&a, inb, 1000, fr, MAXF);
    int nb = a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, outb, sizeof outb);
    HT_CHECK(nb == 1000 && same(inb, outb, 1000), "audio TRUE: PCM bit-exact");
    a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_GLUT, outb, sizeof outb);
    int16_t l = (int16_t) (outb[0] | outb[1] << 8), rr = (int16_t) (outb[2] | outb[3] << 8);
    int16_t L0 = (int16_t) (inb[0] | inb[1] << 8), R0 = (int16_t) (inb[2] | inb[3] << 8);
    HT_CHECK(l == (((int32_t) L0 - R0) >> 1) && rr == -l, "audio GLUT drains the side channel");
    /* cancellation detector on data frames */
    zt_ubh168_frame_t fa, fb;
    const uint32_t s3[3] = {0x10002000u, 0x30004000u, 0x50006000u};
    const uint32_t n3[3] = {(uint32_t) (uint16_t) -0x2000 | (uint32_t) (uint16_t) -0x1000 << 16,
                            (uint32_t) (uint16_t) -0x4000 | (uint32_t) (uint16_t) -0x3000 << 16,
                            (uint32_t) (uint16_t) -0x6000 | (uint32_t) (uint16_t) -0x5000 << 16};
    const uint32_t z2[2] = {0, 0};
    zt_wire_pack_ubh168(4, s3, z2, &fa);
    zt_wire_pack_ubh168(4, n3, z2, &fb);
    a.conflict_run = 0;
    HT_CHECK(a.evaluate_interference(&a, &fa, &fb) == ZT_TRUTH_GLUT, "inverse signals: GLUT");
    HT_CHECK(a.evaluate_interference(&a, &fa, &fa) == ZT_TRUTH_TRUE, "same signal: TRUE");
}

static void text(void)
{
    HT_CHECK(zt_text_symbols_fit(25, 168) == 36 && zt_text_symbols_fit(26, 168) == 35,
             "25^36 < 2^168 < 26^36");
    HT_CHECK(zt_text_symbols_fit(21, 160) == 36 && zt_text_symbols_fit(22, 160) == 35 &&
                 zt_text_symbols_fit(25, 160) == 34,
             "tagged frame (160 bits): 36 symbols need base <= 21; base 25 gives 34");
    /* the Enochian alphabet: 23 letters + space (+ end mark = base 25) */
    char alpha[24];
    for (uint32_t i = 0; i < 23; i++) alpha[i] = swarm_en_letter(i);
    alpha[23] = ' ';
    zt_text_adapter_state_t st;
    zt_adapter_t a;
    HT_CHECK(zt_adapter_text_init(&a, &st, 8, alpha, 24) && st.base == 25 && st.per_frame == 34,
             "Enochian: base 25, 34 symbols per tagged frame");
    const char *msg = "OL SONF VORSG GOHO IAD BALT LANSH CALZ VONPHO";
    size_t len = 0;
    while (msg[len]) len++;
    uint8_t raw[63], ref[63];
    int nr = zt_text_pack_raw168(&st, msg, len, raw, sizeof raw);
    int nref = swarm_en_pack(msg, (uint32_t) len, ref, sizeof ref);
    HT_CHECK(nr == 42 && nref == 42 && same(raw, ref, 42),
             "raw 168-bit blocks: 36 symbols per 21 octets, byte-identical to swarm_en_pack");
    char back[128];
    HT_CHECK(zt_text_unpack_raw168(&st, raw, 42, back, sizeof back) == (int) len &&
                 same((const uint8_t *) back, (const uint8_t *) msg, len),
             "raw168 round trip");
    int nf = a.pump_in(&a, msg, len, fr, MAXF);
    int nb = a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_TRUE, back, sizeof back);
    HT_CHECK(nf == 3 && nb == (int) len && same((const uint8_t *) back, (const uint8_t *) msg, len),
             "tagged text frames round trip");
    nb = a.drain_out(&a, fr, (size_t) nf, ZT_TRUTH_GLUT, back, sizeof back - 1);
    back[nb > 0 ? nb : 0] = 0;
    HT_CHECK(nb == (int) len + 7 && str_has(back, "[GLUT] OL SONF"), "GLUT tags the utterance");
    HT_CHECK(a.pump_in(&a, "ol", 2, fr, MAXF) == ZT_ADAPTER_EDOMAIN,
             "outside the alphabet refused");
    /* a 21-symbol alphabet fits 36 per tagged frame */
    zt_text_adapter_state_t s21;
    HT_CHECK(zt_adapter_text_init(&a, &s21, 8, "ABCDEFGHIJKLMNOPQRST", 20) && s21.per_frame == 36,
             "20 letters + end mark = base 21: 36 symbols per tagged frame");
    HT_CHECK(!zt_adapter_text_init(&a, &s21, 8, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 26) &&
                 !zt_adapter_text_init(&a, &s21, 8, "AA", 2),
             "26 letters (base 27) and repeated letters refused");
    /* answers state both premises */
    char ans[256];
    zt_text_answer("the bridge is open", "the bridge is closed", ZT_TRUTH_GLUT, ans, sizeof ans);
    HT_CHECK(str_has(ans, "GLUT") && str_has(ans, "the bridge is open") &&
                 str_has(ans, "the bridge is closed"),
             "GLUT answer states both premises");
    zt_text_answer("p", "q", ZT_TRUTH_PARADOX, ans, sizeof ans);
    HT_CHECK(str_has(ans, "PARADOX") && str_has(ans, "A: p") && str_has(ans, "NOT A: q"),
             "PARADOX answer states both premises");
    HT_CHECK(zt_text_answer("p", "q", ZT_TRUTH_GLUT, ans, 10) == ZT_ADAPTER_ESPACE && ans[9] == 0,
             "answer bounded by cap, always terminated");
}

int main(void)
{
    t2_lossless();
    t3_explosion();
    t4_commutation();
    tensor();
    graphics();
    audio();
    text();
    return ht_finish("test_adapter_house");
}
