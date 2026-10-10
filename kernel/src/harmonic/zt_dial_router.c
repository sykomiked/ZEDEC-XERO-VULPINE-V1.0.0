/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_dial_router.c — see zt_dial_router.h (D1-D6). */
#include "zt_dial_router.h"
#include "zt_e164_codes.h"
#include "../tensor/zt.h" /* zt_udiv64 */

bool zt_dial_is_country_code(uint32_t code)
{
    uint32_t lo = 0, hi = ZT_E164_CODE_COUNT;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        if (ZT_E164_CODES[mid] == code) return true;
        if (ZT_E164_CODES[mid] < code)
            lo = mid + 1u;
        else
            hi = mid;
    }
    return false;
}

uint32_t zt_dial_crc32(uint32_t crc, const uint8_t *p, size_t n)
{
    crc = ~crc;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void desc_clear(zt_phone_descriptor_t *d)
{
    d->country_code = 0;
    d->routing_prefix = 0;
    d->subscriber_id = 0;
    d->assigned_trunk = 0;
    d->carrier_freq_hz = 0;
    d->is_fleet_line = false;
    d->prefix_digits = 0;
    d->subscriber_digits = 0;
}

static uint32_t digits_value32(const char *p, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) v = v * 10u + (uint32_t) (p[i] - '0');
    return v;
}

static uint64_t digits_value64(const char *p, uint32_t n)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < n; i++) v = v * 10u + (uint64_t) (p[i] - '0');
    return v;
}

/* D4: trunk and carrier from the subscriber number. */
static void desc_line(zt_phone_descriptor_t *d)
{
    uint64_t rem = 0;
    (void) zt_udiv64(d->subscriber_id, ZT_TRUNK_COUNT, &rem);
    d->assigned_trunk = (uint8_t) rem;
    d->carrier_freq_hz = ZT_CARRIER_FREQS[d->assigned_trunk];
    d->is_fleet_line = d->assigned_trunk == ZT_WIRE_EXPANSION;
}

bool zt_dial_parse_number(const char *s, zt_phone_descriptor_t *out)
{
    if (!out) return false;
    desc_clear(out);
    if (!s) return false;
    /* D1: bounded length; never read past the first NUL. */
    uint32_t len = 0;
    while (len < ZT_DIAL_MAX_CHARS && s[len] != '\0') len++;
    if (len == ZT_DIAL_MAX_CHARS) return false;
    if (len < 5u || s[0] != '1' || s[1] != '0' || s[2] != '1' || s[3] != '-' || s[4] != '-')
        return false;
    const char *dg = s + 5;
    uint32_t nd = len - 5u;
    for (uint32_t i = 0; i < nd; i++)
        if (dg[i] < '0' || dg[i] > '9') return false;
    if (nd > ZT_MAX_DIAL_DIGITS) return false;
    /* D2: the unique country-code prefix (the table is prefix-free). */
    uint32_t cc = 0, cl = 0;
    for (uint32_t k = 1; k <= 3u && k < nd; k++) {
        uint32_t v = digits_value32(dg, k);
        if (dg[0] != '0' && zt_dial_is_country_code(v)) {
            cc = v;
            cl = k;
            break;
        }
    }
    if (!cl) return false;
    const char *nat = dg + cl;
    uint32_t L = nd - cl, p;
    /* D3 */
    if (cc == 1u) {
        if (L != 10u || nat[0] < '2' || nat[3] < '2') return false;
        p = 3u;
    } else {
        if (L < ZT_DIAL_SUB_MIN) return false;
        uint32_t a = L >= 12u ? L - 12u : 0u, b = L - 7u < 3u ? L - 7u : 3u;
        p = a > b ? a : b;
    }
    uint32_t sl = L - p;
    if (sl < ZT_DIAL_SUB_MIN || sl > ZT_DIAL_SUB_MAX) return false;
    out->country_code = (int16_t) - (int32_t) cc;
    out->routing_prefix = digits_value32(nat, p);
    out->prefix_digits = (uint8_t) p;
    out->subscriber_id = digits_value64(nat + p, sl);
    out->subscriber_digits = (uint8_t) sl;
    desc_line(out);
    return true;
}

/* SplitMix64 finaliser (a bijective 64-bit mixer; not cryptographic). */
static uint64_t mix64(uint64_t z)
{
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static uint32_t cc_abs(const zt_phone_descriptor_t *d)
{
    int32_t c = d->country_code;
    return (uint32_t) (c < 0 ? -c : c);
}

void zt_dial_derive_coordinates(const zt_phone_descriptor_t *d, zt_resolve_type_t dom,
                                zt_projected_coord_t *o)
{
    if (!o) return;
    o->type = ZT_RESOLVE_NONE;
    o->point.orbital.ra_mdeg = 0;
    o->point.orbital.dec_mdeg = 0;
    o->point.orbital.range_km = 0;
    o->point.socket.ip = 0;
    o->point.socket.port = 0;
    if (!d) return;
    uint64_t m = mix64(d->subscriber_id ^ ((uint64_t) d->routing_prefix << 40) ^
                       ((uint64_t) d->prefix_digits << 50) ^ ((uint64_t) cc_abs(d) << 52));
    uint32_t hi = (uint32_t) (m >> 32), lo = (uint32_t) m;
    switch (dom) {
    case ZT_RESOLVE_SOCKET: {
        uint32_t ip = ZT_DIAL_VIRT_NET | (lo & 0x0FFFFFFFu);
        if (ip == 0xFFFFFFFFu) ip = 0xFFFFFFFEu; /* not the limited broadcast */
        o->type = dom;
        o->point.socket.ip = ip;
        o->point.socket.port = (uint16_t) (ZT_DIAL_VIRT_PORT_LO + (hi & 0x3FFFu));
        break;
    }
    case ZT_RESOLVE_WGS84:
        o->type = dom;
        o->point.geo.lat_e4deg = (int32_t) (hi % 1800001u) - 900000;
        o->point.geo.lon_e4deg = (int32_t) (lo % 3600000u) - 1800000;
        o->point.geo.alt_m = 0;
        break;
    case ZT_RESOLVE_ORBITAL:
        o->type = dom;
        o->point.orbital.ra_mdeg = hi % 360000u;
        o->point.orbital.dec_mdeg = (int32_t) (lo % 180001u) - 90000;
        o->point.orbital.range_km = 0;
        break;
    default:
        break;
    }
}

static void coord_words(const zt_projected_coord_t *c, uint32_t *w2, uint32_t *w3)
{
    *w2 = 0;
    *w3 = 0;
    if (!c) return;
    switch (c->type) {
    case ZT_RESOLVE_SOCKET:
        *w2 = c->point.socket.ip;
        *w3 = c->point.socket.port;
        break;
    case ZT_RESOLVE_WGS84:
        *w2 = (uint32_t) c->point.geo.lat_e4deg;
        *w3 = (uint32_t) c->point.geo.lon_e4deg;
        break;
    case ZT_RESOLVE_ORBITAL:
        *w2 = c->point.orbital.ra_mdeg;
        *w3 = (uint32_t) c->point.orbital.dec_mdeg;
        break;
    default:
        break;
    }
}

/* Top 20 bits of the CRC over octets 0..16 and W4's low 12 bits. */
static uint32_t frame_check(const zt_dial_frame_t *f, uint32_t w4_low12)
{
    uint8_t oct[21];
    zt_wire_to_octets(f, oct);
    const uint8_t tail[2] = {(uint8_t) w4_low12, (uint8_t) ((w4_low12 >> 8) & 0x0Fu)};
    uint32_t crc = zt_dial_crc32(0, oct, 17);
    crc = zt_dial_crc32(crc, tail, 2);
    return crc & 0xFFFFF000u;
}

void zt_dial_pack_frame(const zt_phone_descriptor_t *d, const zt_projected_coord_t *c,
                        zt_dial_frame_t *f)
{
    if (!d || !f) return;
    uint32_t w2, w3;
    coord_words(c, &w2, &w3);
    uint32_t type = c ? (uint32_t) c->type & 3u : 0u;
    uint32_t w0 = (uint32_t) d->subscriber_id;
    uint32_t w1 = (d->routing_prefix & 0x3FFu) | ((uint32_t) (d->prefix_digits & 3u) << 10) |
                  ((uint32_t) (d->subscriber_digits & 0xFu) << 12) |
                  ((uint32_t) (d->subscriber_id >> 32) & 0xFFu) << 24;
    uint32_t low12 = (cc_abs(d) & 0x3FFu) | type << 10;
    uint8_t tag = zt_wire_make_tag(d->assigned_trunk, 0, false);
    uint32_t sp[3] = {w0, w2, low12};
    const uint32_t sm[2] = {w1, w3};
    zt_wire_pack_tagged(tag, sp, sm, f);
    sp[2] = low12 | frame_check(f, low12);
    zt_wire_pack_tagged(tag, sp, sm, f);
}

static bool coord_equal(const zt_projected_coord_t *a, const zt_projected_coord_t *b)
{
    uint32_t a2, a3, b2, b3;
    coord_words(a, &a2, &a3);
    coord_words(b, &b2, &b3);
    return a->type == b->type && a2 == b2 && a3 == b3;
}

bool zt_dial_unpack_frame(const zt_dial_frame_t *f, zt_phone_descriptor_t *d,
                          zt_projected_coord_t *c)
{
    zt_phone_descriptor_t t;
    zt_projected_coord_t k, want;
    desc_clear(&t);
    if (d) *d = t;
    if (c) zt_dial_derive_coordinates(0, ZT_RESOLVE_NONE, c);
    if (!f) return false;
    zt_unpacked_rails_t r;
    zt_wire_rails_init(&r);
    zt_wire_unpack_ubh168(f, &r);
    if (r.tag >= ZT_TRUNK_COUNT) return false; /* shell 0, no sync */
    uint32_t w0 = r.s_plus[0], w2 = r.s_plus[1], w4 = r.s_plus[2];
    uint32_t w1 = r.s_minus[0], w3 = r.s_minus[1];
    if ((w4 & 0xFFFFF000u) != frame_check(f, w4 & 0xFFFu)) return false;
    uint32_t cc = w4 & 0x3FFu, type = (w4 >> 10) & 3u;
    uint32_t pd = (w1 >> 10) & 3u, sd = (w1 >> 12) & 0xFu, pre = w1 & 0x3FFu;
    if (!zt_dial_is_country_code(cc) || (w1 & 0x00FF0000u)) return false;
    if (sd < ZT_DIAL_SUB_MIN || sd > ZT_DIAL_SUB_MAX) return false;
    if (pre >= (pd == 0 ? 1u : pd == 1 ? 10u : pd == 2 ? 100u : 1000u)) return false;
    t.country_code = (int16_t) - (int32_t) cc;
    t.routing_prefix = pre;
    t.prefix_digits = (uint8_t) pd;
    t.subscriber_id = (uint64_t) (w1 >> 24) << 32 | w0;
    t.subscriber_digits = (uint8_t) sd;
    uint64_t lim = 1;
    for (uint32_t i = 0; i < sd; i++) lim *= 10u;
    if (t.subscriber_id >= lim) return false;
    if (cc == 1u && (pd != 3u || sd != 7u)) return false;
    desc_line(&t);
    if (t.assigned_trunk != r.tag) return false;
    k.type = (zt_resolve_type_t) type;
    switch (k.type) {
    case ZT_RESOLVE_SOCKET:
        k.point.socket.ip = w2;
        k.point.socket.port = (uint16_t) w3;
        break;
    case ZT_RESOLVE_WGS84:
        k.point.geo.lat_e4deg = (int32_t) w2;
        k.point.geo.lon_e4deg = (int32_t) w3;
        k.point.geo.alt_m = 0;
        break;
    case ZT_RESOLVE_ORBITAL:
        k.point.orbital.ra_mdeg = w2;
        k.point.orbital.dec_mdeg = (int32_t) w3;
        k.point.orbital.range_km = 0;
        break;
    default:
        zt_dial_derive_coordinates(0, ZT_RESOLVE_NONE, &k);
        break;
    }
    zt_dial_derive_coordinates(&t, k.type, &want);
    if (!coord_equal(&k, &want) || (k.type == ZT_RESOLVE_NONE && (w2 | w3))) return false;
    if (d) *d = t;
    if (c) *c = want;
    return true;
}

static size_t put_digits(char *o, uint64_t v, uint32_t n)
{
    for (uint32_t i = n; i-- > 0;) {
        uint64_t q = zt_udiv64(v, 10u, 0);
        o[i] = (char) ('0' + (v - q * 10u));
        v = q;
    }
    return n;
}

size_t zt_dial_format(const zt_phone_descriptor_t *d, char *out, size_t cap)
{
    if (!d || !out) return 0;
    uint32_t cc = cc_abs(d);
    if (!zt_dial_is_country_code(cc)) return 0;
    uint32_t cl = cc >= 100u ? 3u : cc >= 10u ? 2u : 1u;
    size_t n = 5u + cl + d->prefix_digits + d->subscriber_digits;
    if (n + 1u > cap || d->prefix_digits > 3u || d->subscriber_digits > ZT_DIAL_SUB_MAX) return 0;
    out[0] = '1';
    out[1] = '0';
    out[2] = '1';
    out[3] = '-';
    out[4] = '-';
    size_t i = 5;
    i += put_digits(out + i, cc, cl);
    i += put_digits(out + i, d->routing_prefix, d->prefix_digits);
    i += put_digits(out + i, d->subscriber_id, d->subscriber_digits);
    out[i] = '\0';
    return i;
}

bool zt_dial_line_ready(const zt_trunk_demux_bank_t *bank, const zt_phone_descriptor_t *d)
{
    if (!bank || !d || d->assigned_trunk >= ZT_TRUNK_COUNT) return false;
    return zt_trunk_query_line(bank, (zt_trunk_id_t) d->assigned_trunk) == ZT_LINE_ON_HOOK;
}
