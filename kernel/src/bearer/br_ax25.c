/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* br_ax25.c — AX.25 UI frame encoder/decoder and HDLC bit stuffing
 * (br_ax25.h, rules X1..X2). */
#include "br_ax25.h"

uint16_t br_crc16_x25(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (uint16_t) ((crc >> 1) ^ (0x8408u & (0u - (uint32_t) (crc & 1u))));
    }
    return (uint16_t) ~crc;
}

static bool call_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

br_status_t br_ax25_addr_parse(const char *s, br_ax25_addr_t *a)
{
    if (!s || !a) return BR_ERR_ARG;
    br_mset(a, 0, sizeof *a);
    uint32_t i = 0;
    for (; s[i] && s[i] != '-'; i++) {
        char c = s[i];
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        if (!call_char(c) || i >= 6u) return BR_ERR_FORMAT;
        a->call[i] = c;
    }
    if (i == 0) return BR_ERR_FORMAT;
    a->call[i] = 0;
    if (s[i] == '-') {
        i++;
        uint32_t v = 0, d = 0;
        for (; s[i]; i++, d++) {
            if (s[i] < '0' || s[i] > '9' || d >= 2u) return BR_ERR_FORMAT;
            v = v * 10u + (uint32_t) (s[i] - '0');
        }
        if (d == 0 || v > 15u || (d == 2u && s[i - 2] == '0')) return BR_ERR_FORMAT;
        a->ssid = (uint8_t) v;
    }
    return BR_OK;
}

bool br_ax25_is_station(const br_ax25_addr_t *a)
{
    if (!a || !a->call[0] || a->ssid > 15u) return false;
    bool letter = false, digit = false;
    for (uint32_t i = 0; i < 6u && a->call[i]; i++) {
        if (!call_char(a->call[i])) return false;
        if (a->call[i] >= 'A')
            letter = true;
        else
            digit = true;
    }
    return letter && digit;
}

int32_t br_ax25_addr_format(const br_ax25_addr_t *a, char out[10])
{
    if (!a || !out || !a->call[0] || a->ssid > 15u) return BR_ERR_ARG;
    uint32_t n = 0;
    while (n < 6u && a->call[n]) {
        if (!call_char(a->call[n])) return BR_ERR_FORMAT;
        out[n] = a->call[n];
        n++;
    }
    if (a->ssid) {
        out[n++] = '-';
        if (a->ssid >= 10u) out[n++] = '1';
        out[n++] = (char) ('0' + a->ssid % 10u);
    }
    out[n] = 0;
    return (int32_t) n;
}

br_status_t br_ax25_addr_encode(const br_ax25_addr_t *a, bool cbit, bool last,
                                uint8_t out[BR_AX25_ADDR_LEN])
{
    if (!a || !out || !a->call[0] || a->ssid > 15u) return BR_ERR_ARG;
    uint32_t i = 0;
    for (; i < 6u && a->call[i]; i++) {
        if (!call_char(a->call[i])) return BR_ERR_FORMAT;
        out[i] = (uint8_t) ((uint8_t) a->call[i] << 1);
    }
    for (; i < 6u; i++) out[i] = (uint8_t) (' ' << 1);
    out[6] = (uint8_t) ((cbit ? 0x80u : 0u) | 0x60u | ((uint32_t) a->ssid << 1) | (last ? 1u : 0u));
    return BR_OK;
}

/* Returns 1 if this was the last address, 0 if more follow, <0 on error. */
static int addr_decode(const uint8_t *in, br_ax25_addr_t *a, uint8_t *bit7)
{
    br_mset(a, 0, sizeof *a);
    bool pad = false;
    uint32_t n = 0;
    for (uint32_t i = 0; i < 6u; i++) {
        if (in[i] & 1u) return BR_ERR_FORMAT; /* extension bit inside a call */
        char c = (char) (in[i] >> 1);
        if (c == ' ') {
            pad = true;
            continue;
        }
        if (pad || !call_char(c)) return BR_ERR_FORMAT;
        a->call[n++] = c;
    }
    if (n == 0) return BR_ERR_FORMAT;
    a->ssid = (uint8_t) ((in[6] >> 1) & 0x0Fu);
    *bit7 = (uint8_t) (in[6] >> 7);
    return (in[6] & 1u) ? 1 : 0;
}

int32_t br_ax25_ui_encode(const br_ax25_addr_t *dst, const br_ax25_addr_t *src,
                          const br_ax25_addr_t *digis, uint32_t ndigi, uint8_t pid,
                          const uint8_t *info, uint32_t n, uint8_t *out, uint32_t cap)
{
    if (!dst || !src || !out || ndigi > BR_AX25_MAX_DIGI || (ndigi && !digis) || (n && !info))
        return BR_ERR_ARG;
    if (n > BR_AX25_INFO_MAX) return BR_ERR_SIZE;
    uint32_t len = BR_AX25_ADDR_LEN * (2u + ndigi) + 2u + n + 2u;
    if (cap < len) return BR_ERR_SIZE;
    uint32_t o = 0;
    if (br_ax25_addr_encode(dst, true, false, out) != BR_OK) return BR_ERR_FORMAT;
    o += BR_AX25_ADDR_LEN;
    if (br_ax25_addr_encode(src, false, ndigi == 0, out + o) != BR_OK) return BR_ERR_FORMAT;
    o += BR_AX25_ADDR_LEN;
    for (uint32_t i = 0; i < ndigi; i++) {
        if (br_ax25_addr_encode(&digis[i], false, i + 1u == ndigi, out + o) != BR_OK)
            return BR_ERR_FORMAT;
        o += BR_AX25_ADDR_LEN;
    }
    out[o++] = BR_AX25_CTRL_UI;
    out[o++] = pid;
    br_mcpy(out + o, info, n);
    o += n;
    uint16_t fcs = br_crc16_x25(out, o);
    out[o++] = (uint8_t) fcs;
    out[o++] = (uint8_t) (fcs >> 8);
    return (int32_t) o;
}

br_status_t br_ax25_ui_decode(const uint8_t *f, uint32_t n, br_ax25_ui_t *ui)
{
    if (!f || !ui) return BR_ERR_ARG;
    br_mset(ui, 0, sizeof *ui);
    if (n < 2u * BR_AX25_ADDR_LEN + 2u + 2u) return BR_ERR_FORMAT;
    uint16_t fcs = (uint16_t) (f[n - 2] | ((uint16_t) f[n - 1] << 8));
    if (br_crc16_x25(f, n - 2u) != fcs) return BR_ERR_CRC;
    uint8_t cd = 0, cs = 0;
    uint32_t o = 0;
    if (addr_decode(f, &ui->dst, &cd) != 0) return BR_ERR_FORMAT;
    o += BR_AX25_ADDR_LEN;
    int last = addr_decode(f + o, &ui->src, &cs);
    if (last < 0) return BR_ERR_FORMAT;
    o += BR_AX25_ADDR_LEN;
    while (!last) {
        if (ui->ndigi >= BR_AX25_MAX_DIGI || o + BR_AX25_ADDR_LEN > n - 4u) return BR_ERR_FORMAT;
        uint8_t h = 0;
        last = addr_decode(f + o, &ui->digi[ui->ndigi], &h);
        if (last < 0) return BR_ERR_FORMAT;
        ui->digi_h[ui->ndigi++] = h;
        o += BR_AX25_ADDR_LEN;
    }
    if (o + 2u > n - 2u) return BR_ERR_FORMAT;
    uint8_t ctl = f[o++];
    if ((ctl & 0xEFu) != BR_AX25_CTRL_UI) return BR_ERR_FORMAT;
    ui->pf = (uint8_t) ((ctl >> 4) & 1u);
    ui->pid = f[o++];
    uint32_t il = n - 2u - o;
    if (il > BR_AX25_INFO_MAX) return BR_ERR_SIZE;
    ui->command = (uint8_t) (cd == 1u && cs == 0u);
    ui->info = f + o;
    ui->info_len = il;
    return BR_OK;
}

/* ===== HDLC bits ===== */
static bool put_bit(uint8_t *bits, uint32_t cap_bytes, uint32_t *nb, uint32_t b)
{
    if (*nb >= cap_bytes * 8u) return false;
    uint32_t byte = *nb >> 3, sh = *nb & 7u;
    if (sh == 0) bits[byte] = 0;
    bits[byte] |= (uint8_t) ((b & 1u) << sh);
    (*nb)++;
    return true;
}

static uint32_t get_bit(const uint8_t *bits, uint32_t i)
{
    return (bits[i >> 3] >> (i & 7u)) & 1u;
}

int32_t br_hdlc_stuff(const uint8_t *frame, uint32_t n, uint8_t *bits, uint32_t cap_bytes,
                      uint32_t *nbits)
{
    if (!frame || !bits || !nbits || n == 0 || n > BR_AX25_FRAME_MAX) return BR_ERR_ARG;
    uint32_t nb = 0, ones = 0;
    for (uint32_t k = 0; k < 8u; k++)
        if (!put_bit(bits, cap_bytes, &nb, (BR_AX25_FLAG >> k) & 1u)) return BR_ERR_SIZE;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t k = 0; k < 8u; k++) {
            uint32_t b = (frame[i] >> k) & 1u;
            if (!put_bit(bits, cap_bytes, &nb, b)) return BR_ERR_SIZE;
            if (b) {
                if (++ones == 5u) {
                    if (!put_bit(bits, cap_bytes, &nb, 0)) return BR_ERR_SIZE;
                    ones = 0;
                }
            } else {
                ones = 0;
            }
        }
    }
    for (uint32_t k = 0; k < 8u; k++)
        if (!put_bit(bits, cap_bytes, &nb, (BR_AX25_FLAG >> k) & 1u)) return BR_ERR_SIZE;
    *nbits = nb;
    return (int32_t) ((nb + 7u) >> 3);
}

int32_t br_hdlc_unstuff(const uint8_t *bits, uint32_t nbits, uint8_t *out, uint32_t cap)
{
    if (!bits || !out) return BR_ERR_ARG;
    /* find the first flag 01111110 (LSB-first: 0,1,1,1,1,1,1,0) */
    uint32_t i = 0, ones = 0;
    bool found = false;
    for (; i < nbits; i++) {
        uint32_t b = get_bit(bits, i);
        if (b) {
            ones++;
        } else {
            if (ones == 6u && i >= 7u && get_bit(bits, i - 7u) == 0) {
                found = true;
                i++;
                break;
            }
            ones = 0;
        }
    }
    if (!found) return BR_ERR_FORMAT;
    /* skip repeated flags */
    while (i + 8u <= nbits) {
        uint32_t v = 0;
        for (uint32_t k = 0; k < 8u; k++) v |= get_bit(bits, i + k) << k;
        if (v != BR_AX25_FLAG) break;
        i += 8u;
    }
    uint32_t o = 0, acc = 0, nacc = 0;
    ones = 0;
    for (; i < nbits; i++) {
        uint32_t b = get_bit(bits, i);
        if (b) {
            ones++;
            if (ones >= 7u) return BR_ERR_FORMAT; /* abort */
            acc |= 1u << nacc;
            nacc++;
        } else {
            if (ones == 5u) { /* stuffed zero */
                ones = 0;
                continue;
            }
            if (ones == 6u) { /* closing flag: drop its first 7 bits from acc */
                if (nacc < 7u) return BR_ERR_FORMAT;
                nacc -= 7u;
                if (nacc != 0) return BR_ERR_FORMAT;
                return o >= 4u ? (int32_t) o : BR_ERR_FORMAT;
            }
            ones = 0;
            nacc++;
        }
        if (nacc == 8u + 7u) {
            /* keep 7 bits of look-behind so a flag's leading 0111111 can be
             * removed; emit the oldest octet. */
            if (o >= cap) return BR_ERR_SIZE;
            out[o++] = (uint8_t) acc;
            acc >>= 8;
            nacc -= 8u;
        }
    }
    return BR_ERR_FORMAT; /* no closing flag */
}
