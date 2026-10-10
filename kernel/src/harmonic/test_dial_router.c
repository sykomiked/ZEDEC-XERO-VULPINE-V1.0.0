/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_dial_router.c — the dial-router spec's T1-T3, plus country-code
 * splitting, pack/unpack round trips, single-bit corruption, virtual-address
 * ranges, bounded parser fuzzing (host: a guard page right after the NUL),
 * the line check on a real trunk-bank carrier, a frame sent over the trunk
 * bank, and the rendezvous hook. */
#ifndef ZT_HTEST_BARE
#    define _DEFAULT_SOURCE /* MAP_ANONYMOUS under -std=c11 on newer glibc */
#endif
#include "zt_htest.h"
#include "zt_dial_resolve.h"

#if defined(TEST_HOST)
#    include <sys/mman.h>
#    include <unistd.h>
#endif

static bool str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void t1_reject(void)
{
    zt_phone_descriptor_t d;
    HT_CHECK(!zt_dial_parse_number("011-14155550199", &d), "T1 wrong escape 011- rejected");
    HT_CHECK(!zt_dial_parse_number("101-14155550199", &d),
             "T1 missing country-code minus rejected");
    HT_CHECK(d.subscriber_id == 0 && d.country_code == 0, "T1 rejected parse leaves zeros");
    static const char *const bad[] = {
        "",
        "101-",
        "101--",
        "101--1",
        "101--1415555019",       /* NANP 9 digits */
        "101--141555501999",     /* NANP 11 digits */
        "101--11155550199",      /* NANP area code starting with 1 */
        "101--14151550199",      /* NANP exchange starting with 1 */
        "101--2891234567",       /* 28, 289: spare */
        "101--0441234567",       /* no country code starts with 0 */
        "101--44123456",         /* UK, 6 national digits */
        "101--4412345678901234", /* 16 digits > E.164 15 */
        "101--1415555019a",
        "101--1 4155550199",
        "101-+14155550199",
        "101---14155550199",
        " 101--14155550199",
        "101--14155550199 ",
    };
    bool all = true;
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        all = all && !zt_dial_parse_number(bad[i], &d);
    HT_CHECK(all, "T1 malformed strings rejected");
    HT_CHECK(!zt_dial_parse_number(0, &d) && !zt_dial_parse_number("101--14155550199", 0),
             "T1 NULL arguments rejected");
}

static void t2_trunks(void)
{
    zt_phone_descriptor_t d;
    HT_CHECK(zt_dial_parse_number("101--14155550193", &d), "T2 101--14155550193 parses");
    HT_CHECK(d.country_code == -1 && d.routing_prefix == 415 && d.subscriber_id == 5550193u,
             "T2 NANP split: CC -1, NPA 415, subscriber 5550193");
    HT_CHECK(d.assigned_trunk == 3 && d.carrier_freq_hz == 444 && !d.is_fleet_line,
             "T2 ...0193: trunk 3, 444 Hz, not fleet");
    HT_CHECK(zt_dial_parse_number("101--14155550199", &d), "T2 101--14155550199 parses");
    HT_CHECK(d.assigned_trunk == 9 && d.carrier_freq_hz == 1111 && d.is_fleet_line,
             "T2 ...0199: trunk 9, 1111 Hz, fleet");
    char s[] = "101--14155550190";
    bool all = true;
    for (int k = 0; k < 10; k++) {
        s[15] = (char) ('0' + k);
        all = all && zt_dial_parse_number(s, &d) && d.assigned_trunk == k &&
              d.carrier_freq_hz == zt_wire_trunk_hz((uint8_t) k) && d.is_fleet_line == (k == 9);
    }
    HT_CHECK(all, "T2 every last digit picks its trunk and carrier");
    /* D2/D3 outside NANP */
    HT_CHECK(zt_dial_parse_number("101--442079460000", &d) && d.country_code == -44 &&
                 d.prefix_digits == 3 && d.routing_prefix == 207 && d.subscriber_id == 9460000u,
             "T2 UK +44: approximate split 207 / 9460000");
    HT_CHECK(zt_dial_parse_number("101--3531234567", &d) && d.country_code == -353 &&
                 d.prefix_digits == 0 && d.subscriber_digits == 7,
             "T2 3-digit CC 353, 7 national digits: no prefix");
    HT_CHECK(zt_dial_parse_number("101--8613912345678", &d) && d.country_code == -86 &&
                 d.prefix_digits == 3 && d.subscriber_id == 12345678u,
             "T2 CC 86, 11 national digits");
    HT_CHECK(zt_dial_parse_number("101--70123456789012", &d) && d.country_code == -7 &&
                 d.prefix_digits == 3 && d.subscriber_digits == 10 &&
                 d.subscriber_id == 3456789012ull,
             "T2 CC 7, 14 national digits (15 total): 10-digit subscriber above 2^32");
    HT_CHECK(zt_dial_parse_number("101--4400001234567", &d) && d.routing_prefix == 0 &&
                 d.prefix_digits == 3 && d.subscriber_id == 1234567u,
             "T2 leading zeros kept by the digit counts");
    HT_CHECK(zt_dial_is_country_code(1) && zt_dial_is_country_code(998) &&
                 !zt_dial_is_country_code(28) && !zt_dial_is_country_code(999),
             "T2 E.164 table lookups");
}

static void t3_frame(void)
{
    zt_phone_descriptor_t d;
    zt_projected_coord_t c;
    zt_dial_frame_t f;
    uint8_t o[21];
    zt_dial_parse_number("101--14155550193", &d);
    zt_dial_derive_coordinates(&d, ZT_RESOLVE_WGS84, &c);
    zt_dial_pack_frame(&d, &c, &f);
    zt_wire_to_octets(&f, o);
    HT_CHECK(o[0] == 3, "T3 byte 0 is the trunk");
    uint32_t w0 = 5550193u, w1 = 415u | 3u << 10 | 7u << 12;
    HT_CHECK(o[1] == (uint8_t) w0 && o[4] == (uint8_t) (w0 >> 24), "T3 W0 little-endian");
    HT_CHECK(o[5] == (uint8_t) (w1 >> 24) && o[8] == (uint8_t) w1, "T3 W1 byte-swapped (BE)");
    uint32_t w2 = (uint32_t) c.point.geo.lat_e4deg, w3 = (uint32_t) c.point.geo.lon_e4deg;
    HT_CHECK(o[9] == (uint8_t) w2 && o[12] == (uint8_t) (w2 >> 24), "T3 W2 little-endian (lat)");
    HT_CHECK(o[13] == (uint8_t) (w3 >> 24) && o[16] == (uint8_t) w3,
             "T3 W3 byte-swapped (BE, lon)");
    uint32_t w4 = (uint32_t) o[17] | (uint32_t) o[18] << 8;
    HT_CHECK((w4 & 0x3FFu) == 1u && ((w4 >> 10) & 3u) == ZT_RESOLVE_WGS84,
             "T3 W4 bits 0-9 CC, 10-11 domain");
    uint8_t buf[19];
    for (int i = 0; i < 17; i++) buf[i] = o[i];
    buf[17] = o[17];
    buf[18] = (uint8_t) (o[18] & 0x0Fu);
    uint32_t crc = zt_dial_crc32(0, buf, 19);
    uint32_t w4full =
        (uint32_t) o[17] | (uint32_t) o[18] << 8 | (uint32_t) o[19] << 16 | (uint32_t) o[20] << 24;
    HT_CHECK((w4full & 0xFFFFF000u) == (crc & 0xFFFFF000u), "T3 W4 bits 12-31 are the CRC-32");
    HT_CHECK(zt_dial_crc32(0, (const uint8_t *) "123456789", 9) == 0xCBF43926u,
             "T3 CRC-32 check value 0xCBF43926");
}

/* Round trips over all three domains and a spread of numbers. */
static const char *const good[] = {
    "101--14155550193",   "101--14155550199",    "101--442079460000",  "101--3531234567",
    "101--8613912345678", "101--70123456789012", "101--4400001234567", "101--819012345678",
    "101--2120612345678", "101--9981234567",
};

static void t4_roundtrip(void)
{
    bool rt = true, fmt = true, octs = true;
    for (size_t i = 0; i < sizeof good / sizeof good[0]; i++) {
        zt_phone_descriptor_t d, e;
        char s[ZT_DIAL_MAX_CHARS];
        rt = rt && zt_dial_parse_number(good[i], &d);
        fmt = fmt && zt_dial_format(&d, s, sizeof s) && str_eq(s, good[i]);
        for (int t = 0; t <= 3; t++) {
            zt_projected_coord_t c, k;
            zt_dial_frame_t f, g;
            uint8_t o[21];
            zt_dial_derive_coordinates(&d, (zt_resolve_type_t) t, &c);
            zt_dial_pack_frame(&d, t ? &c : 0, &f);
            zt_wire_to_octets(&f, o);
            zt_wire_from_octets(o, &g);
            bool ok = zt_dial_unpack_frame(&g, &e, &k);
            octs = octs && ok;
            rt = rt && ok && e.country_code == d.country_code &&
                 e.routing_prefix == d.routing_prefix && e.subscriber_id == d.subscriber_id &&
                 e.prefix_digits == d.prefix_digits && e.subscriber_digits == d.subscriber_digits &&
                 e.assigned_trunk == d.assigned_trunk && e.carrier_freq_hz == d.carrier_freq_hz &&
                 e.is_fleet_line == d.is_fleet_line && k.type == c.type;
            if (t == ZT_RESOLVE_SOCKET)
                rt = rt && k.point.socket.ip == c.point.socket.ip &&
                     k.point.socket.port == c.point.socket.port;
            if (t == ZT_RESOLVE_WGS84)
                rt = rt && k.point.geo.lat_e4deg == c.point.geo.lat_e4deg &&
                     k.point.geo.lon_e4deg == c.point.geo.lon_e4deg;
            if (t == ZT_RESOLVE_ORBITAL)
                rt = rt && k.point.orbital.ra_mdeg == c.point.orbital.ra_mdeg &&
                     k.point.orbital.dec_mdeg == c.point.orbital.dec_mdeg;
        }
    }
    HT_CHECK(octs, "RT frames survive octets -> frame -> unpack");
    HT_CHECK(rt, "RT parse -> pack -> unpack restores descriptor and coordinates");
    HT_CHECK(fmt, "RT format(parse(s)) == s");

    /* Every single-bit error in the 168 bits is caught. */
    zt_phone_descriptor_t d;
    zt_projected_coord_t c;
    zt_dial_frame_t f, g;
    uint8_t o[21];
    zt_dial_parse_number("101--442079460000", &d);
    zt_dial_derive_coordinates(&d, ZT_RESOLVE_ORBITAL, &c);
    zt_dial_pack_frame(&d, &c, &f);
    zt_wire_to_octets(&f, o);
    uint32_t caught = 0;
    for (int bit = 0; bit < 168; bit++) {
        o[bit >> 3] ^= (uint8_t) (1u << (bit & 7));
        zt_wire_from_octets(o, &g);
        caught += !zt_dial_unpack_frame(&g, 0, 0);
        o[bit >> 3] ^= (uint8_t) (1u << (bit & 7));
    }
    HT_CHECK(caught == 168, "RT all 168 single-bit corruptions rejected");
    zt_wire_from_octets(o, &g);
    HT_CHECK(zt_dial_unpack_frame(&g, 0, 0), "RT the clean frame still unpacks");
}

static void t5_virtual(void)
{
    /* Many 7-digit numbers (+353) and 10-digit ones (+7): ranges and spread. */
    bool in_range = true;
    int32_t lat_min = 0, lat_max = 0, dec_min = 0, dec_max = 0;
    char s[] = "101--3530000000";
    uint32_t x = 12345u;
    for (int i = 0; i < 2000; i++) {
        x = x * 1103515245u + 12345u;
        uint32_t v = (x >> 8) % 10000000u;
        for (int k = 0; k < 7; k++) {
            s[14 - k] = (char) ('0' + v % 10u);
            v /= 10u;
        }
        zt_phone_descriptor_t d;
        zt_projected_coord_t a, b, e;
        if (!zt_dial_parse_number(s, &d)) {
            in_range = false;
            continue;
        }
        zt_dial_derive_coordinates(&d, ZT_RESOLVE_SOCKET, &a);
        zt_dial_derive_coordinates(&d, ZT_RESOLVE_WGS84, &b);
        zt_dial_derive_coordinates(&d, ZT_RESOLVE_ORBITAL, &e);
        in_range = in_range && (a.point.socket.ip >> 28) == 0xFu &&
                   a.point.socket.ip != 0xFFFFFFFFu && a.point.socket.port >= 49152u;
        in_range = in_range && b.point.geo.lat_e4deg >= -900000 &&
                   b.point.geo.lat_e4deg <= 900000 && b.point.geo.lon_e4deg >= -1800000 &&
                   b.point.geo.lon_e4deg < 1800000 && b.point.geo.alt_m == 0;
        in_range = in_range && e.point.orbital.ra_mdeg < 360000u &&
                   e.point.orbital.dec_mdeg >= -90000 && e.point.orbital.dec_mdeg <= 90000 &&
                   e.point.orbital.range_km == 0;
        if (b.point.geo.lat_e4deg < lat_min) lat_min = b.point.geo.lat_e4deg;
        if (b.point.geo.lat_e4deg > lat_max) lat_max = b.point.geo.lat_e4deg;
        if (e.point.orbital.dec_mdeg < dec_min) dec_min = e.point.orbital.dec_mdeg;
        if (e.point.orbital.dec_mdeg > dec_max) dec_max = e.point.orbital.dec_mdeg;
    }
    HT_CHECK(in_range, "V socket in 240.0.0.0/4 (port >= 49152), lat/lon/ra/dec in range");
    HT_CHECK(lat_min < -850000 && lat_max > 850000 && dec_min < -85000 && dec_max > 85000,
             "V short numbers spread over the whole range (no pole pinning)");
    zt_phone_descriptor_t d;
    zt_projected_coord_t a, b;
    zt_dial_parse_number("101--14155550199", &d);
    zt_dial_derive_coordinates(&d, ZT_RESOLVE_SOCKET, &a);
    zt_dial_derive_coordinates(&d, ZT_RESOLVE_SOCKET, &b);
    HT_CHECK(a.point.socket.ip == b.point.socket.ip && a.point.socket.port == b.point.socket.port,
             "V deterministic");
    zt_dial_derive_coordinates(&d, (zt_resolve_type_t) 7, &a);
    HT_CHECK(a.type == ZT_RESOLVE_NONE && a.point.socket.ip == 0, "V unknown domain -> NONE");
}

/* Bounded parser fuzzing. Host: each string is copied so its NUL is the last
 * byte before a PROT_NONE page; any read past the NUL faults. A window of
 * ZT_DIAL_MAX_CHARS non-NUL bytes before the guard checks the bound. */
static char fuzz_buf_static[64];

static void t6_fuzz(void)
{
    char *page_end = fuzz_buf_static + sizeof fuzz_buf_static;
#if defined(TEST_HOST)
    long ps = sysconf(_SC_PAGESIZE);
    char *m = mmap(0, (size_t) ps * 2u, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    bool guard = m != MAP_FAILED && mprotect(m + ps, (size_t) ps, PROT_NONE) == 0;
    HT_CHECK(guard, "F guard page mapped");
    if (guard) page_end = m + ps;
#endif
    static const char alpha[] = "0123456789-1+ a\x80";
    uint32_t x = 0xC0FFEEu;
    uint32_t accepted = 0, n = 0;
    bool canon = true;
    for (int it = 0; it < 200000; it++) {
        char s[48];
        uint32_t len = 0;
        x = x * 1664525u + 1013904223u;
        if (x & 0x100u) {
            const char *p = (x & 0x200u) ? "101--" : "101-";
            while (*p) s[len++] = *p++;
        }
        x = x * 1664525u + 1013904223u;
        uint32_t extra = (x >> 16) % 24u;
        for (uint32_t k = 0; k < extra && len < 40u; k++) {
            x = x * 1664525u + 1013904223u;
            uint32_t r = x >> 24;
            s[len++] = (r & 0xC0u) ? (char) ('0' + (r % 10u)) : alpha[r % (sizeof alpha - 1u)];
        }
        char *q = page_end - (len + 1u);
        for (uint32_t k = 0; k < len; k++) q[k] = s[k];
        q[len] = '\0';
        zt_phone_descriptor_t d;
        if (zt_dial_parse_number(q, &d)) {
            char t[ZT_DIAL_MAX_CHARS];
            accepted++;
            canon = canon && zt_dial_format(&d, t, sizeof t) && str_eq(t, q) &&
                    d.subscriber_digits >= 7 && d.subscriber_digits <= 12 && d.assigned_trunk < 10;
        }
        n++;
    }
    /* no NUL inside the window: must stop at ZT_DIAL_MAX_CHARS and reject */
    char *w = page_end - ZT_DIAL_MAX_CHARS;
    const char *p = "101--1415555019999999999999999999999";
    for (uint32_t k = 0; k < ZT_DIAL_MAX_CHARS; k++) w[k] = p[k];
    zt_phone_descriptor_t d;
    HT_CHECK(!zt_dial_parse_number(w, &d), "F no NUL within the bound: rejected, no overread");
    HT_CHECK(n == 200000 && accepted > 1000, "F 200000 random strings parsed without overread");
    HT_CHECK(canon, "F every accepted string formats back to itself");
    ht_puts("  fuzz: ");
    ht_putd(accepted);
    ht_puts(" of 200000 accepted\n");
#if defined(TEST_HOST)
    if (guard) munmap(m, (size_t) ps * 2u);
#endif
}

static int16_t pcm[ZT_TRUNK_FRAME_SAMPLES];

static void t7_line(void)
{
    zt_phone_descriptor_t d3, d9;
    zt_dial_parse_number("101--14155550193", &d3);
    zt_dial_parse_number("101--14155550199", &d9);
    zt_trunk_mux_bank_t mux;
    zt_trunk_demux_bank_t bank;
    zt_trunk_mux_init(&mux);
    zt_trunk_bank_init(&bank);
    for (size_t i = 0; i < 8000; i++) pcm[i] = 0;
    zt_trunk_mux_tone(&mux, (zt_trunk_id_t) 3, pcm, 8000);
    zt_trunk_bank_process_pcm(&bank, pcm, 8000);
    HT_CHECK(zt_dial_line_ready(&bank, &d3), "L trunk 3 carrier present: line ready");
    HT_CHECK(!zt_dial_line_ready(&bank, &d9), "L trunk 9 silent (DEAD): not ready");
    /* the dial frame itself over trunk 3 of the trunk bank */
    zt_projected_coord_t c;
    zt_dial_frame_t f, g;
    zt_dial_derive_coordinates(&d3, ZT_RESOLVE_SOCKET, &c);
    zt_dial_pack_frame(&d3, &c, &f);
    for (size_t i = 0; i < ZT_TRUNK_FRAME_SAMPLES; i++) pcm[i] = 0;
    bool tx = zt_trunk_mux_transmit_frame(&mux, (zt_trunk_id_t) 3, &f, pcm, ZT_TRUNK_FRAME_SAMPLES);
    bool rx = zt_trunk_read_frame(&bank, (zt_trunk_id_t) 3, pcm, ZT_TRUNK_FRAME_SAMPLES, &g);
    zt_phone_descriptor_t e;
    HT_CHECK(tx && rx && zt_dial_unpack_frame(&g, &e, 0) && e.subscriber_id == 5550193u,
             "L dial frame sent on trunk 3 and decoded from PCM");
}

static bool verify_stub(void *ctx, const uint8_t id[CALL_RV_ID_LEN], const uint8_t *msg,
                        uint32_t len, const uint8_t sig[CALL_RV_SIG_LEN])
{
    (void) ctx;
    (void) id;
    (void) msg;
    (void) len;
    (void) sig;
    return false;
}

static void t8_resolve(void)
{
    static const uint8_t kat[32] = {0x89, 0xdc, 0xbd, 0xbc, 0xd8, 0xcd, 0x86, 0x55,
                                    0x1d, 0x63, 0x60, 0xd7, 0xe5, 0xab, 0x63, 0x13,
                                    0x30, 0xc5, 0xea, 0x4f, 0x0b, 0x02, 0xd6, 0x0f,
                                    0x22, 0x53, 0x23, 0xe8, 0xc7, 0xf4, 0x2f, 0xa2};
    zt_phone_descriptor_t d, e;
    uint8_t a[32], b[32];
    zt_dial_parse_number("101--14155550199", &d);
    zt_dial_parse_number("101--14155550193", &e);
    bool ok = zt_dial_rendezvous_id(&d, a) && zt_dial_rendezvous_id(&e, b);
    bool same = true, diff = false;
    for (int i = 0; i < 32; i++) {
        same = same && a[i] == kat[i];
        diff = diff || a[i] != b[i];
    }
    HT_CHECK(ok && same, "R id = SHA-256(\"ZXV-DIAL-1\\0+14155550199\") (known answer)");
    HT_CHECK(diff, "R different numbers, different ids");
    static call_lookup_t l;
    call_rv_peer_t seed;
    for (int i = 0; i < 32; i++) seed.id[i] = (uint8_t) i;
    seed.addr.family = 4;
    for (int i = 0; i < 16; i++) seed.addr.ip[i] = 0;
    seed.addr.ip[0] = 192;
    seed.addr.ip[2] = 2; /* 192.0.2.1, documentation range */
    seed.addr.ip[3] = 1;
    seed.addr.port = 4000;
    HT_CHECK(!zt_dial_resolve_start(&l, &d, &seed, 1, 500, 1, 0, 0),
             "R no verifier: lookup refused");
    HT_CHECK(zt_dial_resolve_start(&l, &d, &seed, 1, 500, 1, verify_stub, 0) &&
                 l.state == CALL_LOOKUP_RUNNING && l.ncand == 1,
             "R lookup started with the number's id");
    same = true;
    for (int i = 0; i < 32; i++) same = same && l.target[i] == kat[i];
    HT_CHECK(same, "R lookup target is the rendezvous id");
    call_rv_peer_t to;
    call_rv_msg_t msg;
    HT_CHECK(call_lookup_next(&l, 0, &to, &msg) && msg.type == CALL_RV_LOOKUP &&
                 to.addr.port == 4000,
             "R first LOOKUP goes to the seed");
}

int main(void)
{
    t1_reject();
    t2_trunks();
    t3_frame();
    t4_roundtrip();
    t5_virtual();
    t6_fuzz();
    t7_line();
    t8_resolve();
    return ht_finish("test_dial_router");
}
