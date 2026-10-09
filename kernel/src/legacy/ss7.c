/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ss7.c — see ss7.h. M3UA + SCCP UDT + ITU global title. */
#include "ss7.h"
#include "legacy_util.h"

/* ===================== M3UA ===================== */

bool m3ua_parse_data(const uint8_t *buf, uint32_t len, m3ua_data *d)
{
    if (len < 8) return false;
    if (buf[0] != M3UA_VERSION) return false;
    if (buf[2] != M3UA_CLASS_TRANSFER || buf[3] != M3UA_TYPE_DATA) return false;
    uint32_t mlen = lg_get32(buf + 4);
    if (mlen < 8 || mlen > len) return false;

    /* parameters start at offset 8 */
    uint32_t pos = 8;
    bool got = false;
    while (pos + 4 <= mlen) {
        uint16_t tag = lg_get16(buf + pos);
        uint16_t plen = lg_get16(buf + pos + 2);
        if (plen < 4 || pos + plen > mlen) return false;
        if (tag == M3UA_PARAM_PROTO_DATA) {
            /* protocol data: OPC(4) DPC(4) SI NI MP SLS then user data */
            if (plen < 4 + 12) return false;
            const uint8_t *v = buf + pos + 4;
            d->opc = lg_get32(v);
            d->dpc = lg_get32(v + 4);
            d->si = v[8];
            d->ni = v[9];
            d->mp = v[10];
            d->sls = v[11];
            uint32_t ul = plen - 4 - 12;
            if (ul > SS7_MAX_PAYLOAD) return false;
            lg_copy(d->payload, v + 12, ul);
            d->payload_len = ul;
            got = true;
        }
        /* advance with 4-byte padding (RFC 4666 3.1) */
        uint32_t padded = (plen + 3u) & ~3u;
        pos += padded;
    }
    return got;
}

uint32_t m3ua_build_data(const m3ua_data *d, uint8_t *out, uint32_t cap)
{
    if (d->payload_len > SS7_MAX_PAYLOAD) return 0;
    uint32_t param_v = 12 + d->payload_len; /* value length */
    uint32_t param_len = 4 + param_v;       /* with param header */
    uint32_t padded = (param_len + 3u) & ~3u;
    uint32_t total = 8 + padded;
    if (total > cap) return 0;

    lg_fill(out, 0, total);
    out[0] = M3UA_VERSION;
    out[1] = 0;
    out[2] = M3UA_CLASS_TRANSFER;
    out[3] = M3UA_TYPE_DATA;
    lg_put32(out + 4, total);

    uint8_t *p = out + 8;
    lg_put16(p, M3UA_PARAM_PROTO_DATA);
    lg_put16(p + 2, (uint16_t) param_len);
    uint8_t *v = p + 4;
    lg_put32(v, d->opc);
    lg_put32(v + 4, d->dpc);
    v[8] = d->si;
    v[9] = d->ni;
    v[10] = d->mp;
    v[11] = d->sls;
    lg_copy(v + 12, d->payload, d->payload_len);
    return total;
}

/* ===================== SCCP global title ===================== */

/* decode BCD digits (low nibble first) into ASCII; odd/even per encoding */
static uint32_t bcd_to_ascii(const uint8_t *in, uint32_t octets, bool odd, uint8_t *out,
                             uint32_t cap)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < octets; i++) {
        uint8_t lo = in[i] & 0x0F;
        uint8_t hi = (uint8_t) (in[i] >> 4);
        if (n < cap) out[n++] = (lo <= 9) ? (uint8_t) ('0' + lo) : (lo == 0x0B ? '*' : '#');
        bool last = (i == octets - 1);
        if (last && odd) break; /* odd count: high nibble is filler */
        if (n < cap) out[n++] = (hi <= 9) ? (uint8_t) ('0' + hi) : (hi == 0x0B ? '*' : '#');
    }
    return n;
}

static uint32_t ascii_to_bcd(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap)
{
    uint32_t octets = (n + 1) / 2;
    if (octets > cap) return 0;
    lg_fill(out, 0, octets);
    for (uint32_t i = 0; i < n; i++) {
        uint8_t v;
        if (in[i] == '*')
            v = 0x0B;
        else if (in[i] == '#')
            v = 0x0C;
        else
            v = (uint8_t) (in[i] - '0');
        if (i & 1)
            out[i / 2] |= (uint8_t) (v << 4);
        else
            out[i / 2] |= v;
    }
    if (n & 1) out[octets - 1] |= 0xF0; /* filler nibble */
    return octets;
}

void sccp_set_gt(sccp_addr *a, uint8_t ssn, uint8_t tt, uint8_t np, uint8_t nai, const char *digits)
{
    a->route_on_ssn = false;
    a->ssn = ssn;
    a->pc_present = 0;
    a->pc = 0;
    /* indicator: GTI=0100 (bits 2-5), SSN present if ssn!=0 (bit 0), no PC */
    a->indicator = (uint8_t) (0x04 << 2);
    if (ssn) a->indicator |= 0x01;
    a->gt.present = true;
    a->gt.translation_type = tt;
    a->gt.numbering_plan = np & 0x0F;
    a->gt.nature_of_address = nai & 0x7F;
    uint32_t i = 0;
    while (digits[i] && i < SS7_MAX_GT_DIGITS) {
        a->gt.digits[i] = (uint8_t) digits[i];
        i++;
    }
    a->gt.ndigits = i;
    a->gt.encoding_scheme = (i & 1) ? 1u : 2u; /* 1=BCD odd, 2=BCD even */
}

/* encode an SCCP address into out[cap]; returns length or 0 */
static uint32_t enc_addr(const sccp_addr *a, uint8_t *out, uint32_t cap)
{
    lg_writer w;
    lg_w_init(&w, out, cap);
    lg_w_byte(&w, a->indicator);
    if (a->pc_present) {
        lg_w_byte(&w, (uint8_t) (a->pc & 0xFF));
        lg_w_byte(&w, (uint8_t) (a->pc >> 8));
    }
    if (a->indicator & 0x01) lg_w_byte(&w, a->ssn);
    if (a->gt.present) {
        lg_w_byte(&w, a->gt.translation_type);
        lg_w_byte(
            &w, (uint8_t) ((a->gt.encoding_scheme & 0x0F) | ((a->gt.numbering_plan & 0x0F) << 4)));
        lg_w_byte(&w, (uint8_t) (a->gt.nature_of_address & 0x7F));
        uint8_t bcd[SS7_MAX_GT_DIGITS];
        uint32_t bo = ascii_to_bcd(a->gt.digits, a->gt.ndigits, bcd, sizeof bcd);
        lg_w_bytes(&w, bcd, bo);
    }
    if (!lg_w_ok(&w)) return 0;
    return w.len;
}

/* decode an SCCP address of exactly alen bytes */
static bool dec_addr(const uint8_t *p, uint32_t alen, sccp_addr *a)
{
    if (alen < 1) return false;
    uint32_t pos = 0;
    a->indicator = p[pos++];
    a->pc_present = (a->indicator & 0x01) ? 0 : 0; /* bit0 is SSN indicator in ITU */
    /* ITU address indicator: bit0 = SSN present, bit1 = PC present, bits2-5 GTI */
    bool pc_present = (a->indicator & 0x02) != 0;
    bool ssn_present = (a->indicator & 0x01) != 0;
    uint8_t gti = (uint8_t) ((a->indicator >> 2) & 0x0F);
    a->route_on_ssn = (a->indicator & 0x40) != 0;
    a->pc_present = pc_present ? 1 : 0;
    a->pc = 0;
    a->ssn = 0;
    if (pc_present) {
        if (pos + 2 > alen) return false;
        a->pc = (uint16_t) (p[pos] | ((uint16_t) p[pos + 1] << 8));
        pos += 2;
    }
    if (ssn_present) {
        if (pos + 1 > alen) return false;
        a->ssn = p[pos++];
    }
    a->gt.present = false;
    if (gti == 0x04) {
        if (pos + 3 > alen) return false;
        a->gt.present = true;
        a->gt.translation_type = p[pos++];
        a->gt.encoding_scheme = p[pos] & 0x0F;
        a->gt.numbering_plan = (uint8_t) (p[pos] >> 4);
        pos++;
        a->gt.nature_of_address = p[pos++] & 0x7F;
        bool odd = (a->gt.encoding_scheme == 1);
        a->gt.ndigits = bcd_to_ascii(p + pos, alen - pos, odd, a->gt.digits, SS7_MAX_GT_DIGITS);
    }
    return true;
}

/* ===================== SCCP UDT ===================== */

bool sccp_parse_udt(const uint8_t *buf, uint32_t len, sccp_udt *u)
{
    if (len < 5) return false;
    if (buf[0] != SCCP_MSG_UDT) return false;
    u->protocol_class = buf[1];
    /* three pointers follow, each relative to its own position (Q.713) */
    uint32_t p_called = buf[2];
    uint32_t p_calling = buf[3];
    uint32_t p_data = buf[4];
    /* pointer of value k at byte offset base means field at base + k */
    uint32_t off_called = 2 + p_called;
    uint32_t off_calling = 3 + p_calling;
    uint32_t off_data = 4 + p_data;
    if (off_called >= len || off_calling >= len || off_data >= len) return false;
    /* each party address is length-prefixed */
    uint8_t lc = buf[off_called];
    if (off_called + 1 + lc > len) return false;
    if (!dec_addr(buf + off_called + 1, lc, &u->called)) return false;
    uint8_t lg = buf[off_calling];
    if (off_calling + 1 + lg > len) return false;
    if (!dec_addr(buf + off_calling + 1, lg, &u->calling)) return false;
    /* data is length-prefixed too */
    uint16_t ld = buf[off_data];
    if (off_data + 1 + ld > len) return false;
    if (ld > SS7_MAX_PAYLOAD) return false;
    lg_copy(u->data, buf + off_data + 1, ld);
    u->data_len = ld;
    return true;
}

uint32_t sccp_build_udt(const sccp_udt *u, uint8_t *out, uint32_t cap)
{
    if (u->data_len > SS7_MAX_PAYLOAD) return 0;
    /* layout: type, class, ptr_called, ptr_calling, ptr_data, then
     * [len called][called][len calling][calling][len data][data] */
    uint8_t called[64], calling[64];
    uint32_t cl = enc_addr(&u->called, called, sizeof called);
    uint32_t gl = enc_addr(&u->calling, calling, sizeof calling);
    if ((cl == 0 && u->called.indicator) || (gl == 0 && u->calling.indicator)) return 0;

    uint32_t pos = 5; /* fixed header */
    uint32_t off_called = pos;
    uint32_t off_calling = off_called + 1 + cl;
    uint32_t off_data = off_calling + 1 + gl;
    uint32_t total = off_data + 1 + u->data_len;
    if (total > cap) return 0;
    if (cl > 255 || gl > 255 || u->data_len > 255) return 0;

    out[0] = SCCP_MSG_UDT;
    out[1] = u->protocol_class;
    out[2] = (uint8_t) (off_called - 2);
    out[3] = (uint8_t) (off_calling - 3);
    out[4] = (uint8_t) (off_data - 4);
    out[off_called] = (uint8_t) cl;
    lg_copy(out + off_called + 1, called, cl);
    out[off_calling] = (uint8_t) gl;
    lg_copy(out + off_calling + 1, calling, gl);
    out[off_data] = (uint8_t) u->data_len;
    lg_copy(out + off_data + 1, u->data, u->data_len);
    return total;
}
