/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_stripe.c — the erasure-coded storage service over in-memory members
 * that can cut power after N blocks, fail one member's writes, flip a byte,
 * or vanish. */
#include "stripe.h"
#include "freight.h"
#include "sha256.h"
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(c)) {                                                                                \
            g_fail++;                                                                              \
            if (g_fail < 40) printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                    \
        }                                                                                          \
    } while (0)

/* ---- the fake member ---- */
#define POOL_BYTES (96u * 1024u)
static uint8_t g_pool[STRIPE_MAX_MEMBERS + 1][POOL_BYTES];
static uint8_t g_snap[STRIPE_MAX_MEMBERS + 1][POOL_BYTES];
static int64_t g_power = -1; /* blocks the whole set may still write; -1 = no limit */

typedef struct {
    hw_blk_t blk;
    uint8_t *mem;
    int vanished;
    int64_t wbudget; /* blocks this member may still write; -1 = no limit */
    uint64_t wblocks;
} fake_t;

static fake_t g_fake[STRIPE_MAX_MEMBERS + 1];
static hw_blk_t *g_mem[STRIPE_MAX_MEMBERS + 1];

static int fake_read(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    fake_t *f = (fake_t *) b->drv;
    if (f->vanished || lba + n > b->blocks) return -1;
    memcpy(buf, f->mem + lba * b->block_size, (size_t) n * b->block_size);
    return 0;
}

static int fake_write(hw_blk_t *b, uint64_t lba, uint32_t n, const void *buf)
{
    fake_t *f = (fake_t *) b->drv;
    if (f->vanished || lba + n > b->blocks) return -1;
    uint32_t allow = n;
    if (g_power >= 0 && (int64_t) allow > g_power) allow = (uint32_t) g_power;
    if (f->wbudget >= 0 && (int64_t) allow > f->wbudget) allow = (uint32_t) f->wbudget;
    memcpy(f->mem + lba * b->block_size, buf, (size_t) allow * b->block_size);
    if (g_power >= 0) g_power -= allow;
    if (f->wbudget >= 0) f->wbudget -= allow;
    f->wblocks += allow;
    return allow == n ? 0 : -1;
}

static void fake_setup(uint32_t i, uint32_t bs, uint64_t blocks, uint8_t fill)
{
    fake_t *f = &g_fake[i];
    memset(f, 0, sizeof *f);
    snprintf(f->blk.name, sizeof f->blk.name, "fake%u", i);
    f->blk.block_size = bs;
    f->blk.blocks = blocks;
    f->blk.drv = f;
    f->blk.read = fake_read;
    f->blk.write = fake_write;
    f->mem = g_pool[i];
    f->wbudget = -1;
    memset(g_pool[i], fill, POOL_BYTES);
    g_mem[i] = &f->blk;
}

static uint8_t g_work[1u << 20];
static stripe_t g_s, g_s2;

static void lcg_fill(uint8_t *b, uint32_t n, uint32_t seed)
{
    uint32_t x = seed * 2654435761u + 12345u;
    for (uint32_t i = 0; i < n; i++) {
        x = x * 1103515245u + 12345u;
        b[i] = (uint8_t) (x >> 16);
    }
}

static uint32_t g_rng = 1;
static uint32_t rnd(void)
{
    g_rng = g_rng * 1103515245u + 12345u;
    return g_rng >> 8;
}

static uint32_t pop(uint32_t v)
{
    uint32_t c = 0;
    for (; v; v &= v - 1) c++;
    return c;
}

/* A fresh set: n fakes of `blocks` blocks, initialised and formatted. */
static int set_up(stripe_t *s, uint8_t k, uint8_t m, uint32_t bs, uint64_t blocks,
                  uint32_t piece_blocks, uint32_t slots, uint32_t logical, uint64_t set_id)
{
    uint32_t n = (uint32_t) k + m;
    for (uint32_t i = 0; i < n; i++) fake_setup(i, bs, blocks, 0);
    stripe_cfg_t c = {set_id, k, m, piece_blocks, slots, logical};
    int rc = stripe_init(s, &c, g_mem, n, g_work, sizeof g_work);
    if (rc) return rc;
    return stripe_format(s);
}

/* Re-open the same members in another stripe_t, as after a reboot. */
static int remount(stripe_t *s, const stripe_cfg_t *c)
{
    int rc = stripe_init(s, c, g_mem, (uint32_t) c->k + c->m, g_work, sizeof g_work);
    if (rc) return rc;
    return stripe_mount(s);
}

static uint8_t *piece_on_disk(const stripe_t *s, uint32_t member, uint32_t slot)
{
    return g_fake[member].mem + ((uint64_t) slot * s->slot_blocks + s->hdr_blocks) * s->block_size;
}

static uint8_t *hdr_on_disk(const stripe_t *s, uint32_t member, uint32_t slot)
{
    return g_fake[member].mem + (uint64_t) slot * s->slot_blocks * s->block_size;
}

static uint8_t g_data[64][32 * 1024];
static uint32_t g_len[64];
static uint8_t g_out[32 * 1024 + 16];

static void write_all(stripe_t *s, uint32_t seed)
{
    uint32_t cap = (uint32_t) s->cfg.k * s->piece_bytes;
    for (uint32_t lid = 0; lid < s->cfg.logical; lid++) {
        uint32_t lens[5] = {cap, 1, cap - 1, cap / 2 + 3, 0};
        g_len[lid] = lens[lid % 5];
        lcg_fill(g_data[lid], cap, seed + lid);
        CHECK(stripe_write(s, lid, g_data[lid], g_len[lid]) == STRIPE_OK);
    }
}

static int read_matches(stripe_t *s, uint32_t lid, stripe_report_t *rep)
{
    uint32_t len = 0xdeadu;
    memset(g_out, 0xee, sizeof g_out);
    int rc = stripe_read(s, lid, g_out, sizeof g_out, &len, rep);
    if (rc < 0) return rc;
    if (len != g_len[lid] || memcmp(g_out, g_data[lid], len) != 0) return -100;
    return rc;
}

/* ---- profiles ---- */
static void test_profiles(void)
{
    static const struct {
        const char *name;
        uint8_t k, m, nodes, stripes;
    } want[] = {{"hearth", 4, 2, 1, 1},    {"homestead", 16, 4, 1, 1},   {"guildhall", 16, 4, 1, 2},
                {"strongbox", 8, 4, 1, 1}, {"count_house", 16, 8, 3, 1}, {"hoard", 12, 8, 4, 1}};
    CHECK(stripe_profile_count() == 6);
    CHECK(stripe_profile(6) == NULL);
    CHECK(stripe_profile_find(NULL) == NULL);
    CHECK(stripe_profile_find("") == NULL);
    CHECK(stripe_profile_find("hoar") == NULL);
    CHECK(stripe_profile_find("hoards") == NULL);
    for (uint32_t i = 0; i < 6; i++) {
        const stripe_profile_t *p = stripe_profile(i);
        CHECK(p != NULL);
        CHECK(strcmp(p->name, want[i].name) == 0);
        CHECK(p->k == want[i].k && p->m == want[i].m);
        CHECK(p->nodes == want[i].nodes && p->stripes == want[i].stripes);
        CHECK(stripe_profile_find(want[i].name) == p);
        CHECK((uint32_t) p->k + p->m <= STRIPE_MAX_MEMBERS);
        uint32_t n = (uint32_t) p->k + p->m, per_node[8] = {0};
        for (uint32_t mbr = 0; mbr < n; mbr++) {
            uint32_t node = stripe_profile_node(p, mbr);
            CHECK(node < p->nodes);
            if (node < 8) per_node[node]++;
            if (mbr) CHECK(node >= stripe_profile_node(p, mbr - 1));
        }
        CHECK(stripe_profile_node(p, n) == STRIPE_NONE);
        /* losing one whole server never loses more than m pieces */
        for (uint32_t node = 0; node < p->nodes; node++) {
            CHECK(per_node[node] >= 1);
            if (p->nodes > 1) CHECK(per_node[node] <= p->m);
        }
        stripe_cfg_t c;
        CHECK(stripe_profile_cfg(p, 77 + i, 2, 9, 5, &c) == STRIPE_OK);
        CHECK(c.set_id == 77 + i && c.k == p->k && c.m == p->m);
        CHECK(c.piece_blocks == 2 && c.slots == 9 && c.logical == 5);
        CHECK(stripe_work_bytes(&c, 512) != 0);
    }
    /* Count House: 8 per server, so one server is exactly m. Hoard: 5 per
     * server, so one server and 3 more drives is exactly m. */
    const stripe_profile_t *ch = stripe_profile_find("count_house");
    const stripe_profile_t *ho = stripe_profile_find("hoard");
    CHECK(stripe_profile_node(ch, 7) == 0 && stripe_profile_node(ch, 8) == 1);
    CHECK(stripe_profile_node(ch, 23) == 2);
    CHECK(stripe_profile_node(ho, 4) == 0 && stripe_profile_node(ho, 5) == 1);
    CHECK(stripe_profile_node(ho, 19) == 3);
    CHECK(stripe_profile_node(stripe_profile_find("hearth"), 5) == 0);
    CHECK(stripe_profile_node(NULL, 0) == STRIPE_NONE);
    stripe_cfg_t c;
    CHECK(stripe_profile_cfg(NULL, 1, 1, 1, 1, &c) == STRIPE_ERR_ARG);
    CHECK(stripe_profile_cfg(ch, 1, 1, 1, 1, NULL) == STRIPE_ERR_ARG);
}

/* ---- bounds ---- */
static void test_bounds(void)
{
    stripe_cfg_t c = {1, 4, 2, 1, 0, 3};
    /* work bytes: n pieces plus the header blocks */
    CHECK(stripe_work_bytes(&c, 512) == 6u * 512u + 512u);
    CHECK(stripe_work_bytes(&c, 64) == 6u * 64u + 5u * 64u); /* 264-byte header */
    CHECK(stripe_work_bytes(NULL, 512) == 0);
    CHECK(stripe_work_bytes(&c, 8) == 0);
    CHECK(stripe_work_bytes(&c, 65537) == 0);
    stripe_cfg_t big = {1, 24, 9, 1, 0, 3};
    CHECK(stripe_work_bytes(&big, 512) == 0);
    stripe_cfg_t huge = {1, 4, 2, 40000, 0, 3};
    CHECK(stripe_work_bytes(&huge, 512) == 0);
    CHECK(STRIPE_HDR_BYTES(6) == 264u && STRIPE_HDR_BYTES(32) == 1096u);

    for (uint32_t i = 0; i < STRIPE_MAX_MEMBERS + 1; i++) fake_setup(i, 512, 64, 0);
    CHECK(stripe_init(NULL, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    CHECK(stripe_init(&g_s, NULL, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    CHECK(stripe_init(&g_s, &c, NULL, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    CHECK(stripe_init(&g_s, &c, g_mem, 6, NULL, sizeof g_work) == STRIPE_ERR_ARG);
    CHECK(stripe_init(&g_s, &c, g_mem, 5, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    CHECK(stripe_init(&g_s, &c, g_mem, 7, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    stripe_cfg_t z = c;
    z.k = 0;
    CHECK(stripe_init(&g_s, &z, g_mem, 2, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    z = c;
    z.m = 0;
    CHECK(stripe_init(&g_s, &z, g_mem, 4, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    z = c;
    z.piece_blocks = 0;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    /* k + m over capacity */
    CHECK(stripe_init(&g_s, &big, g_mem, 33, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    stripe_cfg_t edge = {1, 16, 16, 1, 0, 3};
    CHECK(stripe_init(&g_s, &edge, g_mem, 32, g_work, sizeof g_work) == STRIPE_OK);
    /* workspace short by one byte */
    uint64_t need = stripe_work_bytes(&c, 512);
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, need - 1) == STRIPE_ERR_SPACE);
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, need) == STRIPE_OK);
    CHECK(g_s.slots == 64 / 2 && g_s.hdr_blocks == 1 && g_s.slot_blocks == 2);
    /* a member with no write, or no read */
    g_fake[2].blk.write = NULL;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    g_fake[2].blk.write = fake_write;
    g_fake[3].blk.read = NULL;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    g_fake[3].blk.read = fake_read;
    hw_blk_t *saved = g_mem[1];
    g_mem[1] = NULL;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_ARG);
    g_mem[1] = saved;
    /* mismatched and out-of-range block sizes */
    g_fake[4].blk.block_size = 4096;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    g_fake[4].blk.block_size = 512;
    for (uint32_t i = 0; i < 6; i++) g_fake[i].blk.block_size = 8;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    for (uint32_t i = 0; i < 6; i++) g_fake[i].blk.block_size = 512;
    /* a short member: the set shrinks to it, and refuses when too short */
    g_fake[5].blk.blocks = 10;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_OK);
    CHECK(g_s.slots == 5);
    g_fake[5].blk.blocks = 3;
    CHECK(stripe_init(&g_s, &c, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    g_fake[5].blk.blocks = 64;
    /* slots and logical counts */
    z = c;
    z.slots = 33;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    z.slots = 32;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_OK);
    z.slots = 4;
    z.logical = 4;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    z.logical = 0;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);
    z.logical = 3;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_OK);
    z.slots = 1;
    z.logical = 1;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_ERR_GEOM);

    /* before mount, nothing reads or writes */
    z.slots = 4;
    z.logical = 3;
    CHECK(stripe_init(&g_s, &z, g_mem, 6, g_work, sizeof g_work) == STRIPE_OK);
    uint32_t len;
    uint8_t b[8] = {0};
    stripe_report_t r;
    CHECK(stripe_write(&g_s, 0, b, 8) == STRIPE_ERR_STATE);
    CHECK(stripe_read(&g_s, 0, g_out, sizeof g_out, &len, &r) == STRIPE_ERR_STATE);
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_ERR_STATE);
    CHECK(stripe_rebuild(&g_s, 0, g_mem[0], &r) == STRIPE_ERR_STATE);
    CHECK(stripe_slot_of(&g_s, 0) == STRIPE_NONE && stripe_gen_of(&g_s, 0) == 0);
    CHECK(stripe_write(NULL, 0, b, 8) == STRIPE_ERR_STATE);
    CHECK(stripe_mount(NULL) == STRIPE_ERR_STATE && stripe_format(NULL) == STRIPE_ERR_STATE);
    CHECK(stripe_failed_count(NULL) == 0);

    /* after format: argument checks */
    CHECK(stripe_format(&g_s) == STRIPE_OK);
    CHECK(stripe_write(&g_s, 3, b, 8) == STRIPE_ERR_ARG);
    CHECK(stripe_write(&g_s, 0, NULL, 8) == STRIPE_ERR_ARG);
    CHECK(stripe_write(&g_s, 0, g_data[0], 4 * 512 + 1) == STRIPE_ERR_SPACE);
    CHECK(stripe_read(&g_s, 0, g_out, sizeof g_out, &len, &r) == STRIPE_ERR_NOENT);
    CHECK(stripe_read(&g_s, 3, g_out, sizeof g_out, &len, &r) == STRIPE_ERR_ARG);
    CHECK(stripe_read(&g_s, 0, g_out, sizeof g_out, NULL, &r) == STRIPE_ERR_ARG);
    CHECK(stripe_write(&g_s, 0, b, 8) == STRIPE_OK);
    CHECK(stripe_read(&g_s, 0, g_out, 7, &len, &r) == STRIPE_ERR_SPACE);
    CHECK(stripe_read(&g_s, 0, NULL, 8, &len, &r) == STRIPE_ERR_SPACE);
    CHECK(stripe_read(&g_s, 0, g_out, 8, &len, NULL) == STRIPE_OK && len == 8);
    CHECK(stripe_write(&g_s, 1, NULL, 0) == STRIPE_OK);
    CHECK(stripe_read(&g_s, 1, NULL, 0, &len, &r) == STRIPE_OK && len == 0);
    CHECK(stripe_rebuild(&g_s, 6, g_mem[6], &r) == STRIPE_ERR_ARG);
    CHECK(stripe_rebuild(&g_s, 0, NULL, &r) == STRIPE_ERR_ARG);
    CHECK(stripe_slot_of(&g_s, 3) == STRIPE_NONE && stripe_gen_of(&g_s, 2) == 0);
    CHECK(stripe_slot_of(NULL, 0) == STRIPE_NONE && stripe_gen_of(NULL, 0) == 0);
}

/* ---- round trip on every server code ---- */
static void test_round_trip(void)
{
    for (uint32_t pi = 0; pi < stripe_profile_count(); pi++) {
        const stripe_profile_t *p = stripe_profile(pi);
        stripe_cfg_t c;
        stripe_profile_cfg(p, 0x5eed0000u + pi, 1, 8, 5, &c);
        CHECK(set_up(&g_s, p->k, p->m, 512, 8 * 4, 1, 8, 5, c.set_id) == STRIPE_OK);
        CHECK(g_s.n == (uint32_t) p->k + p->m);
        write_all(&g_s, 100 + pi);
        stripe_report_t r;
        for (uint32_t lid = 0; lid < 5; lid++) {
            CHECK(read_matches(&g_s, lid, &r) == STRIPE_OK);
            CHECK(r.slots == 1 && r.missing == 0 && r.corrupt == 0);
            /* data pieces sit on the members verbatim */
            uint32_t slot = stripe_slot_of(&g_s, lid);
            CHECK(slot < g_s.slots);
            for (uint32_t j = 0; j < p->k; j++) {
                uint32_t off = j * g_s.piece_bytes;
                uint32_t n =
                    off >= g_len[lid]
                        ? 0
                        : (g_len[lid] - off < g_s.piece_bytes ? g_len[lid] - off : g_s.piece_bytes);
                CHECK(memcmp(piece_on_disk(&g_s, j, slot), g_data[lid] + off, n) == 0);
            }
        }
        /* after a reboot, the same contents and generations */
        uint64_t gens[5];
        for (uint32_t lid = 0; lid < 5; lid++) gens[lid] = stripe_gen_of(&g_s, lid);
        CHECK(remount(&g_s2, &c) == STRIPE_OK);
        for (uint32_t lid = 0; lid < 5; lid++) {
            CHECK(read_matches(&g_s2, lid, &r) == STRIPE_OK);
            CHECK(stripe_gen_of(&g_s2, lid) == gens[lid]);
            CHECK(stripe_slot_of(&g_s2, lid) == stripe_slot_of(&g_s, lid));
        }
        CHECK(stripe_scrub(&g_s2, &r) == STRIPE_OK);
        CHECK(r.slots == 5 && r.corrupt == 0 && r.hdr_bad == 0 && r.repaired == 0);
        /* another set's id sees nothing */
        stripe_cfg_t other = c;
        other.set_id ^= 1;
        CHECK(remount(&g_s2, &other) == STRIPE_OK);
        for (uint32_t lid = 0; lid < 5; lid++)
            CHECK(read_matches(&g_s2, lid, &r) == STRIPE_ERR_NOENT);
    }
}

/* ---- the code is freight's ---- */
static void test_matches_freight(void)
{
    static const uint8_t ks[3][2] = {{4, 2}, {8, 4}, {16, 8}};
    for (uint32_t t = 0; t < 3; t++) {
        uint8_t k = ks[t][0], m = ks[t][1];
        CHECK(set_up(&g_s, k, m, 512, 32, 1, 8, 3, 9) == STRIPE_OK);
        uint32_t cap = (uint32_t) k * 512u;
        lcg_fill(g_data[0], cap, 900 + t);
        CHECK(stripe_write(&g_s, 0, g_data[0], cap) == STRIPE_OK);
        uint32_t slot = stripe_slot_of(&g_s, 0);
        /* every parity byte: sum_j freight_gen_coef(k, r, j) * data_j */
        uint32_t bad = 0;
        for (uint32_t r = k; r < (uint32_t) k + m; r++) {
            const uint8_t *pp = piece_on_disk(&g_s, r, slot);
            for (uint32_t b = 0; b < 512; b++) {
                uint8_t v = 0;
                for (uint32_t j = 0; j < k; j++)
                    v ^= freight_gf_mul(freight_gen_coef(k, r, j), g_data[0][j * 512 + b]);
                bad += v != pp[b];
            }
        }
        CHECK(bad == 0);
        /* the first 20 bytes of each piece are one freight row */
        static uint8_t payload[FREIGHT_MAX_PAYLOAD], packets[FREIGHT_BYTES];
        for (uint32_t j = 0; j < k; j++) memcpy(payload + j * 20, g_data[0] + j * 512, 20);
        freight_header_t h;
        CHECK(freight_header_init(&h, 1, k, 0, 0, payload, (uint32_t) k * 20) == FREIGHT_OK);
        CHECK(freight_encode(&h, payload, packets) == FREIGHT_OK);
        for (uint32_t r = 0; r < (uint32_t) k + m; r++)
            CHECK(memcmp(packets + r * FREIGHT_PACKET_BYTES + 1, piece_on_disk(&g_s, r, slot),
                         20) == 0);
        /* each piece hash in the header copy on every member */
        for (uint32_t i = 0; i < (uint32_t) k + m; i++) {
            uint8_t d[32];
            sha256(piece_on_disk(&g_s, i, slot), 512, d);
            for (uint32_t mbr = 0; mbr < (uint32_t) k + m; mbr++)
                CHECK(memcmp(hdr_on_disk(&g_s, mbr, slot) + STRIPE_HDR_FIXED + i * 32, d, 32) == 0);
        }
        CHECK(memcmp(hdr_on_disk(&g_s, 0, slot), "ZXST", 4) == 0);
    }
}

/* Flip one byte of the piece of `slot` on every member in mask. */
static void corrupt_mask(const stripe_t *s, uint32_t slot, uint32_t mask, uint32_t at)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (mask & (1u << i)) piece_on_disk(s, i, slot)[at % s->piece_bytes] ^= 0x5a;
}

/* ---- every pattern of up to m erasures ---- */
static void test_erasures_by_corruption(uint8_t k, uint8_t m, uint32_t full_upto, uint32_t samples)
{
    uint32_t n = (uint32_t) k + m;
    CHECK(set_up(&g_s, k, m, 512, 32, 1, 8, 3, 0xe0 + k) == STRIPE_OK);
    write_all(&g_s, 300 + k);
    uint32_t slot = stripe_slot_of(&g_s, 0);
    stripe_report_t r;
    uint64_t tried = 0;
    uint32_t total = n == 32 ? 0xffffffffu : (1u << n) - 1u;
    if (n <= full_upto) {
        for (uint32_t mask = 0; mask <= total; mask++) {
            uint32_t e = pop(mask);
            if (e > (uint32_t) m + 1) continue;
            corrupt_mask(&g_s, slot, mask, mask * 7u);
            int rc = read_matches(&g_s, 0, &r);
            if (e <= m) {
                CHECK(rc == (e ? STRIPE_DEGRADED : STRIPE_OK));
                CHECK(r.corrupt == e && r.missing == 0);
            } else {
                CHECK(rc == STRIPE_ERR_TOO_FEW);
                CHECK(r.unrecoverable == 1);
            }
            corrupt_mask(&g_s, slot, mask, mask * 7u);
            tried++;
            if (mask == total) break;
        }
        CHECK(tried > 0);
    }
    /* random patterns of 1..m and of m + 1 */
    for (uint32_t t = 0; t < samples; t++) {
        uint32_t want = (t & 1) ? (uint32_t) m + 1 : 1 + rnd() % m, mask = 0;
        while (pop(mask) < want) mask |= 1u << (rnd() % n);
        uint32_t off = rnd();
        corrupt_mask(&g_s, slot, mask, off);
        int rc = read_matches(&g_s, 0, &r);
        CHECK(rc == (want <= m ? STRIPE_DEGRADED : STRIPE_ERR_TOO_FEW));
        CHECK(r.corrupt == want);
        corrupt_mask(&g_s, slot, mask, off);
        CHECK(read_matches(&g_s, 0, &r) == STRIPE_OK);
    }
}

static void vanish(uint32_t n, uint32_t mask)
{
    for (uint32_t i = 0; i < n; i++) g_fake[i].vanished = (mask >> i) & 1u;
}

/* ---- every pattern of up to m lost members, found at mount ---- */
static void test_erasures_by_vanishing(uint8_t k, uint8_t m)
{
    uint32_t n = (uint32_t) k + m;
    stripe_cfg_t c = {0xa0 + k, k, m, 1, 8, 3};
    CHECK(set_up(&g_s, k, m, 512, 32, 1, 8, 3, c.set_id) == STRIPE_OK);
    write_all(&g_s, 500 + k);
    stripe_report_t r;
    for (uint32_t mask = 0; mask < (1u << n); mask++) {
        uint32_t e = pop(mask);
        if (e > (uint32_t) m + 1) continue;
        vanish(n, mask);
        int rc = remount(&g_s2, &c);
        if (e > m) {
            CHECK(rc == STRIPE_ERR_TOO_FEW);
            CHECK(stripe_read(&g_s2, 0, g_out, sizeof g_out, &e, &r) == STRIPE_ERR_STATE);
            continue;
        }
        CHECK(rc == (e ? STRIPE_DEGRADED : STRIPE_OK));
        CHECK(stripe_failed_count(&g_s2) == e);
        for (uint32_t lid = 0; lid < 3; lid++) {
            CHECK(read_matches(&g_s2, lid, &r) == (e ? STRIPE_DEGRADED : STRIPE_OK));
            CHECK(r.missing == e && r.corrupt == 0);
        }
        /* a degraded set still takes writes, and a whole set reads them */
        if (e == m && (mask & 1u)) {
            g_len[1] = (uint32_t) k * 512u; /* every piece differs from the old one */
            lcg_fill(g_data[1], g_len[1], mask);
            CHECK(stripe_write(&g_s2, 1, g_data[1], g_len[1]) == STRIPE_DEGRADED);
            CHECK(read_matches(&g_s2, 1, &r) == STRIPE_DEGRADED);
            vanish(n, 0);
            CHECK(remount(&g_s2, &c) == STRIPE_OK);
            /* the members that were gone hold no piece of it yet */
            CHECK(read_matches(&g_s2, 1, &r) == STRIPE_DEGRADED && r.corrupt == e);
            CHECK(stripe_scrub(&g_s2, &r) == STRIPE_DEGRADED);
            CHECK(r.repaired == e && r.hdr_repaired == e);
            CHECK(stripe_scrub(&g_s2, &r) == STRIPE_OK);
            CHECK(read_matches(&g_s2, 1, &r) == STRIPE_OK);
        }
    }
    vanish(n, 0);
}

/* ---- whole servers: Count House and Hoard ---- */
static void test_node_loss(void)
{
    const char *names[2] = {"count_house", "hoard"};
    for (uint32_t t = 0; t < 2; t++) {
        const stripe_profile_t *p = stripe_profile_find(names[t]);
        uint32_t n = (uint32_t) p->k + p->m;
        stripe_cfg_t c;
        stripe_profile_cfg(p, 0xc0 + t, 1, 8, 3, &c);
        CHECK(set_up(&g_s, p->k, p->m, 512, 32, 1, 8, 3, c.set_id) == STRIPE_OK);
        write_all(&g_s, 700 + t);
        stripe_report_t r;
        for (uint32_t node = 0; node < p->nodes; node++) {
            uint32_t mask = 0;
            for (uint32_t i = 0; i < n; i++)
                if (stripe_profile_node(p, i) == node) mask |= 1u << i;
            uint32_t lost = pop(mask);
            vanish(n, mask);
            CHECK(remount(&g_s2, &c) == STRIPE_DEGRADED);
            for (uint32_t lid = 0; lid < 3; lid++) {
                CHECK(read_matches(&g_s2, lid, &r) == STRIPE_DEGRADED);
                CHECK(r.missing == lost);
            }
            /* plus corrupt drives on the other servers, up to m in all */
            uint32_t slot = stripe_slot_of(&g_s2, 0), extra = 0;
            for (uint32_t i = 0; i < n && lost + pop(extra) < p->m; i++)
                if (!(mask & (1u << i))) extra |= 1u << i;
            corrupt_mask(&g_s2, slot, extra, 11);
            CHECK(read_matches(&g_s2, 0, &r) == STRIPE_DEGRADED);
            CHECK(r.missing + r.corrupt == p->m);
            /* one more is too many */
            uint32_t one = 0;
            for (uint32_t i = 0; i < n && !one; i++)
                if (!((mask | extra) & (1u << i))) one = 1u << i;
            corrupt_mask(&g_s2, slot, one, 11);
            CHECK(read_matches(&g_s2, 0, &r) == STRIPE_ERR_TOO_FEW);
            corrupt_mask(&g_s2, slot, one | extra, 11);
            CHECK(read_matches(&g_s2, 0, &r) == STRIPE_DEGRADED && r.corrupt == 0);
        }
        vanish(n, 0);
    }
}

/* ---- corruption is caught by the hash and repaired by scrub ---- */
static void test_scrub(void)
{
    stripe_cfg_t c = {0x5c, 4, 2, 2, 8, 4};
    CHECK(set_up(&g_s, 4, 2, 512, 64, 2, 8, 4, c.set_id) == STRIPE_OK);
    write_all(&g_s, 1000);
    stripe_report_t r;
    uint32_t slot = stripe_slot_of(&g_s, 0);
    for (uint32_t i = 0; i < 6; i++) {
        uint8_t *pp = piece_on_disk(&g_s, i, slot);
        uint8_t keep = pp[700];
        pp[700] ^= 0x01; /* one bit, in the second block of the piece */
        CHECK(read_matches(&g_s, 0, &r) == STRIPE_DEGRADED);
        CHECK(r.corrupt == 1 && r.missing == 0);
        CHECK(pp[700] == (uint8_t) (keep ^ 1)); /* a read writes nothing */
        CHECK(stripe_scrub(&g_s, &r) == STRIPE_DEGRADED);
        CHECK(r.slots == 4 && r.corrupt == 1 && r.repaired == 1 && r.unrecoverable == 0);
        CHECK(pp[700] == keep);
        CHECK(stripe_scrub(&g_s, &r) == STRIPE_OK);
        CHECK(r.corrupt == 0 && r.repaired == 0 && r.hdr_bad == 0);
    }
    /* m bad pieces in every slot at once */
    uint32_t want = 0;
    for (uint32_t lid = 0; lid < 4; lid++) {
        uint32_t mask = (1u << lid) | 0x20u;
        want += pop(mask);
        corrupt_mask(&g_s, stripe_slot_of(&g_s, lid), mask, lid);
    }
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_DEGRADED);
    CHECK(r.corrupt == want && r.repaired == want && want == 8);
    for (uint32_t lid = 0; lid < 4; lid++) CHECK(read_matches(&g_s, lid, &r) == STRIPE_OK);

    /* a header copy: torn or flipped, it is rewritten */
    for (uint32_t i = 0; i < 6; i++) {
        hdr_on_disk(&g_s, i, slot)[30 + i] ^= 0x80;
        CHECK(read_matches(&g_s, 0, &r) == STRIPE_OK);
        CHECK(stripe_scrub(&g_s, &r) == STRIPE_DEGRADED);
        CHECK(r.hdr_bad == 1 && r.hdr_repaired == 1 && r.corrupt == 0);
        CHECK(stripe_scrub(&g_s, &r) == STRIPE_OK);
    }
    /* a valid header moved to another slot is not that slot's header */
    uint32_t free_slot = 0;
    while (g_s.slot_lid[free_slot] != STRIPE_NONE) free_slot++;
    memcpy(hdr_on_disk(&g_s, 2, free_slot), hdr_on_disk(&g_s, 2, slot), g_s.block_size);
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    CHECK(stripe_slot_of(&g_s2, 0) == slot);
    CHECK(g_s2.slot_lid[free_slot] == STRIPE_NONE);

    /* m + 1 bad pieces: reported, nothing rewritten */
    corrupt_mask(&g_s, slot, 0x07, 5);
    uint8_t before = piece_on_disk(&g_s, 0, slot)[5];
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_ERR_TOO_FEW);
    CHECK(r.unrecoverable == 1 && r.repaired == 0 && r.corrupt == 3);
    CHECK(piece_on_disk(&g_s, 0, slot)[5] == before);
    CHECK(read_matches(&g_s, 0, &r) == STRIPE_ERR_TOO_FEW);
    CHECK(read_matches(&g_s, 1, &r) == STRIPE_OK); /* the others are untouched */
    corrupt_mask(&g_s, slot, 0x07, 5);
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_OK);

    /* every header copy gone: the stripe cannot be read, and scrub says so */
    for (uint32_t i = 0; i < 6; i++) hdr_on_disk(&g_s, i, slot)[0] ^= 0xff;
    CHECK(read_matches(&g_s, 0, &r) == STRIPE_ERR_TOO_FEW);
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_ERR_TOO_FEW && r.unrecoverable == 1);
    CHECK(r.hdr_bad == 6);
    for (uint32_t i = 0; i < 6; i++) hdr_on_disk(&g_s, i, slot)[0] ^= 0xff;
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_OK);

    /* a member gone: scrub works around it and says the set is degraded */
    g_fake[3].vanished = 1;
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_DEGRADED);
    CHECK(r.missing == 4 && r.corrupt == 0 && r.unrecoverable == 0);
    CHECK(stripe_failed_count(&g_s) == 1);
    CHECK(stripe_scrub(&g_s, NULL) == STRIPE_DEGRADED);
    g_fake[3].vanished = 0;
}

/* ---- a power cut at every block of a write ---- */
static void test_torn(uint8_t k, uint8_t m, uint32_t piece_blocks)
{
    uint32_t n = (uint32_t) k + m;
    stripe_cfg_t c = {0x70 + k, k, m, piece_blocks, 6, 3};
    CHECK(set_up(&g_s, k, m, 512, 64, piece_blocks, 6, 3, c.set_id) == STRIPE_OK);
    write_all(&g_s, 1100 + k);
    /* walk the log so the next write reuses a slot that held an old generation */
    for (uint32_t t = 0; t < 4; t++) CHECK(stripe_write(&g_s, 2, g_data[2], g_len[2]) == STRIPE_OK);
    const uint32_t hb = g_s.hdr_blocks;
    const uint64_t old_gen = stripe_gen_of(&g_s, 0);
    const uint32_t old_slot = stripe_slot_of(&g_s, 0);
    for (uint32_t i = 0; i < n; i++) memcpy(g_snap[i], g_pool[i], POOL_BYTES);
    static uint8_t v1[32 * 1024], v2[32 * 1024];
    uint32_t cap = (uint32_t) k * g_s.piece_bytes, v1len = g_len[0];
    memcpy(v1, g_data[0], cap);
    lcg_fill(v2, cap, 4242 + k);
    /* blocks in the write: invalidate n headers, n pieces, n headers */
    const int64_t total = (int64_t) n * (2 * hb + piece_blocks);
    const int64_t first_hdr = (int64_t) n * (hb + piece_blocks) + hb;
    stripe_report_t r;
    for (int64_t B = 0; B <= total + 2; B++) {
        for (uint32_t i = 0; i < n; i++) memcpy(g_pool[i], g_snap[i], POOL_BYTES);
        CHECK(remount(&g_s2, &c) == STRIPE_OK);
        CHECK(stripe_gen_of(&g_s2, 0) == old_gen);
        g_power = B;
        int rc = stripe_write(&g_s2, 0, v2, cap);
        g_power = -1;
        if (B < first_hdr) {
            CHECK(rc == STRIPE_ERR_IO);
            CHECK(stripe_gen_of(&g_s2, 0) == old_gen && stripe_slot_of(&g_s2, 0) == old_slot);
        } else {
            CHECK(rc == (B >= total ? STRIPE_OK : STRIPE_DEGRADED));
            CHECK(stripe_gen_of(&g_s2, 0) > old_gen && stripe_slot_of(&g_s2, 0) != old_slot);
        }
        /* reboot: the old generation in full, or the new one in full */
        CHECK(remount(&g_s2, &c) == STRIPE_OK);
        uint32_t len = 0;
        memset(g_out, 0, sizeof g_out);
        CHECK(stripe_read(&g_s2, 0, g_out, sizeof g_out, &len, &r) == STRIPE_OK);
        if (B < first_hdr) {
            CHECK(len == v1len && memcmp(g_out, v1, v1len) == 0);
            CHECK(stripe_gen_of(&g_s2, 0) == old_gen);
        } else {
            CHECK(len == cap && memcmp(g_out, v2, cap) == 0);
            CHECK(stripe_gen_of(&g_s2, 0) > old_gen);
        }
        for (uint32_t lid = 1; lid < 3; lid++) CHECK(read_matches(&g_s2, lid, &r) == STRIPE_OK);
        /* scrub completes the header copies the cut left out */
        int64_t landed = B < first_hdr ? n : (B - first_hdr) / hb + 1;
        if (landed > n) landed = n;
        CHECK(stripe_scrub(&g_s2, &r) == (landed == n ? STRIPE_OK : STRIPE_DEGRADED));
        CHECK(r.hdr_repaired == (uint32_t) (n - landed) && r.corrupt == 0);
        CHECK(stripe_scrub(&g_s2, &r) == STRIPE_OK);
        /* and the log goes on: the next write lands and reads back */
        CHECK(stripe_write(&g_s2, 0, v1, v1len) == STRIPE_OK);
        CHECK(read_matches(&g_s2, 0, &r) == STRIPE_OK);
        CHECK(remount(&g_s2, &c) == STRIPE_OK);
        CHECK(read_matches(&g_s2, 0, &r) == STRIPE_OK);
    }
}

/* ---- one member's writes fail part way ---- */
static void test_member_write_fail(void)
{
    stripe_cfg_t c = {0x3f, 8, 4, 2, 8, 3};
    CHECK(set_up(&g_s, 8, 4, 512, 64, 2, 8, 3, c.set_id) == STRIPE_OK);
    write_all(&g_s, 1300);
    stripe_report_t r;
    /* member 6 takes the header invalidation and one block of its piece */
    g_fake[6].wbudget = (int64_t) g_s.hdr_blocks + 1;
    lcg_fill(g_data[1], 8 * 1024, 77);
    g_len[1] = 8 * 1024;
    CHECK(stripe_write(&g_s, 1, g_data[1], g_len[1]) == STRIPE_DEGRADED);
    CHECK(stripe_failed_count(&g_s) == 1 && g_s.failed[6]);
    CHECK(read_matches(&g_s, 1, &r) == STRIPE_DEGRADED && r.missing == 1);
    /* a later write skips the failed member */
    uint64_t w6 = g_fake[6].wblocks;
    g_len[2] = 8 * 1024;
    lcg_fill(g_data[2], g_len[2], 78);
    CHECK(stripe_write(&g_s, 2, g_data[2], g_len[2]) == STRIPE_DEGRADED);
    CHECK(g_fake[6].wblocks == w6);
    /* the member comes back: its torn piece fails the hash, scrub fixes it */
    g_fake[6].wbudget = -1;
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    CHECK(read_matches(&g_s2, 1, &r) == STRIPE_DEGRADED && r.corrupt == 1);
    CHECK(stripe_scrub(&g_s2, &r) == STRIPE_DEGRADED);
    CHECK(r.repaired == 2 && r.hdr_repaired == 2 && r.hdr_bad == 2);
    CHECK(stripe_scrub(&g_s2, &r) == STRIPE_OK);
    for (uint32_t lid = 0; lid < 3; lid++) CHECK(read_matches(&g_s2, lid, &r) == STRIPE_OK);

    /* more than m members refuse writes: nothing commits, the old data stands */
    for (uint32_t i = 0; i < 5; i++) g_fake[i].wbudget = 0;
    uint64_t gen = stripe_gen_of(&g_s2, 0);
    CHECK(stripe_write(&g_s2, 0, g_data[5], 512) == STRIPE_ERR_IO);
    CHECK(stripe_gen_of(&g_s2, 0) == gen);
    CHECK(stripe_failed_count(&g_s2) == 5);
    CHECK(stripe_write(&g_s2, 0, g_data[5], 512) == STRIPE_ERR_TOO_FEW);
    for (uint32_t i = 0; i < 5; i++) g_fake[i].wbudget = -1;
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    CHECK(read_matches(&g_s2, 0, &r) == STRIPE_OK);
    CHECK(stripe_gen_of(&g_s2, 0) == gen);
    /* a set whose members all vanish cannot mount; format needs k members */
    vanish(12, 0xfff);
    CHECK(remount(&g_s2, &c) == STRIPE_ERR_TOO_FEW);
    CHECK(stripe_write(&g_s2, 0, g_data[0], 1) == STRIPE_ERR_STATE);
    vanish(12, 0x01f);
    CHECK(remount(&g_s2, &c) == STRIPE_ERR_TOO_FEW);
    CHECK(stripe_format(&g_s2) == STRIPE_ERR_TOO_FEW);
    vanish(12, 0);
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    vanish(12, 0x01f);
    CHECK(stripe_format(&g_s2) == STRIPE_ERR_IO);
    vanish(12, 0);
}

/* ---- a replaced member is rebuilt ---- */
static void test_rebuild(uint8_t k, uint8_t m)
{
    uint32_t n = (uint32_t) k + m;
    stripe_cfg_t c = {0xbb00 + k, k, m, 1, 8, 6};
    CHECK(set_up(&g_s, k, m, 512, 32, 1, 8, 6, c.set_id) == STRIPE_OK);
    write_all(&g_s, 1500 + k);
    stripe_report_t r;
    const uint32_t dead = n - 2, spare = STRIPE_MAX_MEMBERS, spare2 = STRIPE_MAX_MEMBERS - 1;
    g_fake[dead].vanished = 1;
    CHECK(read_matches(&g_s, 0, &r) == STRIPE_DEGRADED && r.missing == 1);
    /* writes go on while degraded */
    lcg_fill(g_data[3], 300, 31);
    g_len[3] = 300;
    CHECK(stripe_write(&g_s, 3, g_data[3], 300) == STRIPE_DEGRADED);
    /* refusals: wrong block size, too short */
    fake_setup(spare, 4096, 64, 0xa5);
    CHECK(stripe_rebuild(&g_s, dead, g_mem[spare], &r) == STRIPE_ERR_GEOM);
    fake_setup(spare, 512, (uint64_t) g_s.slots * g_s.slot_blocks - 1, 0xa5);
    CHECK(stripe_rebuild(&g_s, dead, g_mem[spare], &r) == STRIPE_ERR_GEOM);
    /* a replacement full of old bytes */
    fake_setup(spare, 512, 32, 0xa5);
    CHECK(stripe_rebuild(&g_s, dead, g_mem[spare], &r) == STRIPE_OK);
    CHECK(r.slots == 6 && r.repaired == 6 && r.hdr_repaired == 6 && r.unrecoverable == 0);
    CHECK(stripe_failed_count(&g_s) == 0);
    CHECK(stripe_scrub(&g_s, &r) == STRIPE_OK);
    CHECK(r.slots == 6 && r.corrupt == 0 && r.hdr_bad == 0);
    for (uint32_t lid = 0; lid < 6; lid++) CHECK(read_matches(&g_s, lid, &r) == STRIPE_OK);
    /* the rebuilt member carries real pieces: lose m others and still read */
    g_mem[dead] = g_mem[spare];
    uint32_t mask = 0;
    for (uint32_t i = 0; i < n && pop(mask) < m; i++)
        if (i != dead) mask |= 1u << i;
    vanish(n, mask);
    CHECK(remount(&g_s2, &c) == STRIPE_DEGRADED);
    for (uint32_t lid = 0; lid < 6; lid++) {
        CHECK(read_matches(&g_s2, lid, &r) == STRIPE_DEGRADED);
        CHECK(r.missing == m);
    }
    /* rebuilding with too few others left: reported, not invented */
    fake_setup(spare2, 512, 32, 0);
    CHECK(stripe_rebuild(&g_s2, dead, g_mem[spare2], &r) == STRIPE_ERR_TOO_FEW);
    CHECK(r.unrecoverable == 6 && r.repaired == 0);
    vanish(n, 0);
    /* a replacement that fails its writes */
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    fake_setup(spare2, 512, 32, 0);
    g_fake[spare2].wbudget = 3;
    CHECK(stripe_rebuild(&g_s2, 1, g_mem[spare2], &r) == STRIPE_ERR_IO);
    CHECK(stripe_failed_count(&g_s2) == 1);
    CHECK(read_matches(&g_s2, 0, &r) == STRIPE_DEGRADED);
    fake_setup(spare2, 512, 32, 0);
    CHECK(stripe_rebuild(&g_s2, 1, g_mem[spare2], NULL) == STRIPE_OK);
    g_mem[1] = g_mem[spare2];
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    for (uint32_t lid = 0; lid < 6; lid++) CHECK(read_matches(&g_s2, lid, &r) == STRIPE_OK);
    CHECK(stripe_scrub(&g_s2, &r) == STRIPE_OK);
    g_mem[1] = &g_fake[1].blk;
    g_mem[dead] = &g_fake[dead].blk;
    g_fake[dead].vanished = 0;
}

/* ---- the log: generations only grow, every write takes a new slot ---- */
static void test_log(void)
{
    stripe_cfg_t c = {0x106, 4, 2, 1, 5, 4};
    CHECK(set_up(&g_s, 4, 2, 512, 16, 1, 5, 4, c.set_id) == STRIPE_OK);
    CHECK(g_s.slots == 5);
    write_all(&g_s, 1700);
    stripe_report_t r;
    uint64_t last = 0;
    for (uint32_t t = 0; t < 60; t++) {
        uint32_t lid = (t * 7u) % 4u;
        uint32_t prev = stripe_slot_of(&g_s, lid);
        uint64_t pg = stripe_gen_of(&g_s, lid);
        g_len[lid] = 1 + (t * 97u) % 2048u;
        lcg_fill(g_data[lid], g_len[lid], 9000 + t);
        CHECK(stripe_write(&g_s, lid, g_data[lid], g_len[lid]) == STRIPE_OK);
        CHECK(stripe_slot_of(&g_s, lid) != prev);
        CHECK(stripe_gen_of(&g_s, lid) > pg && stripe_gen_of(&g_s, lid) > last);
        last = stripe_gen_of(&g_s, lid);
        /* no two logical stripes share a slot */
        for (uint32_t a = 0; a < 4; a++)
            for (uint32_t b = a + 1; b < 4; b++)
                CHECK(stripe_slot_of(&g_s, a) != stripe_slot_of(&g_s, b));
        if (t % 5 == 0) {
            CHECK(remount(&g_s, &c) == STRIPE_OK);
            CHECK(g_s.next_gen == last + 1);
        }
        for (uint32_t l = 0; l < 4; l++) CHECK(read_matches(&g_s, l, &r) == STRIPE_OK);
    }
    /* format empties the set, on disk too */
    CHECK(stripe_format(&g_s) == STRIPE_OK);
    for (uint32_t l = 0; l < 4; l++) CHECK(read_matches(&g_s, l, &r) == STRIPE_ERR_NOENT);
    CHECK(remount(&g_s2, &c) == STRIPE_OK);
    for (uint32_t l = 0; l < 4; l++) CHECK(read_matches(&g_s2, l, &r) == STRIPE_ERR_NOENT);
    CHECK(g_s2.next_gen == 1);
}

int main(void)
{
    test_profiles();
    test_bounds();
    test_round_trip();
    test_matches_freight();
    test_erasures_by_corruption(4, 2, 12, 40);
    test_erasures_by_corruption(8, 4, 12, 200);
    test_erasures_by_corruption(16, 8, 0, 400);
    test_erasures_by_corruption(12, 8, 0, 200);
    test_erasures_by_vanishing(4, 2);
    test_erasures_by_vanishing(8, 4);
    test_node_loss();
    test_scrub();
    test_torn(4, 2, 1);
    test_torn(4, 2, 3);
    test_torn(8, 4, 2);
    test_torn(16, 8, 1);
    test_member_write_fail();
    test_rebuild(4, 2);
    test_rebuild(8, 4);
    test_rebuild(16, 8);
    test_log();
    printf("%d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
