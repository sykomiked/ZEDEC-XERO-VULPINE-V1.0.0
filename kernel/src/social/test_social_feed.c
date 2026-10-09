/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_social_feed.c — signed records, ISF feed, store + gossip, buckets.
 *
 * Build and run from kernel/ (host test, uses libc):
 *
 *   PATH=/tmp/claude-0/gcc11:$PATH gcc -std=c11 -Wall -Wextra -Werror -O2 \
 *     -Iinclude -Isrc/modbind -Isrc/e8 -Isrc/event_space -Isrc/pqsec -Isrc/mlkem \
 *     -Isrc/lpres -Isrc/surplus -Isrc/edp_risk \
 *     src/social/test_social_feed.c src/social/social_post.c src/social/social_feed.c \
 *     src/social/social_store.c src/social/social_bucket.c src/social/social_group.c \
 *     src/social/social_sign_mldsa.c \
 *     src/social/zx_notify.c src/tensor/zt.c src/tensor/zt_isf.c \
 *     src/reputation/reputation.c src/robin_debanks/sha256.c src/mlkem/keccak.c \
 *     src/pqsec/pq_mldsa65.c src/pqsec/mldsa/[a-z]*.c -o /tmp/test_social_feed &&
 * /tmp/test_social_feed
 *
 * Sanitizers: add -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "social_post.h"
#include "social_feed.h"
#include "social_store.h"
#include "social_bucket.h"
#include "social_group.h"
#include "social_sign_mldsa.h"
#include "zx_notify.h"
#include "../reputation/reputation.h"

static int failures, passes;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else {                                                                                   \
            printf("[PASS] %s\n", m);                                                              \
            passes++;                                                                              \
        }                                                                                          \
    } while (0)
#define Q(x) ((double) (x) / 65536.0)

static uint64_t rng = 0x36A9C0FFEEull;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (uint32_t) (rng >> 16);
}

/* ---------------- users with real ML-DSA-65 keys ---------------- */

#define NUSERS 6
typedef struct {
    uint8_t pk[SP_PK_LEN], sk[SP_MLDSA_SK_LEN], id[SP_NODEID_LEN];
    sp_mldsa_key_t key;
    sp_signer_t signer;
} user_t;
static user_t U[NUSERS];

static const uint8_t *lookup(void *ctx, const uint8_t author[SP_NODEID_LEN])
{
    (void) ctx;
    for (int i = 0; i < NUSERS; i++)
        if (!memcmp(U[i].id, author, SP_NODEID_LEN)) return U[i].pk;
    return NULL;
}

static sp_verifier_t V = {sp_mldsa_verify, NULL, lookup, NULL};

static void make_users(void)
{
    for (int i = 0; i < NUSERS; i++) {
        uint8_t seed[32];
        for (int j = 0; j < 32; j++) seed[j] = (uint8_t) (i * 37 + j);
        sp_mldsa_keypair(seed, U[i].pk, U[i].sk, U[i].id);
        U[i].key.sk = U[i].sk;
        U[i].key.rnd = NULL;
        U[i].signer.sign = sp_mldsa_sign;
        U[i].signer.ctx = &U[i].key;
    }
}

static sp_record_t *new_rec(void)
{
    sp_record_t *r = calloc(1, sizeof *r);
    if (!r) abort();
    return r;
}

static void text_post(sp_record_t *r, const uint8_t *author, uint64_t t, const char *s)
{
    sp_init(r, SP_KIND_POST, SP_VIS_PUBLIC, author, t);
    sp_set_body(r, (const uint8_t *) s, (uint32_t) strlen(s));
}

static uint8_t encbuf[SP_ENC_MAX], encbuf2[SP_ENC_MAX];

/* ======================================================================== */

static void test_records(void)
{
    printf("\n=== signed records ===\n");
    sp_record_t *r = new_rec(), *d = new_rec();
    text_post(r, U[0].id, 7, "hello commons #intro");
    sp_set_wall(r, 1760000000000ull);
    uint8_t cid[36];
    for (int i = 0; i < 36; i++) cid[i] = (uint8_t) (0x01 + i);
    CHECK(sp_add_media(r, cid, sizeof cid), "media CID attaches");
    CHECK(sp_sign(r, &U[0].signer) == SP_OK, "ML-DSA-65 signs a post");
    int32_t n = sp_encode(r, encbuf, sizeof encbuf);
    printf("       encoded post without key: %d bytes (max %u)\n", n, SP_ENC_MAX);
    CHECK(n > 0, "encodes");
    CHECK(sp_decode(encbuf, (uint32_t) n, d, &V) == SP_OK, "decodes and verifies (key by lookup)");
    CHECK(!memcmp(d->id, r->id, 32) && d->lamport == 7 && d->wall_ms == 1760000000000ull &&
              d->body_len == r->body_len && !memcmp(d->body, r->body, r->body_len) &&
              d->n_media == 1 && !memcmp(d->media[0], cid, 36),
          "round trip keeps every field");
    int32_t n2 = sp_encode(d, encbuf2, sizeof encbuf2);
    CHECK(n2 == n && !memcmp(encbuf, encbuf2, (size_t) n), "re-encoding is byte-identical");

    /* self-certifying with the key attached */
    sp_record_t *k = new_rec();
    *k = *r;
    sp_attach_pk(k, U[0].pk);
    int32_t nk = sp_encode(k, encbuf2, sizeof encbuf2);
    sp_verifier_t nolookup = {sp_mldsa_verify, NULL, NULL, NULL};
    CHECK(sp_decode(encbuf2, (uint32_t) nk, d, &nolookup) == SP_OK,
          "a record carrying its key verifies with no directory");
    CHECK(!memcmp(d->id, r->id, 32), "carrying the key does not change the id");
    CHECK(sp_decode(encbuf, (uint32_t) n, d, &nolookup) == SP_ERR_KEY,
          "keyless record with no directory -> SP_ERR_KEY");

    /* tamper */
    encbuf[60] ^= 1; /* inside the signed part (body/time area) */
    int32_t t = sp_decode(encbuf, (uint32_t) n, d, &V);
    CHECK(t == SP_ERR_SIG || t == SP_ERR_KEY || t == SP_ERR_FORMAT,
          "tampered signed part rejected");
    encbuf[60] ^= 1;
    encbuf[n - 5] ^= 0x40;
    CHECK(sp_decode(encbuf, (uint32_t) n, d, &V) == SP_ERR_SIG, "tampered signature rejected");
    encbuf[n - 5] ^= 0x40;
    uint32_t body_off = 4 + 4 + 32 + 8 + 8 + 2; /* magic, hdr, author, lamport, wall, len */
    encbuf[body_off] ^= 0x20;                   /* 'h' -> 'H' */
    CHECK(sp_decode(encbuf, (uint32_t) n, d, &V) == SP_ERR_SIG,
          "tampered body rejected by signature");
    encbuf[body_off] ^= 0x20;
    memcpy(k->pk, U[1].pk, SP_PK_LEN); /* someone else's key */
    nk = sp_encode(k, encbuf2, sizeof encbuf2);
    CHECK(sp_decode(encbuf2, (uint32_t) nk, d, &V) == SP_ERR_KEY,
          "a key that does not hash to the author is rejected");
    sp_record_t *f = new_rec();
    *f = *r;
    memcpy(f->author, U[1].id, 32); /* claim another author, keep U0's signature */
    sp_compute_id(f);
    int32_t nf = sp_encode(f, encbuf2, sizeof encbuf2);
    CHECK(sp_decode(encbuf2, (uint32_t) nf, d, &V) == SP_ERR_SIG, "forged author rejected");
    CHECK(sp_decode(encbuf, (uint32_t) n - 1, d, &V) != SP_OK, "truncated record rejected");
    encbuf[n] = 0;
    CHECK(sp_decode(encbuf, (uint32_t) n + 1, d, &V) == SP_ERR_TRAILING, "trailing byte rejected");
    CHECK(sp_decode(encbuf, (uint32_t) n, d, &V) == SP_OK, "untouched copy still verifies");

    /* kind rules */
    sp_record_t *x = new_rec();
    sp_init(x, SP_KIND_REPLY, SP_VIS_PUBLIC, U[0].id, 1);
    sp_set_body(x, (const uint8_t *) "hi", 2);
    CHECK(sp_validate(x) == SP_ERR_RULE, "reply without parent is invalid");
    sp_set_parent(x, r->id);
    CHECK(sp_validate(x) == SP_OK, "reply with parent is valid");
    sp_init(x, SP_KIND_DM, SP_VIS_PUBLIC, U[0].id, 1);
    sp_set_body(x, (const uint8_t *) "\x9f\x10", 2);
    sp_set_audience(x, U[1].id);
    CHECK(sp_validate(x) == SP_ERR_RULE, "a public DM is invalid");
    x->vis = SP_VIS_PRIVATE;
    CHECK(sp_validate(x) == SP_OK, "private DM to one recipient is valid");
    sp_init(x, SP_KIND_FOLLOW, SP_VIS_PUBLIC, U[0].id, 1);
    sp_set_audience(x, U[1].id);
    sp_set_body(x, (const uint8_t *) "\x02", 1);
    CHECK(sp_validate(x) == SP_ERR_RULE, "follow body must be 0 or 1");
    x->body[0] = 1;
    CHECK(sp_validate(x) == SP_OK, "follow is valid");
    sp_init(x, SP_KIND_REACTION, SP_VIS_PUBLIC, U[0].id, 1);
    sp_set_parent(x, r->id);
    sp_set_body(x, (const uint8_t *) "0123456789012345678901234567890123", 34);
    CHECK(sp_validate(x) == SP_ERR_RULE, "reaction longer than 32 bytes is invalid");
    sp_init(x, SP_KIND_PRESENCE, SP_VIS_PUBLIC, U[0].id, 1);
    CHECK(sp_validate(x) == SP_OK, "empty presence is valid");

    sp_clock_t c = {5};
    sp_clock_observe(&c, 9);
    CHECK(sp_clock_next(&c) == 10, "Lamport clock: observe(9) then next = 10");
    free(r), free(d), free(k), free(f), free(x);
}

/* ---------------- fuzz ---------------- */

static void test_fuzz(void)
{
    printf("\n=== decoder fuzz ===\n");
    sp_record_t *seeds[3], *d = new_rec();
    static uint8_t enc[3][SP_ENC_MAX];
    int32_t len[3];
    for (int i = 0; i < 3; i++) seeds[i] = new_rec();
    text_post(seeds[0], U[2].id, 3, "fuzz seed #one");
    sp_init(seeds[1], SP_KIND_DM, SP_VIS_PRIVATE, U[2].id, 4);
    sp_set_audience(seeds[1], U[3].id);
    sp_set_body(seeds[1], (const uint8_t *) "\x01\x02\x03\xff ciphertext", 15);
    sp_init(seeds[2], SP_KIND_BUCKET_LISTING, SP_VIS_PUBLIC, U[2].id, 5);
    uint8_t fpr[32] = {7};
    sb_make_listing(seeds[2], SB_CAT_APPS, SB_SRC_CID, (const uint8_t *) SB_DEFAULT_CID,
                    (uint32_t) strlen(SB_DEFAULT_CID), (const uint8_t *) "Bucket", 6,
                    (const uint8_t *) "desc", 4, fpr);
    sp_attach_pk(seeds[2], U[2].pk);
    for (int i = 0; i < 3; i++) {
        sp_sign(seeds[i], &U[2].signer);
        len[i] = sp_encode(seeds[i], enc[i], SP_ENC_MAX);
    }
    static uint8_t m[SP_ENC_MAX + 16];
    uint32_t accepted = 0, reencode_ok = 0, rejected = 0, codes[16] = {0};
    const uint32_t N = 24000;
    for (uint32_t it = 0; it < N; it++) {
        int s = (int) (it % 3);
        uint32_t L = (uint32_t) len[s];
        memcpy(m, enc[s], L);
        uint32_t op = rnd() % 6;
        /* Mutate mostly the header and signed part, where the parser lives. */
        uint32_t hot = 4u + rnd() % (L < 160 ? L - 4 : 156u);
        if (op == 0)
            m[hot] ^= (uint8_t) (1u << (rnd() % 8));
        else if (op == 1)
            m[rnd() % L] = (uint8_t) rnd();
        else if (op == 2)
            L = rnd() % L; /* truncate */
        else if (op == 3 && L < sizeof m)
            m[L++] = (uint8_t) rnd(); /* append */
        else if (op == 4) {           /* a length field */
            uint32_t bl_off = 4 + 4 + 32 + 8;
            m[bl_off + (rnd() % 4)] = (uint8_t) rnd();
        } else {
            for (int k = 0; k < 4; k++) m[4u + rnd() % 60u] = (uint8_t) rnd();
        }
        if (it % 1000 == 999) L = 0; /* empty */
        int32_t st = sp_decode(m, L, d, &V);
        if (st == SP_OK) {
            accepted++;
            int32_t n2 = sp_encode(d, encbuf2, sizeof encbuf2);
            if (n2 == (int32_t) L && !memcmp(encbuf2, m, L)) reencode_ok++;
        } else {
            rejected++;
            codes[-st & 15]++;
        }
    }
    printf("       %u mutated inputs: %u accepted, %u rejected (format %u, range %u, rule %u, "
           "trailing %u, key %u, sig %u)\n",
           N, accepted, rejected, codes[3], codes[4], codes[5], codes[6], codes[7], codes[8]);
    CHECK(N >= 20000, "at least 20000 mutated record inputs");
    CHECK(reencode_ok == accepted, "every accepted input re-encodes to exactly its own bytes");
    CHECK(codes[8] > 0 && codes[3] > 0, "mutations exercised both framing and signature checks");

    /* gossip message fuzz: HAVE/WANT framing */
    static sp_record_t slots[8];
    static uint32_t idx[16];
    ss_store_t st;
    ss_init(&st, slots, 8, idx, 16, 1);
    static uint8_t tx[SS_MSG_MAX];
    int32_t sink(void *ctx, const uint8_t *msg, uint32_t l);
    sp_policy_t pol = {0};
    ss_gossip_t g = {&st, U[0].id, U[1].id, &pol, &V, NULL, sink, NULL, NULL,
                     tx,  NULL,    d,       0,    0,  0,    0,    0};
    uint32_t ok = 0, bad = 0;
    for (uint32_t it = 0; it < 20000; it++) {
        uint32_t L = rnd() % 64;
        for (uint32_t i = 0; i < L; i++) m[i] = (uint8_t) rnd();
        if (L) m[0] = "HWRX"[rnd() % 4];
        if (L >= 3 && (rnd() & 1)) {
            uint32_t c = (L - 3) / 8;
            m[1] = (uint8_t) (c >> 8);
            m[2] = (uint8_t) c;
            L = 3 + c * 8;
        }
        int32_t r = ss_gossip_handle(&g, m, L);
        if (r >= 0)
            ok++;
        else
            bad++;
    }
    printf("       20000 random gossip messages: %u well-formed, %u rejected\n", ok, bad);
    CHECK(ok > 0 && bad > 0 && st.count == 0, "gossip fuzz: framing checked, nothing stored");

    /* listing payload fuzz */
    uint32_t lok = 0;
    bool bounds_ok = true;
    for (uint32_t it = 0; it < 20000; it++) {
        uint32_t L = seeds[2]->body_len;
        memcpy(m, seeds[2]->body, L);
        m[rnd() % L] = (uint8_t) rnd();
        if (rnd() % 4 == 0) L = rnd() % (L + 1);
        if (sb_check_listing_body(m, L) == SP_OK) {
            lok++;
            sp_record_t *t = d;
            sp_init(t, SP_KIND_BUCKET_LISTING, SP_VIS_PUBLIC, U[2].id, 1);
            sp_set_body(t, m, L);
            sb_listing_t l;
            if (sb_parse_listing(t, &l) != SP_OK || l.src + l.src_len > t->body + L ||
                l.title + l.title_len > t->body + L || l.desc + l.desc_len > t->body + L ||
                l.fingerprint + SB_FPR_LEN != t->body + L)
                bounds_ok = false;
        }
    }
    printf("       20000 mutated listing payloads: %u passed the strict check\n", lok);
    CHECK(bounds_ok, "every listing payload that passes the check parses within its bounds");
    for (int i = 0; i < 3; i++) free(seeds[i]);
    free(d);
}

/* ---------------- visibility ---------------- */

static bool follows(void *ctx, const uint8_t *author, const uint8_t *peer)
{
    (void) ctx;
    return !memcmp(author, U[0].id, 32) && !memcmp(peer, U[1].id, 32); /* U1 follows U0 */
}
static bool in_list(void *ctx, const uint8_t *author, const uint8_t *list, const uint8_t *peer)
{
    (void) ctx, (void) author;
    return list[0] == 0xAA && !memcmp(peer, U[2].id, 32); /* list 0xAA.. = {U2} */
}
static bool agree_not_u4(void *ctx, const sp_record_t *r, const uint8_t *peer)
{
    (void) ctx, (void) r;
    return memcmp(peer, U[4].id, 32) != 0; /* the user chose not to share with U4 */
}

static void test_visibility(void)
{
    printf("\n=== visibility at the point of sharing ===\n");
    sp_policy_t p = {follows, in_list, agree_not_u4, NULL, NULL};
    sp_policy_t none = {0};
    sp_record_t *r = new_rec();
    text_post(r, U[0].id, 1, "for everyone");
    CHECK(sp_may_share(r, U[0].id, U[3].id, &p), "public: shared with anyone");
    CHECK(!sp_may_share(r, U[0].id, U[4].id, &p), "public: the user's own veto still applies");
    CHECK(sp_may_share(r, U[3].id, U[5].id, &p), "public: relays freely");
    r->vis = SP_VIS_FOLLOWERS;
    CHECK(sp_may_share(r, U[0].id, U[1].id, &p), "followers: the author shares with a follower");
    CHECK(!sp_may_share(r, U[0].id, U[3].id, &p), "followers: not with a non-follower");
    CHECK(!sp_may_share(r, U[1].id, U[3].id, &p), "followers: a follower does not relay it");
    CHECK(!sp_may_share(r, U[0].id, U[1].id, &none), "followers: no callback -> fail closed");
    CHECK(sp_may_share(r, U[3].id, U[0].id, &none), "anyone may return a record to its author");
    r->vis = SP_VIS_LIST;
    uint8_t list[32];
    memset(list, 0xAA, 32);
    sp_set_audience(r, list);
    CHECK(sp_may_share(r, U[0].id, U[2].id, &p), "list: shared with a member");
    CHECK(!sp_may_share(r, U[0].id, U[1].id, &p), "list: not with a non-member");
    sp_init(r, SP_KIND_DM, SP_VIS_PRIVATE, U[0].id, 2);
    sp_set_audience(r, U[1].id);
    sp_set_body(r, (const uint8_t *) "\x13\x37", 2);
    CHECK(sp_may_share(r, U[3].id, U[1].id, &p), "DM: any node may relay it to the recipient");
    CHECK(!sp_may_share(r, U[0].id, U[2].id, &p), "DM: never to anyone else");
    free(r);
}

/* ---------------- feed ---------------- */

static const char *TOPIC_TEXT[10] = {"sourdough starter hydration crumb oven #baking",
                                     "orbital mechanics hohmann transfer delta budget #space",
                                     "mycorrhizal fungi soil carbon forest network #ecology",
                                     "counterpoint fugue bach voice leading #music",
                                     "rust borrow checker lifetimes ownership #code",
                                     "tide pools anemone starfish coastline #ocean",
                                     "chess endgame rook pawn opposition #chess",
                                     "permaculture swales rainwater garden beds #garden",
                                     "medieval manuscripts vellum illumination ink #history",
                                     "bouldering crimp heel hook overhang #climbing"};

static void test_feed(rep_state_t *rep)
{
    printf("\n=== ISF feed ===\n");
    uint8_t viral_author[32], auth[10][32];
    memset(viral_author, 0x5A, 32);
    for (int i = 0; i < 10; i++) memset(auth[i], 0x10 + i, 32);
    badge_award(rep, sf_subject_of(viral_author), 1, 500); /* a very "reputable" spammer */

    /* surplus properties */
    sf_vec_t a, b, c;
    sf_seen_t seen;
    sf_seen_init(&seen);
    sf_embed_bytes((const uint8_t *) TOPIC_TEXT[0], (uint32_t) strlen(TOPIC_TEXT[0]), auth[0], &a);
    sf_embed_bytes((const uint8_t *) "sourdough starter hydration crumb oven #baking!!", 48,
                   auth[0], &b);
    sf_embed_bytes((const uint8_t *) TOPIC_TEXT[1], (uint32_t) strlen(TOPIC_TEXT[1]), auth[1], &c);
    zt_fx lnN = zt_surplus_f(ZT_ONE, 8);
    CHECK(sf_surplus_over(&a, &seen, 8) == lnN, "nothing seen: surplus = ln N");
    sf_seen_add(&seen, &a);
    zt_fx sb_ = sf_surplus_over(&b, &seen, 8), sc = sf_surplus_over(&c, &seen, 8);
    printf("       ln 8 = %.4f; near-duplicate adds %.4f, independent adds %.4f\n", Q(lnN), Q(sb_),
           Q(sc));
    CHECK(sb_ < lnN / 20, "near-collinear with the seen set: surplus near 0");
    CHECK(sc > lnN * 3 / 4, "independent of the seen set: surplus near ln N");

    /* 50 near-duplicate viral posts + 10 independent ones */
    enum { NV = 50, NI = 10, NC = NV + NI + 5 };
    sp_record_t *cand[NC];
    for (int i = 0; i < NV; i++) {
        char s[128];
        snprintf(s, sizeof s, "SHOCKING you will not believe this one trick doctors hate #viral %d",
                 i);
        cand[i] = new_rec();
        text_post(cand[i], viral_author, 1000 + (uint64_t) i, s);
        sp_compute_id(cand[i]);
    }
    for (int i = 0; i < NI; i++) {
        cand[NV + i] = new_rec();
        text_post(cand[NV + i], auth[i], 600 + (uint64_t) i * 7, TOPIC_TEXT[i]);
        sp_compute_id(cand[NV + i]);
    }
    for (int i = 0; i < 5; i++) { /* reactions and a DM are not feed candidates */
        cand[NV + NI + i] = new_rec();
        sp_init(cand[NV + NI + i], i < 4 ? SP_KIND_REACTION : SP_KIND_DM,
                i < 4 ? SP_VIS_PUBLIC : SP_VIS_PRIVATE, auth[i], 1100);
        if (i < 4) sp_set_parent(cand[NV + NI + i], cand[0]->id);
        sp_set_audience(cand[NV + NI + i], auth[9]);
        sp_set_body(cand[NV + NI + i], (const uint8_t *) "\xf0\x9f\x94\xa5", 4);
        sp_compute_id(cand[NV + NI + i]);
    }
    sf_config_t cfg;
    sf_config_default(&cfg, 1050);
    cfg.horizon = 600;
    cfg.rep = sf_rep_badges;
    cfg.rep_ctx = rep;
    cfg.w_fresh = lnN; /* asks for too much: clamped to ln N / 4 */
    cfg.w_rep = lnN;   /* clamped to ln N / 8 */
    static sf_work_t work[NC];
    sf_pick_t pick[10], pick2[10];
    sf_seen_init(&seen);
    uint32_t k = sf_rank(&cfg, &seen, (const sp_record_t *const *) cand, NC, work, pick, 10);
    int viral = 0, nonpost = 0;
    for (uint32_t i = 0; i < k; i++) {
        viral += pick[i].index < NV;
        nonpost += pick[i].index >= NV + NI;
    }
    zt_fx div_isf = sf_diversity(work, pick, k);

    /* recency-only baseline */
    sf_pick_t base[10];
    bool used[NC] = {0};
    for (int i = 0; i < 10; i++) {
        int best = -1;
        for (int j = 0; j < NV + NI; j++)
            if (!used[j] && (best < 0 || cand[j]->lamport > cand[best]->lamport)) best = j;
        used[best] = true;
        base[i].index = (uint32_t) best;
    }
    zt_fx div_base = sf_diversity(work, base, 10);
    int viral_base = 0;
    for (int i = 0; i < 10; i++) viral_base += base[i].index < NV;
    printf("       top 10 of 50 near-duplicate viral + 10 independent posts:\n");
    printf("         ISF feed:     %d viral, %d independent, diversity (mean pairwise u) %.4f\n",
           viral, (int) k - viral, Q(div_isf));
    printf("         recency only: %d viral, %d independent, diversity %.4f\n", viral_base,
           10 - viral_base, Q(div_base));
    for (uint32_t i = 0; i < 3 && i < k; i++)
        printf("         pick %u: surplus %.4f fresh %.4f rep %.4f\n", i, Q(pick[i].surplus),
               Q(pick[i].fresh), Q(pick[i].rep));
    CHECK(k == 10 && viral <= 1, "50 near-duplicates cannot crowd out independent posts");
    CHECK(nonpost == 0, "reactions and DMs are never ranked (no engagement signal, no DM bodies)");
    CHECK(div_isf > div_base * 3, "ISF feed is measurably more diverse than recency-only");
    CHECK(pick[0].fresh <= lnN / 4 && pick[0].rep <= lnN / 8, "freshness and reputation clamped");

    /* bounded blends: redundant + max bonuses < independent + none */
    CHECK(lnN / 4 + lnN / 8 < lnN / 2, "clamps keep fresh + rep below half the ISF range");

    /* determinism, including under candidate order */
    sp_record_t *shuf[NC];
    memcpy(shuf, cand, sizeof cand);
    for (int i = NC - 1; i > 0; i--) {
        int j = (int) (rnd() % (uint32_t) (i + 1));
        sp_record_t *t = shuf[i];
        shuf[i] = shuf[j];
        shuf[j] = t;
    }
    static sf_work_t work2[NC];
    uint32_t k2 = sf_rank(&cfg, &seen, (const sp_record_t *const *) shuf, NC, work2, pick2, 10);
    bool same = k2 == k;
    for (uint32_t i = 0; same && i < k; i++)
        same = !memcmp(cand[pick[i].index]->id, shuf[pick2[i].index]->id, 32) &&
               pick[i].score == pick2[i].score;
    CHECK(same, "deterministic: same picks and scores under any candidate order");

    /* engagement-independence: 1000 more reactions change nothing */
    uint32_t k3 =
        sf_rank(&cfg, &seen, (const sp_record_t *const *) cand, NV + NI, work2, pick2, 10);
    same = k3 == k;
    for (uint32_t i = 0; same && i < k; i++) same = pick[i].index == pick2[i].index;
    CHECK(same, "ranking ignores reactions entirely");

    /* commit: seen posts stop scoring */
    sf_commit(&seen, work, pick, k);
    uint32_t k4 = sf_rank(&cfg, &seen, (const sp_record_t *const *) cand, NV + NI, work2, pick2, 3);
    for (uint32_t i = 0; i < k4; i++)
        printf("         after commit pick %u: idx %u surplus %.4f fresh %.4f\n", i, pick2[i].index,
               Q(pick2[i].surplus), Q(pick2[i].fresh));
    CHECK(k4 == 3 && pick2[0].index >= NV && pick2[1].surplus < lnN / 4 &&
              pick2[2].surplus < lnN / 4,
          "after commit: the one unseen topic leads; the 49 duplicates add < ln N / 4");
    for (int i = 0; i < NC; i++) free(cand[i]);
}

/* ---------------- store + gossip ---------------- */

#define QMAX 4096
typedef struct {
    uint8_t *msg[QMAX];
    uint32_t len[QMAX];
    uint32_t head, tail;
} queue_t;
static queue_t QAB, QBA; /* A -> B, B -> A */
typedef struct {
    queue_t *out, *in;
} link_t;

static int32_t q_send(void *ctx, const uint8_t *msg, uint32_t len)
{
    queue_t *q = ((link_t *) ctx)->out;
    if (q->tail - q->head >= QMAX) return -1;
    uint8_t *c = malloc(len);
    memcpy(c, msg, len);
    q->msg[q->tail % QMAX] = c;
    q->len[q->tail % QMAX] = len;
    q->tail++;
    return 0;
}
static int32_t q_recv(void *ctx, uint8_t *buf, uint32_t cap)
{
    queue_t *q = ((link_t *) ctx)->in;
    if (q->head == q->tail) return 0;
    uint32_t i = q->head++ % QMAX;
    uint32_t l = q->len[i];
    if (l > cap) l = cap;
    memcpy(buf, q->msg[i], l);
    free(q->msg[i]);
    return (int32_t) l;
}
int32_t sink(void *ctx, const uint8_t *msg, uint32_t l)
{
    (void) ctx, (void) msg, (void) l;
    return 0;
}

static zxn_bus_t *g_bus;
static const uint8_t *g_me;
static void on_new(void *ctx, const sp_record_t *r)
{
    (void) ctx;
    if (r->kind == SP_KIND_DM && !memcmp(r->audience, g_me, 32)) {
        char from[24];
        snprintf(from, sizeof from, "peer %02x%02x%02x%02x", r->author[0], r->author[1],
                 r->author[2], r->author[3]);
        zxn_message_received(g_bus, "social", from, (uint64_t) r->author[0] << 8 | r->author[1]);
    }
}

static void test_store(void)
{
    printf("\n=== store ===\n");
    enum { CAP = 16 };
    static sp_record_t slots[CAP];
    static uint32_t idx[32];
    ss_store_t s;
    CHECK(!ss_init(&s, slots, CAP, idx, 24, 0x1234), "index_cap must be a power of two >= 2 cap");
    CHECK(ss_init(&s, slots, CAP, idx, 32, 0x1234), "store over caller memory");
    sp_record_t *r = new_rec(), *root = new_rec();
    text_post(root, U[0].id, 1, "thread root");
    sp_compute_id(root);
    CHECK(ss_put(&s, root) == SS_ADDED && ss_put(&s, root) == SS_DUP, "dedupe by id");
    for (int i = 0; i < 5; i++) {
        sp_init(r, SP_KIND_REPLY, SP_VIS_PUBLIC, U[i % NUSERS].id, (uint64_t) (10 - i));
        sp_set_parent(r, root->id);
        char b[16];
        snprintf(b, sizeof b, "reply %d", i);
        sp_set_body(r, (const uint8_t *) b, (uint32_t) strlen(b));
        sp_compute_id(r);
        ss_put(&s, r);
    }
    const sp_record_t *kids[8];
    uint32_t nk = ss_children(&s, root->id, kids, 8);
    bool ordered = nk == 5;
    for (uint32_t i = 1; ordered && i < nk; i++) ordered = kids[i - 1]->lamport <= kids[i]->lamport;
    CHECK(ordered, "thread children come back in Lamport order");
    CHECK(ss_root(&s, kids[2]) == ss_get(&s, root->id), "ss_root walks to the thread root");

    /* eviction + index consistency against brute force */
    uint8_t ids[400][32];
    for (int i = 0; i < 400; i++) {
        char b[24];
        snprintf(b, sizeof b, "churn %d", i);
        text_post(r, U[1].id, 100 + (uint64_t) i, b);
        sp_compute_id(r);
        memcpy(ids[i], r->id, 32);
        ss_put(&s, r);
        if (rnd() % 3 == 0) ss_put(&s, r); /* dup */
    }
    bool consistent = ss_count(&s) == CAP;
    for (int i = 0; i < 400; i++) {
        bool should = i >= 400 - CAP;
        consistent &= (ss_get(&s, ids[i]) != NULL) == should;
    }
    CHECK(consistent, "after 400 inserts into 16 slots exactly the newest 16 are indexed");
    CHECK(s.evicted == 400 + 6 - CAP, "evictions counted");
    free(r), free(root);
}

static void test_gossip(void)
{
    printf("\n=== gossip reconciliation ===\n");
    enum { CAP = 64 };
    static sp_record_t sa[CAP], sb[CAP], tmpa, tmpb;
    static uint32_t ia[128], ib[128];
    static uint8_t txa[SS_MSG_MAX], rxa[SS_MSG_MAX], txb[SS_MSG_MAX], rxb[SS_MSG_MAX];
    ss_store_t A, B;
    ss_init(&A, sa, CAP, ia, 128, 0xA);
    ss_init(&B, sb, CAP, ib, 128, 0xB);
    zxn_note_t notes[8];
    zxn_bus_t bus;
    zxn_init(&bus, notes, 8);
    g_bus = &bus;
    g_me = U[1].id;
    B.on_new = on_new;

    /* A is U0, B is U1 (U1 follows U0). */
    sp_policy_t pol = {follows, in_list, NULL, NULL, NULL};
    sp_record_t *r = new_rec();
    int pubA = 0, pubB = 0;
    for (int i = 0; i < 12; i++) { /* A: 12 public posts by U0 */
        char t[32];
        snprintf(t, sizeof t, "A post %d", i);
        text_post(r, U[0].id, (uint64_t) i + 1, t);
        sp_sign(r, &U[0].signer);
        ss_put(&A, r);
        pubA++;
    }
    text_post(r, U[0].id, 50, "for my followers only");
    r->vis = SP_VIS_FOLLOWERS;
    sp_sign(r, &U[0].signer);
    ss_put(&A, r);
    text_post(r, U[3].id, 51, "U3's followers-only post that A happens to hold");
    r->vis = SP_VIS_FOLLOWERS;
    sp_sign(r, &U[3].signer);
    ss_put(&A, r);
    uint8_t u3_followers_id[32];
    memcpy(u3_followers_id, r->id, 32);
    sp_init(r, SP_KIND_DM, SP_VIS_PRIVATE, U[3].id, 52); /* U3 -> U1, carried by A */
    sp_set_audience(r, U[1].id);
    sp_set_body(r, (const uint8_t *) "\x8a\x11\x02 sealed", 9);
    sp_sign(r, &U[3].signer);
    ss_put(&A, r);
    sp_init(r, SP_KIND_DM, SP_VIS_PRIVATE, U[3].id, 53); /* U3 -> U5: not B's */
    sp_set_audience(r, U[5].id);
    sp_set_body(r, (const uint8_t *) "\x8a\x12", 2);
    sp_sign(r, &U[3].signer);
    ss_put(&A, r);
    for (int i = 0; i < 9; i++) { /* B: 9 public posts by U1, one also held by A */
        char t[32];
        snprintf(t, sizeof t, "B post %d", i);
        text_post(r, U[1].id, (uint64_t) i + 1, t);
        sp_sign(r, &U[1].signer);
        ss_put(&B, r);
        if (i == 0) ss_put(&A, r);
        pubB++;
    }

    link_t la = {&QAB, &QBA}, lb = {&QBA, &QAB};
    ss_gossip_t ga = {&A,  U[0].id, U[1].id, &pol, &V, NULL, q_send, q_recv, &la,
                      txa, rxa,     &tmpa,   0,    0,  0,    0,      0};
    ss_gossip_t gb = {&B,  U[1].id, U[0].id, &pol, &V, NULL, q_send, q_recv, &lb,
                      txb, rxb,     &tmpb,   0,    0,  0,    0,      0};
    int rounds = 0;
    for (; rounds < 10; rounds++) {
        uint32_t before = A.count + B.count;
        uint32_t ca = 0, cb = 0;
        while (ss_gossip_have(&ga, &ca) > 0) {
        }
        while (ss_gossip_have(&gb, &cb) > 0) {
        }
        for (;;) {
            uint32_t h = ss_gossip_pump(&ga, 64) + ss_gossip_pump(&gb, 64);
            if (!h) break;
        }
        if (A.count + B.count == before && rounds > 0) break;
    }
    /* convergence: everything each may share with the other is held by both */
    bool conv = true;
    for (uint32_t i = 0; i < A.count; i++) {
        const sp_record_t *x = ss_at(&A, i);
        if (sp_may_share(x, U[0].id, U[1].id, &pol)) conv &= ss_get(&B, x->id) != NULL;
    }
    for (uint32_t i = 0; i < B.count; i++) {
        const sp_record_t *x = ss_at(&B, i);
        if (sp_may_share(x, U[1].id, U[0].id, &pol)) conv &= ss_get(&A, x->id) != NULL;
    }
    ss_fingerprint_t fa, fb;
    ss_fingerprint(&A, U[0].id, U[1].id, &pol, &fa);
    fb.count = B.count; /* everything B holds */
    memset(fb.xor_ids, 0, 32);
    for (uint32_t i = 0; i < B.count; i++)
        for (int j = 0; j < 32; j++) fb.xor_ids[j] ^= ss_at(&B, i)->id[j];
    printf("       converged after %d round(s): A holds %u, B holds %u; A sent %u, B sent %u, "
           "B added %u\n",
           rounds + 1, A.count, B.count, ga.recs_sent, gb.recs_sent, gb.recs_added);
    CHECK(conv, "both peers converge on everything each may share with the other");
    CHECK(B.count == (uint32_t) (pubA + pubB + 2),
          "B got A's public posts, the follower post and its DM");
    CHECK(!ss_get(&B, u3_followers_id), "a followers-only post is not relayed by a non-author");
    bool leak = false;
    for (uint32_t i = 0; i < B.count; i++) {
        const sp_record_t *x = ss_at(&B, i);
        if (x->kind == SP_KIND_DM && memcmp(x->audience, U[1].id, 32)) leak = true;
    }
    CHECK(!leak, "someone else's DM never reaches B");
    CHECK(fa.count == fb.count && !memcmp(fa.xor_ids, fb.xor_ids, 32),
          "fingerprint of what A may share with B equals what B now holds");
    CHECK(zxn_unread(&bus, ZXN_KIND_BIT(ZXN_MESSAGE)) == 1,
          "a received DM raised one notification");
    const zxn_note_t *nl[2];
    zxn_list(&bus, nl, 2, true);
    CHECK(!strstr(nl[0]->body, "sealed") && !strstr(nl[0]->title, "sealed"),
          "the notification names the sender, never the DM content");

    /* a WANT is a request, not a right */
    uint8_t want[3 + 8] = {SS_MSG_WANT, 0, 1};
    memcpy(want + 3, u3_followers_id, 8);
    uint32_t sent0 = ga.recs_sent;
    ss_gossip_handle(&ga, want, sizeof want);
    CHECK(ga.recs_sent == sent0, "WANT for an unshareable id gets nothing");
    /* a forged REC is rejected */
    text_post(r, U[0].id, 99, "forged");
    sp_compute_id(r);
    memset(r->sig, 0x55, SP_SIG_LEN);
    txa[0] = SS_MSG_REC;
    int32_t el = sp_encode(r, txa + 1, SS_MSG_MAX - 1);
    uint32_t cnt = B.count;
    CHECK(ss_gossip_handle(&gb, txa, 1u + (uint32_t) el) == SP_ERR_SIG && B.count == cnt,
          "a REC with a bad signature is rejected and not stored");
    free(r);
}

/* ---------------- buckets ---------------- */

static void test_buckets(rep_state_t *rep)
{
    printf("\n=== update buckets: listings, ratings, sybil resistance ===\n");
    enum { CAP = 2048 };
    static sp_record_t slots[CAP];
    static uint32_t idx[4096];
    ss_store_t s;
    ss_init(&s, slots, CAP, idx, 4096, 77);
    sp_record_t *r = new_rec();
    uint8_t fpr_good[32], fpr_other[32];
    memset(fpr_good, 0xC1, 32);
    memset(fpr_other, 0xD2, 32);

    /* the target listing and 40 other listings */
    uint8_t pub[32];
    memset(pub, 0x77, 32);
    sp_init(r, SP_KIND_BUCKET_LISTING, SP_VIS_PUBLIC, pub, 1);
    CHECK(sb_make_listing(r, SB_CAT_APPS, SB_SRC_IPNS, (const uint8_t *) "k51qzi5uqu5dexample", 19,
                          (const uint8_t *) "Target app", 10,
                          (const uint8_t *) "the bucket under test", 21, fpr_other) == SP_OK,
          "listing payload builds");
    CHECK(sp_compute_id(r) == SP_OK, "listing record validates");
    ss_put(&s, r);
    uint8_t target[32];
    memcpy(target, r->id, 32);
    uint8_t other[40][32];
    for (int i = 0; i < 40; i++) {
        char t[32];
        snprintf(t, sizeof t, "Other bucket %d", i);
        sp_init(r, SP_KIND_BUCKET_LISTING, SP_VIS_PUBLIC, pub, 2 + (uint64_t) i);
        sb_make_listing(r, (sb_category_t) (i % SB_CAT_COUNT), SB_SRC_CID, (const uint8_t *) t,
                        (uint32_t) strlen(t), (const uint8_t *) t, (uint32_t) strlen(t),
                        (const uint8_t *) TOPIC_TEXT[i % 10], (uint32_t) strlen(TOPIC_TEXT[i % 10]),
                        fpr_other);
        sp_compute_id(r);
        ss_put(&s, r);
        memcpy(other[i], r->id, 32);
    }
    sb_listing_t l;
    CHECK(sb_parse_listing(ss_get(&s, target), &l) == SP_OK && l.src_kind == SB_SRC_IPNS &&
              l.title_len == 10,
          "listing parses back");

    /* 100 sybils, identical histories: 1 star on the target + the same 5 others */
    uint64_t t = 1000;
    for (int i = 0; i < 100; i++) {
        uint8_t a[32];
        memset(a, 0, 32);
        a[0] = 0xEE, a[1] = (uint8_t) i;
        int sel[6] = {-1, 0, 1, 2, 3, 4};
        for (int j = 0; j < 6; j++) {
            sp_init(r, SP_KIND_RATING, SP_VIS_PUBLIC, a, t++);
            sb_make_rating(r, sel[j] < 0 ? target : other[sel[j]], sel[j] < 0 ? 1 : 5,
                           (const uint8_t *) "bad", 3);
            sp_compute_id(r);
            ss_put(&s, r);
        }
    }
    /* 10 honest raters, independent histories: 5 stars on the target + 5 random others */
    for (int i = 0; i < 10; i++) {
        uint8_t a[32];
        memset(a, 0, 32);
        a[0] = 0x0D, a[1] = (uint8_t) i;
        badge_award(rep, sf_subject_of(a), 7, 40); /* established, earned standing */
        sp_init(r, SP_KIND_RATING, SP_VIS_PUBLIC, a, t++);
        sb_make_rating(r, target, 5, (const uint8_t *) "works well, clean release", 25);
        sp_compute_id(r);
        ss_put(&s, r);
        for (int j = 0; j < 5; j++) {
            sp_init(r, SP_KIND_RATING, SP_VIS_PUBLIC, a, t++);
            sb_make_rating(r, other[5 + (i * 5 + j * 7) % 35], (uint8_t) (1 + rnd() % 5), NULL, 0);
            sp_compute_id(r);
            ss_put(&s, r);
        }
    }
    static sb_rater_t raters[256];
    sb_config_t cfg;
    sb_config_default(&cfg);
    sb_aggregate_t ag, ag_rep;
    sb_aggregate(&s, target, &cfg, raters, 256, &ag);
    cfg.rep = sf_rep_badges;
    cfg.rep_ctx = rep;
    sb_aggregate(&s, target, &cfg, raters, 256, &ag_rep);
    printf(
        "       100 sybils (1 star, identical histories) vs 10 honest (5 stars, independent):\n");
    printf("         unweighted mean %.3f stars over %u raters\n", Q(ag.unweighted), ag.count);
    printf("         ISF only:       effective raters %.3f, weighted mean %.3f, confidence %.3f, "
           "score %.3f\n",
           Q(ag.effective), Q(ag.mean), Q(ag.confidence), Q(ag.score));
    printf("         ISF + reputation: effective %.3f, weighted mean %.3f, confidence %.3f, score "
           "%.3f\n",
           Q(ag_rep.effective), Q(ag_rep.mean), Q(ag_rep.confidence), Q(ag_rep.score));
    zt_fx w_syb = 0, w_hon = 0;
    for (uint32_t i = 0; i < ag_rep.count; i++)
        *(raters[i].rating->author[0] == 0xEE ? &w_syb : &w_hon) += raters[i].weight;
    printf("         total weight: sybil ring %.3f, honest %.3f\n", Q(w_syb), Q(w_hon));
    CHECK(ag.count == 110 && ag.unweighted < 2 * ZT_ONE, "unweighted, the sybils would win");
    CHECK(ag.mean > 4 * ZT_ONE && ag_rep.mean > 4 * ZT_ONE,
          "100 identical sybils cannot outvote 10 independent honest raters");
    CHECK(w_syb < ZT_ONE / 2 && w_hon > w_syb * 10, "the sybil ring weighs less than one rater");
    CHECK(ag_rep.score > 7 * ZT_ONE / 2, "shrunk score still favours the honest verdict");

    /* one rater, one vote: latest wins */
    uint8_t solo[32];
    memset(solo, 0x51, 32);
    sp_init(r, SP_KIND_BUCKET_LISTING, SP_VIS_PUBLIC, pub, t++);
    sb_make_listing(r, SB_CAT_GAMES, SB_SRC_CID, (const uint8_t *) "bafysolo", 8,
                    (const uint8_t *) "Solo", 4, NULL, 0, fpr_good);
    sp_compute_id(r);
    ss_put(&s, r);
    uint8_t solo_listing[32];
    memcpy(solo_listing, r->id, 32);
    for (int st = 1; st <= 4; st++) {
        sp_init(r, SP_KIND_RATING, SP_VIS_PUBLIC, solo, 5000 + (uint64_t) st);
        sb_make_rating(r, solo_listing, (uint8_t) st, NULL, 0);
        sp_compute_id(r);
        ss_put(&s, r);
    }
    cfg.rep = NULL;
    sb_aggregate(&s, solo_listing, &cfg, raters, 256, &ag);
    CHECK(ag.count == 1 && ag.mean == 4 * ZT_ONE, "one rating per (author, listing): latest wins");
    CHECK(ag.confidence < ZT_ONE / 4, "a single rater gives low confidence");
    sp_init(r, SP_KIND_RATING, SP_VIS_PUBLIC, solo, 1);
    CHECK(sb_make_rating(r, solo_listing, 6, NULL, 0) == SP_ERR_RANGE, "6 stars is refused");
    r->body[0] = 0;
    r->body_len = 1;
    sp_set_parent(r, solo_listing);
    CHECK(sp_validate(r) == SP_ERR_RULE, "0 stars fails record validation");

    /* THE RULE: ratings never grant install trust */
    const uint8_t trusted[1][32] = {{0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,
                                     0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,
                                     0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1,
                                     0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1, 0xC1}};
    CHECK(!sb_install_trusted(ss_get(&s, target), trusted, 1),
          "a well-rated listing from an untrusted publisher is NOT install-trusted");
    CHECK(sb_install_trusted(ss_get(&s, solo_listing), trusted, 1),
          "the user's own trusted-publisher list alone grants install trust");

    /* ordering: builtin default first, then score with a diversity pass */
    static sb_order_work_t ow[64];
    static sb_entry_t ent[8];
    uint32_t ne = sb_order(&s, &cfg, ow, 64, raters, 256, ent, 8);
    CHECK(ne == 8 && ent[0].store_index == SB_BUILTIN && !ent[0].listing,
          "the user's default bucket is first (builtin, not yet listed in the store)");
    sp_init(r, SP_KIND_BUCKET_LISTING, SP_VIS_PUBLIC, U[0].id, t++);
    sb_make_listing(r, SB_CAT_SYSTEM, SB_SRC_CID, (const uint8_t *) SB_DEFAULT_CID,
                    (uint32_t) strlen(SB_DEFAULT_CID), (const uint8_t *) "ZXV updates", 11,
                    (const uint8_t *) "the user's own bucket", 21, fpr_good);
    sp_compute_id(r);
    ss_put(&s, r);
    ne = sb_order(&s, &cfg, ow, 64, raters, 256, ent, 8);
    CHECK(ne == 8 && ent[0].listing && sb_parse_listing(ent[0].listing, &l) == SP_OK &&
              l.src_len == strlen(SB_DEFAULT_CID),
          "once listed, the stored default listing is pinned first");
    printf("       UI order:");
    for (uint32_t i = 0; i < ne; i++)
        printf(" [%.2f%s]", Q(ent[i].agg.score),
               ent[i].store_index == SB_BUILTIN ? " builtin" : "");
    printf("\n");
    CHECK(ent[1].agg.score >= ent[2].agg.score || ent[1].surplus > ent[2].surplus,
          "order follows score, adjusted by ISF novelty");
    free(r);
}

/* ---------------- groups and syndicates ---------------- */

#define GMAX 64
static sp_record_t *glog[GMAX];
static uint32_t gn;

static sp_record_t *gnew(sp_kind_t k, const uint8_t *author, uint64_t t)
{
    sp_record_t *r = new_rec();
    sp_init(r, k, SP_VIS_PUBLIC, author, t);
    glog[gn++] = r;
    return r;
}

static void gfin(sp_record_t *r)
{
    if (sp_compute_id(r) != SP_OK) printf("       (bad group record kind %u)\n", r->kind);
}

static uint8_t role_in(const sg_member_t *m, uint32_t n, const uint8_t *node)
{
    const sg_member_t *e = sg_find(m, n, node);
    return e ? e->role : SG_ROLE_NONE;
}

static const sg_member_t *g_roster;
static uint32_t g_roster_n;
static bool in_group_cb(void *ctx, const uint8_t *group, const uint8_t *node)
{
    (void) ctx, (void) group;
    return sg_is_member(g_roster, g_roster_n, node);
}

static void test_groups(void)
{
    printf("\n=== groups and syndicates: roster from the signed log ===\n");
    uint8_t P[10][32]; /* P0 owner, P1 admin, P2.. members */
    for (int i = 0; i < 10; i++) memset(P[i], 0xA0 + i, 32);
    gn = 0;
    sp_record_t *c = gnew(SP_KIND_GROUP_CREATE, P[0], 1);
    CHECK(sg_make_create(c, SG_KIND_GROUP, SG_JOIN_INVITE, (const uint8_t *) "Tide pool club", 14,
                         (const uint8_t *) "treasury:zxv:0042", 17, NULL, 0) == SP_OK,
          "group create builds (with a treasury id)");
    gfin(c);
    uint8_t gid[32];
    memcpy(gid, c->id, 32);
    sp_record_t *r;
    /* owner makes P1 an admin by invite */
    r = gnew(SP_KIND_GROUP_INVITE, P[0], 2), sg_make_invite(r, gid, P[1], SG_ROLE_ADMIN), gfin(r);
    r = gnew(SP_KIND_GROUP_JOIN, P[1], 3), sg_make_join(r, gid, NULL, 0), gfin(r);
    /* admin P1 invites P2, P3 (t=4); concurrent joins at the same Lamport time */
    r = gnew(SP_KIND_GROUP_INVITE, P[1], 4), sg_make_invite(r, gid, P[2], SG_ROLE_MEMBER), gfin(r);
    r = gnew(SP_KIND_GROUP_INVITE, P[1], 4), sg_make_invite(r, gid, P[3], SG_ROLE_MEMBER), gfin(r);
    r = gnew(SP_KIND_GROUP_INVITE, P[1], 4), sg_make_invite(r, gid, P[4], SG_ROLE_OBSERVER),
    gfin(r);
    r = gnew(SP_KIND_GROUP_JOIN, P[2], 5), sg_make_join(r, gid, NULL, 0), gfin(r);
    r = gnew(SP_KIND_GROUP_JOIN, P[3], 5), sg_make_join(r, gid, NULL, 0), gfin(r);
    r = gnew(SP_KIND_GROUP_JOIN, P[4], 5), sg_make_join(r, gid, NULL, 0), gfin(r);
    /* uninvited P5 tries to join an invite-only group */
    r = gnew(SP_KIND_GROUP_JOIN, P[5], 5), sg_make_join(r, gid, NULL, 0), gfin(r);
    /* admin invites P6 while still admin (t=6) */
    r = gnew(SP_KIND_GROUP_INVITE, P[1], 6), sg_make_invite(r, gid, P[6], SG_ROLE_MEMBER), gfin(r);
    /* concurrent at t=7: admin P1 removes P2, P2 leaves on its own */
    sp_record_t *kick = gnew(SP_KIND_GROUP_LEAVE, P[1], 7);
    sg_make_leave(kick, gid, P[2]), gfin(kick);
    sp_record_t *quit = gnew(SP_KIND_GROUP_LEAVE, P[2], 7);
    sg_make_leave(quit, gid, P[2]), gfin(quit);
    /* owner REVOKES the admin (t=8) */
    r = gnew(SP_KIND_GROUP_ROLE, P[0], 8), sg_make_role(r, gid, P[1], SG_ROLE_MEMBER), gfin(r);
    /* after revocation P1's actions must not count (t=9) */
    r = gnew(SP_KIND_GROUP_INVITE, P[1], 9), sg_make_invite(r, gid, P[7], SG_ROLE_MEMBER), gfin(r);
    r = gnew(SP_KIND_GROUP_LEAVE, P[1], 9), sg_make_leave(r, gid, P[3]), gfin(r);
    r = gnew(SP_KIND_GROUP_ROLE, P[1], 9), sg_make_role(r, gid, P[4], SG_ROLE_MEMBER), gfin(r);
    r = gnew(SP_KIND_GROUP_JOIN, P[7], 10), sg_make_join(r, gid, NULL, 0), gfin(r);
    /* P6's invite was issued while P1 was an admin: it still holds */
    r = gnew(SP_KIND_GROUP_JOIN, P[6], 10), sg_make_join(r, gid, NULL, 0), gfin(r);
    /* the owner cannot leave; charter set and signed; then re-set */
    r = gnew(SP_KIND_GROUP_LEAVE, P[0], 11), sg_make_leave(r, gid, P[0]), gfin(r);
    uint8_t h1[32], h2[32];
    memset(h1, 0x11, 32), memset(h2, 0x22, 32);
    r = gnew(SP_KIND_GROUP_CHARTER, P[0], 12), sg_make_charter(r, gid, SG_CHARTER_SET, h1), gfin(r);
    r = gnew(SP_KIND_GROUP_CHARTER, P[3], 13), sg_make_charter(r, gid, SG_CHARTER_SIGN, h1),
    gfin(r);
    r = gnew(SP_KIND_GROUP_CHARTER, P[6], 13), sg_make_charter(r, gid, SG_CHARTER_SIGN, h2),
    gfin(r);
    r = gnew(SP_KIND_GROUP_CHARTER, P[0], 13), sg_make_charter(r, gid, SG_CHARTER_SIGN, h1),
    gfin(r);
    /* a record that predates the group, and a record of another group */
    r = gnew(SP_KIND_GROUP_JOIN, P[8], 0), sg_make_join(r, gid, NULL, 0), gfin(r);
    uint8_t other[32];
    memset(other, 0x99, 32);
    r = gnew(SP_KIND_GROUP_ROLE, P[0], 14), sg_make_role(r, other, P[3], SG_ROLE_ADMIN), gfin(r);

    static sg_member_t m[32], m2[32];
    const sp_record_t *sorted[GMAX];
    sg_group_t g, g2;
    uint32_t ne = sg_derive((const sp_record_t *const *) glog, gn, gid, sorted, m, 32, &g);
    printf("       %u records -> %u applied, %u ignored; %u members of %u entries\n", gn, g.applied,
           g.ignored, g.n_members, ne);
    CHECK(g.exists && g.kind == SG_KIND_GROUP && g.treasury_len == 17 &&
              !memcmp(g.treasury, "treasury:zxv:0042", 17),
          "group header carries kind and treasury id");
    CHECK(role_in(m, ne, P[0]) == SG_ROLE_OWNER, "founder is the owner");
    CHECK(role_in(m, ne, P[3]) == SG_ROLE_MEMBER && role_in(m, ne, P[4]) == SG_ROLE_OBSERVER,
          "concurrent joins at one Lamport time all take their invited roles");
    CHECK(role_in(m, ne, P[5]) == SG_ROLE_NONE, "uninvited join to an invite-only group ignored");
    CHECK(role_in(m, ne, P[2]) == SG_ROLE_NONE,
          "concurrent removal and leave: P2 is out either way (one applies, one is ignored)");
    CHECK(role_in(m, ne, P[1]) == SG_ROLE_MEMBER, "the revoked admin is now a member");
    CHECK(role_in(m, ne, P[7]) == SG_ROLE_NONE,
          "an invite issued after revocation does not let anyone in");
    CHECK(role_in(m, ne, P[3]) == SG_ROLE_MEMBER && role_in(m, ne, P[4]) == SG_ROLE_OBSERVER,
          "a removal and a role change after revocation are ignored");
    CHECK(role_in(m, ne, P[6]) == SG_ROLE_MEMBER, "an invite issued while admin still holds");
    CHECK(role_in(m, ne, P[8]) == SG_ROLE_NONE, "a record older than the group is ignored");
    CHECK(g.has_charter && !memcmp(g.charter, h1, 32) && sg_charter_signers(m, ne) == 2,
          "charter: two members signed the current hash; a wrong hash does not count");
    r = gnew(SP_KIND_GROUP_CHARTER, P[0], 20), sg_make_charter(r, gid, SG_CHARTER_SET, h2), gfin(r);
    ne = sg_derive((const sp_record_t *const *) glog, gn, gid, sorted, m, 32, &g);
    CHECK(sg_charter_signers(m, ne) == 0, "a new charter clears every signature");

    /* ownership transfer */
    r = gnew(SP_KIND_GROUP_ROLE, P[0], 21), sg_make_role(r, gid, P[3], SG_ROLE_OWNER), gfin(r);
    r = gnew(SP_KIND_GROUP_ROLE, P[0], 22), sg_make_role(r, gid, P[4], SG_ROLE_ADMIN), gfin(r);
    ne = sg_derive((const sp_record_t *const *) glog, gn, gid, sorted, m, 32, &g);
    CHECK(role_in(m, ne, P[3]) == SG_ROLE_OWNER && role_in(m, ne, P[0]) == SG_ROLE_ADMIN &&
              !memcmp(g.owner, P[3], 32),
          "transfer: new owner, the old owner becomes an admin");
    CHECK(role_in(m, ne, P[4]) == SG_ROLE_OBSERVER, "the former owner can no longer grant admin");

    /* order independence: 300 shuffles give the identical roster */
    bool same = true;
    sp_record_t *sh[GMAX];
    for (int it = 0; it < 300 && same; it++) {
        memcpy(sh, glog, sizeof(sp_record_t *) * gn);
        for (uint32_t i = gn - 1; i > 0; i--) {
            uint32_t j = rnd() % (i + 1);
            sp_record_t *t = sh[i];
            sh[i] = sh[j];
            sh[j] = t;
        }
        uint32_t n2 = gn;
        if (it % 3 == 0) sh[n2++] = sh[0]; /* a duplicate delivery */
        memset(m2, 0, sizeof m2);
        uint32_t ne2 = sg_derive((const sp_record_t *const *) sh, n2, gid, sorted, m2, 32, &g2);
        same = ne2 == ne && g2.n_members == g.n_members && g2.applied == g.applied &&
               !memcmp(g2.owner, g.owner, 32);
        for (uint32_t i = 0; same && i < ne; i++)
            same = !memcmp(m[i].node, m2[i].node, 32) && m[i].role == m2[i].role &&
                   m[i].since == m2[i].since && m[i].charter_signed == m2[i].charter_signed;
    }
    CHECK(same, "order independence: 300 shuffled deliveries (with duplicates) give one roster");

    /* which of the concurrent pair applied is fixed by id, on every peer */
    ne = sg_derive((const sp_record_t *const *) glog, gn, gid, sorted, m, 32, &g);
    const sg_member_t *e2 = sg_find(m, ne, P[2]);
    CHECK(e2 && e2->since == 7, "the tie at t=7 is broken by record id, the same everywhere");

    /* the derived roster through a store */
    static sp_record_t slots[GMAX];
    static uint32_t idx[128];
    ss_store_t st;
    ss_init(&st, slots, GMAX, idx, 128, 5);
    for (uint32_t i = gn; i-- > 0;) ss_put(&st, glog[i]); /* reverse order */
    uint32_t ns = sg_derive_store(&st, gid, sorted, GMAX, m2, 32, &g2);
    same = ns == ne && g2.n_members == g.n_members;
    for (uint32_t i = 0; same && i < ne; i++) same = m[i].role == m2[i].role;
    CHECK(same, "sg_derive_store over a store matches sg_derive");

    /* group-scoped visibility */
    g_roster = m, g_roster_n = ne;
    sp_policy_t pol = {NULL, NULL, NULL, NULL, in_group_cb};
    sp_record_t *post = new_rec();
    sp_init(post, SP_KIND_POST, SP_VIS_GROUP, P[3], 30);
    sp_set_body(post, (const uint8_t *) "members only", 12);
    CHECK(sp_validate(post) == SP_ERR_RULE, "a group post needs the group id as audience");
    sp_set_audience(post, gid);
    CHECK(sp_validate(post) == SP_OK, "group post validates");
    CHECK(sp_may_share(post, P[3], P[6], &pol), "group post: shared member to member");
    CHECK(!sp_may_share(post, P[3], P[2], &pol), "group post: not to a former member");
    CHECK(!sp_may_share(post, P[5], P[6], &pol), "group post: a non-member does not relay it");
    sp_policy_t nopol = {0};
    CHECK(!sp_may_share(post, P[3], P[6], &nopol), "group post: no in_group callback -> denied");

    /* syndicates: organisations, each with an org id */
    sp_record_t *sc = new_rec();
    sp_init(sc, SP_KIND_GROUP_CREATE, SP_VIS_PUBLIC, P[0], 1);
    CHECK(sg_make_create(sc, SG_KIND_SYNDICATE, SG_JOIN_OPEN, (const uint8_t *) "Harbor co-op", 12,
                         NULL, 0, NULL, 0) == SP_ERR_RANGE,
          "a syndicate founder must give an org id");
    sg_make_create(sc, SG_KIND_SYNDICATE, SG_JOIN_OPEN, (const uint8_t *) "Harbor co-op", 12,
                   (const uint8_t *) "treasury:harbor", 15,
                   (const uint8_t *) "5493001KJTIIGC8Y1R12", 20);
    sp_compute_id(sc);
    sp_record_t *sj[3];
    for (int i = 0; i < 3; i++) {
        sj[i] = new_rec();
        sp_init(sj[i], SP_KIND_GROUP_JOIN, SP_VIS_PUBLIC, P[1 + i], 2);
        if (i < 2)
            sg_make_join(sj[i], sc->id, (const uint8_t *) "529900T8BM49AURSDO55", 20);
        else
            sg_make_join(sj[i], sc->id, NULL, 0); /* no org id */
        sp_compute_id(sj[i]);
    }
    const sp_record_t *slog[4] = {sc, sj[0], sj[1], sj[2]};
    ne = sg_derive(slog, 4, sc->id, sorted, m2, 32, &g2);
    const sg_member_t *org = sg_find(m2, ne, P[1]);
    CHECK(g2.kind == SG_KIND_SYNDICATE && g2.n_members == 3 && org && org->org_len == 20 &&
              role_in(m2, ne, P[3]) == SG_ROLE_NONE,
          "syndicate: open join for organisations with an org id; without one, refused");
    free(sc), free(post);
    for (int i = 0; i < 3; i++) free(sj[i]);
    for (uint32_t i = 0; i < gn; i++) free(glog[i]);
}

int main(void)
{
    printf("=== ZXV social: records, ISF feed, store, gossip, buckets ===\n");
    make_users();
    static rep_state_t rep;
    rep_init(&rep);
    test_records();
    test_fuzz();
    test_visibility();
    test_feed(&rep);
    test_store();
    test_gossip();
    test_buckets(&rep);
    test_groups();
    printf("\n%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
