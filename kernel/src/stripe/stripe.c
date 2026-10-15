/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* stripe.c — the erasure-coded storage service (stripe.h).
 *
 * The field arithmetic and the generator coefficients are freight's
 * (freight_gf_mul, freight_gf_inv, freight_gen_coef); the hash is sha256.
 * Nothing here re-derives a GF(2^8) table. */
#include "stripe.h"

#include "freight.h"
#include "sha256.h"

static const uint8_t st_magic[4] = {'Z', 'X', 'S', 'T'};

static const stripe_profile_t st_profiles[] = {
    {"hearth", 4, 2, 1, 1},    {"homestead", 16, 4, 1, 1},   {"guildhall", 16, 4, 1, 2},
    {"strongbox", 8, 4, 1, 1}, {"count_house", 16, 8, 3, 1}, {"hoard", 12, 8, 4, 1},
};
#define ST_NPROFILES ((uint32_t) (sizeof st_profiles / sizeof st_profiles[0]))

/* ---- bytes ---- */
static void st_zero(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}

static void st_copy(uint8_t *d, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}

static int st_eq(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t x = 0;
    for (uint32_t i = 0; i < n; i++) x |= (uint8_t) (a[i] ^ b[i]);
    return x == 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t get32(const uint8_t *p)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* ---- geometry ---- */
static int cfg_fields_ok(const stripe_cfg_t *c)
{
    return c->k >= 1 && c->m >= 1 && c->piece_blocks >= 1;
}

static int cfg_fits(const stripe_cfg_t *c)
{
    return (uint32_t) c->k + c->m <= STRIPE_MAX_MEMBERS;
}

static int block_ok(uint32_t bs)
{
    return bs >= STRIPE_MIN_BLOCK && bs <= STRIPE_MAX_BLOCK;
}

uint64_t stripe_work_bytes(const stripe_cfg_t *cfg, uint32_t block_size)
{
    if (!cfg || !cfg_fields_ok(cfg) || !cfg_fits(cfg) || !block_ok(block_size)) return 0;
    uint64_t piece = (uint64_t) cfg->piece_blocks * block_size;
    if (piece > STRIPE_MAX_PIECE) return 0;
    uint32_t n = (uint32_t) cfg->k + cfg->m;
    uint32_t hdr_blocks = (STRIPE_HDR_BYTES(n) + block_size - 1) / block_size;
    return (uint64_t) n * piece + (uint64_t) hdr_blocks * block_size;
}

static uint64_t slot_lba(const stripe_t *s, uint32_t slot)
{
    return (uint64_t) slot * s->slot_blocks;
}

/* ---- member I/O: a failed member is never touched again ---- */
static int mem_read(stripe_t *s, uint32_t i, uint64_t lba, uint32_t nblk, uint8_t *buf)
{
    if (s->failed[i]) return -1;
    hw_blk_t *b = s->mem[i];
    if (b->read(b, lba, nblk, buf) != 0) {
        s->failed[i] = 1;
        return -1;
    }
    return 0;
}

static int mem_write(stripe_t *s, uint32_t i, uint64_t lba, uint32_t nblk, const uint8_t *buf)
{
    if (s->failed[i]) return -1;
    hw_blk_t *b = s->mem[i];
    if (b->write(b, lba, nblk, buf) != 0) {
        s->failed[i] = 1;
        return -1;
    }
    return 0;
}

static uint32_t present_count(const stripe_t *s)
{
    uint32_t c = 0;
    for (uint32_t i = 0; i < s->n; i++) c += s->failed[i] ? 0u : 1u;
    return c;
}

uint32_t stripe_failed_count(const stripe_t *s)
{
    return s ? s->n - present_count(s) : 0;
}

/* ---- header ---- */
static void hdr_build(stripe_t *s, uint64_t gen, uint32_t lid, uint32_t len, uint32_t slot)
{
    uint8_t *h = s->hdrbuf;
    st_zero(h, s->hdr_blocks * s->block_size);
    st_copy(h, st_magic, 4);
    h[4] = STRIPE_VERSION;
    h[5] = s->cfg.k;
    h[6] = s->cfg.m;
    put64(h + 8, s->cfg.set_id);
    put64(h + 16, gen);
    put32(h + 24, lid);
    put32(h + 28, len);
    put32(h + 32, s->cfg.piece_blocks);
    put32(h + 36, slot);
    for (uint32_t i = 0; i < s->n; i++)
        st_copy(h + STRIPE_HDR_FIXED + i * STRIPE_HASH, s->hash[i], STRIPE_HASH);
    uint32_t body = s->hdr_bytes - STRIPE_HASH;
    sha256(h, body, h + body);
}

typedef struct {
    uint64_t gen;
    uint32_t lid, len;
    const uint8_t *hashes; /* n hashes inside hdrbuf */
} st_hdr_t;

/* Parse hdrbuf as the header of `slot`; 1 when it is a valid header of this set. */
static int hdr_parse(const stripe_t *s, uint32_t slot, st_hdr_t *out)
{
    const uint8_t *h = s->hdrbuf;
    if (!st_eq(h, st_magic, 4) || h[4] != STRIPE_VERSION || h[5] != s->cfg.k || h[6] != s->cfg.m ||
        h[7] != 0)
        return 0;
    if (get64(h + 8) != s->cfg.set_id || get32(h + 32) != s->cfg.piece_blocks ||
        get32(h + 36) != slot)
        return 0;
    uint32_t body = s->hdr_bytes - STRIPE_HASH;
    uint8_t d[STRIPE_HASH];
    sha256(h, body, d);
    if (!st_eq(d, h + body, STRIPE_HASH)) return 0;
    out->gen = get64(h + 16);
    out->lid = get32(h + 24);
    out->len = get32(h + 28);
    out->hashes = h + STRIPE_HDR_FIXED;
    if (out->gen == 0 || out->len > (uint32_t) s->cfg.k * s->piece_bytes) return 0;
    return 1;
}

/* Read member i's copy of slot's header and check it is the live header of
 * lid. With have_ref, it must also carry the hashes already in s->hash;
 * without, its hashes are loaded into s->hash. */
static int hdr_load(stripe_t *s, uint32_t i, uint32_t slot, uint32_t lid, int have_ref)
{
    st_hdr_t h;
    if (mem_read(s, i, slot_lba(s, slot), s->hdr_blocks, s->hdrbuf) != 0) return 0;
    if (!hdr_parse(s, slot, &h) || h.lid != lid || h.gen != s->lid_gen[lid] ||
        h.len != s->lid_len[lid])
        return 0;
    if (have_ref) {
        for (uint32_t p = 0; p < s->n; p++)
            if (!st_eq(h.hashes + p * STRIPE_HASH, s->hash[p], STRIPE_HASH)) return 0;
        return 1;
    }
    for (uint32_t p = 0; p < s->n; p++)
        st_copy(s->hash[p], h.hashes + p * STRIPE_HASH, STRIPE_HASH);
    return 1;
}

/* The live header of lid from the first present member outside `skip`. */
static int hdr_find(stripe_t *s, uint32_t slot, uint32_t lid, uint32_t skip)
{
    for (uint32_t i = 0; i < s->n; i++) {
        if (skip & (1u << i)) continue;
        if (hdr_load(s, i, slot, lid, 0)) return 1;
    }
    return 0;
}

/* ---- the code ---- */
static uint8_t *piece(const stripe_t *s, uint32_t i)
{
    return s->pieces + (uint64_t) i * s->piece_bytes;
}

/* dst ^= c * src over n bytes, in freight's field. */
static void st_axpy(uint8_t *dst, const uint8_t *src, uint8_t c, uint32_t n)
{
    if (c == 0) return;
    if (c == 1) {
        for (uint32_t b = 0; b < n; b++) dst[b] ^= src[b];
        return;
    }
    for (uint32_t b = 0; b < n; b++) dst[b] ^= freight_gf_mul(c, src[b]);
}

static void encode_parity(stripe_t *s, uint32_t r)
{
    const uint32_t k = s->cfg.k;
    uint8_t *dst = piece(s, r);
    st_zero(dst, s->piece_bytes);
    for (uint32_t j = 0; j < k; j++)
        st_axpy(dst, piece(s, j), freight_gen_coef((uint8_t) k, r, j), s->piece_bytes);
}

static uint32_t popcount(uint32_t v)
{
    uint32_t c = 0;
    for (; v; v &= v - 1) c++;
    return c;
}

/* Fill every piece not in `present` from the k first present ones: invert the
 * k x k generator rows of those pieces (Gauss-Jordan over GF(2^8)), rebuild
 * the missing data, then re-encode the missing parity. */
static int reconstruct(stripe_t *s, uint32_t present)
{
    const uint32_t k = s->cfg.k, n = s->n;
    uint32_t all = n == 32 ? 0xffffffffu : ((1u << n) - 1u);
    present &= all;
    if (popcount(present) < k) return STRIPE_ERR_TOO_FEW;
    uint32_t data = (1u << k) - 1u;
    if ((present & data) != data) {
        uint32_t idx[STRIPE_MAX_MEMBERS], a = 0;
        for (uint32_t i = 0; i < n && a < k; i++)
            if (present & (1u << i)) idx[a++] = i;
        for (uint32_t r = 0; r < k; r++) {
            for (uint32_t j = 0; j < k; j++) {
                s->mat[r][j] = freight_gen_coef((uint8_t) k, idx[r], j);
                s->mat[r][k + j] = (uint8_t) (r == j);
            }
        }
        const uint32_t w = 2 * k;
        for (uint32_t c = 0; c < k; c++) {
            uint32_t p = c;
            while (p < k && s->mat[p][c] == 0) p++;
            if (p == k) return STRIPE_ERR_SINGULAR;
            if (p != c) {
                for (uint32_t x = 0; x < w; x++) {
                    uint8_t t = s->mat[p][x];
                    s->mat[p][x] = s->mat[c][x];
                    s->mat[c][x] = t;
                }
            }
            uint8_t inv = freight_gf_inv(s->mat[c][c]);
            for (uint32_t x = 0; x < w; x++) s->mat[c][x] = freight_gf_mul(s->mat[c][x], inv);
            for (uint32_t r = 0; r < k; r++) {
                uint8_t f = s->mat[r][c];
                if (r == c || f == 0) continue;
                for (uint32_t x = 0; x < w; x++) s->mat[r][x] ^= freight_gf_mul(f, s->mat[c][x]);
            }
        }
        /* data_j = sum_a inv[j][a] * piece(idx[a]); every idx piece is present. */
        for (uint32_t j = 0; j < k; j++) {
            if (present & (1u << j)) continue;
            uint8_t *dst = piece(s, j);
            st_zero(dst, s->piece_bytes);
            for (uint32_t r = 0; r < k; r++)
                st_axpy(dst, piece(s, idx[r]), s->mat[j][k + r], s->piece_bytes);
        }
    }
    for (uint32_t r = k; r < n; r++)
        if (!(present & (1u << r))) encode_parity(s, r);
    return STRIPE_OK;
}

/* Read and verify every piece of slot from members outside `skip`. Returns
 * the mask of intact pieces; counts the rest in rep. */
static uint32_t read_pieces(stripe_t *s, uint32_t slot, uint32_t skip, stripe_report_t *rep,
                            uint32_t *corrupt_mask)
{
    uint32_t ok = 0, bad = 0;
    uint64_t lba = slot_lba(s, slot) + s->hdr_blocks;
    for (uint32_t i = 0; i < s->n; i++) {
        if (skip & (1u << i)) continue;
        if (mem_read(s, i, lba, s->cfg.piece_blocks, piece(s, i)) != 0) {
            rep->missing++;
            continue;
        }
        uint8_t d[STRIPE_HASH];
        sha256(piece(s, i), s->piece_bytes, d);
        if (st_eq(d, s->hash[i], STRIPE_HASH)) {
            ok |= 1u << i;
        } else {
            bad |= 1u << i;
            rep->corrupt++;
        }
    }
    if (corrupt_mask) *corrupt_mask = bad;
    return ok;
}

static void report_clear(stripe_report_t *r)
{
    r->slots = r->missing = r->corrupt = r->repaired = 0;
    r->hdr_bad = r->hdr_repaired = r->unrecoverable = 0;
}

static void tables_clear(stripe_t *s)
{
    for (uint32_t i = 0; i < STRIPE_MAX_SLOTS; i++) {
        s->lid_slot[i] = STRIPE_NONE;
        s->lid_gen[i] = 0;
        s->lid_len[i] = 0;
        s->slot_lid[i] = STRIPE_NONE;
    }
    s->next_gen = 1;
    s->cursor = 0;
}

/* ---- set up ---- */
int stripe_init(stripe_t *s, const stripe_cfg_t *cfg, hw_blk_t *const *members, uint32_t n,
                uint8_t *work, uint64_t work_len)
{
    if (!s || !cfg || !members || !work) return STRIPE_ERR_ARG;
    if (!cfg_fields_ok(cfg)) return STRIPE_ERR_ARG;
    if (!cfg_fits(cfg)) return STRIPE_ERR_GEOM;
    if (n != (uint32_t) cfg->k + cfg->m) return STRIPE_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) {
        const hw_blk_t *b = members[i];
        if (!b || !b->read || !b->write) return STRIPE_ERR_ARG;
        if (!block_ok(b->block_size) || b->block_size != members[0]->block_size)
            return STRIPE_ERR_GEOM;
    }
    const uint32_t bs = members[0]->block_size;
    uint64_t need = stripe_work_bytes(cfg, bs);
    if (need == 0) return STRIPE_ERR_GEOM;
    if (work_len < need) return STRIPE_ERR_SPACE;
    const uint32_t hdr_bytes = STRIPE_HDR_BYTES(n);
    const uint32_t hdr_blocks = (hdr_bytes + bs - 1) / bs;
    const uint32_t slot_blocks = hdr_blocks + cfg->piece_blocks;
    uint64_t fit = STRIPE_MAX_SLOTS;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t f = members[i]->blocks / slot_blocks;
        if (f < fit) fit = f;
    }
    uint32_t slots = cfg->slots ? cfg->slots : (uint32_t) fit;
    if (slots > fit || slots < 2) return STRIPE_ERR_GEOM;
    if (cfg->logical < 1 || cfg->logical >= slots) return STRIPE_ERR_GEOM;

    for (uint32_t i = 0; i < STRIPE_MAX_MEMBERS; i++) {
        s->mem[i] = i < n ? members[i] : 0;
        s->failed[i] = 0;
    }
    s->cfg = *cfg;
    s->n = n;
    s->block_size = bs;
    s->hdr_bytes = hdr_bytes;
    s->hdr_blocks = hdr_blocks;
    s->slot_blocks = slot_blocks;
    s->piece_bytes = cfg->piece_blocks * bs;
    s->slots = slots;
    s->mounted = 0;
    s->pieces = work;
    s->hdrbuf = work + (uint64_t) n * s->piece_bytes;
    tables_clear(s);
    return STRIPE_OK;
}

static int bound(const stripe_t *s)
{
    return s && s->n && s->pieces && s->hdrbuf;
}

int stripe_format(stripe_t *s)
{
    if (!bound(s)) return STRIPE_ERR_STATE;
    if (present_count(s) < s->cfg.k) return STRIPE_ERR_TOO_FEW;
    st_zero(s->hdrbuf, s->hdr_blocks * s->block_size);
    for (uint32_t slot = 0; slot < s->slots; slot++)
        for (uint32_t i = 0; i < s->n; i++)
            (void) mem_write(s, i, slot_lba(s, slot), s->hdr_blocks, s->hdrbuf);
    tables_clear(s);
    if (present_count(s) < s->cfg.k) {
        s->mounted = 0;
        return STRIPE_ERR_IO;
    }
    s->mounted = 1;
    return present_count(s) == s->n ? STRIPE_OK : STRIPE_DEGRADED;
}

int stripe_mount(stripe_t *s)
{
    if (!bound(s)) return STRIPE_ERR_STATE;
    tables_clear(s);
    s->mounted = 0;
    uint64_t top = 0;
    uint32_t top_slot = STRIPE_NONE;
    for (uint32_t slot = 0; slot < s->slots; slot++) {
        st_hdr_t best = {0, 0, 0, 0}, h;
        for (uint32_t i = 0; i < s->n; i++) {
            if (mem_read(s, i, slot_lba(s, slot), s->hdr_blocks, s->hdrbuf) != 0) continue;
            if (!hdr_parse(s, slot, &h) || h.lid >= s->cfg.logical) continue;
            if (h.gen > best.gen) best = h;
        }
        if (best.gen == 0) continue;
        if (best.gen > top) {
            top = best.gen;
            top_slot = slot;
        }
        uint32_t lid = best.lid;
        if (s->lid_slot[lid] != STRIPE_NONE && s->lid_gen[lid] >= best.gen) continue;
        if (s->lid_slot[lid] != STRIPE_NONE) s->slot_lid[s->lid_slot[lid]] = STRIPE_NONE;
        s->lid_slot[lid] = slot;
        s->lid_gen[lid] = best.gen;
        s->lid_len[lid] = best.len;
        s->slot_lid[slot] = lid;
    }
    if (present_count(s) < s->cfg.k) return STRIPE_ERR_TOO_FEW;
    s->next_gen = top + 1;
    s->cursor = top_slot == STRIPE_NONE ? 0 : (top_slot + 1) % s->slots;
    s->mounted = 1;
    return present_count(s) == s->n ? STRIPE_OK : STRIPE_DEGRADED;
}

/* ---- write ---- */
int stripe_write(stripe_t *s, uint32_t lid, const uint8_t *data, uint32_t len)
{
    /* 1. check */
    if (!bound(s) || !s->mounted) return STRIPE_ERR_STATE;
    if (lid >= s->cfg.logical || (len && !data)) return STRIPE_ERR_ARG;
    const uint32_t k = s->cfg.k, n = s->n, pb = s->piece_bytes;
    if (len > k * pb) return STRIPE_ERR_SPACE;
    if (present_count(s) < k) return STRIPE_ERR_TOO_FEW;

    /* 2. reserve: the next slot in the log no live stripe holds */
    uint32_t slot = STRIPE_NONE;
    for (uint32_t t = 0; t < s->slots && slot == STRIPE_NONE; t++) {
        uint32_t c = (s->cursor + t) % s->slots;
        if (s->slot_lid[c] == STRIPE_NONE) slot = c;
    }
    if (slot == STRIPE_NONE) return STRIPE_ERR_SPACE;
    const uint64_t gen = s->next_gen++;

    /* 3. compute: data pieces, parity, hashes */
    for (uint32_t b = 0; b < k * pb; b++) s->pieces[b] = b < len ? data[b] : 0;
    for (uint32_t r = k; r < n; r++) encode_parity(s, r);
    for (uint32_t i = 0; i < n; i++) sha256(piece(s, i), pb, s->hash[i]);

    /* 4. write: drop the slot's old headers, then every piece */
    st_zero(s->hdrbuf, s->hdr_blocks * s->block_size);
    for (uint32_t i = 0; i < n; i++)
        (void) mem_write(s, i, slot_lba(s, slot), s->hdr_blocks, s->hdrbuf);
    uint32_t landed = 0;
    for (uint32_t i = 0; i < n; i++)
        if (mem_write(s, i, slot_lba(s, slot) + s->hdr_blocks, s->cfg.piece_blocks, piece(s, i)) ==
            0)
            landed |= 1u << i;
    if (popcount(landed) < k) return STRIPE_ERR_IO;

    /* 5. commit: the header, last, on every member that holds its piece */
    hdr_build(s, gen, lid, len, slot);
    uint32_t hdrs = 0;
    for (uint32_t i = 0; i < n; i++)
        if ((landed & (1u << i)) &&
            mem_write(s, i, slot_lba(s, slot), s->hdr_blocks, s->hdrbuf) == 0)
            hdrs++;
    if (hdrs == 0) return STRIPE_ERR_IO;

    uint32_t old = s->lid_slot[lid];
    if (old != STRIPE_NONE) s->slot_lid[old] = STRIPE_NONE;
    s->lid_slot[lid] = slot;
    s->lid_gen[lid] = gen;
    s->lid_len[lid] = len;
    s->slot_lid[slot] = lid;
    s->cursor = (slot + 1) % s->slots;
    return (popcount(landed) == n && hdrs == n) ? STRIPE_OK : STRIPE_DEGRADED;
}

/* ---- read ---- */
int stripe_read(stripe_t *s, uint32_t lid, uint8_t *out, uint32_t cap, uint32_t *len,
                stripe_report_t *rep)
{
    stripe_report_t local;
    if (!rep) rep = &local;
    report_clear(rep);
    if (!bound(s) || !s->mounted) return STRIPE_ERR_STATE;
    if (lid >= s->cfg.logical || !len) return STRIPE_ERR_ARG;
    uint32_t slot = s->lid_slot[lid];
    if (slot == STRIPE_NONE) return STRIPE_ERR_NOENT;
    uint32_t want = s->lid_len[lid];
    if (want > cap || (want && !out)) return STRIPE_ERR_SPACE;
    rep->slots = 1;

    if (!hdr_find(s, slot, lid, 0)) {
        rep->unrecoverable = 1;
        return STRIPE_ERR_TOO_FEW;
    }
    uint32_t ok = read_pieces(s, slot, 0, rep, 0);
    if (popcount(ok) < s->cfg.k) {
        rep->unrecoverable = 1;
        return STRIPE_ERR_TOO_FEW;
    }
    int rc = reconstruct(s, ok);
    if (rc != STRIPE_OK) return rc;
    if (want) st_copy(out, s->pieces, want);
    *len = want;
    return (rep->missing || rep->corrupt) ? STRIPE_DEGRADED : STRIPE_OK;
}

/* ---- scrub ---- */
int stripe_scrub(stripe_t *s, stripe_report_t *rep)
{
    stripe_report_t local;
    if (!rep) rep = &local;
    report_clear(rep);
    if (!bound(s) || !s->mounted) return STRIPE_ERR_STATE;
    const uint32_t n = s->n;
    for (uint32_t slot = 0; slot < s->slots; slot++) {
        uint32_t lid = s->slot_lid[slot];
        if (lid == STRIPE_NONE) continue;
        rep->slots++;
        /* every present member's header copy, against the first valid one */
        uint32_t hdr_ok = 0;
        int have = 0;
        for (uint32_t i = 0; i < n; i++) {
            if (s->failed[i]) continue;
            if (hdr_load(s, i, slot, lid, have)) {
                hdr_ok |= 1u << i;
                have = 1;
            } else if (!s->failed[i]) {
                rep->hdr_bad++;
            }
        }
        if (!have) {
            rep->unrecoverable++;
            continue;
        }
        uint32_t bad = 0;
        uint32_t ok = read_pieces(s, slot, 0, rep, &bad);
        if (reconstruct(s, ok) != STRIPE_OK) {
            rep->unrecoverable++;
            continue;
        }
        uint64_t lba = slot_lba(s, slot);
        for (uint32_t i = 0; i < n; i++)
            if ((bad & (1u << i)) &&
                mem_write(s, i, lba + s->hdr_blocks, s->cfg.piece_blocks, piece(s, i)) == 0)
                rep->repaired++;
        hdr_build(s, s->lid_gen[lid], lid, s->lid_len[lid], slot);
        for (uint32_t i = 0; i < n; i++)
            if (!(hdr_ok & (1u << i)) && !s->failed[i] &&
                mem_write(s, i, lba, s->hdr_blocks, s->hdrbuf) == 0)
                rep->hdr_repaired++;
    }
    if (rep->unrecoverable) return STRIPE_ERR_TOO_FEW;
    if (rep->missing || rep->corrupt || rep->hdr_bad || stripe_failed_count(s))
        return STRIPE_DEGRADED;
    return STRIPE_OK;
}

/* ---- rebuild ---- */
int stripe_rebuild(stripe_t *s, uint32_t member, hw_blk_t *replacement, stripe_report_t *rep)
{
    stripe_report_t local;
    if (!rep) rep = &local;
    report_clear(rep);
    if (!bound(s) || !s->mounted) return STRIPE_ERR_STATE;
    if (member >= s->n || !replacement || !replacement->read || !replacement->write)
        return STRIPE_ERR_ARG;
    if (replacement->block_size != s->block_size ||
        replacement->blocks < (uint64_t) s->slots * s->slot_blocks)
        return STRIPE_ERR_GEOM;
    s->mem[member] = replacement;
    s->failed[member] = 0;
    const uint32_t bit = 1u << member;
    for (uint32_t slot = 0; slot < s->slots; slot++) {
        uint64_t lba = slot_lba(s, slot);
        uint32_t lid = s->slot_lid[slot];
        if (lid == STRIPE_NONE) {
            st_zero(s->hdrbuf, s->hdr_blocks * s->block_size);
            if (mem_write(s, member, lba, s->hdr_blocks, s->hdrbuf) != 0) return STRIPE_ERR_IO;
            continue;
        }
        rep->slots++;
        if (!hdr_find(s, slot, lid, bit)) {
            rep->unrecoverable++;
            continue;
        }
        uint32_t ok = read_pieces(s, slot, bit, rep, 0);
        if (reconstruct(s, ok) != STRIPE_OK) {
            rep->unrecoverable++;
            continue;
        }
        if (mem_write(s, member, lba + s->hdr_blocks, s->cfg.piece_blocks, piece(s, member)) != 0)
            return STRIPE_ERR_IO;
        rep->repaired++;
        hdr_build(s, s->lid_gen[lid], lid, s->lid_len[lid], slot);
        if (mem_write(s, member, lba, s->hdr_blocks, s->hdrbuf) != 0) return STRIPE_ERR_IO;
        rep->hdr_repaired++;
    }
    return rep->unrecoverable ? STRIPE_ERR_TOO_FEW : STRIPE_OK;
}

/* ---- queries ---- */
uint32_t stripe_slot_of(const stripe_t *s, uint32_t lid)
{
    if (!s || !s->mounted || lid >= s->cfg.logical) return STRIPE_NONE;
    return s->lid_slot[lid];
}

uint64_t stripe_gen_of(const stripe_t *s, uint32_t lid)
{
    if (!s || !s->mounted || lid >= s->cfg.logical || s->lid_slot[lid] == STRIPE_NONE) return 0;
    return s->lid_gen[lid];
}

/* ---- profiles ---- */
uint32_t stripe_profile_count(void)
{
    return ST_NPROFILES;
}

const stripe_profile_t *stripe_profile(uint32_t i)
{
    return i < ST_NPROFILES ? &st_profiles[i] : 0;
}

const stripe_profile_t *stripe_profile_find(const char *name)
{
    if (!name) return 0;
    for (uint32_t i = 0; i < ST_NPROFILES; i++) {
        const char *a = st_profiles[i].name, *b = name;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == 0 && *b == 0) return &st_profiles[i];
    }
    return 0;
}

uint32_t stripe_profile_node(const stripe_profile_t *p, uint32_t member)
{
    if (!p || p->nodes == 0) return STRIPE_NONE;
    uint32_t n = (uint32_t) p->k + p->m;
    if (member >= n) return STRIPE_NONE;
    uint32_t per = (n + p->nodes - 1u) / p->nodes;
    return member / per;
}

int stripe_profile_cfg(const stripe_profile_t *p, uint64_t set_id, uint32_t piece_blocks,
                       uint32_t slots, uint32_t logical, stripe_cfg_t *cfg)
{
    if (!p || !cfg) return STRIPE_ERR_ARG;
    cfg->set_id = set_id;
    cfg->k = p->k;
    cfg->m = p->m;
    cfg->piece_blocks = piece_blocks;
    cfg->slots = slots;
    cfg->logical = logical;
    return STRIPE_OK;
}
