/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_serial.c — COBS against the published examples (Cheshire & Baker,
 * as listed in the Wikipedia "Consistent Overhead Byte Stuffing" article),
 * CRC-32 check values, exact frame bytes, and decoder resynchronisation
 * after garbage, corruption and over-long frames. */
#include <string.h>

#include "br_serial.h"
#include "test_check.h"

static uint8_t g_in[1100], g_enc[1200], g_dec[1200], g_exp[1200];

static int cobs_vec(const uint8_t *in, uint32_t n, const uint8_t *exp, uint32_t en)
{
    int32_t e = br_cobs_encode(in, n, g_enc, sizeof g_enc);
    if (e != (int32_t) en || memcmp(g_enc, exp, en)) return 0;
    int32_t d = br_cobs_decode(exp, en, g_dec, sizeof g_dec);
    return d == (int32_t) n && (n == 0 || !memcmp(g_dec, in, n));
}

static void test_cobs(void)
{
    /* expected values are without the trailing 0x00 delimiter */
    { uint8_t i[] = {0x00}, e[] = {0x01, 0x01};
      CHECK("COBS example 1: 00 -> 01 01", cobs_vec(i, 1, e, 2)); }
    { uint8_t i[] = {0x00, 0x00}, e[] = {0x01, 0x01, 0x01};
      CHECK("COBS example 2: 00 00 -> 01 01 01", cobs_vec(i, 2, e, 3)); }
    { uint8_t i[] = {0x00, 0x11, 0x00}, e[] = {0x01, 0x02, 0x11, 0x01};
      CHECK("COBS example 3: 00 11 00 -> 01 02 11 01", cobs_vec(i, 3, e, 4)); }
    { uint8_t i[] = {0x11, 0x22, 0x00, 0x33}, e[] = {0x03, 0x11, 0x22, 0x02, 0x33};
      CHECK("COBS example 4: 11 22 00 33 -> 03 11 22 02 33", cobs_vec(i, 4, e, 5)); }
    { uint8_t i[] = {0x11, 0x22, 0x33, 0x44}, e[] = {0x05, 0x11, 0x22, 0x33, 0x44};
      CHECK("COBS example 5: 11 22 33 44 -> 05 11 22 33 44", cobs_vec(i, 4, e, 5)); }
    { uint8_t i[] = {0x11, 0x00, 0x00, 0x00}, e[] = {0x02, 0x11, 0x01, 0x01, 0x01};
      CHECK("COBS example 6: 11 00 00 00 -> 02 11 01 01 01", cobs_vec(i, 4, e, 5)); }
    /* 7: 01..FE (254 bytes) -> FF 01..FE */
    for (int k = 0; k < 254; k++) g_in[k] = (uint8_t) (k + 1);
    g_exp[0] = 0xFF;
    for (int k = 0; k < 254; k++) g_exp[1 + k] = (uint8_t) (k + 1);
    CHECK("COBS example 7: 01..FE -> FF 01..FE (no trailing code)", cobs_vec(g_in, 254, g_exp, 255));
    /* 8: 00..FE (255 bytes) -> 01 FF 01..FE */
    for (int k = 0; k < 255; k++) g_in[k] = (uint8_t) k;
    g_exp[0] = 0x01;
    g_exp[1] = 0xFF;
    for (int k = 0; k < 254; k++) g_exp[2 + k] = (uint8_t) (k + 1);
    CHECK("COBS example 8: 00..FE -> 01 FF 01..FE", cobs_vec(g_in, 255, g_exp, 256));
    /* 9: 01..FF (255 bytes) -> FF 01..FE 02 FF */
    for (int k = 0; k < 255; k++) g_in[k] = (uint8_t) (k + 1);
    g_exp[0] = 0xFF;
    for (int k = 0; k < 254; k++) g_exp[1 + k] = (uint8_t) (k + 1);
    g_exp[255] = 0x02;
    g_exp[256] = 0xFF;
    CHECK("COBS example 9: 01..FF -> FF 01..FE 02 FF", cobs_vec(g_in, 255, g_exp, 257));
    /* 10: 02..FF 00 (255 bytes) -> FF 02..FF 01 01 */
    for (int k = 0; k < 254; k++) g_in[k] = (uint8_t) (k + 2);
    g_in[254] = 0;
    g_exp[0] = 0xFF;
    for (int k = 0; k < 254; k++) g_exp[1 + k] = (uint8_t) (k + 2);
    g_exp[255] = 0x01;
    g_exp[256] = 0x01;
    CHECK("COBS example 10: 02..FF 00 -> FF 02..FF 01 01", cobs_vec(g_in, 255, g_exp, 257));
    /* 11: 03..FF 00 01 (255 bytes) -> FE 03..FF 02 01 */
    for (int k = 0; k < 253; k++) g_in[k] = (uint8_t) (k + 3);
    g_in[253] = 0;
    g_in[254] = 1;
    g_exp[0] = 0xFE;
    for (int k = 0; k < 253; k++) g_exp[1 + k] = (uint8_t) (k + 3);
    g_exp[254] = 0x02;
    g_exp[255] = 0x01;
    CHECK("COBS example 11: 03..FF 00 01 -> FE 03..FF 02 01", cobs_vec(g_in, 255, g_exp, 256));
    { uint8_t e[] = {0x01};
      CHECK("COBS empty input -> 01", cobs_vec(g_in, 0, e, 1)); }
    CHECK("COBS max bound n + n/254 + 1", br_cobs_max(254) == 256 && br_cobs_max(0) == 1);
    CHECK("COBS encode refuses a short buffer", br_cobs_encode(g_in, 254, g_enc, 255) == BR_ERR_ARG);
    { uint8_t bad[] = {0x03, 0x11, 0x00, 0x22};
      CHECK("COBS decode refuses a zero inside", br_cobs_decode(bad, 4, g_dec, 10) == BR_ERR_FORMAT); }
    { uint8_t bad[] = {0x05, 0x11, 0x22};
      CHECK("COBS decode refuses a code past the end", br_cobs_decode(bad, 3, g_dec, 10) == BR_ERR_FORMAT); }
    { uint8_t z[] = {0x00};
      CHECK("COBS decode refuses code 0", br_cobs_decode(z, 1, g_dec, 10) == BR_ERR_FORMAT); }
    { uint8_t e[] = {0x05, 0x11, 0x22, 0x33, 0x44};
      CHECK("COBS decode refuses a small output buffer", br_cobs_decode(e, 5, g_dec, 3) == BR_ERR_SIZE); }
    /* random round trips, zero-heavy and zero-free */
    uint32_t s = 99;
    int ok = 1;
    for (int it = 0; it < 3000; it++) {
        uint32_t n = (uint32_t) (it % 1000);
        for (uint32_t k = 0; k < n; k++) {
            s = s * 1664525u + 1013904223u;
            uint8_t b = (uint8_t) (s >> 24);
            g_in[k] = (it % 3 == 0) ? (b & 1 ? 0 : b) : (it % 3 == 1 ? (uint8_t) (b | 1) : b);
        }
        int32_t e = br_cobs_encode(g_in, n, g_enc, sizeof g_enc);
        if (e < 0 || (uint32_t) e > br_cobs_max(n) || memchr(g_enc, 0, (size_t) e)) ok = 0;
        int32_t d = br_cobs_decode(g_enc, (uint32_t) e, g_dec, sizeof g_dec);
        if (d != (int32_t) n || (n && memcmp(g_dec, g_in, n))) ok = 0;
    }
    CHECK("COBS 3000 random round trips, no zero in output, within bound", ok);
}

static void test_crc(void)
{
    CHECK("CRC-32 check value of \"123456789\" = CBF43926",
          br_crc32((const uint8_t *) "123456789", 9) == 0xCBF43926u);
    CHECK("CRC-32 of empty = 0", br_crc32((const uint8_t *) "", 0) == 0);
    CHECK("CRC-32 of the quick brown fox = 414FA339",
          br_crc32((const uint8_t *) "The quick brown fox jumps over the lazy dog", 43) == 0x414FA339u);
    uint32_t c = br_crc32_update(0, (const uint8_t *) "12345", 5);
    CHECK("CRC-32 incremental update", br_crc32_update(c, (const uint8_t *) "6789", 4) == 0xCBF43926u);
    CHECK("CRC-32 of \"hello\" = 3610A686", br_crc32((const uint8_t *) "hello", 5) == 0x3610A686u);
}

static int feed(br_serial_rx_t *rx, const uint8_t *p, uint32_t n, uint8_t out[][300], uint32_t *lens,
                int max)
{
    int got = 0;
    uint8_t tmp[1200];
    for (uint32_t i = 0; i < n; i++) {
        uint32_t l = 0;
        if (br_serial_rx_byte(rx, p[i], tmp, sizeof tmp, &l) == 1 && got < max) {
            memcpy(out[got], tmp, l < 300 ? l : 300);
            lens[got++] = l;
        }
    }
    return got;
}

static void test_frames(void)
{
    uint8_t f[64];
    static const uint8_t exp[] = {0x00, 0x0A, 'h', 'e', 'l', 'l', 'o', 0x86, 0xA6, 0x10, 0x36, 0x00};
    int32_t n = br_serial_frame((const uint8_t *) "hello", 5, f, sizeof f);
    CHECK("frame of \"hello\" is 00 0A 68 65 6C 6C 6F 86 A6 10 36 00",
          n == (int32_t) sizeof exp && !memcmp(f, exp, sizeof exp));
    CHECK("frame refuses empty payload", br_serial_frame((const uint8_t *) "x", 0, f, sizeof f) == BR_ERR_ARG);
    CHECK("frame refuses short buffer", br_serial_frame((const uint8_t *) "hello", 5, f, 11) == BR_ERR_SIZE);
    { uint8_t z4[4] = {0, 0, 0, 0}, g[32];
      int32_t m = br_serial_frame(z4, 4, g, sizeof g);
      uint8_t d[32];
      int32_t dl = br_cobs_decode(g + 1, (uint32_t) m - 2u, d, sizeof d);
      uint32_t crc = br_crc32(z4, 4);
      CHECK("frame of zeros: COBS of payload || CRC little-endian",
            dl == 8 && !memcmp(d, z4, 4) && d[4] == (uint8_t) crc && d[7] == (uint8_t) (crc >> 24)); }

    static uint8_t raw[BR_SERIAL_FRAME_MAX(256)];
    br_serial_rx_t rx;
    CHECK("rx init", br_serial_rx_init(&rx, raw, sizeof raw) == BR_OK);
    CHECK("rx init refuses tiny buffer", br_serial_rx_init(&rx, raw, 4) == BR_ERR_ARG);
    br_serial_rx_init(&rx, raw, sizeof raw);
    uint8_t out[8][300];
    uint32_t lens[8];
    uint8_t stream[4096];
    uint32_t sn = 0;
    /* garbage, then a good frame, then a corrupted one, then a good one */
    const char *garbage = "\x13\x37 line noise \xff\xfe";
    memcpy(stream + sn, garbage, 16);
    sn += 16;
    sn += (uint32_t) br_serial_frame((const uint8_t *) "first", 5, stream + sn, 64);
    uint32_t bad_at = sn;
    sn += (uint32_t) br_serial_frame((const uint8_t *) "second", 6, stream + sn, 64);
    stream[bad_at + 4] ^= 0x01; /* flip a payload bit */
    sn += (uint32_t) br_serial_frame((const uint8_t *) "third", 5, stream + sn, 64);
    int got = feed(&rx, stream, sn, out, lens, 8);
    CHECK("garbage before the first frame is skipped; good frames delivered",
          got == 2 && lens[0] == 5 && !memcmp(out[0], "first", 5) && lens[1] == 5 &&
              !memcmp(out[1], "third", 5));
    CHECK("corrupted frame counted as bad CRC", rx.bad_crc == 1);
    CHECK("garbage counted once as bad COBS or CRC", rx.bad_cobs + rx.bad_crc == 2 && rx.frames == 2);
    /* joining mid-frame */
    br_serial_rx_init(&rx, raw, sizeof raw);
    sn = (uint32_t) br_serial_frame((const uint8_t *) "abcdefgh", 8, stream, 64);
    uint32_t s2 = (uint32_t) br_serial_frame((const uint8_t *) "next", 4, stream + sn, 64);
    got = feed(&rx, stream + 5, sn - 5 + s2, out, lens, 8);
    CHECK("receiver joining mid-frame resyncs on the next frame",
          got == 1 && lens[0] == 4 && !memcmp(out[0], "next", 4));
    /* over-long frame */
    br_serial_rx_init(&rx, raw, sizeof raw);
    static uint8_t big[600], bigf[700];
    memset(big, 0x55, sizeof big);
    sn = (uint32_t) br_serial_frame(big, 600, bigf, sizeof bigf);
    memcpy(stream, bigf, sn);
    sn += (uint32_t) br_serial_frame((const uint8_t *) "ok", 2, stream + sn, 64);
    got = feed(&rx, stream, sn, out, lens, 8);
    CHECK("over-long frame dropped and counted; next frame delivered",
          got == 1 && rx.too_long == 1 && lens[0] == 2 && !memcmp(out[0], "ok", 2));
    /* too short to hold a CRC */
    br_serial_rx_init(&rx, raw, sizeof raw);
    { uint8_t s3[] = {0x00, 0x03, 0x11, 0x22, 0x00};
      got = feed(&rx, s3, 5, out, lens, 8);
      CHECK("frame shorter than payload + CRC refused", got == 0 && rx.bad_cobs == 1); }
    /* random garbage fuzz: never crashes, frames after it still arrive */
    uint32_t s = 7;
    int ok = 1;
    for (int it = 0; it < 500; it++) {
        br_serial_rx_init(&rx, raw, sizeof raw);
        uint32_t gl = (uint32_t) (it % 700);
        for (uint32_t k = 0; k < gl; k++) {
            s = s * 1664525u + 1013904223u;
            stream[k] = (uint8_t) (s >> 24);
        }
        uint32_t pl = 1u + (uint32_t) (it % 250);
        for (uint32_t k = 0; k < pl; k++) g_in[k] = (uint8_t) (k * 31u + (uint32_t) it);
        sn = gl + (uint32_t) br_serial_frame(g_in, pl, stream + gl, sizeof stream - gl);
        got = feed(&rx, stream, sn, out, lens, 8);
        if (got < 1 || lens[got - 1] != pl || memcmp(out[got - 1], g_in, pl < 300 ? pl : 300)) ok = 0;
        if (got > 1) ok = 0; /* random garbage passing a CRC-32: astronomically unlikely */
    }
    CHECK("500 garbage prefixes: the real frame always arrives, nothing false", ok);
}

int main(void)
{
    test_cobs();
    test_crc();
    test_frames();
    return check_summary();
}
