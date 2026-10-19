/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aero.c — aero-orbital: WGS-84 conversions against an 80-digit decimal
 * reference (test/geo_vectors.h, from tools/gen_geo.py) with the measured
 * error printed; range refusals, symmetry and round trips; CRC-32C; the
 * telemetry packet (every single-bit flip detected); UBH-168 carriage
 * (every single-bit flip, reordering, truncation, padding refused); the
 * SEU receiver (clean, disagreement, CRC failure, majority policy, late
 * copies, a full log, capacity) and analyst review (forged, stale,
 * unregistered, accept, reject) with offline decision verification. */
#include "geo_vectors.h"
#include "zaero.h"
#include "zind_test.h"

#undef CHECK_RC
#define CHECK_RC(expr, want)                                                                   \
    do {                                                                                       \
        int32_t rc_ = (expr);                                                                  \
        CHECK(rc_ == (want), "%s -> %d (%s), want %d (%s)", #expr, (int) rc_,                  \
              zao_status_name(rc_), (int) (want), zao_status_name(want));                     \
    } while (0)

#define NFWD ((int) (sizeof k_fwd / sizeof k_fwd[0]))
#define NINV ((int) (sizeof k_inv / sizeof k_inv[0]))

static int64_t iabs(int64_t v) { return v < 0 ? -v : v; }

static int64_t g_fwd_fine, g_fwd_mm, g_inv_ang_fine, g_inv_h_fine, g_inv_ang, g_inv_h;

static void track(int64_t *m, int64_t v) {
    if (iabs(v) > *m) *m = iabs(v);
}

/* ===== geodesy ===== */

static void test_geo_vectors(void) {
    for (int i = 0; i < NFWD; i++) {
        zao_geo g = {k_fwd[i].lat_nd, k_fwd[i].lon_nd, k_fwd[i].h_mm};
        zao_ecef_um u;
        zao_ecef m;
        CHECK_RC(zao_geo_to_ecef_um(&g, &u), ZXV_OK);
        CHECK_RC(zao_geo_to_ecef(&g, &m), ZXV_OK);
        track(&g_fwd_fine, u.x_um - k_fwd[i].x_um);
        track(&g_fwd_fine, u.y_um - k_fwd[i].y_um);
        track(&g_fwd_fine, u.z_um - k_fwd[i].z_um);
        CHECK(iabs(u.x_um - k_fwd[i].x_um) <= 1 && iabs(u.y_um - k_fwd[i].y_um) <= 1 &&
                  iabs(u.z_um - k_fwd[i].z_um) <= 1,
              "fwd %d: micrometre result within 1 um of the reference", i);
        track(&g_fwd_mm, m.x_mm * 1000 - k_fwd[i].x_um);
        track(&g_fwd_mm, m.y_mm * 1000 - k_fwd[i].y_um);
        track(&g_fwd_mm, m.z_mm * 1000 - k_fwd[i].z_um);
        CHECK(iabs(m.x_mm * 1000 - k_fwd[i].x_um) <= 501 &&
                  iabs(m.y_mm * 1000 - k_fwd[i].y_um) <= 501 &&
                  iabs(m.z_mm * 1000 - k_fwd[i].z_um) <= 501,
              "fwd %d: millimetre result is the reference rounded", i);
    }
    for (int i = 0; i < NINV; i++) {
        zao_ecef e = {k_inv[i].x_mm, k_inv[i].y_mm, k_inv[i].z_mm};
        zao_geo_fine f;
        zao_geo g;
        int in_range = k_inv[i].h_um >= ZAO_H_MIN * 1000 && k_inv[i].h_um <= ZAO_H_MAX * 1000;
        int edge = iabs(k_inv[i].h_um - ZAO_H_MIN * 1000) <= 1000 ||
                   iabs(k_inv[i].h_um - ZAO_H_MAX * 1000) <= 1000;
        int32_t st = zao_ecef_to_geo_fine(&e, &f);
        if (edge && st == ZAO_E_RANGE) {
            CHECK(1, "inv %d: within 1 mm of a height limit, refusal allowed", i);
            continue;
        }
        if (!in_range && !edge) {
            CHECK(st == ZAO_E_RANGE, "inv %d: reference height %lld um is out of range", i,
                  (long long) k_inv[i].h_um);
            CHECK_RC(zao_ecef_to_geo(&e, &g), ZAO_E_RANGE);
            continue;
        }
        CHECK(st == ZXV_OK, "inv %d -> %s", i, zao_status_name(st));
        CHECK_RC(zao_ecef_to_geo(&e, &g), ZXV_OK);
        track(&g_inv_ang_fine, f.lat_pd - k_inv[i].lat_pd);
        track(&g_inv_ang_fine, f.lon_pd - k_inv[i].lon_pd);
        track(&g_inv_h_fine, f.h_um - k_inv[i].h_um);
        CHECK(iabs(f.lat_pd - k_inv[i].lat_pd) <= 1 && iabs(f.lon_pd - k_inv[i].lon_pd) <= 1 &&
                  iabs(f.h_um - k_inv[i].h_um) <= 1,
              "inv %d: fine result within 1 pico-degree / 1 um", i);
        track(&g_inv_ang, g.lat_nd * 1000 - k_inv[i].lat_pd);
        track(&g_inv_ang, g.lon_nd * 1000 - k_inv[i].lon_pd);
        track(&g_inv_h, g.h_mm * 1000 - k_inv[i].h_um);
        CHECK(iabs(g.lat_nd * 1000 - k_inv[i].lat_pd) <= 501 &&
                  iabs(g.lon_nd * 1000 - k_inv[i].lon_pd) <= 501 &&
                  iabs(g.h_mm * 1000 - k_inv[i].h_um) <= 501,
              "inv %d: nano-degree / mm result is the reference rounded", i);
    }
    printf("geodesy vs the 80-digit reference (max |error|): geodetic->ECEF %lld um (fine), "
           "%lld um (mm output); ECEF->geodetic %lld pdeg / %lld um (fine), %lld pdeg / %lld um "
           "(nano-degree / mm output)\n",
           (long long) g_fwd_fine, (long long) g_fwd_mm, (long long) g_inv_ang_fine,
           (long long) g_inv_h_fine, (long long) g_inv_ang, (long long) g_inv_h);
}

static void test_geo_known(void) {
    zao_geo g = {0, 0, 0}, back;
    zao_ecef e;
    zao_geo_fine f;
    CHECK_RC(zao_geo_to_ecef(&g, &e), ZXV_OK);
    CHECK(e.x_mm == 6378137000 && e.y_mm == 0 && e.z_mm == 0, "equator/prime meridian = a");
    g.lat_nd = ZAO_LAT_MAX;
    CHECK_RC(zao_geo_to_ecef(&g, &e), ZXV_OK);
    CHECK(e.x_mm == 0 && e.y_mm == 0 && e.z_mm == 6356752314, "north pole = b (%lld)",
          (long long) e.z_mm);
    g.lat_nd = -ZAO_LAT_MAX;
    CHECK_RC(zao_geo_to_ecef(&g, &e), ZXV_OK);
    CHECK(e.z_mm == -6356752314, "south pole = -b");
    CHECK_RC(zao_ecef_to_geo(&e, &back), ZXV_OK);
    CHECK(back.lat_nd == -ZAO_LAT_MAX && back.lon_nd == 0 && back.h_mm == 0, "south pole back");
    g.lat_nd = 0;
    g.lon_nd = ZAO_LON_MAX;
    CHECK_RC(zao_geo_to_ecef(&g, &e), ZXV_OK);
    CHECK(e.x_mm == -6378137000 && e.y_mm == 0, "antimeridian");
    CHECK_RC(zao_ecef_to_geo(&e, &back), ZXV_OK);
    CHECK(back.lon_nd == ZAO_LON_MAX, "longitude comes back as +180, never -180");
    g.lon_nd = -ZAO_LON_MAX;
    CHECK_RC(zao_geo_to_ecef(&g, &e), ZXV_OK);
    CHECK_RC(zao_ecef_to_geo(&e, &back), ZXV_OK);
    CHECK(back.lon_nd == ZAO_LON_MAX, "-180 normalises to +180");
    CHECK_RC(zao_ecef_to_geo_fine(&e, &f), ZXV_OK);
    CHECK(f.lon_pd == ZAO_LON_MAX * 1000, "fine -180 normalises too");
    /* GEO: 35,786 km over 75 E */
    g.lat_nd = 0;
    g.lon_nd = 75000000000;
    g.h_mm = 35786000000;
    CHECK_RC(zao_geo_to_ecef(&g, &e), ZXV_OK);
    CHECK_RC(zao_ecef_to_geo(&e, &back), ZXV_OK);
    CHECK(iabs(back.lat_nd) <= 1 && iabs(back.lon_nd - 75000000000) <= 1 &&
              iabs(back.h_mm - 35786000000) <= 1,
          "GEO round trip");
}

static void test_geo_range(void) {
    zao_geo g = {0, 0, 0};
    zao_ecef e = {0, 0, 0};
    zao_ecef_um u;
    zao_geo_fine f;
    const zao_geo bad[] = {
        {ZAO_LAT_MAX + 1, 0, 0}, {-ZAO_LAT_MAX - 1, 0, 0}, {0, ZAO_LON_MAX + 1, 0},
        {0, -ZAO_LON_MAX - 1, 0}, {0, 0, ZAO_H_MIN - 1}, {0, 0, ZAO_H_MAX + 1},
        {INT64_MAX, 0, 0}, {0, INT64_MIN, 0}, {0, 0, INT64_MIN}, {0, 0, INT64_MAX},
    };
    const zao_ecef bade[] = {
        {0, 0, 0},                       /* the centre */
        {1000000000, 0, 0},              /* 1,000 km from the centre */
        {6368136999, 0, 0},              /* 10 km and 1 mm below the equator */
        {ZAO_ECEF_MAX + 1, 0, 0},        {0, ZAO_ECEF_MAX + 1, 0},
        {0, 0, -ZAO_ECEF_MAX - 1},       {INT64_MIN, 0, 0},
        {INT64_MAX, INT64_MAX, INT64_MAX}, {ZAO_ECEF_MAX, ZAO_ECEF_MAX, ZAO_ECEF_MAX}, /* too high */
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        CHECK_RC(zao_geo_to_ecef(&bad[i], &e), ZAO_E_RANGE);
        CHECK_RC(zao_geo_to_ecef_um(&bad[i], &u), ZAO_E_RANGE);
    }
    for (size_t i = 0; i < sizeof bade / sizeof bade[0]; i++) {
        CHECK_RC(zao_ecef_to_geo(&bade[i], &g), ZAO_E_RANGE);
        CHECK_RC(zao_ecef_to_geo_fine(&bade[i], &f), ZAO_E_RANGE);
    }
    e.x_mm = 6368137000; /* exactly 10 km below the equator: the edge, accepted */
    CHECK_RC(zao_ecef_to_geo(&e, &g), ZXV_OK);
    CHECK(g.h_mm == ZAO_H_MIN, "edge height %lld", (long long) g.h_mm);
    CHECK_RC(zao_geo_to_ecef(NULL, &e), ZXV_E_ARG);
    CHECK_RC(zao_geo_to_ecef(&g, NULL), ZXV_E_ARG);
    CHECK_RC(zao_ecef_to_geo(NULL, &g), ZXV_E_ARG);
    CHECK_RC(zao_ecef_to_geo(&e, NULL), ZXV_E_ARG);
    CHECK_RC(zao_geo_to_ecef_um(NULL, &u), ZXV_E_ARG);
    CHECK_RC(zao_ecef_to_geo_fine(&e, NULL), ZXV_E_ARG);
}

/* symmetry and round trips over a deterministic stream */
static void test_geo_roundtrip(void) {
    uint64_t s = 0xA370ULL;
    int64_t worst_axis = 0, worst_h = 0;
    int sym_ok = 1, rt_ok = 1;
    for (int i = 0; i < 1500; i++) {
        zao_geo g, back, gm;
        zao_ecef e, e2, em;
        uint64_t r;
        r = zt_rand(&s);
        g.lat_nd = (int64_t) (r % 180000000001ULL) - ZAO_LAT_MAX;
        r = zt_rand(&s);
        g.lon_nd = (int64_t) (r % 360000000001ULL) - ZAO_LON_MAX;
        r = zt_rand(&s);
        g.h_mm = i % 3 == 0 ? (int64_t) (r % 20000001ULL) - 10000000
                 : i % 3 == 1 ? (int64_t) (r % 2000000001ULL)
                              : (int64_t) (r % (uint64_t) ZAO_H_MAX);
        if (zao_geo_to_ecef(&g, &e) != ZXV_OK || zao_ecef_to_geo(&e, &back) != ZXV_OK ||
            zao_geo_to_ecef(&back, &e2) != ZXV_OK) {
            rt_ok = 0;
            continue;
        }
        track(&worst_axis, e2.x_mm - e.x_mm);
        track(&worst_axis, e2.y_mm - e.y_mm);
        track(&worst_axis, e2.z_mm - e.z_mm);
        track(&worst_h, back.h_mm - g.h_mm);
        if (iabs(e2.x_mm - e.x_mm) > 2 || iabs(e2.y_mm - e.y_mm) > 2 || iabs(e2.z_mm - e.z_mm) > 2 ||
            iabs(back.h_mm - g.h_mm) > 2)
            rt_ok = 0;
        /* mirror in latitude negates Z, mirror in longitude negates Y, exactly */
        gm = g;
        gm.lat_nd = -g.lat_nd;
        if (zao_geo_to_ecef(&gm, &em) != ZXV_OK || em.z_mm != -e.z_mm || em.x_mm != e.x_mm ||
            em.y_mm != e.y_mm)
            sym_ok = 0;
        gm = g;
        gm.lon_nd = -g.lon_nd;
        if (zao_geo_to_ecef(&gm, &em) != ZXV_OK || em.y_mm != -e.y_mm || em.x_mm != e.x_mm ||
            em.z_mm != e.z_mm)
            sym_ok = 0;
        if (i % 10 == 0) {
            CHECK(rt_ok, "round trip %d", i);
            CHECK(sym_ok, "symmetry %d", i);
        }
    }
    CHECK(rt_ok && sym_ok, "1,500 round trips within 2 mm and exactly symmetric");
    printf("round trips: geodetic->ECEF->geodetic->ECEF worst axis %lld mm, height %lld mm\n",
           (long long) worst_axis, (long long) worst_h);
}

/* ===== packets and the wire ===== */

static size_t mk_pkt(uint16_t scid, uint8_t vcid, uint32_t seq, int n, int64_t salt,
                     uint8_t *buf) {
    zao_tm tm;
    size_t len = 0;
    memset(&tm, 0, sizeof tm);
    tm.scid = scid;
    tm.vcid = vcid;
    tm.seq = seq;
    tm.time_ms = 1760000000000ULL + seq;
    tm.n = (uint8_t) n;
    for (int i = 0; i < n; i++) {
        tm.v[i].channel = (uint16_t) (100 + i);
        tm.v[i].value = salt * 1000 + i * 7 - 3;
    }
    if (zao_tm_encode(&tm, buf, ZAO_TM_MAX_BYTES, &len) != ZXV_OK) return 0;
    return len;
}

static void recrc(uint8_t *buf, size_t len) {
    uint32_t c = zao_crc32c(buf, len - 4);
    for (int i = 0; i < 4; i++) buf[len - 4 + (size_t) i] = (uint8_t) (c >> (8 * i));
}

static void test_crc_tm(void) {
    zao_tm tm, back;
    uint8_t buf[ZAO_TM_MAX_BYTES], small[8];
    size_t len = 0;
    int all = 1;
    CHECK(zao_crc32c((const uint8_t *) "123456789", 9) == 0xE3069283u, "CRC-32C check value");
    CHECK(zao_crc32c(NULL, 0) == 0, "CRC-32C of nothing");
    memset(&tm, 0, sizeof tm);
    tm.scid = 0x1234;
    tm.vcid = 7;
    tm.seq = 99;
    tm.time_ms = 0x0102030405060708ULL;
    tm.n = ZAO_TM_VALUES;
    for (uint32_t i = 0; i < ZAO_TM_VALUES; i++) {
        tm.v[i].channel = (uint16_t) i;
        tm.v[i].value = i % 2 ? INT64_MIN + (int64_t) i : INT64_MAX - (int64_t) i;
    }
    CHECK_RC(zao_tm_encode(&tm, buf, sizeof buf, &len), ZXV_OK);
    CHECK(len == ZAO_TM_MAX_BYTES, "full packet is %zu bytes", len);
    CHECK_RC(zao_tm_decode(buf, len, &back), ZXV_OK);
    CHECK(back.scid == tm.scid && back.vcid == 7 && back.seq == 99 &&
              back.time_ms == tm.time_ms && back.n == ZAO_TM_VALUES &&
              memcmp(back.v, tm.v, sizeof tm.v) == 0,
          "round trip with extreme values");
    CHECK_RC(zao_tm_encode(&tm, small, sizeof small, &len), ZXV_E_BUFFER);
    CHECK(len == ZAO_TM_MAX_BYTES, "size reported");
    CHECK_RC(zao_tm_encode(&tm, NULL, 0, &len), ZXV_E_BUFFER);
    tm.n = ZAO_TM_VALUES + 1;
    CHECK_RC(zao_tm_encode(&tm, buf, sizeof buf, &len), ZXV_E_ARG);
    CHECK_RC(zao_tm_encode(NULL, buf, sizeof buf, &len), ZXV_E_ARG);
    CHECK_RC(zao_tm_decode(buf, 0, &back), ZXV_E_FORMAT);
    CHECK_RC(zao_tm_decode(NULL, 5, &back), ZXV_E_ARG);
    /* every single-bit flip of a 4-reading packet is caught */
    len = mk_pkt(7, 1, 42, 4, 5, buf);
    CHECK(len == ZAO_TM_MIN_BYTES + 40, "4-reading packet");
    for (size_t bit = 0; bit < len * 8; bit++) {
        int32_t st;
        buf[bit / 8] ^= (uint8_t) (1u << (bit % 8));
        st = zao_tm_decode(buf, len, &back);
        buf[bit / 8] ^= (uint8_t) (1u << (bit % 8));
        if (st != ZAO_E_CRC && st != ZXV_E_FORMAT) all = 0;
        CHECK(st == ZAO_E_CRC || st == ZXV_E_FORMAT, "bit %zu flipped -> %s", bit,
              zao_status_name(st));
    }
    CHECK(all, "every flip detected");
    CHECK_RC(zao_tm_decode(buf, len, &back), ZXV_OK);
    CHECK_RC(zao_tm_decode(buf, len - 1, &back), ZXV_E_FORMAT);
    buf[16] = 33; /* n beyond 32 */
    CHECK_RC(zao_tm_decode(buf, len, &back), ZXV_E_FORMAT);
}

static void test_wire(void) {
    uint8_t bytes[300], out[300], fr[32 * 21], fr2[32 * 21];
    size_t nf = 0, len = 0;
    uint32_t trunk = 99;
    int32_t truth = -1;
    for (size_t i = 0; i < sizeof bytes; i++) bytes[i] = (uint8_t) (i * 37 + 11);
    for (size_t n = 1; n <= 100; n++) {
        CHECK_RC(zao_wire_encode((uint32_t) (n % 10), bytes, n, fr, sizeof fr, &nf), ZXV_OK);
        CHECK(nf == zao_wire_frames(n) && nf == 1 + (n + 15) / 16, "frames for %zu bytes", n);
        CHECK_RC(zao_wire_decode(fr, nf, out, sizeof out, &len, &trunk), ZXV_OK);
        CHECK(len == n && memcmp(out, bytes, n) == 0 && trunk == n % 10, "round trip %zu", n);
    }
    /* frames are real UBH-168 frames: every data frame differs from the idle pilot */
    CHECK_RC(zao_wire_encode(4, bytes, 40, fr, sizeof fr, &nf), ZXV_OK);
    {
        uint8_t idle[21];
        CHECK_RC(zxv_wire_idle(4, idle), ZXV_OK);
        for (size_t k = 0; k < nf; k++) {
            CHECK(memcmp(fr + k * 21, idle, 21) != 0, "frame %zu is not idle", k);
            CHECK_RC(zxv_wire_resolve(fr + k * 21, &truth), ZXV_OK);
        }
        CHECK(fr[0] == 104 && fr[21] == 4 && fr[42] == 14 && fr[63] == 24, "tags: header 104, "
              "data 4, 14, 24");
    }
    /* every single-bit flip in a 4-frame carrier is refused */
    {
        int all = 1;
        for (size_t bit = 0; bit < nf * 21 * 8; bit++) {
            int32_t st;
            fr[bit / 8] ^= (uint8_t) (1u << (bit % 8));
            st = zao_wire_decode(fr, nf, out, sizeof out, &len, NULL);
            fr[bit / 8] ^= (uint8_t) (1u << (bit % 8));
            if (st != ZAO_E_WIRE) all = 0;
            if (bit % 8 == 0) CHECK(st == ZAO_E_WIRE, "frame bit %zu -> %s", bit, zao_status_name(st));
        }
        CHECK(all, "all %zu single-bit flips refused", nf * 21 * 8);
    }
    CHECK_RC(zao_wire_decode(fr, nf, out, sizeof out, &len, NULL), ZXV_OK);
    /* reordered, missing, extra frames */
    memcpy(fr2, fr, nf * 21);
    memcpy(fr2 + 21, fr + 42, 21);
    memcpy(fr2 + 42, fr + 21, 21);
    CHECK_RC(zao_wire_decode(fr2, nf, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    CHECK_RC(zao_wire_decode(fr, nf - 1, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    memcpy(fr2, fr, nf * 21);
    memcpy(fr2 + nf * 21, fr + 21, 21);
    CHECK_RC(zao_wire_decode(fr2, nf + 1, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    CHECK_RC(zao_wire_decode(fr + 21, nf - 1, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    CHECK_RC(zao_wire_decode(fr, 1, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    /* a frame from another trunk */
    CHECK_RC(zao_wire_encode(5, bytes, 40, fr2, sizeof fr2, &nf), ZXV_OK);
    memcpy(fr2 + 42, fr + 42, 21);
    CHECK_RC(zao_wire_decode(fr2, nf, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    /* non-zero padding, even with matching checksums, is refused */
    {
        uint32_t sp[3], sm[2];
        uint8_t tag;
        CHECK_RC(zao_wire_encode(4, bytes, 33, fr, sizeof fr, &nf), ZXV_OK);
        CHECK_RC(zxv_wire_unpack(fr + 3 * 21, &tag, sp, sm), ZXV_OK);
        sp[2] |= 0x80000000u;
        CHECK_RC(zxv_wire_pack(tag, sp, sm, fr + 3 * 21), ZXV_OK);
        CHECK_RC(zao_wire_decode(fr, nf, out, sizeof out, &len, NULL), ZAO_E_WIRE);
    }
    CHECK_RC(zao_wire_encode(10, bytes, 10, fr, sizeof fr, &nf), ZXV_E_ARG);
    CHECK_RC(zao_wire_encode(1, bytes, 0, fr, sizeof fr, &nf), ZXV_E_ARG);
    CHECK_RC(zao_wire_encode(1, bytes, ZAO_WIRE_MAX_BYTES + 1, fr, sizeof fr, &nf), ZXV_E_ARG);
    CHECK_RC(zao_wire_encode(1, bytes, 100, fr, 21, &nf), ZXV_E_BUFFER);
    CHECK(nf == 8, "size asked");
    CHECK_RC(zao_wire_encode(1, bytes, 100, fr, sizeof fr, &nf), ZXV_OK);
    CHECK_RC(zao_wire_decode(fr, nf, out, 50, &len, NULL), ZXV_E_BUFFER);
    CHECK(len == 100, "needed length reported");
    CHECK_RC(zao_wire_decode(NULL, nf, out, 50, &len, NULL), ZXV_E_ARG);
    CHECK(zao_bit_diff((const uint8_t *) "\x0f", 1, (const uint8_t *) "\xff", 1) == 4, "bits");
    CHECK(zao_bit_diff((const uint8_t *) "ab", 2, (const uint8_t *) "a", 1) == 8, "length");
}

/* ===== the SEU receiver ===== */

typedef struct {
    zt_env e;
    zind_signer *op, *an, *an2, *rogue;
    zxv_hexlog *qlog;
    zao_rx *rx;
} fx_t;

static int fx_open(fx_t *f, uint8_t copies, uint8_t policy, uint32_t qcap) {
    zao_rx_cfg cfg = {copies, policy, 4096};
    memset(f, 0, sizeof *f);
    if (zt_env_open(&f->e, NULL) != 0) return -1;
    f->op = zt_signer(f->e.cap, 1);
    f->an = zt_signer(f->e.cap, 2);
    f->an2 = zt_signer(f->e.cap, 3);
    f->rogue = zt_signer(f->e.cap, 4);
    if (!f->op || !f->an || !f->an2 || !f->rogue) return -1;
    if (zxv_hexlog_create(f->e.cap, qcap, &f->qlog) != ZXV_OK) return -1;
    if (zao_rx_create(f->e.cap, f->op, f->qlog, &cfg, &f->rx) != ZXV_OK) return -1;
    if (zao_analyst_add(f->rx, zind_signer_pk(f->an)) != ZXV_OK) return -1;
    return zao_analyst_add(f->rx, zind_signer_pk(f->an2)) == ZXV_OK ? 0 : -1;
}

static void fx_close(fx_t *f) {
    zao_rx_destroy(f->rx);
    zxv_hexlog_destroy(f->qlog);
    zind_signer_destroy(f->op);
    zind_signer_destroy(f->an);
    zind_signer_destroy(f->an2);
    zind_signer_destroy(f->rogue);
    zt_env_close(&f->e);
}

static int32_t send(fx_t *f, const uint8_t *b, size_t len, zao_key *k) {
    uint8_t fr[40 * 21];
    size_t nf = 0;
    if (zao_wire_encode(2, b, len, fr, sizeof fr, &nf) != ZXV_OK) return -999;
    return zao_rx_submit(f->rx, fr, nf, k);
}

static uint32_t qcount(fx_t *f) {
    uint32_t n = 0;
    zxv_hexlog_count(f->qlog, &n);
    return n;
}

static int last_reason(fx_t *f, uint8_t *base) {
    uint8_t t = 0, r = 0;
    uint32_t n = qcount(f);
    if (!n) return -1;
    zxv_hexlog_at(f->qlog, n - 1, NULL, &t, &r, NULL, NULL, NULL, NULL);
    zxv_hex_unpack(t, base, NULL, NULL);
    return r;
}

static int sign_review(fx_t *f, const zind_signer *k, zao_key key, int32_t d, uint32_t copy,
                       uint8_t sig[ZIND_SIG_BYTES]) {
    uint8_t m[64];
    size_t len = 0;
    if (zao_review_message(f->rx, key, d, copy, m, sizeof m, &len) != ZXV_OK) return -1;
    return zind_sign(f->e.cap, k, m, len, ZAO_REVIEW_CTX, sig) == ZXV_OK ? 0 : -1;
}

static void test_rx_setup(void) {
    zt_env e;
    zxv_hexlog *q = NULL;
    zind_signer *op;
    zao_rx *rx = NULL;
    zxv_cap *weak = NULL;
    zao_rx_cfg cfg = {3, ZAO_POLICY_HOLD, 16};
    CHECK(zt_env_open(&e, NULL) == 0, "env");
    op = zt_signer(e.cap, 9);
    CHECK_RC(zxv_hexlog_create(e.cap, 8, &q), ZXV_OK);
    CHECK_RC(zao_rx_create(e.cap, op, NULL, &cfg, &rx), ZXV_E_ARG); /* no log: no receiver */
    CHECK_RC(zao_rx_create(e.cap, NULL, q, &cfg, &rx), ZXV_E_ARG);
    CHECK_RC(zao_rx_create(e.cap, op, q, NULL, &rx), ZXV_E_ARG);
    cfg.copies = 0;
    CHECK_RC(zao_rx_create(e.cap, op, q, &cfg, &rx), ZXV_E_ARG);
    cfg.copies = 6;
    CHECK_RC(zao_rx_create(e.cap, op, q, &cfg, &rx), ZXV_E_ARG);
    cfg.copies = 3;
    cfg.policy = 7;
    CHECK_RC(zao_rx_create(e.cap, op, q, &cfg, &rx), ZXV_E_ARG);
    cfg.policy = ZAO_POLICY_HOLD;
    cfg.capacity = 0;
    CHECK_RC(zao_rx_create(e.cap, op, q, &cfg, &rx), ZXV_E_ARG);
    cfg.capacity = ZAO_MAX_PACKETS + 1;
    CHECK_RC(zao_rx_create(e.cap, op, q, &cfg, &rx), ZXV_E_ARG);
    cfg.capacity = 2;
    CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_CRYPTO_SIGN, &weak), ZXV_OK);
    CHECK_RC(zao_rx_create(weak, op, q, &cfg, &rx), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zxv_cap_attenuate(e.cap, ZXV_CAP_ATTEST, &weak), ZXV_OK);
    CHECK_RC(zao_rx_create(weak, op, q, &cfg, &rx), ZXV_E_CAP);
    zxv_cap_destroy(weak);
    CHECK_RC(zao_rx_create(e.cap, op, q, &cfg, &rx), ZXV_OK);
    CHECK_RC(zao_analyst_add(rx, zind_signer_pk(op)), ZXV_OK);
    CHECK_RC(zao_analyst_add(rx, zind_signer_pk(op)), ZXV_E_DUPLICATE);
    CHECK_RC(zao_analyst_add(rx, NULL), ZXV_E_ARG);
    for (int i = 0; i < 16; i++) {
        zind_signer *k = zt_signer(e.cap, (uint8_t) (40 + i));
        int32_t st = zao_analyst_add(rx, zind_signer_pk(k));
        CHECK(st == (i < 15 ? ZXV_OK : ZXV_E_CAPACITY), "analyst %d", i);
        zind_signer_destroy(k);
    }
    /* capacity 2: the third packet is refused */
    {
        uint8_t b[ZAO_TM_MAX_BYTES], fr[40 * 21];
        size_t len, nf = 0;
        for (uint32_t s = 0; s < 3; s++) {
            len = mk_pkt(1, 0, s, 2, 1, b);
            zao_wire_encode(0, b, len, fr, sizeof fr, &nf);
            CHECK_RC(zao_rx_submit(rx, fr, nf, NULL), s < 2 ? ZXV_OK : ZXV_E_CAPACITY);
        }
        CHECK_RC(zao_rx_submit(rx, NULL, nf, NULL), ZXV_E_ARG);
        CHECK_RC(zao_rx_submit(NULL, fr, nf, NULL), ZXV_E_ARG);
    }
    zao_rx_destroy(rx);
    zxv_hexlog_destroy(q);
    zind_signer_destroy(op);
    zt_env_close(&e);
}

static void test_rx_clean_and_seu(void) {
    fx_t f;
    uint8_t a[ZAO_TM_MAX_BYTES], b[ZAO_TM_MAX_BYTES], fr[40 * 21];
    size_t la, lb, nf = 0;
    zao_key k;
    zao_pkt_info info;
    zao_tm tm;
    uint8_t base = 0, ph = 0, pol = 0;
    int32_t act = -1;
    CHECK(fx_open(&f, 3, ZAO_POLICY_HOLD, 256) == 0, "fixture");
    /* clean: three identical copies */
    la = mk_pkt(0x51, 1, 1000, 6, 1, a);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK(k.scid == 0x51 && k.vcid == 1 && k.seq == 1000, "key reported");
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZAO_E_PENDING);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_COLLECTING && info.copies == 1, "collecting");
    CHECK(zxv_hex_unpack(info.tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_NEITHER &&
              ph == ZXV_HEX_PHASE_EMERGING,
          "collecting = neither/emerging");
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_RELEASED && info.copies == 3 && info.distinct == 1 &&
              info.crc_bad == 0 && info.max_bit_diff == 0,
          "released clean");
    CHECK(zxv_hex_unpack(info.tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              ph == ZXV_HEX_PHASE_MANIFEST,
          "released = true/manifest");
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZXV_OK);
    CHECK(tm.seq == 1000 && tm.n == 6 && tm.v[2].value == 1000 + 14 - 3, "value");
    CHECK(qcount(&f) == 0, "a clean packet logs nothing");
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_REPLAY);
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_REPLAY);
    CHECK_RC(zao_rx_close(f.rx, k), ZXV_E_STATE);

    /* an SEU before the CRC was computed: a good CRC, a different value */
    la = mk_pkt(0x51, 1, 1001, 6, 2, a);
    memcpy(b, a, la);
    lb = la;
    b[30] ^= 0x04;
    recrc(b, lb);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, b, lb, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZAO_E_SEU); /* HOLD policy: even 2 of 3 is held */
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_HELD && info.distinct == 2 && info.crc_bad == 0 &&
              info.holds == 1 && info.max_bit_diff >= 1,
          "held: two distinct good copies");
    CHECK(zxv_hex_unpack(info.tag, &base, &pol, NULL) == ZXV_OK && base == ZXV_HEX_BASE_BOTH &&
              zxv_hex_action(info.tag, &act) == ZXV_OK && act == ZXV_HEX_ACT_ESCALATE,
          "held = BOTH, escalate (%s)", zxv_hex_name(info.tag));
    CHECK(qcount(&f) == 1 && last_reason(&f, &base) == ZAO_REASON_SEU &&
              base == ZXV_HEX_BASE_BOTH,
          "logged once, as an SEU with base BOTH");
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZIND_E_HELD);
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_HELD); /* more evidence is kept */
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_HELD);
    CHECK_RC(send(&f, a, la, NULL), ZXV_E_CAPACITY);
    CHECK(qcount(&f) == 1, "evidence on a held packet is not re-logged");

    /* a CRC failure after the CRC (bit flip in a buffer) */
    la = mk_pkt(0x51, 1, 1002, 3, 3, a);
    memcpy(b, a, la);
    b[20] ^= 0x80;
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, b, la, NULL), ZAO_E_SEU);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_HELD && info.crc_bad == 1 && info.distinct == 1 &&
              info.max_bit_diff == 1,
          "held: a CRC failure (one bit)");
    CHECK(last_reason(&f, &base) == ZAO_REASON_CRC, "logged as a CRC failure");
    {
        size_t cl = 0;
        int32_t ok = -1;
        uint8_t cb[ZAO_TM_MAX_BYTES];
        CHECK_RC(zao_rx_copy(f.rx, k, 2, cb, sizeof cb, &cl, &ok), ZXV_OK);
        CHECK(cl == la && ok == 0 && memcmp(cb, b, la) == 0, "the bad copy is kept as sent");
        CHECK_RC(zao_rx_copy(f.rx, k, 3, cb, sizeof cb, &cl, &ok), ZXV_E_NOT_FOUND);
        CHECK_RC(zao_rx_copy(f.rx, k, 0, cb, 4, &cl, &ok), ZXV_E_BUFFER);
    }

    /* a differing copy after release re-opens the packet */
    la = mk_pkt(0x51, 2, 7, 2, 4, a);
    memcpy(b, a, la);
    b[18] ^= 1;
    recrc(b, la);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_RELEASED, "released");
    CHECK_RC(send(&f, b, la, NULL), ZAO_E_SEU);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_HELD && info.copies == 4 &&
              zxv_hex_unpack(info.tag, &base, NULL, &ph) == ZXV_OK && base == ZXV_HEX_BASE_BOTH &&
              ph == ZXV_HEX_PHASE_MANIFEST,
          "late SEU: BOTH, phase kept");
    CHECK(last_reason(&f, &base) == ZAO_REASON_LATE, "logged as late");

    /* close with fewer copies: a single copy is decided on its own */
    la = mk_pkt(0x51, 3, 1, 1, 5, a);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(zao_rx_close(f.rx, k), ZXV_OK);
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZXV_OK);
    la = mk_pkt(0x51, 3, 2, 1, 5, a);
    a[la - 1] ^= 0x10;
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(zao_rx_close(f.rx, k), ZAO_E_SEU); /* its only copy failed its CRC */
    k.seq = 999999;
    CHECK_RC(zao_rx_close(f.rx, k), ZAO_E_PACKET);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZAO_E_PACKET);
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZAO_E_PACKET);

    /* frames that do not decode, and bytes that are not a packet */
    {
        uint32_t q0 = qcount(&f);
        CHECK_RC(zao_wire_encode(2, (const uint8_t *) "hello", 5, fr, sizeof fr, &nf), ZXV_OK);
        CHECK_RC(zao_rx_submit(f.rx, fr, nf, NULL), ZXV_E_FORMAT);
        CHECK(qcount(&f) == q0 + 1 && last_reason(&f, &base) == ZAO_REASON_UNPARSED,
              "an unparsable copy is logged");
        fr[30] ^= 1;
        CHECK_RC(zao_rx_submit(f.rx, fr, nf, NULL), ZAO_E_WIRE);
        CHECK(qcount(&f) == q0 + 1, "a transport error is not an SEU");
    }
    {
        uint32_t np = 0, nh = 0, nl = 0;
        CHECK_RC(zao_rx_counts(f.rx, &np, &nh, &nl), ZXV_OK);
        CHECK(np == 6 && nh == 4 && nl == qcount(&f), "counts: %u packets, %u holds, %u logged",
              np, nh, nl);
    }
    fx_close(&f);
}

static void test_rx_majority(void) {
    fx_t f;
    uint8_t a[ZAO_TM_MAX_BYTES], b[ZAO_TM_MAX_BYTES], c[ZAO_TM_MAX_BYTES];
    size_t la;
    zao_key k;
    zao_pkt_info info;
    zao_tm tm, ta;
    uint8_t base = 0, pol = 0;
    CHECK(fx_open(&f, 3, ZAO_POLICY_MAJORITY, 256) == 0, "fixture");
    /* 2 of 3 good identical: released, with a log entry and a signed record */
    la = mk_pkt(9, 0, 1, 5, 11, a);
    memcpy(b, a, la);
    b[25] ^= 0x40;
    recrc(b, la);
    CHECK_RC(send(&f, b, la, &k), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZAO_E_CORRECTED);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_CORRECTED && info.distinct == 2, "corrected");
    CHECK(zxv_hex_unpack(info.tag, &base, &pol, NULL) == ZXV_OK && base == ZXV_HEX_BASE_TRUE &&
              pol == ZXV_HEX_POL_NEGENTROPIC,
          "corrected = true/negentropic");
    CHECK(qcount(&f) == 1 && last_reason(&f, &base) == ZAO_REASON_CORRECTED &&
              base == ZXV_HEX_BASE_BOTH,
          "a correction is never silent: one BOTH log entry");
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZXV_OK);
    CHECK_RC(zao_tm_decode(a, la, &ta), ZXV_OK);
    CHECK(memcmp(&tm, &ta, sizeof tm) == 0, "the majority value, not the first copy");
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_REPLAY);
    /* a CRC failure and two good copies: majority */
    la = mk_pkt(9, 0, 2, 5, 12, a);
    memcpy(b, a, la);
    b[20] ^= 2;
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, b, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZAO_E_CORRECTED);
    /* three different good copies: no majority, held */
    la = mk_pkt(9, 0, 3, 5, 13, a);
    memcpy(b, a, la);
    memcpy(c, a, la);
    b[25] ^= 1;
    recrc(b, la);
    c[26] ^= 1;
    recrc(c, la);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, b, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, c, la, NULL), ZAO_E_SEU);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_HELD && info.distinct == 3, "no majority: held");
    /* one good and two CRC failures: 1 of 3 is no majority */
    la = mk_pkt(9, 0, 4, 5, 14, a);
    memcpy(b, a, la);
    memcpy(c, a, la);
    b[25] ^= 1;
    c[27] ^= 1;
    CHECK_RC(send(&f, b, la, &k), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, c, la, NULL), ZAO_E_SEU);
    /* closed early with 2 copies that disagree: 1 of 2 is not a strict majority */
    la = mk_pkt(9, 0, 5, 5, 15, a);
    memcpy(b, a, la);
    b[30] ^= 8;
    recrc(b, la);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, b, la, NULL), ZXV_OK);
    CHECK_RC(zao_rx_close(f.rx, k), ZAO_E_SEU);
    {
        uint8_t buf[65536];
        size_t len = 0;
        uint32_t nr = 9, nc = 9, bad = 0;
        const uint8_t *an[1] = {zind_signer_pk(f.an)};
        CHECK_RC(zao_decisions_export(f.rx, buf, sizeof buf, &len), ZXV_OK);
        CHECK_RC(zao_decisions_verify(f.e.cap, buf, len, an, 1, zind_signer_pk(f.op), &nr, &nc,
                                      &bad),
                 ZXV_OK);
        CHECK(nr == 0 && nc == 2 && bad == UINT32_MAX, "two signed corrections");
    }
    fx_close(&f);

    /* a full log: the majority cannot be logged, so nothing is released */
    CHECK(fx_open(&f, 3, ZAO_POLICY_MAJORITY, 1) == 0, "fixture, log of 1");
    la = mk_pkt(9, 0, 1, 5, 11, a);
    memcpy(b, a, la);
    b[25] ^= 0x40;
    recrc(b, la);
    CHECK_RC(send(&f, b, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, &k), ZAO_E_CORRECTED); /* takes the only log slot */
    la = mk_pkt(9, 0, 2, 5, 12, a);
    memcpy(b, a, la);
    b[25] ^= 0x40;
    recrc(b, la);
    CHECK_RC(send(&f, b, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, NULL), ZXV_OK);
    CHECK_RC(send(&f, a, la, &k), ZXV_E_CAPACITY);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_HELD, "unloggable correction: held, not released");
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZIND_E_HELD);
    fx_close(&f);
}

static void test_rx_review(void) {
    fx_t f;
    uint8_t a[ZAO_TM_MAX_BYTES], b[ZAO_TM_MAX_BYTES], sig[ZIND_SIG_BYTES], m[64];
    size_t la, ml = 0;
    zao_key k, k2, k3;
    zao_pkt_info info;
    zao_tm tm, tb;
    uint8_t base = 0, pol = 0;
    CHECK(fx_open(&f, 2, ZAO_POLICY_HOLD, 256) == 0, "fixture");
    la = mk_pkt(3, 0, 50, 4, 21, a);
    memcpy(b, a, la);
    b[22] ^= 0x20;
    recrc(b, la);
    CHECK_RC(send(&f, a, la, &k), ZXV_OK);
    CHECK_RC(send(&f, b, la, NULL), ZAO_E_SEU);
    /* message refusals */
    CHECK_RC(zao_review_message(f.rx, k, 3, 0, m, sizeof m, &ml), ZXV_E_ARG);
    CHECK_RC(zao_review_message(f.rx, k, ZAO_ACCEPT, 2, m, sizeof m, &ml), ZXV_E_ARG);
    CHECK_RC(zao_review_message(f.rx, k, ZAO_REJECT, 1, m, sizeof m, &ml), ZXV_E_ARG);
    CHECK_RC(zao_review_message(f.rx, k, ZAO_ACCEPT, 1, m, 8, &ml), ZXV_E_BUFFER);
    /* forged: signed by a rogue key, claimed by a registered analyst */
    CHECK(sign_review(&f, f.rogue, k, ZAO_ACCEPT, 1, sig) == 0, "sign");
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.an), ZAO_ACCEPT, 1, sig), ZXV_E_VERIFY);
    CHECK(last_reason(&f, &base) == ZIND_REASON_TAMPER, "forgery logged");
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.rogue), ZAO_ACCEPT, 1, sig),
             ZAO_E_ANALYST);
    /* signed for copy 1, applied to copy 0 */
    CHECK(sign_review(&f, f.an, k, ZAO_ACCEPT, 1, sig) == 0, "sign");
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.an), ZAO_ACCEPT, 0, sig), ZXV_E_VERIFY);
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.an), ZAO_REJECT, 0, sig), ZXV_E_VERIFY);
    /* stale: another copy arrives after signing, so the copies changed */
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_HELD);
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.an), ZAO_ACCEPT, 1, sig), ZXV_E_VERIFY);
    /* a proper decision by the second analyst: accept copy 1 */
    CHECK(sign_review(&f, f.an2, k, ZAO_ACCEPT, 1, sig) == 0, "sign");
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.an2), ZAO_ACCEPT, 1, sig), ZXV_OK);
    CHECK_RC(zao_review_resolve(f.rx, k, zind_signer_pk(f.an2), ZAO_ACCEPT, 1, sig), ZXV_E_STATE);
    CHECK_RC(zao_rx_info(f.rx, k, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_ACCEPTED &&
              zxv_hex_unpack(info.tag, &base, &pol, NULL) == ZXV_OK && base == ZXV_HEX_BASE_TRUE,
          "accepted = TRUE");
    CHECK_RC(zao_rx_value(f.rx, k, &tm), ZXV_OK);
    CHECK_RC(zao_tm_decode(b, la, &tb), ZXV_OK);
    CHECK(memcmp(&tm, &tb, sizeof tm) == 0, "the analyst's copy is the value");
    CHECK_RC(send(&f, b, la, NULL), ZIND_E_REPLAY);
    CHECK_RC(send(&f, a, la, NULL), ZAO_E_SEU); /* the other copy again: re-held */
    /* reject: the packet is frozen for good */
    la = mk_pkt(3, 0, 51, 4, 22, a);
    memcpy(b, a, la);
    b[22] ^= 0x20;
    CHECK_RC(send(&f, a, la, &k2), ZXV_OK);
    CHECK_RC(send(&f, b, la, NULL), ZAO_E_SEU);
    CHECK_RC(zao_review_message(f.rx, k2, ZAO_ACCEPT, 1, m, sizeof m, &ml), ZXV_E_ARG); /* CRC bad */
    CHECK(sign_review(&f, f.an, k2, ZAO_REJECT, 0, sig) == 0, "sign");
    CHECK_RC(zao_review_resolve(f.rx, k2, zind_signer_pk(f.an), ZAO_REJECT, 0, sig), ZXV_OK);
    CHECK_RC(zao_rx_info(f.rx, k2, &info), ZXV_OK);
    CHECK(info.state == ZAO_PK_REJECTED &&
              zxv_hex_unpack(info.tag, &base, &pol, NULL) == ZXV_OK && base == ZXV_HEX_BASE_FALSE &&
              pol == ZXV_HEX_POL_FORBIDDEN,
          "rejected = false/forbidden");
    CHECK(last_reason(&f, &base) == ZIND_REASON_FORBIDDEN, "rejection logged");
    CHECK_RC(send(&f, a, la, NULL), ZIND_E_FROZEN);
    CHECK_RC(zao_rx_value(f.rx, k2, &tm), ZIND_E_FROZEN);
    CHECK_RC(zao_review_message(f.rx, k2, ZAO_REJECT, 0, m, sizeof m, &ml), ZXV_E_STATE);
    /* a review of a packet that is not held */
    la = mk_pkt(3, 0, 52, 1, 23, a);
    CHECK_RC(send(&f, a, la, &k3), ZXV_OK);
    CHECK_RC(zao_review_message(f.rx, k3, ZAO_REJECT, 0, m, sizeof m, &ml), ZXV_E_STATE);
    k3.seq = 4242;
    CHECK_RC(zao_review_message(f.rx, k3, ZAO_REJECT, 0, m, sizeof m, &ml), ZAO_E_PACKET);
    CHECK_RC(zao_review_resolve(f.rx, k3, zind_signer_pk(f.an), ZAO_REJECT, 0, sig), ZAO_E_PACKET);
    /* offline: the two decisions verify; tampering does not */
    {
        static uint8_t buf[65536];
        size_t len = 0, offs;
        uint32_t nr = 0, nc = 9, bad = 0;
        int all = 1;
        const uint8_t *an[2] = {zind_signer_pk(f.an), zind_signer_pk(f.an2)};
        CHECK_RC(zao_decisions_export(f.rx, buf, sizeof buf, &len), ZXV_OK);
        CHECK_RC(zao_decisions_verify(f.e.cap, buf, len, an, 2, zind_signer_pk(f.op), &nr, &nc,
                                      &bad),
                 ZXV_OK);
        CHECK(nr == 2 && nc == 0, "two reviews offline");
        CHECK_RC(zao_decisions_verify(f.e.cap, buf, len, an, 1, zind_signer_pk(f.op), &nr, &nc,
                                      &bad),
                 ZIND_E_UNTRUSTED);
        CHECK(bad == 0, "the first record names the missing analyst");
        CHECK_RC(zao_decisions_verify(f.e.cap, buf, len, an, 2, zind_signer_pk(f.an), &nr, &nc,
                                      &bad),
                 ZIND_E_UNTRUSTED);
        CHECK_RC(zao_decisions_verify(f.e.cap, buf, len - 1, an, 2, zind_signer_pk(f.op), &nr,
                                      &nc, &bad),
                 ZXV_E_FORMAT);
        for (offs = 7; offs < len; offs += len / 40 + 1) {
            int32_t st;
            buf[offs] ^= 0x10;
            st = zao_decisions_verify(f.e.cap, buf, len, an, 2, zind_signer_pk(f.op), &nr, &nc,
                                      &bad);
            buf[offs] ^= 0x10;
            if (st == ZXV_OK) all = 0;
            CHECK(st != ZXV_OK, "flipped byte %zu verifies", offs);
        }
        CHECK(all, "every tampered export refused");
        CHECK_RC(zao_decisions_verify(f.e.cap, buf, len, an, 2, zind_signer_pk(f.op), &nr, &nc,
                                      &bad),
                 ZXV_OK);
        CHECK_RC(zao_decisions_verify(NULL, buf, len, an, 2, zind_signer_pk(f.op), &nr, &nc, &bad),
                 ZXV_E_ARG);
        CHECK_RC(zao_decisions_export(f.rx, buf, 10, &len), ZXV_E_BUFFER);
    }
    fx_close(&f);
}

int main(void) {
    test_geo_vectors();
    test_geo_known();
    test_geo_range();
    test_geo_roundtrip();
    test_crc_tm();
    test_wire();
    test_rx_setup();
    test_rx_clean_and_seu();
    test_rx_majority();
    test_rx_review();
    CHECK(strcmp(zao_status_name(ZAO_E_SEU), "ZAO_E_SEU") == 0, "status names");
    CHECK(strcmp(zao_status_name(ZIND_E_HELD), "ZIND_E_HELD") == 0, "falls back");
    return zt_report("test_aero");
}
