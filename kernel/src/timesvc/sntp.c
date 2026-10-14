/* sntp.c — SNTP client packets. See sntp.h.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "sntp.h"

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static uint64_t rd64(const uint8_t *p)
{
    return ((uint64_t) rd32(p) << 32) | rd32(p + 4);
}

/* 16.16 fixed-point seconds (NTP short format) as microseconds */
static uint32_t short_to_us(uint32_t v)
{
    uint64_t us = ((uint64_t) v * 1000000u) >> 16;
    return us > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) us;
}

uint32_t sntp_build_request(uint8_t out[SNTP_PKT_LEN], uint64_t nonce)
{
    for (uint32_t i = 0; i < SNTP_PKT_LEN; i++) out[i] = 0;
    out[0] = (0u << 6) | (4u << 3) | 3u; /* LI 0, version 4, mode 3 (client) */
    for (uint32_t i = 0; i < 8; i++) out[40 + i] = (uint8_t) (nonce >> (56 - 8 * i));
    return SNTP_PKT_LEN;
}

int64_t sntp_ntp_to_unix_us(uint64_t ntp, int64_t pivot_s)
{
    int64_t secs = (int64_t) (ntp >> 32) - (int64_t) NTP_UNIX_DELTA; /* era 0 */
    /* Shift by whole eras (2^32 s) until within 2^31 s of the pivot. */
    int64_t d = secs - pivot_s + ((int64_t) 1 << 31);
    int64_t eras = d >> 32; /* floor, as d may be negative */
    secs -= eras * ((int64_t) 1 << 32);
    uint64_t frac_us = ((ntp & 0xFFFFFFFFu) * 1000000u) >> 32;
    return secs * 1000000 + (int64_t) frac_us;
}

sntp_status_t sntp_parse_reply(const uint8_t *pkt, uint32_t len, uint64_t nonce, int64_t t1_us,
                               int64_t t4_us, int64_t pivot_s, sntp_sample_t *out)
{
    if (!pkt || !out || len < SNTP_PKT_LEN) return SNTP_BAD;
    for (uint32_t i = 0; i < sizeof out->kod; i++) out->kod[i] = 0;

    uint8_t li = pkt[0] >> 6, vn = (pkt[0] >> 3) & 7u, mode = pkt[0] & 7u;
    if (mode != 4 || vn < 3 || vn > 4) return SNTP_BAD;
    if (rd64(pkt + 24) != nonce) return SNTP_NOT_OURS;

    uint8_t stratum = pkt[1];
    if (stratum == 0) {
        for (uint32_t i = 0; i < 4; i++) {
            uint8_t ch = pkt[12 + i];
            out->kod[i] = (ch >= 0x20 && ch < 0x7f) ? (char) ch : '?';
        }
        return SNTP_KOD;
    }
    if (li == 3 || stratum > 15) return SNTP_UNSYNCED;

    uint64_t rx = rd64(pkt + 32), tx = rd64(pkt + 40);
    if (rx == 0 || tx == 0) return SNTP_BAD;

    uint32_t root_delay = short_to_us(rd32(pkt + 4)), root_disp = short_to_us(rd32(pkt + 8));
    uint64_t dist = (uint64_t) (root_delay / 2) + root_disp;
    if (dist >= SNTP_MAX_ROOT_DIST_US) return SNTP_UNSYNCED;

    int64_t t2 = sntp_ntp_to_unix_us(rx, pivot_s);
    int64_t t3 = sntp_ntp_to_unix_us(tx, pivot_s);
    if (t3 < t2 || t4_us < t1_us) return SNTP_BAD;

    int64_t delay = (t4_us - t1_us) - (t3 - t2);
    if (delay < 0) delay = 0; /* clock granularity; RFC 5905 clamps too */
    out->offset_us = ((t2 - t1_us) + (t3 - t4_us)) / 2;
    out->delay_us = delay;
    out->server_us = t3;
    out->root_dist_us = (uint32_t) dist;
    out->stratum = stratum;
    out->leap = li;
    return SNTP_OK;
}
