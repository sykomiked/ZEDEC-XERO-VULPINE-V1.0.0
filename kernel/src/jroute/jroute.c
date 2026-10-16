/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* jroute.c — the jurisdictional routing matrix (jroute.h R1-R10). Integer only. */
#include "jroute.h"
#include "../robin_debanks/sha256.h"

/* domain tags are exact-length byte labels hashed by size — never read as
 * C strings, so no terminator: mark them where the compiler knows how */
#if defined(__has_attribute)
#if __has_attribute(nonstring)
#define DOM_TAG __attribute__((nonstring))
#endif
#endif
#ifndef DOM_TAG
#define DOM_TAG
#endif
static const char NULL_DOM[14] DOM_TAG = "zxv/jr-null/v1";
static const char TABLE_DOM[15] DOM_TAG = "zxv/jr-table/v1";
static const char CLAIM_DOM[16] DOM_TAG = "zxv/jr-zclaim/v1";
static const char RULING_DOM[16] DOM_TAG = "zxv/jr-ruling/v1";
static const char ESCROW_DOM[16] DOM_TAG = "zxv/jr-escrow/v1";
static const char RECEIPT_DOM[17] DOM_TAG = "zxv/jr-receipt/v1";

#define LOT_FREE      0u
#define LOT_ESCROWED  1u
#define LOT_DELIVERED 2u
#define LOT_WITHDRAWN 3u

/* ---- small byte helpers (no libc) ---- */
static void zero(void *p, size_t n)
{
    uint8_t *b = (uint8_t *) p;
    for (size_t i = 0; i < n; i++) b[i] = 0;
}
static void copy(void *d, const void *s, size_t n)
{
    uint8_t *o = (uint8_t *) d;
    const uint8_t *i = (const uint8_t *) s;
    for (size_t k = 0; k < n; k++) o[k] = i[k];
}
static bool same(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t d = 0;
    for (size_t i = 0; i < n; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}
static void le16(uint8_t *o, uint16_t v)
{
    o[0] = (uint8_t) v;
    o[1] = (uint8_t) (v >> 8);
}
static void le32(uint8_t *o, uint32_t v)
{
    for (int i = 0; i < 4; i++) o[i] = (uint8_t) (v >> (8 * i));
}
static void le64(uint8_t *o, uint64_t v)
{
    for (int i = 0; i < 8; i++) o[i] = (uint8_t) (v >> (8 * i));
}
static uint16_t rd16(const uint8_t *i)
{
    return (uint16_t) (i[0] | (i[1] << 8));
}
static uint32_t rd32(const uint8_t *i)
{
    return (uint32_t) i[0] | ((uint32_t) i[1] << 8) | ((uint32_t) i[2] << 16) |
           ((uint32_t) i[3] << 24);
}
static uint64_t rd64(const uint8_t *i)
{
    return (uint64_t) rd32(i) | ((uint64_t) rd32(i + 4) << 32);
}
static void h32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4];
    le32(b, v);
    sha256_update(c, b, 4);
}
static void h64(sha256_ctx_t *c, uint64_t v)
{
    uint8_t b[8];
    le64(b, v);
    sha256_update(c, b, 8);
}
static void h8(sha256_ctx_t *c, uint8_t v)
{
    sha256_update(c, &v, 1);
}

/* ---- exact 128-bit arithmetic (R4) ---- */
jr_u128_t jr_mul64(uint64_t a, uint64_t b)
{
    uint64_t al = a & 0xFFFFFFFFu, ah = a >> 32, bl = b & 0xFFFFFFFFu, bh = b >> 32;
    uint64_t ll = al * bl, lh = al * bh, hl = ah * bl, hh = ah * bh;
    uint64_t mid = (ll >> 32) + (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu);
    jr_u128_t r;
    r.lo = (ll & 0xFFFFFFFFu) | (mid << 32);
    r.hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    return r;
}

static jr_u128_t add64(jr_u128_t a, uint64_t b)
{
    jr_u128_t r;
    r.lo = a.lo + b;
    r.hi = a.hi + (r.lo < a.lo ? 1u : 0u);
    return r;
}

bool jr_divmod(jr_u128_t n, uint64_t d, uint64_t *q, uint64_t *rem)
{
    if (d == 0 || n.hi >= d) return false;
    uint64_t r = n.hi, quo = 0;
    for (int i = 63; i >= 0; i--) {
        uint64_t carry = r >> 63;
        r = (r << 1) | ((n.lo >> i) & 1u);
        if (carry || r >= d) {
            r -= d;
            quo |= (uint64_t) 1 << i;
        }
    }
    *q = quo;
    *rem = r;
    return true;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
    if (a == 0) return b;
    if (b == 0) return a;
    int k = 0;
    while (((a | b) & 1u) == 0) {
        a >>= 1;
        b >>= 1;
        k++;
    }
    while ((a & 1u) == 0) a >>= 1;
    while (b != 0) {
        while ((b & 1u) == 0) b >>= 1;
        if (a > b) {
            uint64_t t = a;
            a = b;
            b = t;
        }
        b -= a;
    }
    return a << k;
}

static uint64_t div_exact(uint64_t a, uint64_t b)
{
    uint64_t q = 0, r = 0;
    jr_u128_t n = {0, a};
    (void) jr_divmod(n, b, &q, &r);
    return q;
}

/* ---- lot header ---- */
static void lot_check(const uint8_t *b, uint8_t out[4])
{
    uint8_t d[32];
    sha256(b, 124, d);
    copy(out, d, 4);
}

void jr_lot_hdr_encode(const jr_lot_hdr_t *h, uint8_t out[JR_LOT_HDR_LEN])
{
    zero(out, JR_LOT_HDR_LEN);
    out[0] = 'Z';
    out[1] = 'X';
    out[2] = 'L';
    out[3] = 'T';
    le16(out + 4, 1);
    le16(out + 6, h->grade_kind);
    le32(out + 8, h->warehouse_id);
    le32(out + 12, h->volume);
    le64(out + 16, h->drum_serial);
    le64(out + 24, h->lot_number);
    le32(out + 32, h->grade);
    le16(out + 36, h->haircut_bps);
    copy(out + 40, h->vault_pubkey, 32);
    copy(out + 72, h->custodian_receipt, 32);
    le32(out + 104, (uint32_t) h->depot_lat);
    le32(out + 108, (uint32_t) h->depot_lon);
    lot_check(out, out + 124);
}

jr_status_t jr_lot_hdr_decode(const uint8_t in[JR_LOT_HDR_LEN], jr_lot_hdr_t *h)
{
    if (!in || !h) return JR_E_ARG;
    if (in[0] != 'Z' || in[1] != 'X' || in[2] != 'L' || in[3] != 'T') return JR_E_FORMAT;
    if (rd16(in + 4) != 1 || rd16(in + 38) != 0) return JR_E_FORMAT;
    for (uint32_t i = 112; i < 124; i++)
        if (in[i] != 0) return JR_E_FORMAT;
    uint8_t c[4];
    lot_check(in, c);
    if (!same(c, in + 124, 4)) return JR_E_FORMAT;
    jr_lot_hdr_t t;
    t.grade_kind = rd16(in + 6);
    t.warehouse_id = rd32(in + 8);
    t.volume = rd32(in + 12);
    t.drum_serial = rd64(in + 16);
    t.lot_number = rd64(in + 24);
    t.grade = rd32(in + 32);
    t.haircut_bps = rd16(in + 36);
    copy(t.vault_pubkey, in + 40, 32);
    copy(t.custodian_receipt, in + 72, 32);
    t.depot_lat = (int32_t) rd32(in + 104);
    t.depot_lon = (int32_t) rd32(in + 108);
    if (t.volume == 0 || t.haircut_bps > 10000) return JR_E_FORMAT;
    if (t.depot_lat < -90000000 || t.depot_lat > 90000000 || t.depot_lon < -180000000 ||
        t.depot_lon > 180000000)
        return JR_E_FORMAT;
    copy(h, &t, sizeof t);
    return JR_OK;
}

/* ---- Portolan zones (R8) ---- */
static bool zone_ok(const jr_zone_t *z)
{
    return z->pts && z->npts >= 3 && z->npts <= JR_MAX_ZONE_PTS &&
           (z->kind == JR_ZONE || z->kind == JR_SANCTION);
}

jr_status_t jr_table_hash(const jr_zone_t *zones, uint32_t n, uint8_t out[32])
{
    if (!out || (n && !zones)) return JR_E_ARG;
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) TABLE_DOM, sizeof TABLE_DOM);
    h32(&c, n);
    for (uint32_t i = 0; i < n; i++) {
        const jr_zone_t *z = &zones[i];
        if (!zone_ok(z)) return JR_E_ARG;
        h32(&c, z->id);
        h8(&c, z->kind);
        h32(&c, z->caps);
        h32(&c, z->region);
        h32(&c, z->npts);
        for (uint32_t k = 0; k < z->npts; k++) {
            h32(&c, (uint32_t) z->pts[k].lat);
            h32(&c, (uint32_t) z->pts[k].lon);
        }
    }
    sha256_final(&c, out);
    return JR_OK;
}

/* Closed polygon: a point on an edge is inside. x = lon, y = lat. */
static bool in_poly(const jr_zone_t *z, jr_pt_t p)
{
    bool in = false;
    int64_t px = p.lon, py = p.lat;
    for (uint32_t i = 0, j = (uint32_t) z->npts - 1u; i < z->npts; j = i++) {
        int64_t ax = z->pts[j].lon, ay = z->pts[j].lat;
        int64_t bx = z->pts[i].lon, by = z->pts[i].lat;
        int64_t cr = (bx - ax) * (py - ay) - (by - ay) * (px - ax);
        if (cr == 0 && px >= (ax < bx ? ax : bx) && px <= (ax < bx ? bx : ax) &&
            py >= (ay < by ? ay : by) && py <= (ay < by ? by : ay))
            return true;
        if ((ay > py) != (by > py)) {
            int64_t s = (py - ay) * (bx - ax) - (px - ax) * (by - ay);
            if (by > ay ? s > 0 : s < 0) in = !in;
        }
    }
    return in;
}

lpres_state_t jr_zone_locate(const jr_zone_t *zones, uint32_t n, jr_pt_t p, uint32_t *zone_id,
                             uint32_t *caps, uint32_t *region)
{
    uint32_t zid = 0, zc = 0, zr = 0;
    lpres_state_t v = LPRES_STATE_NEITHER;
    bool ok = zones || n == 0, found = false, clash = false;
    for (uint32_t i = 0; ok && i < n; i++) ok = zone_ok(&zones[i]);
    for (uint32_t i = 0; ok && i < n; i++)
        if (zones[i].kind == JR_SANCTION && in_poly(&zones[i], p)) v = LPRES_STATE_FALSE;
    for (uint32_t i = 0; ok && v != LPRES_STATE_FALSE && i < n; i++) {
        const jr_zone_t *z = &zones[i];
        if (z->kind != JR_ZONE || !in_poly(z, p)) continue;
        if (!found) {
            found = true;
            zid = z->id;
            zc = z->caps;
            zr = z->region;
            continue;
        }
        if (z->caps != zc || z->region != zr) clash = true;
        if (z->id < zid) zid = z->id;
    }
    if (found && v != LPRES_STATE_FALSE)
        v = (clash || (zc & ~JR_CAP_KNOWN)) ? LPRES_STATE_BOTH : LPRES_STATE_TRUE;
    else
        zid = zc = zr = 0;
    if (zone_id) *zone_id = zid;
    if (caps) *caps = zc;
    if (region) *region = zr;
    return v;
}

jr_status_t jr_zone_attest(const jr_zone_t *zones, uint32_t n, jr_pt_t p,
                           const uint8_t device_secret[32], uint64_t epoch, jr_zone_claim_t *out)
{
    if (!out || !device_secret) return JR_E_ARG;
    zero(out, sizeof *out);
    jr_status_t s = jr_table_hash(zones, n, out->table_hash);
    if (s != JR_OK) return s;
    out->verdict = (uint8_t) jr_zone_locate(zones, n, p, &out->zone_id, &out->caps, &out->region);
    out->epoch = epoch;
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) NULL_DOM, sizeof NULL_DOM);
    sha256_update(&c, device_secret, 32);
    h64(&c, epoch);
    sha256_final(&c, out->nullifier);
    return JR_OK;
}

void jr_zone_claim_msg(const jr_zone_claim_t *c, uint8_t out[JR_CLAIM_MSG_LEN])
{
    uint8_t *o = out;
    copy(o, CLAIM_DOM, sizeof CLAIM_DOM);
    o += sizeof CLAIM_DOM;
    copy(o, c->table_hash, 32);
    o += 32;
    le32(o, c->zone_id);
    le32(o + 4, c->caps);
    le32(o + 8, c->region);
    o[12] = c->verdict;
    le64(o + 13, c->epoch);
    copy(o + 21, c->nullifier, 32);
}

/* ---- router setup ---- */
jr_status_t jr_init(jr_router_t *r, const jr_hooks_t *hooks, const jr_zone_t *zones,
                    uint32_t nzones, uint64_t max_age)
{
    if (!r || !hooks || !hooks->burn) return JR_E_ARG;
    zero(r, sizeof *r);
    if (jr_table_hash(zones, nzones, r->table_hash) != JR_OK) return JR_E_ARG;
    copy(&r->hooks, hooks, sizeof *hooks);
    r->zones = zones;
    r->nzones = nzones;
    r->max_age = max_age;
    r->profile = JR_PROFILE_COMMUNITY;
    r->next_receipt = 1;
    return JR_OK;
}

jr_status_t jr_set_profile(jr_router_t *r, jr_profile_t p)
{
    if (!r || (p != JR_PROFILE_COMMUNITY && p != JR_PROFILE_INSTITUTION)) return JR_E_ARG;
    r->profile = (uint8_t) p;
    if (p == JR_PROFILE_COMMUNITY) r->enabled = false;
    return JR_OK;
}

jr_status_t jr_enable(jr_router_t *r, bool on)
{
    if (!r) return JR_E_ARG;
    if (on && r->profile != JR_PROFILE_INSTITUTION) return JR_E_PROFILE;
    r->enabled = on;
    return JR_OK;
}

static jr_asset_t *find_asset(jr_router_t *r, uint32_t id)
{
    for (uint32_t i = 0; i < r->nassets; i++)
        if (r->assets[i].id == id) return &r->assets[i];
    return NULL;
}

static bool one_known_bit(uint32_t c)
{
    return c == JR_CAP_COMMODITY || c == JR_CAP_COMPUTE || c == JR_CAP_MUTUAL || c == JR_CAP_METALS;
}

static bool physical(uint32_t c)
{
    return c == JR_CAP_COMMODITY || c == JR_CAP_METALS;
}

jr_status_t jr_add_asset(jr_router_t *r, const jr_asset_t *a)
{
    if (!r || !a || a->id == 0 || !one_known_bit(a->cap) || a->num == 0 || a->den == 0 ||
        a->lot == 0)
        return JR_E_ARG;
    if (find_asset(r, a->id)) return JR_E_ARG;
    if (jr_mul64(a->den, a->lot).hi != 0) return JR_E_RANGE;
    if (r->nassets >= JR_MAX_ASSETS) return JR_E_FULL;
    jr_asset_t *d = &r->assets[r->nassets++];
    copy(d, a, sizeof *a);
    d->active = true;
    return JR_OK;
}

jr_status_t jr_retire_asset(jr_router_t *r, uint32_t id)
{
    jr_asset_t *a = r ? find_asset(r, id) : NULL;
    if (!a) return JR_E_NOTFOUND;
    a->active = false;
    return JR_OK;
}

jr_status_t jr_set_n_asset(jr_router_t *r, uint32_t id)
{
    if (!r) return JR_E_ARG;
    if (id != 0) {
        jr_asset_t *a = find_asset(r, id);
        if (!a) return JR_E_NOTFOUND;
        if (a->cap != JR_CAP_COMPUTE) return JR_E_ARG;
    }
    r->n_asset = id;
    return JR_OK;
}

jr_status_t jr_add_registry(jr_router_t *r, uint32_t id, jr_registry_fn fn, void *ctx)
{
    if (!r || !fn || id == 0) return JR_E_ARG;
    if (r->nregs >= JR_MAX_REGISTRIES) return JR_E_FULL;
    r->regs[r->nregs] = fn;
    r->reg_ctx[r->nregs] = ctx;
    r->reg_id[r->nregs] = id;
    r->nregs++;
    return JR_OK;
}

jr_status_t jr_add_lot_src(jr_router_t *r, uint32_t asset_id, const uint8_t hdr[JR_LOT_HDR_LEN],
                           uint32_t source)
{
    if (!r || !hdr) return JR_E_ARG;
    jr_asset_t *a = find_asset(r, asset_id);
    if (!a) return JR_E_NOTFOUND;
    if (!physical(a->cap)) return JR_E_ARG;
    jr_lot_hdr_t h;
    jr_status_t s = jr_lot_hdr_decode(hdr, &h);
    if (s != JR_OK) return s;
    for (uint32_t i = 0; i < r->nlots; i++)
        if (r->lots[i].hdr.warehouse_id == h.warehouse_id &&
            r->lots[i].hdr.drum_serial == h.drum_serial)
            return JR_E_ARG;
    if (r->nlots >= JR_MAX_LOTS) return JR_E_FULL;
    jr_lot_t *l = &r->lots[r->nlots++];
    copy(&l->hdr, &h, sizeof h);
    l->asset_id = asset_id;
    l->state = LOT_FREE;
    l->source = source;
    return JR_OK;
}

jr_status_t jr_add_lot(jr_router_t *r, uint32_t asset_id, const uint8_t hdr[JR_LOT_HDR_LEN])
{
    return jr_add_lot_src(r, asset_id, hdr, 0);
}

uint32_t jr_withdraw_source(jr_router_t *r, uint32_t source)
{
    uint32_t n = 0;
    if (!r || source == 0) return 0;
    for (uint32_t i = 0; i < r->nlots; i++)
        if (r->lots[i].source == source && r->lots[i].state == LOT_FREE) {
            r->lots[i].state = LOT_WITHDRAWN;
            n++;
        }
    if (jr_free_physical(r) == 0) r->physical_online = false;
    return n;
}

uint32_t jr_free_physical(const jr_router_t *r)
{
    uint32_t n = 0;
    if (!r) return 0;
    for (uint32_t i = 0; i < r->nlots; i++) n += r->lots[i].state == LOT_FREE;
    return n;
}

jr_status_t jr_set_physical(jr_router_t *r, bool on)
{
    if (!r) return JR_E_ARG;
    if (on && jr_free_physical(r) == 0) return JR_E_NOTFOUND;
    r->physical_online = on;
    return JR_OK;
}

uint32_t jr_free_lots(const jr_router_t *r, uint32_t asset_id)
{
    uint32_t n = 0;
    if (!r) return 0;
    for (uint32_t i = 0; i < r->nlots; i++)
        if (r->lots[i].asset_id == asset_id && r->lots[i].state == LOT_FREE) n++;
    return n;
}

/* ---- zone verification (R8) ---- */
static bool null_seen(const jr_router_t *r, const uint8_t nf[32])
{
    for (uint32_t i = 0; i < r->nnull; i++)
        if (same(r->nulls[i].nf, nf, 32)) return true;
    return false;
}

static jr_status_t null_record(jr_router_t *r, const uint8_t nf[32], uint64_t epoch, uint64_t now)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < r->nnull; i++) {
        /* keep entries still inside the freshness window */
        if (now - r->nulls[i].epoch <= r->max_age) {
            if (k != i) copy(&r->nulls[k], &r->nulls[i], sizeof r->nulls[i]);
            k++;
        }
    }
    r->nnull = k;
    if (r->nnull >= JR_MAX_NULLIFIERS) return JR_E_FULL;
    copy(r->nulls[r->nnull].nf, nf, 32);
    r->nulls[r->nnull].epoch = epoch;
    r->nnull++;
    return JR_OK;
}

static const jr_zone_t *table_zone(const jr_router_t *r, uint32_t id)
{
    for (uint32_t i = 0; i < r->nzones; i++)
        if (r->zones[i].kind == JR_ZONE && r->zones[i].id == id) return &r->zones[i];
    return NULL;
}

jr_status_t jr_zone_verify(jr_router_t *r, const jr_zone_claim_t *c, const uint8_t *proof,
                           uint32_t proof_len, uint64_t now_epoch, jr_zone_ok_t *out)
{
    if (!r || !c || !out) return JR_E_ARG;
    out->verdict = LPRES_STATE_NEITHER;
    out->zone_id = 0;
    out->caps = 0;
    out->region = 0;
    if (c->verdict > LPRES_STATE_BOTH) return JR_E_CLAIM;
    if (!same(c->table_hash, r->table_hash, 32))
        return JR_OK; /* a map version this node does not hold */
    if (c->epoch > now_epoch || now_epoch - c->epoch > r->max_age)
        return JR_OK; /* stale or from the future */
    if (null_seen(r, c->nullifier)) return JR_E_REPLAY;
    if (!r->hooks.proof_verify) return JR_OK; /* stub slot: unverified reads NEITHER */
    uint8_t msg[JR_CLAIM_MSG_LEN];
    jr_zone_claim_msg(c, msg);
    if (!r->hooks.proof_verify(r->hooks.proof_ctx, msg, JR_CLAIM_MSG_LEN, proof, proof_len))
        return JR_E_PROOF;
    lpres_state_t v = (lpres_state_t) c->verdict;
    if (v == LPRES_STATE_TRUE) {
        const jr_zone_t *z = table_zone(r, c->zone_id);
        if (!z || z->caps != c->caps || z->region != c->region) return JR_E_CLAIM;
        if (c->caps & ~JR_CAP_KNOWN) v = LPRES_STATE_BOTH;
    }
    jr_status_t s = null_record(r, c->nullifier, c->epoch, now_epoch);
    if (s != JR_OK) return s;
    out->verdict = v;
    if (v != LPRES_STATE_FALSE) {
        out->zone_id = c->zone_id;
        out->caps = c->caps;
        out->region = c->region;
    }
    return JR_OK;
}

/* ---- vouchers (R5) ---- */
static jr_voucher_t *find_voucher(jr_router_t *r, const uint8_t user[32], uint32_t asset_id)
{
    for (uint32_t i = 0; i < r->nvouchers; i++)
        if (r->vouchers[i].asset_id == asset_id && same(r->vouchers[i].user, user, 32))
            return &r->vouchers[i];
    return NULL;
}

static void set_voucher(jr_router_t *r, const uint8_t user[32], uint32_t asset_id, uint64_t v)
{
    jr_voucher_t *w = find_voucher(r, user, asset_id);
    if (v == 0) {
        if (w) {
            jr_voucher_t *last = &r->vouchers[r->nvouchers - 1u];
            if (w != last) copy(w, last, sizeof *w);
            r->nvouchers--;
        }
        return;
    }
    if (!w) {
        w = &r->vouchers[r->nvouchers++]; /* room checked when planning */
        copy(w->user, user, 32);
        w->asset_id = asset_id;
    }
    w->r = v;
}

static void reduce(uint64_t rv, uint64_t num, uint64_t *p, uint64_t *q)
{
    if (rv == 0) {
        *p = 0;
        *q = 1;
        return;
    }
    uint64_t g = gcd64(rv, num);
    *p = div_exact(rv, g);
    *q = div_exact(num, g);
}

jr_status_t jr_voucher(const jr_router_t *r, const uint8_t user[32], uint32_t asset_id, uint64_t *p,
                       uint64_t *q)
{
    if (!r || !user || !p || !q) return JR_E_ARG;
    jr_router_t *m = (jr_router_t *) r;
    const jr_asset_t *a = find_asset(m, asset_id);
    if (!a) return JR_E_NOTFOUND;
    const jr_voucher_t *w = find_voucher(m, user, asset_id);
    reduce(w ? w->r : 0, a->num, p, q);
    return JR_OK;
}

/* ---- planning (R3, R4) ---- */
typedef struct {
    const jr_asset_t *a;
    uint64_t lots;
    uint64_t rnew;
    uint32_t nlot;
    uint16_t lot_idx[JR_MAX_RECEIPT_LOTS];
} plan_t;

typedef enum { P_OK = 0, P_DUST = 1, P_NO = 2, P_FULL = 3 } pres_t;

static bool voucher_frozen(const jr_router_t *r, const uint8_t user[32], uint32_t asset_id)
{
    for (uint32_t i = 0; i < r->nescrows; i++) {
        const jr_escrow_t *e = &r->escrows[i];
        if (!e->settled && e->asset_id == asset_id && same(e->user, user, 32)) return true;
    }
    return false;
}

static pres_t plan_asset(jr_router_t *r, const jr_asset_t *a, const uint8_t user[32], uint64_t vino,
                         bool allow_zero, plan_t *p)
{
    if (voucher_frozen(r, user, a->id)) return P_NO;
    jr_voucher_t *w = find_voucher(r, user, a->id);
    jr_u128_t total = add64(jr_mul64(vino, a->num), w ? w->r : 0);
    uint64_t d = a->den * a->lot; /* fits: checked in jr_add_asset */
    uint64_t lots, rnew;
    if (!jr_divmod(total, d, &lots, &rnew)) return P_NO; /* more lots than 64 bits: never wraps */
    if (lots == 0 && !allow_zero) return P_DUST;
    if (rnew != 0 && !w && r->nvouchers >= JR_MAX_VOUCHERS) return P_FULL;
    p->a = a;
    p->lots = lots;
    p->rnew = rnew;
    p->nlot = 0;
    if (physical(a->cap)) {
        if (lots > JR_MAX_RECEIPT_LOTS) return P_NO;
        for (uint32_t i = 0; i < r->nlots && p->nlot < lots; i++)
            if (r->lots[i].asset_id == a->id && r->lots[i].state == LOT_FREE)
                p->lot_idx[p->nlot++] = (uint16_t) i;
        if (p->nlot < lots) return P_NO;
    }
    return P_OK;
}

static uint32_t level_of(uint32_t cap)
{
    if (physical(cap)) return 1;
    return cap == JR_CAP_COMPUTE ? 2u : 3u;
}

static uint32_t sublevel(uint32_t cap)
{
    return cap == JR_CAP_METALS ? 1u : 0u;
}

/* The hierarchy order of R3 as asset indexes; returns the count. */
static uint32_t order_assets(const jr_router_t *r, uint32_t want, uint32_t *idx)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < r->nassets; i++)
        if (want != 0 && r->assets[i].id == want) idx[n++] = i;
    for (uint32_t lv = 1; lv <= 3; lv++)
        for (uint32_t sub = 0; sub <= 1; sub++)
            for (uint32_t i = 0; i < r->nassets; i++) {
                const jr_asset_t *a = &r->assets[i];
                if (a->id == want || level_of(a->cap) != lv || sublevel(a->cap) != sub) continue;
                idx[n++] = i;
            }
    return n;
}

static bool eligible(const jr_asset_t *a, uint32_t caps, uint32_t region, bool op)
{
    if (!a->active || !(caps & a->cap)) return false;
    if (a->needs_operator && !op) return false;
    if (a->region != 0 && a->region != region) return false;
    return true;
}

typedef struct {
    uint8_t outcome;
    uint8_t reason;
    uint8_t route;
    bool transferable;
    bool have_plan;
    plan_t plan;
    uint32_t nclaims;
    jr_claim_t claims[JR_MAX_CLAIMS];
} route_t;

static void claim_zone(route_t *t, uint32_t zone_id, lpres_state_t v)
{
    jr_claim_t *c = &t->claims[t->nclaims++];
    c->source = 0;
    c->verdict = (uint8_t) v;
    zero(c->evidence, 32);
    le32(c->evidence, zone_id);
}

/* Plans a allotment without changing anything. use_regs is false for a
 * ruling's REROUTE (R7). */
static jr_status_t plan_route(jr_router_t *r, const jr_request_t *q, const jr_zone_ok_t *z,
                              bool use_regs, route_t *t)
{
    zero(t, sizeof *t);
    t->route = (uint8_t) z->verdict;
    t->transferable = true;
    if (z->verdict == LPRES_STATE_FALSE) return JR_E_SANCTIONED;
    if (z->verdict == LPRES_STATE_BOTH ||
        (z->verdict == LPRES_STATE_TRUE && (z->caps & ~JR_CAP_KNOWN))) {
        t->route = LPRES_STATE_BOTH;
        t->outcome = JR_OUT_ESCROW;
        t->reason = JR_ESC_ZONE_CONFLICT;
        claim_zone(t, z->zone_id, LPRES_STATE_BOTH);
        return JR_OK;
    }
    if (z->verdict == LPRES_STATE_NEITHER) {
        const jr_asset_t *a = r->n_asset ? find_asset(r, r->n_asset) : NULL;
        if (a && a->active) {
            pres_t p = plan_asset(r, a, q->user, q->vino, false, &t->plan);
            if (p == P_DUST) return JR_E_DUST;
            if (p == P_FULL) return JR_E_FULL;
            if (p == P_OK) {
                t->have_plan = true;
                t->outcome = JR_OUT_LOCAL_CREDIT;
                t->transferable = false;
                return JR_OK;
            }
        }
        t->outcome = JR_OUT_ESCROW;
        t->reason = JR_ESC_NO_ROUTE;
        claim_zone(t, 0, LPRES_STATE_NEITHER);
        return JR_OK;
    }
    uint32_t caps = r->physical_online ? z->caps : (z->caps & ~JR_CAP_PHYSICAL);
    uint32_t idx[2 * JR_MAX_ASSETS];
    uint32_t n = order_assets(r, q->want_asset, idx);
    bool dust = false;
    for (uint32_t k = 0; k < n; k++) {
        const jr_asset_t *a = &r->assets[idx[k]];
        if (!eligible(a, caps, z->region, q->operator_node)) continue;
        bool allow_zero = q->accumulate && a->id == q->want_asset;
        pres_t p = plan_asset(r, a, q->user, q->vino, allow_zero, &t->plan);
        if (p == P_FULL) return JR_E_FULL;
        if (p == P_DUST) dust = true;
        if (p != P_OK) continue;
        t->have_plan = true;
        lpres_state_t v = LPRES_STATE_TRUE;
        t->nclaims = 0;
        claim_zone(t, z->zone_id, LPRES_STATE_TRUE);
        for (uint32_t g = 0; use_regs && g < r->nregs; g++) {
            uint8_t ev[32];
            zero(ev, 32);
            lpres_state_t rv = r->regs[g](r->reg_ctx[g], a->cap, z->zone_id, ev);
            if (rv > LPRES_STATE_BOTH)
                rv = LPRES_STATE_BOTH; /* an answer out of range is a contradiction */
            v = (lpres_state_t) (v | rv);
            jr_claim_t *c = &t->claims[t->nclaims++];
            c->source = r->reg_id[g];
            c->verdict = (uint8_t) rv;
            copy(c->evidence, ev, 32);
        }
        t->route = (uint8_t) v;
        if (v == LPRES_STATE_BOTH) {
            t->outcome = JR_OUT_ESCROW;
            t->reason = JR_ESC_CLASS_CONFLICT;
        } else {
            t->outcome = t->plan.lots ? JR_OUT_ALLOTTED : JR_OUT_VOUCHER_ONLY;
        }
        return JR_OK;
    }
    if (dust) return JR_E_DUST;
    t->have_plan = false;
    t->outcome = JR_OUT_ESCROW;
    t->reason = JR_ESC_NO_ROUTE;
    t->nclaims = 0;
    claim_zone(t, z->zone_id, LPRES_STATE_TRUE);
    return JR_OK;
}

/* ---- escrow slots (R7) ---- */
static jr_escrow_t *escrow_slot(jr_router_t *r)
{
    if (r->nescrows < JR_MAX_ESCROWS) return &r->escrows[r->nescrows];
    for (uint32_t i = 0; i < JR_MAX_ESCROWS; i++)
        if (r->escrows[i].settled) return &r->escrows[i];
    return NULL;
}

static void escrow_digest(const jr_escrow_t *e, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) ESCROW_DOM, sizeof ESCROW_DOM);
    h64(&c, e->id);
    sha256_update(&c, e->user, 32);
    h64(&c, e->vino);
    h8(&c, e->reason);
    h32(&c, e->zone_id);
    h32(&c, e->caps);
    h32(&c, e->region);
    h8(&c, e->operator_node ? 1u : 0u);
    h32(&c, e->asset_id);
    h64(&c, e->lots);
    h64(&c, e->voucher_after);
    h32(&c, e->nlot_idx);
    for (uint32_t i = 0; i < e->nlot_idx; i++) h32(&c, e->lot_idx[i]);
    h32(&c, e->nclaims);
    for (uint32_t i = 0; i < e->nclaims; i++) {
        h32(&c, e->claims[i].source);
        h8(&c, e->claims[i].verdict);
        sha256_update(&c, e->claims[i].evidence, 32);
    }
    sha256_final(&c, out);
}

/* ---- receipts (R10) ---- */
static void receipt_seal(jr_router_t *r, jr_receipt_t *o)
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) RECEIPT_DOM, sizeof RECEIPT_DOM);
    sha256_update(&c, r->head, 32);
    h64(&c, o->receipt_id);
    sha256_update(&c, o->user, 32);
    h64(&c, o->vino_burned);
    h8(&c, o->outcome);
    h8(&c, o->event);
    h32(&c, o->asset_id);
    h32(&c, o->cap);
    h8(&c, o->transferable ? 1u : 0u);
    h64(&c, o->lots);
    h32(&c, o->nlot_idx);
    for (uint32_t i = 0; i < o->nlot_idx; i++) h32(&c, o->lot_idx[i]);
    h64(&c, o->voucher_num);
    h64(&c, o->voucher_den);
    h8(&c, o->route);
    h64(&c, o->escrow_id);
    sha256_final(&c, o->digest);
    copy(r->head, o->digest, 32);
}

/* Commits a planned route whose VINO is already burned. */
static void commit(jr_router_t *r, const uint8_t user[32], uint64_t vino, bool op, bool released,
                   const jr_zone_ok_t *z, route_t *t, jr_receipt_t *o)
{
    zero(o, sizeof *o);
    o->receipt_id = r->next_receipt++;
    copy(o->user, user, 32);
    o->vino_burned = vino;
    o->outcome = t->outcome;
    o->event = t->outcome == JR_OUT_ESCROW ? JR_EVENT_CUSTODIAL_HOLD
               : released                  ? JR_EVENT_CUSTODIAL_IN_KIND_DISBURSEMENT
                                           : JR_EVENT_DISCRETIONARY_ALLOTMENT;
    o->route = t->route;
    o->transferable = t->transferable;
    o->voucher_den = 1;
    if (t->have_plan) {
        o->asset_id = t->plan.a->id;
        o->cap = t->plan.a->cap;
        o->lots = t->plan.lots;
        o->nlot_idx = t->plan.nlot;
        copy(o->lot_idx, t->plan.lot_idx, sizeof o->lot_idx);
    }
    if (t->outcome == JR_OUT_ESCROW) {
        jr_escrow_t *e = escrow_slot(r); /* room checked before the burn */
        if (e == &r->escrows[r->nescrows]) r->nescrows++;
        zero(e, sizeof *e);
        e->id = o->receipt_id;
        copy(e->user, user, 32);
        e->vino = vino;
        e->reason = t->reason;
        e->zone_id = z->zone_id;
        e->caps = z->caps;
        e->region = z->region;
        e->operator_node = op;
        if (t->have_plan) {
            e->asset_id = t->plan.a->id;
            e->lots = t->plan.lots;
            e->voucher_after = t->plan.rnew;
            e->nlot_idx = t->plan.nlot;
            copy(e->lot_idx, t->plan.lot_idx, sizeof e->lot_idx);
            for (uint32_t i = 0; i < t->plan.nlot; i++)
                r->lots[t->plan.lot_idx[i]].state = LOT_ESCROWED;
        }
        e->nclaims = t->nclaims;
        copy(e->claims, t->claims, sizeof e->claims);
        escrow_digest(e, e->digest);
        o->escrow_id = e->id;
        if (t->have_plan) {
            const jr_voucher_t *w = find_voucher(r, user, t->plan.a->id);
            reduce(w ? w->r : 0, t->plan.a->num, &o->voucher_num, &o->voucher_den);
        }
        receipt_seal(r, o);
        if (r->hooks.escrow) r->hooks.escrow(r->hooks.ctx, e);
        return;
    }
    for (uint32_t i = 0; i < t->plan.nlot; i++) r->lots[t->plan.lot_idx[i]].state = LOT_DELIVERED;
    set_voucher(r, user, t->plan.a->id, t->plan.rnew);
    reduce(t->plan.rnew, t->plan.a->num, &o->voucher_num, &o->voucher_den);
    receipt_seal(r, o);
    if (r->hooks.deliver) r->hooks.deliver(r->hooks.ctx, o);
}

jr_status_t jr_accept_allotment(jr_router_t *r, const jr_request_t *q, const jr_zone_ok_t *z,
                                jr_receipt_t *out)
{
    if (!r || !q || !z || !out || q->vino == 0) return JR_E_ARG;
    if (r->profile != JR_PROFILE_INSTITUTION || !r->enabled) return JR_E_PROFILE;
    route_t t;
    jr_status_t s = plan_route(r, q, z, true, &t);
    if (s != JR_OK) return s;
    if (t.outcome == JR_OUT_ESCROW && !escrow_slot(r)) return JR_E_FULL;
    if (r->hooks.burn(r->hooks.ctx, q->user, q->vino) != 0) return JR_E_BURN;
    r->burned_total += q->vino;
    commit(r, q->user, q->vino, q->operator_node, false, z, &t, out);
    return JR_OK;
}

/* ---- rulings (R7) ---- */
static jr_escrow_t *find_escrow(jr_router_t *r, uint64_t id)
{
    for (uint32_t i = 0; i < r->nescrows; i++)
        if (r->escrows[i].id == id && id != 0) return &r->escrows[i];
    return NULL;
}

const jr_escrow_t *jr_escrow_get(const jr_router_t *r, uint64_t id)
{
    return r ? find_escrow((jr_router_t *) r, id) : NULL;
}

void jr_ruling_msg(const jr_escrow_t *e, uint8_t kind, uint32_t caps, uint32_t region,
                   uint8_t out[JR_RULING_MSG_LEN])
{
    copy(out, RULING_DOM, sizeof RULING_DOM);
    le64(out + 16, e->id);
    out[24] = kind;
    le32(out + 25, caps);
    le32(out + 29, region);
    copy(out + 33, e->digest, 32);
}

static void release_lots(jr_router_t *r, const jr_escrow_t *e)
{
    for (uint32_t i = 0; i < e->nlot_idx; i++) r->lots[e->lot_idx[i]].state = LOT_FREE;
}

jr_status_t jr_escrow_settle(jr_router_t *r, uint64_t escrow_id, uint8_t kind, uint32_t caps,
                             uint32_t region, const uint8_t *sig, uint32_t siglen,
                             jr_receipt_t *out)
{
    if (!r || !out) return JR_E_ARG;
    jr_escrow_t *e = find_escrow(r, escrow_id);
    if (!e) return JR_E_NOTFOUND;
    if (e->settled) return JR_E_SETTLED;
    if (kind != JR_RULE_DELIVER && kind != JR_RULE_REROUTE && kind != JR_RULE_REFUND)
        return JR_E_RULING;
    if (kind == JR_RULE_DELIVER && e->asset_id == 0) return JR_E_RULING;
    if (!r->hooks.arbiter_verify) return JR_E_SIG;
    uint8_t msg[JR_RULING_MSG_LEN];
    jr_ruling_msg(e, kind, caps, region, msg);
    if (!r->hooks.arbiter_verify(r->hooks.arbiter_ctx, msg, JR_RULING_MSG_LEN, sig, siglen))
        return JR_E_SIG;
    zero(out, sizeof *out);

    if (kind == JR_RULE_REFUND) {
        if (!r->hooks.refund || r->hooks.refund(r->hooks.ctx, e->user, e->vino) != 0)
            return JR_E_REFUND;
        release_lots(r, e);
        r->refunded_total += e->vino;
        e->settled = true;
        e->ruling = kind;
        return JR_OK;
    }

    if (kind == JR_RULE_DELIVER) {
        const jr_asset_t *a = find_asset(r, e->asset_id);
        if (!a) return JR_E_NOTFOUND;
        if (e->voucher_after != 0 && !find_voucher(r, e->user, a->id) &&
            r->nvouchers >= JR_MAX_VOUCHERS)
            return JR_E_FULL;
        route_t t;
        zero(&t, sizeof t);
        t.outcome = e->lots ? JR_OUT_ALLOTTED : JR_OUT_VOUCHER_ONLY;
        t.route = LPRES_STATE_TRUE;
        t.transferable = true;
        t.have_plan = true;
        t.plan.a = a;
        t.plan.lots = e->lots;
        t.plan.rnew = e->voucher_after;
        t.plan.nlot = e->nlot_idx;
        copy(t.plan.lot_idx, e->lot_idx, sizeof t.plan.lot_idx);
        e->settled = true;
        e->ruling = kind;
        jr_zone_ok_t z = {LPRES_STATE_TRUE, e->zone_id, e->caps, e->region};
        commit(r, e->user, e->vino, e->operator_node, true, &z, &t, out);
        return JR_OK;
    }

    /* REROUTE: the ruling's caps and region, no registries. Unfreeze first
     * so the plan can use the holder's voucher, and restore on failure. */
    release_lots(r, e);
    e->settled = true;
    jr_request_t q;
    zero(&q, sizeof q);
    copy(q.user, e->user, 32);
    q.vino = e->vino;
    q.operator_node = e->operator_node;
    jr_zone_ok_t z = {LPRES_STATE_TRUE, e->zone_id, caps, region};
    route_t t;
    jr_status_t s = plan_route(r, &q, &z, false, &t);
    if (s == JR_E_DUST || s == JR_E_SANCTIONED) {
        /* already burned: never lost, park it with no route */
        zero(&t, sizeof t);
        t.outcome = JR_OUT_ESCROW;
        t.reason = JR_ESC_NO_ROUTE;
        t.route = LPRES_STATE_TRUE;
        t.transferable = true;
        claim_zone(&t, z.zone_id, LPRES_STATE_TRUE);
        s = JR_OK;
    }
    if (s != JR_OK) {
        for (uint32_t i = 0; i < e->nlot_idx; i++) r->lots[e->lot_idx[i]].state = LOT_ESCROWED;
        e->settled = false;
        return s;
    }
    e->ruling = kind;
    /* the settled slot itself may be reused for a new escrow; copy what
     * commit needs first (it reads only the request and zone) */
    commit(r, q.user, q.vino, q.operator_node, true, &z, &t, out);
    return JR_OK;
}
