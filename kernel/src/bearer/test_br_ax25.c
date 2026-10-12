/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_ax25.c — AX.25 UI frames and HDLC bit stuffing.
 *
 * Published references:
 *   - AX.25 v2.2 (TAPR, 2017) Figure 3.4: destination NJ7P-0 is
 *     9C 94 6E A0 40 40 E0 and source N7LEM-0 (last address) is
 *     9C 6E 98 8A 9A 40 61. The figure prints 0x98 for the first 'N' of
 *     both calls; 'N' (0x4E) shifted left is 0x9C, as the same figure's
 *     binary column for 'L' (0x98) shows, so 0x9C is checked.
 *   - CRC-16/X-25 (ISO/IEC 13239, RFC 1662 FCS-16): check value of
 *     "123456789" is 0x906E (CRC RevEng catalogue); a good frame with its
 *     FCS appended leaves the residue 0xF0B8 (RFC 1662 PPPGOODFCS16), i.e.
 *     br_crc16_x25() over frame || FCS equals ~0xF0B8 = 0x0F47.
 */
#include <string.h>

#include "br_ax25.h"
#include "test_check.h"

static void test_crc(void)
{
    CHECK("CRC-16/X-25 check value 0x906E",
          br_crc16_x25((const uint8_t *) "123456789", 9) == 0x906E);
    uint8_t f[11];
    memcpy(f, "123456789", 9);
    f[9] = 0x6E;
    f[10] = 0x90;
    CHECK("FCS appended low byte first gives the RFC 1662 good-frame residue",
          br_crc16_x25(f, 11) == 0x0F47);
    CHECK("CRC-16/X-25 of empty is 0x0000", br_crc16_x25(f, 0) == 0x0000);
}

static void test_addr(void)
{
    br_ax25_addr_t a, b;
    uint8_t o[7];
    br_ax25_addr_parse("NJ7P", &a);
    br_ax25_addr_encode(&a, true, false, o);
    static const uint8_t nj7p[7] = {0x9C, 0x94, 0x6E, 0xA0, 0x40, 0x40, 0xE0};
    CHECK("AX.25 Fig 3.4: destination NJ7P-0 (C=1) = 9C 94 6E A0 40 40 E0", !memcmp(o, nj7p, 7));
    br_ax25_addr_parse("N7LEM", &b);
    br_ax25_addr_encode(&b, false, true, o);
    static const uint8_t n7lem[7] = {0x9C, 0x6E, 0x98, 0x8A, 0x9A, 0x40, 0x61};
    CHECK("AX.25 Fig 3.4: source N7LEM-0 (last) = 9C 6E 98 8A 9A 40 61", !memcmp(o, n7lem, 7));
    CHECK("parse lower case + SSID",
          br_ax25_addr_parse("n0call-7", &a) == BR_OK && !strcmp(a.call, "N0CALL") && a.ssid == 7);
    br_ax25_addr_encode(&a, false, false, o);
    CHECK("SSID 7 encodes as 0x6E (0 11 0111 0)", o[6] == 0x6E);
    CHECK("SSID 15", br_ax25_addr_parse("W1AW-15", &a) == BR_OK && a.ssid == 15);
    CHECK("SSID 16 refused", br_ax25_addr_parse("W1AW-16", &a) == BR_ERR_FORMAT);
    CHECK("SSID 07 (leading zero) refused", br_ax25_addr_parse("W1AW-07", &a) == BR_ERR_FORMAT);
    CHECK("SSID 123 refused", br_ax25_addr_parse("W1AW-123", &a) == BR_ERR_FORMAT);
    CHECK("empty SSID refused", br_ax25_addr_parse("W1AW-", &a) == BR_ERR_FORMAT);
    CHECK("7-char call refused", br_ax25_addr_parse("ABCDEFG", &a) == BR_ERR_FORMAT);
    CHECK("6-char call ok", br_ax25_addr_parse("ABCDE1", &a) == BR_OK);
    CHECK("space in call refused", br_ax25_addr_parse("A B", &a) == BR_ERR_FORMAT);
    CHECK("punctuation refused", br_ax25_addr_parse("W1A/P", &a) == BR_ERR_FORMAT);
    CHECK("empty call refused", br_ax25_addr_parse("-1", &a) == BR_ERR_FORMAT);
    br_ax25_addr_parse("N0CALL", &a);
    CHECK("N0CALL is a station call sign", br_ax25_is_station(&a));
    br_ax25_addr_parse("CQ", &a);
    CHECK("CQ is not a station call sign", !br_ax25_is_station(&a));
    br_ax25_addr_parse("EMERG", &a);
    CHECK("EMERG is not a station call sign", !br_ax25_is_station(&a));
    br_ax25_addr_parse("1234", &a);
    CHECK("1234 is not a station call sign", !br_ax25_is_station(&a));
    char s[10];
    br_ax25_addr_parse("VK2ABC-12", &a);
    CHECK("format VK2ABC-12", br_ax25_addr_format(&a, s) == 9 && !strcmp(s, "VK2ABC-12"));
    br_ax25_addr_parse("G4XYZ", &a);
    CHECK("format G4XYZ (SSID 0 omitted)", br_ax25_addr_format(&a, s) == 5 && !strcmp(s, "G4XYZ"));
}

static uint8_t g_f[BR_AX25_FRAME_MAX + 16];

static void test_frame(void)
{
    br_ax25_addr_t d, s, dg[3];
    br_ax25_addr_parse("NJ7P", &d);
    br_ax25_addr_parse("N7LEM", &s);
    const uint8_t *info = (const uint8_t *) "Hello";
    int32_t n = br_ax25_ui_encode(&d, &s, NULL, 0, BR_AX25_PID_NONE, info, 5, g_f, sizeof g_f);
    static const uint8_t hdr[16] = {0x9C, 0x94, 0x6E, 0xA0, 0x40, 0x40, 0xE0, 0x9C,
                                    0x6E, 0x98, 0x8A, 0x9A, 0x40, 0x61, 0x03, 0xF0};
    CHECK("UI frame: Fig 3.4 addresses, control 03, PID F0, info, 2-byte FCS",
          n == 23 && !memcmp(g_f, hdr, 16) && !memcmp(g_f + 16, "Hello", 5));
    uint16_t fcs = br_crc16_x25(g_f, 21);
    CHECK("FCS is CRC-16/X-25 of the frame, low byte first",
          g_f[21] == (uint8_t) fcs && g_f[22] == (uint8_t) (fcs >> 8));
    CHECK("whole frame leaves the good residue", br_crc16_x25(g_f, 23) == 0x0F47);
    br_ax25_ui_t ui;
    CHECK("decode", br_ax25_ui_decode(g_f, 23, &ui) == BR_OK);
    CHECK("decoded fields", !strcmp(ui.dst.call, "NJ7P") && !strcmp(ui.src.call, "N7LEM") &&
                                ui.command == 1 && ui.pf == 0 && ui.pid == 0xF0 && ui.ndigi == 0 &&
                                ui.info_len == 5 && !memcmp(ui.info, "Hello", 5));
    for (int bit = 0; bit < 23 * 8; bit++) {
        g_f[bit / 8] ^= (uint8_t) (1u << (bit % 8));
        int r = br_ax25_ui_decode(g_f, 23, &ui);
        g_f[bit / 8] ^= (uint8_t) (1u << (bit % 8));
        if (r != BR_ERR_CRC) {
            CHECK("every single-bit error is caught by the FCS", 0);
            return;
        }
    }
    CHECK("every single-bit error (184 positions) is caught by the FCS", 1);
    /* P/F set: control 0x13 accepted */
    g_f[14] = 0x13;
    uint16_t f2 = br_crc16_x25(g_f, 21);
    g_f[21] = (uint8_t) f2;
    g_f[22] = (uint8_t) (f2 >> 8);
    CHECK("UI with P/F = 1 accepted", br_ax25_ui_decode(g_f, 23, &ui) == BR_OK && ui.pf == 1);
    g_f[14] = 0x3E; /* the I frame of Fig 3.4 */
    f2 = br_crc16_x25(g_f, 21);
    g_f[21] = (uint8_t) f2;
    g_f[22] = (uint8_t) (f2 >> 8);
    CHECK("I frame (control 3E) refused: UI only",
          br_ax25_ui_decode(g_f, 23, &ui) == BR_ERR_FORMAT);
    /* digipeaters */
    br_ax25_addr_parse("WIDE1-1", &dg[0]);
    br_ax25_addr_parse("WIDE2-2", &dg[1]);
    br_ax25_addr_parse("RELAY", &dg[2]);
    n = br_ax25_ui_encode(&d, &s, dg, 2, 0xF0, info, 5, g_f, sizeof g_f);
    CHECK("two digipeaters encode",
          n == 37 && (g_f[13] & 1) == 0 && (g_f[20] & 1) == 0 && (g_f[27] & 1) == 1);
    CHECK("two digipeaters decode", br_ax25_ui_decode(g_f, (uint32_t) n, &ui) == BR_OK &&
                                        ui.ndigi == 2 && !strcmp(ui.digi[1].call, "WIDE2") &&
                                        ui.digi[1].ssid == 2 && ui.digi_h[0] == 0);
    CHECK("three digipeaters refused by the encoder",
          br_ax25_ui_encode(&d, &s, dg, 3, 0xF0, info, 5, g_f, sizeof g_f) == BR_ERR_ARG);
    /* hand-build three digis: clear E on the second digi, add a third */
    n = br_ax25_ui_encode(&d, &s, dg, 2, 0xF0, info, 5, g_f, sizeof g_f);
    uint8_t g3[64];
    memcpy(g3, g_f, 28);
    g3[27] &= 0xFE;
    br_ax25_addr_encode(&dg[2], false, true, g3 + 28);
    memcpy(g3 + 35, g_f + 28, 7); /* control, pid, info */
    uint16_t f3 = br_crc16_x25(g3, 42);
    g3[42] = (uint8_t) f3;
    g3[43] = (uint8_t) (f3 >> 8);
    CHECK("three digipeaters refused by the decoder",
          br_ax25_ui_decode(g3, 44, &ui) == BR_ERR_FORMAT);
    /* extension bit inside a call sign */
    n = br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, info, 5, g_f, sizeof g_f);
    g_f[2] |= 1;
    f2 = br_crc16_x25(g_f, 21);
    g_f[21] = (uint8_t) f2;
    g_f[22] = (uint8_t) (f2 >> 8);
    CHECK("extension bit inside a call refused", br_ax25_ui_decode(g_f, 23, &ui) == BR_ERR_FORMAT);
    n = br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, info, 5, g_f, sizeof g_f);
    g_f[6] |= 1; /* E on the destination: address field would end early */
    f2 = br_crc16_x25(g_f, 21);
    g_f[21] = (uint8_t) f2;
    g_f[22] = (uint8_t) (f2 >> 8);
    CHECK("address field ending at the destination refused",
          br_ax25_ui_decode(g_f, 23, &ui) == BR_ERR_FORMAT);
    n = br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, info, 5, g_f, sizeof g_f);
    g_f[1] = (uint8_t) ('a' << 1); /* lower case is not valid on air */
    f2 = br_crc16_x25(g_f, 21);
    g_f[21] = (uint8_t) f2;
    g_f[22] = (uint8_t) (f2 >> 8);
    CHECK("lower-case character in an address refused",
          br_ax25_ui_decode(g_f, 23, &ui) == BR_ERR_FORMAT);
    n = br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, info, 5, g_f, sizeof g_f);
    g_f[2] = 0x40; /* "NJ P": space before a character */
    f2 = br_crc16_x25(g_f, 21);
    g_f[21] = (uint8_t) f2;
    g_f[22] = (uint8_t) (f2 >> 8);
    CHECK("embedded space in a call refused", br_ax25_ui_decode(g_f, 23, &ui) == BR_ERR_FORMAT);
    static uint8_t big[300];
    memset(big, 'A', sizeof big);
    CHECK("256-byte info accepted",
          br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, big, 256, g_f, sizeof g_f) == 14 + 2 + 256 + 2);
    CHECK("256-byte info decodes", br_ax25_ui_decode(g_f, 274, &ui) == BR_OK && ui.info_len == 256);
    CHECK("257-byte info refused",
          br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, big, 257, g_f, sizeof g_f) == BR_ERR_SIZE);
    CHECK("short buffer refused",
          br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, info, 5, g_f, 22) == BR_ERR_SIZE);
    CHECK("frame shorter than the minimum refused",
          br_ax25_ui_decode(g_f, 17, &ui) == BR_ERR_FORMAT);
    n = br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, NULL, 0, g_f, sizeof g_f);
    CHECK("empty info field ok",
          n == 18 && br_ax25_ui_decode(g_f, 18, &ui) == BR_OK && ui.info_len == 0);
}

static uint8_t g_bits[2048], g_out[600];

static void test_hdlc(void)
{
    uint8_t ff[10];
    memset(ff, 0xFF, sizeof ff);
    uint32_t nb = 0;
    int32_t bytes = br_hdlc_stuff(ff, 10, g_bits, sizeof g_bits, &nb);
    CHECK("80 ones gain 16 stuffed zeros: 16 + 80 + 16 bits", nb == 112 && bytes == 14);
    CHECK("opening flag 0x7E", g_bits[0] == 0x7E);
    int ok = 1;
    uint32_t ones = 0;
    for (uint32_t i = 8; i + 8 < nb; i++) {
        uint32_t b = (g_bits[i >> 3] >> (i & 7)) & 1u;
        ones = b ? ones + 1 : 0;
        if (ones > 5) ok = 0;
    }
    CHECK("never six ones in a row between the flags", ok);
    CHECK("unstuff 0xFF x 10",
          br_hdlc_unstuff(g_bits, nb, g_out, sizeof g_out) == 10 && !memcmp(g_out, ff, 10));
    uint8_t flagdata[6] = {0x7E, 0x7E, 0x00, 0x7E, 0xFC, 0x3F};
    br_hdlc_stuff(flagdata, 6, g_bits, sizeof g_bits, &nb);
    CHECK("data containing 0x7E survives stuffing",
          br_hdlc_unstuff(g_bits, nb, g_out, sizeof g_out) == 6 && !memcmp(g_out, flagdata, 6));
    /* a real UI frame, with idle flags in front */
    br_ax25_addr_t d, s;
    br_ax25_addr_parse("NJ7P", &d);
    br_ax25_addr_parse("N7LEM", &s);
    int32_t n = br_ax25_ui_encode(&d, &s, NULL, 0, 0xF0, (const uint8_t *) "73 de N7LEM", 11, g_f,
                                  sizeof g_f);
    uint8_t tmp[2048];
    br_hdlc_stuff(g_f, (uint32_t) n, tmp, sizeof tmp, &nb);
    memset(g_bits, 0x7E, 3); /* three idle flags */
    for (uint32_t i = 0; i < nb; i++) {
        uint32_t b = (tmp[i >> 3] >> (i & 7)) & 1u, j = 24 + i;
        if ((j & 7) == 0) g_bits[j >> 3] = 0;
        g_bits[j >> 3] |= (uint8_t) (b << (j & 7));
    }
    int32_t m = br_hdlc_unstuff(g_bits, nb + 24, g_out, sizeof g_out);
    br_ax25_ui_t ui;
    CHECK("UI frame through stuffing with leading idle flags decodes",
          m == n && !memcmp(g_out, g_f, (size_t) n) &&
              br_ax25_ui_decode(g_out, (uint32_t) m, &ui) == BR_OK && ui.info_len == 11);
    /* random round trips */
    uint32_t sd = 5;
    ok = 1;
    for (int it = 0; it < 2000; it++) {
        uint32_t len = 4u + (uint32_t) (it % 200);
        for (uint32_t k = 0; k < len; k++) {
            sd = sd * 1664525u + 1013904223u;
            g_out[k] = (uint8_t) ((it & 1) ? (sd >> 24) | 0xF8 : (sd >> 24));
        }
        uint8_t keep[256];
        memcpy(keep, g_out, len);
        if (br_hdlc_stuff(keep, len, g_bits, sizeof g_bits, &nb) < 0) ok = 0;
        if (br_hdlc_unstuff(g_bits, nb, g_out, sizeof g_out) != (int32_t) len ||
            memcmp(g_out, keep, len))
            ok = 0;
    }
    CHECK("2000 random frames (half of them ones-heavy) round trip", ok);
    /* abort: seven ones */
    br_hdlc_stuff(ff, 10, g_bits, sizeof g_bits, &nb);
    uint8_t ab[64];
    memcpy(ab, g_bits, 14);
    ab[2] = 0xFF;
    ab[3] = 0xFF;
    CHECK("seven ones (abort) refused",
          br_hdlc_unstuff(ab, nb, g_out, sizeof g_out) == BR_ERR_FORMAT);
    CHECK("no closing flag refused",
          br_hdlc_unstuff(g_bits, nb - 8, g_out, sizeof g_out) == BR_ERR_FORMAT);
    uint8_t noflag[4] = {0x00, 0xFF, 0x00, 0xFF};
    CHECK("no opening flag refused",
          br_hdlc_unstuff(noflag, 32, g_out, sizeof g_out) == BR_ERR_FORMAT);
    /* odd bit count between flags */
    uint8_t odd[8] = {0x7E, 0x01, 0x7E, 0, 0, 0, 0, 0}; /* flag, 8 data bits, flag */
    CHECK("frame shorter than 4 octets refused",
          br_hdlc_unstuff(odd, 24, g_out, sizeof g_out) == BR_ERR_FORMAT);
    uint32_t onb = 0;
    uint8_t zeros[5] = {0, 0, 0, 0, 0}; /* no stuffing, so a lost bit cannot be absorbed */
    br_hdlc_stuff(zeros, 5, g_bits, sizeof g_bits, &onb);
    /* drop one data bit: shift the tail */
    uint8_t cut[64];
    memset(cut, 0, sizeof cut);
    for (uint32_t i = 0, j = 0; i < onb; i++) {
        if (i == 12) continue;
        uint32_t b = (g_bits[i >> 3] >> (i & 7)) & 1u;
        cut[j >> 3] |= (uint8_t) (b << (j & 7));
        j++;
    }
    CHECK("bit count not a whole number of octets refused",
          br_hdlc_unstuff(cut, onb - 1, g_out, sizeof g_out) == BR_ERR_FORMAT);
    CHECK("stuff into a too-small buffer refused",
          br_hdlc_stuff(ff, 10, g_bits, 13, &nb) == BR_ERR_SIZE);
    CHECK("unstuff into a too-small buffer refused",
          (br_hdlc_stuff(ff, 10, g_bits, sizeof g_bits, &nb),
           br_hdlc_unstuff(g_bits, nb, g_out, 5) == BR_ERR_SIZE));
}

int main(void)
{
    test_crc();
    test_addr();
    test_frame();
    test_hdlc();
    return check_summary();
}
