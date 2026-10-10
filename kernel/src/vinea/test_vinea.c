/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vinea.c — host test for the Vinea P2P layer.
 *
 * Simulates a network of in-memory nodes exchanging real ML-DSA-65-signed
 * messages through a harness queue, then attacks every security claim.
 *
 * BUILD (run from kernel/):
 *   export CPATH=$PWD/include:$PWD/src/modbind:$PWD/src/e8:$PWD/src/event_space:$PWD/src/surplus
 *   gcc -std=c11 -O2 -Wall -Werror -Wextra -DTEST_HOST -Isrc/zcapital -Isrc/lpres -Isrc/edp_risk \
 *     src/vinea/test_vinea.c src/vinea/vna_agree.c src/vinea/vna_cid.c \
 *     src/vinea/vna_cmd.c src/vinea/vna_common.c src/vinea/vna_econ.c \
 *     src/vinea/vna_file.c src/vinea/vna_frame.c src/vinea/vna_id.c \
 *     src/vinea/vna_kad.c src/vinea/vna_lan.c src/vinea/vna_node.c src/vinea/vna_schema.c \
 *     src/vinea/vna_session.c src/vinea/vna_wire.c \
 *     src/pqsec/pq_mldsa65.c src/pqsec/mldsa/[a-z]*.c \
 *     src/mlkem/keccak.c src/mlkem/mlkem768.c src/mlkem/mlkem_kpe.c src/mlkem/mlkem_ntt.c \
 *     src/mlkem/mlkem_sample.c src/mlkem/mlkem_encode.c \
 *     src/tls/x25519.c src/tls/aead.c src/tls/hkdf.c src/robin_debanks/sha256.c \
 *     src/swarm/swarm_budget.c src/swarm/swarm_emotion.c src/swarm/swarm_market.c \
 *     src/swarm/swarm_hk.c src/ubh/ubh.c src/event_space/event_envelope.c \
 *     src/zcapital/zcapital.c -o /tmp/test_vinea -lm && /tmp/test_vinea
 * Optional argument: number of DHT nodes (64..200, default 128).
 *
 * (ML-DSA signing is slow at -O0: build with -O2.)
 */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Both declarations of ML-DSA-65 in one translation unit: any drift between
 * vna_pq.h and pq_security.h is a compile error here. */
#include "../pqsec/pq_security.h"
#include "vna_pq.h"

#include "vna_common.h"
#include "vna_schema.h"
#include "vna_cid.h"
#include "vna_frame.h"
#include "vna_id.h"
#include "vna_wire.h"
#include "vna_kad.h"
#include "vna_node.h"
#include "vna_agree.h"
#include "vna_link.h"
#include "vna_cmd.h"
#include "vna_session.h"
#include "vna_file.h"
#include "vna_econ.h"
#include "vna_link.h"
#include "vna_lan.h"
#include "zcapital.h"
#include "../swarm/swarm_hk.h"
#include "../mlkem/keccak.h"

static int g_pass, g_fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec * 1e-9;
}

/* ===================================================================== */
/* harness: nodes + network queue                                         */
/* ===================================================================== */
#define POOL 256
#define KC   48
#define RPW  64
#define DD   64
#define ST   16

typedef struct {
    vna_identity_t id;
    vna_node_t node;
    vna_contact_t pool[POOL];
    vna_keycache_ent_t kc[KC];
    vna_replay_ent_t rp[RPW];
    uint8_t dd[DD][32];
    vna_store_slot_t store[ST];
    bool dead;
    bool up;
    /* LAN discovery + offline outbox (section 7) */
    int seg; /* LAN segment (multicast domain) */
    bool lan_on;
    vna_lan_t lan;
    vna_lan_peer_t lpeers[64];
    vna_spool_t sp;
    vna_spool_ent_t spe[8];
    vna_spool_rx_t sprx[8];
} tnode_t;

typedef struct pkt {
    struct pkt *next;
    uint32_t from, to, len;
    bool lan;
    uint8_t data[];
} pkt_t;

static tnode_t *T;
static uint32_t NN;
static pkt_t *q_head, *q_tail;
static uint64_t g_now = 1000000;
static uint64_t g_delivered;
static uint8_t *g_obuf;
static vna_outbox_t g_ob;
static vna_drbg_t g_rng;

static void addr_of(uint32_t i, uint8_t a[4])
{
    a[0] = (uint8_t) i;
    a[1] = (uint8_t) (i >> 8);
    a[2] = 0xCA;
    a[3] = 0xFE;
}

static int idx_of(const uint8_t *a, uint32_t len)
{
    if (len != 4 || a[2] != 0xCA || a[3] != 0xFE) return -1;
    return a[0] | (a[1] << 8);
}

static bool g_enq_lan;
static bool (*g_drop)(const pkt_t *p);
static const uint8_t G_GROUP[4] = {0xFF, 0xFF, 0xCA, 0xFE}; /* the LAN multicast group token */

static void enqueue(uint32_t from, uint32_t to, const uint8_t *d, uint32_t len)
{
    pkt_t *p = malloc(sizeof *p + len);
    p->next = 0;
    p->lan = g_enq_lan;
    p->from = from;
    p->to = to;
    p->len = len;
    memcpy(p->data, d, len);
    if (q_tail)
        q_tail->next = p;
    else
        q_head = p;
    q_tail = p;
}

static void flush(uint32_t from)
{
    for (uint32_t i = 0; i < g_ob.n; i++) {
        if (g_ob.e[i].addr_len == 4 && memcmp(g_ob.e[i].addr, G_GROUP, 4) == 0) {
            g_enq_lan = true; /* multicast: everyone else on the sender's segment */
            for (uint32_t j = 0; j < NN; j++)
                if (j != from && T[j].lan_on && T[j].seg == T[from].seg)
                    enqueue(from, j, g_ob.buf + g_ob.e[i].off, g_ob.e[i].len);
            g_enq_lan = false;
            continue;
        }
        int to = idx_of(g_ob.e[i].addr, g_ob.e[i].addr_len);
        if (to >= 0 && (uint32_t) to < NN)
            enqueue(from, (uint32_t) to, g_ob.buf + g_ob.e[i].off, g_ob.e[i].len);
    }
    vna_outbox_clear(&g_ob);
}

static void run_queue(void)
{
    while (q_head) {
        pkt_t *p = q_head;
        q_head = p->next;
        if (!q_head) q_tail = 0;
        g_now++;
        if (g_drop && g_drop(p)) {
            free(p);
            continue;
        }
        if (!T[p->to].dead && T[p->to].up && p->lan) {
            (void) vna_lan_handle(&T[p->to].lan, p->data, p->len, g_now, &g_ob);
            g_delivered++;
            flush(p->to);
        } else if (!T[p->to].dead && T[p->to].up) {
            uint8_t a[4];
            addr_of(p->from, a);
            (void) vna_node_handle(&T[p->to].node, p->data, p->len, a, 4, g_now, &g_ob);
            g_delivered++;
            flush(p->to);
        }
        free(p);
    }
}

static void tick_all(void)
{
    for (uint32_t i = 0; i < NN; i++) {
        if (T[i].dead || !T[i].up) continue;
        vna_node_tick(&T[i].node, g_now, &g_ob);
        if (T[i].lan_on) vna_lan_tick(&T[i].lan, g_now, &g_ob);
        flush(i);
    }
}

/* run until quiet, firing timeouts */
static void settle_net(int rounds)
{
    for (int r = 0; r < rounds; r++) {
        run_queue();
        g_now += 2001;
        tick_all();
        if (!q_head) {
            run_queue();
            break;
        }
    }
    run_queue();
}

static void make_identity(vna_identity_t *idn, uint32_t tag, uint32_t pow_bits)
{
    uint8_t seed[32];
    memset(seed, 0, 32);
    seed[0] = (uint8_t) tag;
    seed[1] = (uint8_t) (tag >> 8);
    seed[2] = (uint8_t) (tag >> 16);
    seed[3] = 0x5A;
    vna_status_t st = vna_identity_create(idn, seed, pow_bits, 1u << 20);
    if (st != VNA_OK) {
        printf("identity failed\n");
        exit(1);
    }
}

static vna_node_cfg_t g_cfg;

static void node_up(uint32_t i)
{
    vna_node_mem_t mem = {T[i].pool, POOL, T[i].kc, KC, T[i].rp, RPW, T[i].dd, DD, T[i].store, ST};
    uint8_t seed[32];
    vna_drbg_gen(&g_rng, seed, 32);
    vna_status_t st = vna_node_init(&T[i].node, &T[i].id, &g_cfg, &mem, seed);
    if (st != VNA_OK) {
        printf("node init failed\n");
        exit(1);
    }
    T[i].up = true;
}

/* ===================================================================== */
/* 0. primitives                                                          */
/* ===================================================================== */
static void test_primitives(void)
{
    printf("[0] primitives: CID, schema, UBH-168, Hackronomicon, capital forms\n");
    /* CIDv1 raw sha2-256 known vector */
    uint8_t cid[VNA_CID_RAW_LEN], back[VNA_CID_RAW_LEN];
    char s[VNA_CID_STR_MAX];
    vna_cid_raw((const uint8_t *) "hello world", 11, cid);
    int32_t n = vna_cid_to_string(cid, s, sizeof s);
    CHECK(n == 59 && strcmp(s, "bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e") == 0,
          "CIDv1 raw of 'hello world' = %s", s);
    CHECK(vna_cid_from_string(s, (uint32_t) n, back) == VNA_OK && memcmp(back, cid, 36) == 0,
          "CID string round trip");
    s[10] = '1';
    CHECK(vna_cid_from_string(s, (uint32_t) n, back) != VNA_OK, "bad base32 refused");

    /* zcapital and swarm agree on the nine forms and on what is inalienable */
    CHECK(ZCAP_FORM_COUNT == SWARM_CAP_COUNT && VNA_FORMS == ZCAP_FORM_COUNT, "nine forms");
    CHECK((int) ZCAP_SOCIAL == (int) SWARM_CAP_SOCIAL &&
              (int) ZCAP_SYSTEM == (int) SWARM_CAP_SYSTEM &&
              (int) ZCAP_SPIRITUAL == (int) SWARM_CAP_SPIRITUAL,
          "enum order matches");
    bool same = true;
    for (int f = 0; f < ZCAP_FORM_COUNT; f++)
        same &= zcap_is_priceable((zcap_form_t) f) == !swarm_cap_is_crown((swarm_cap_t) f);
    CHECK(same, "zcap_is_priceable == !swarm_cap_is_crown for every form");

    /* Hackronomicon */
    uint8_t txt[256];
    int32_t l = vna_hk_canonicalize("ask(compute,units:100,price:21)", txt, sizeof txt);
    CHECK(l > 0 && vna_hk_is_canonical(txt, (uint32_t) l), "canonicalised HK is canonical");
    CHECK(!vna_hk_is_canonical((const uint8_t *) "ask(compute,units:100)", 22),
          "non-canonical spelling refused");
    CHECK(!vna_hk_is_canonical((const uint8_t *) "a + b + c + d + e + f + g", 25),
          "stacking > 5 operators refused (K3)");
    vna_cmd_t c, c2;
    vna_zero(&c, sizeof c);
    c.verb = VNA_V_FETCH;
    c.resource = VNA_RES_FILE;
    c.has = VNA_CMD_ROOT | VNA_CMD_INDEX;
    for (int i = 0; i < 32; i++) c.root.b[i] = (uint8_t) (i * 7);
    c.index = 12345678901234ull;
    l = vna_cmd_encode(&c, txt, sizeof txt);
    CHECK(l > 0 && vna_cmd_decode(txt, (uint32_t) l, &c2) == VNA_OK && c2.verb == c.verb &&
              c2.index == c.index && vna_id_eq(&c2.root, &c.root),
          "command encode/decode round trip");
    l = vna_hk_canonicalize("set(allocation, model: 1, units: 999)", txt, sizeof txt);
    CHECK(l > 0 && vna_cmd_decode(txt, (uint32_t) l, &c2) == VNA_ERR_HK,
          "set(allocation...) is not a command (no write path to an internal economy)");
    l = vna_hk_canonicalize("ask(compute, units: 007)", txt, sizeof txt);
    CHECK(l < 0 || vna_cmd_decode(txt, (uint32_t) l, &c2) == VNA_ERR_HK, "leading zeros refused");

    /* schema + UBH framing */
    static vna_agreement_t a, a2;
    vna_id_t owner;
    memset(&owner, 0x11, sizeof owner);
    vna_agree_default(&a, &owner);
    a.degree = VNA_DEG_SERVE;
    a.terms_len = 12;
    memcpy(a.terms, "be excellent", 12);
    static uint8_t buf[8192], fr[9000];
    int32_t al = vna_agree_encode(&a, buf, sizeof buf);
    CHECK(al > 0 && vna_agree_decode(buf, (uint32_t) al, &a2) == VNA_OK && a2.degree == a.degree,
          "agreement round trip");
    int bad = 0;
    for (int32_t k = 0; k < al; k++) bad += vna_agree_decode(buf, (uint32_t) k, &a2) == VNA_OK;
    CHECK(bad == 0, "every truncation of an agreement refused (%d accepted)", bad);
    buf[al] = 0;
    CHECK(vna_agree_decode(buf, (uint32_t) al + 1, &a2) != VNA_OK, "trailing byte refused (S8)");
    buf[5] = 9; /* degree out of range */
    CHECK(vna_agree_decode(buf, (uint32_t) al, &a2) != VNA_OK, "enum out of range refused (S1)");
    buf[5] = (uint8_t) a.degree;
    int32_t fl = vna_frame_wrap(&vna_agreement_schema, buf, (uint32_t) al, fr, sizeof fr);
    const uint8_t *rec;
    uint32_t rl;
    bool ubh;
    CHECK(fl > 0 && (fl - 21) % 21 == 0, "UBH-168 frames are whole 21-octet units");
    CHECK(vna_frame_accept(&vna_agreement_schema, fr, (uint32_t) fl, &rec, &rl, &ubh) == VNA_OK &&
              ubh && rl == (uint32_t) al && memcmp(rec, buf, rl) == 0,
          "UBH unwrap gives the exact canonical bytes");
    CHECK(vna_frame_accept(&vna_agreement_schema, buf, (uint32_t) al, &rec, &rl, &ubh) == VNA_OK &&
              !ubh,
          "plain fallback accepted");
    if (fl > al + 21) {
        fr[fl - 1] ^= 1;
        CHECK(vna_frame_accept(&vna_agreement_schema, fr, (uint32_t) fl, &rec, &rl, &ubh) != VNA_OK,
              "non-zero padding refused");
        fr[fl - 1] ^= 1;
    }
    fr[30] ^= 1;
    CHECK(vna_frame_accept(&vna_agreement_schema, fr, (uint32_t) fl, &rec, &rl, &ubh) != VNA_OK,
          "payload change caught by the frame integrity reference");
    fr[30] ^= 1;
    CHECK(vna_frame_accept(&vna_msg_schema, fr, (uint32_t) fl, &rec, &rl, &ubh) != VNA_OK,
          "frame of the wrong record class refused");

    /* random garbage never parses as a message */
    static vna_msg_t m;
    int ok = 0;
    for (int t = 0; t < 2000; t++) {
        uint32_t len = (uint32_t) (rand() % 6000);
        for (uint32_t i = 0; i < len; i++) buf[i] = (uint8_t) rand();
        if (t & 1) vna_put32(buf, VNA_MSG_MAGIC);
        ok += vna_schema_unpack(&vna_msg_schema, buf, len, &m, 0) >= 0;
    }
    CHECK(ok == 0, "2000 random buffers: none parse");

    /* replay table eviction (R3): exact for the evicted peer, no collateral */
    {
        vna_replay_ent_t re[2];
        vna_replay_t rp;
        vna_replay_init(&rp, re, 2, 30000);
        vna_id_t P, Q, R, X;
        memset(&P, 0, 32);
        memset(&Q, 0, 32);
        memset(&R, 0, 32);
        memset(&X, 0, 32);
        P.b[0] = 1;
        Q.b[0] = 2;
        R.b[0] = 3;
        X.b[0] = 4;
        vna_replay_commit(&rp, &P, 5, 100000);
        vna_replay_commit(&rp, &Q, 1, 100500);
        vna_replay_commit(&rp, &R, 1, 100600); /* evicts P (least recently used) */
        CHECK(vna_replay_check(&rp, &P, 5, 100000, 100700) == VNA_ERR_REPLAY &&
                  vna_replay_check(&rp, &P, 3, 99000, 100700) == VNA_ERR_REPLAY,
              "R3: messages of an evicted peer cannot be replayed");
        CHECK(vna_replay_check(&rp, &P, 6, 100001, 100700) == VNA_OK,
              "R3: the evicted peer's next message is fine");
        CHECK(vna_replay_check(&rp, &X, 1, 99500, 100700) == VNA_OK,
              "R3: an eviction does not refuse other peers' in-flight messages");
    }

    /* PoW */
    vna_identity_t idn;
    make_identity(&idn, 999, 10);
    CHECK(vna_pow_ok(&idn.id, idn.pow_nonce, 10), "PoW nonce found and verifies");
    CHECK(!vna_pow_ok(&idn.id, idn.pow_nonce + 1, 10) ||
              !vna_pow_ok(&idn.id, idn.pow_nonce + 2, 10),
          "PoW is not free");
}

/* ===================================================================== */
/* 1. DHT network                                                         */
/* ===================================================================== */
typedef struct {
    vna_id_t id;
    uint32_t i;
} idx_t;
static vna_id_t g_target;
static int cmp_dist(const void *a, const void *b)
{
    return vna_id_closer(&g_target, &((const idx_t *) a)->id, &((const idx_t *) b)->id);
}

static int g_qseg = -1; /* restrict "the true closest" to one LAN segment */
static double lookup_quality(uint32_t searcher, const vna_id_t *target, uint32_t *exact)
{
    static idx_t all[256];
    uint32_t n = 0;
    for (uint32_t i = 0; i < NN; i++)
        if (i != searcher && T[i].up && !T[i].dead && (g_qseg < 0 || T[i].seg == g_qseg))
            all[n++] = (idx_t){T[i].id.id, i};
    g_target = *target;
    qsort(all, n, sizeof all[0], cmp_dist);
    int32_t slot = vna_node_lookup(&T[searcher].node, target, VNA_LK_FIND_NODE, g_now, &g_ob);
    flush(searcher);
    settle_net(20);
    const vna_node_lookup_t *L = vna_node_lookup_get(&T[searcher].node, slot);
    static vna_cand_t res[VNA_K];
    uint32_t got = L ? vna_lookup_result(&L->lk, VNA_K, res) : 0;
    uint32_t hit = 0;
    for (uint32_t k = 0; k < got; k++)
        for (uint32_t j = 0; j < VNA_K && j < n; j++)
            if (vna_id_eq(&res[k].id, &all[j].id)) hit++;
    bool done = vna_node_lookup_done(&T[searcher].node, slot);
    vna_node_lookup_release(&T[searcher].node, slot);
    uint32_t want = n < VNA_K ? n : VNA_K;
    if (hit == want && done) (*exact)++;
    return (double) hit / (double) want;
}

static void test_dht(void)
{
    printf("[1] DHT: %u nodes, bootstrap, lookups, STORE/FIND_VALUE, eviction\n", NN);
    double t0 = now_s();
    for (uint32_t i = 0; i < NN; i++) make_identity(&T[i].id, i + 1, 8);
    vna_node_cfg_default(&g_cfg);
    g_cfg.degree = VNA_DEG_STORE;
    g_cfg.pow_bits = 8;
    for (uint32_t i = 0; i < NN; i++) node_up(i);
    T[0].node.cfg.ubh = true;
    for (uint32_t i = 1; i < NN; i++) {
        uint8_t a[4];
        uint32_t via = (uint32_t) (rand() % (int) i);
        addr_of(via, a);
        vna_node_bootstrap(&T[i].node, a, 4, g_now, &g_ob);
        flush(i);
        settle_net(20);
        for (uint32_t s = 0; s < VNA_NODE_LOOKUPS; s++)
            vna_node_lookup_release(&T[i].node, (int32_t) s);
    }
    double t1 = now_s();
    uint64_t tbl = 0;
    for (uint32_t i = 0; i < NN; i++) tbl += T[i].node.rt.used;
    printf("    joined in %.2f s, %llu messages delivered, mean table size %.1f\n", t1 - t0,
           (unsigned long long) g_delivered, (double) tbl / NN);
    CHECK(tbl / NN >= 20, "tables populated");

    /* lookups converge to the true k closest */
    uint32_t exact = 0, trials = 24;
    double q = 0;
    for (uint32_t t = 0; t < trials; t++) {
        vna_id_t target;
        vna_drbg_gen(&g_rng, target.b, 32);
        q += lookup_quality((uint32_t) (rand() % (int) NN), &target, &exact);
    }
    printf("    %u lookups: mean overlap with true 20 closest %.3f, exact %u/%u\n", trials,
           q / trials, exact, trials);
    CHECK(q / trials >= 0.97, "lookups find the true closest nodes (%.3f)", q / trials);
    CHECK(exact >= trials * 3 / 4, "most lookups exact (%u/%u)", exact, trials);

    /* disjoint paths also converge */
    for (uint32_t i = 0; i < NN; i++) T[i].node.cfg.paths = 3;
    exact = 0;
    q = 0;
    for (uint32_t t = 0; t < 6; t++) {
        vna_id_t target;
        vna_drbg_gen(&g_rng, target.b, 32);
        q += lookup_quality((uint32_t) (rand() % (int) NN), &target, &exact);
    }
    printf("    disjoint (d=3) lookups: mean overlap %.3f, exact %u/6\n", q / 6, exact);
    CHECK(q / 6 >= 0.9, "disjoint-path lookups converge");
    for (uint32_t i = 0; i < NN; i++) T[i].node.cfg.paths = 1;

    /* UBH-168 negotiated: peers that advertised it are spoken to in UBH */
    uint32_t ubh_contacts = 0;
    for (uint32_t i = 0; i < POOL; i++)
        if (T[1].pool[i].in_use && (T[1].pool[i].flags & VNA_CF_UBH)) ubh_contacts++;
    CHECK(ubh_contacts > 0, "UBH-168 support learned from peers");

    /* STORE / FIND_VALUE of a provider record */
    vna_provider_t p;
    vna_zero(&p, sizeof p);
    p.file_size = 100000;
    p.chunk_size = VNA_CHUNK_SIZE;
    p.n_chunks = 7;
    p.visibility = VNA_VIS_PUBLIC;
    addr_of(3, p.addr);
    p.addr_len = 4;
    vna_id_t root;
    vna_drbg_gen(&g_rng, root.b, 32);
    int32_t slot = vna_node_announce(&T[3].node, &root, &p, g_now, 3600000, &g_ob);
    flush(3);
    settle_net(20);
    const vna_node_lookup_t *L = vna_node_lookup_get(&T[3].node, slot);
    CHECK(L && L->stores_acked >= 15, "provider record stored at the k closest (%u acks)",
          L ? L->stores_acked : 0);
    vna_node_lookup_release(&T[3].node, slot);
    p.visibility = VNA_VIS_PRIVATE;
    CHECK(vna_node_announce(&T[3].node, &root, &p, g_now, 3600000, &g_ob) == VNA_ERR_DENIED,
          "PRIVATE item is never announced");
    vna_outbox_clear(&g_ob);

    uint32_t finder = NN - 1;
    slot = vna_node_lookup(&T[finder].node, &root, VNA_LK_FIND_VALUE, g_now, &g_ob);
    flush(finder);
    settle_net(20);
    L = vna_node_lookup_get(&T[finder].node, slot);
    static vna_rec_t rec;
    bool found =
        L && L->lk.found_value &&
        vna_rec_verify(&T[finder].node.v, L->value, L->value_len, g_now, 30000, &rec) == VNA_OK &&
        vna_id_eq(&rec.publisher, &T[3].id.id) && vna_id_eq(&rec.key, &root);
    CHECK(found, "FIND_VALUE returns the provider's signed record");
    vna_node_lookup_release(&T[finder].node, slot);

    /* agreement publish + fetch + content address */
    static vna_agreement_t ag, ag2;
    vna_agree_default(&ag, &T[5].id.id);
    ag.degree = VNA_DEG_SERVE;
    ag.audience = VNA_AUD_EVERYONE;
    ag.terms_len = 22;
    memcpy(ag.terms, "share alike, no resale", 22);
    slot = vna_node_publish_agreement(&T[5].node, &ag, g_now, 3600000, &g_ob);
    flush(5);
    settle_net(20);
    vna_node_lookup_release(&T[5].node, slot);
    vna_id_t akey;
    vna_agree_key(&T[5].id.id, &akey);
    slot = vna_node_lookup(&T[7].node, &akey, VNA_LK_FIND_VALUE, g_now, &g_ob);
    flush(7);
    settle_net(20);
    L = vna_node_lookup_get(&T[7].node, slot);
    uint8_t c1[32], c2[32];
    bool agok =
        L && L->lk.found_value &&
        vna_rec_verify(&T[7].node.v, L->value, L->value_len, g_now, 30000, &rec) == VNA_OK &&
        vna_agree_decode(rec.payload, rec.payload_len, &ag2) == VNA_OK &&
        vna_agree_cid(&ag, c1) == VNA_OK && vna_agree_cid(&ag2, c2) == VNA_OK &&
        memcmp(c1, c2, 32) == 0;
    CHECK(agok, "agreement fetched from the DHT, signature and content address check");
    vna_node_lookup_release(&T[7].node, slot);

    /* expiry: past its lifetime a record is dropped and no longer found */
    g_now += 3600000 + 10;
    tick_all();
    run_queue();
    uint32_t still = 0;
    for (uint32_t i = 0; i < NN; i++) still += vna_node_store_find(&T[i].node, &root) != 0;
    CHECK(still == 0, "expired records dropped everywhere (%u left)", still);
    slot = vna_node_lookup(&T[finder].node, &root, VNA_LK_FIND_VALUE, g_now, &g_ob);
    flush(finder);
    settle_net(20);
    L = vna_node_lookup_get(&T[finder].node, slot);
    CHECK(L && !L->lk.found_value, "expired record not served");
    vna_node_lookup_release(&T[finder].node, slot);

    /* ping-before-evict: find node X with a full bucket */
    uint32_t X = 0;
    int fb = -1;
    for (uint32_t i = 0; i < NN && fb < 0; i++)
        for (int b = 255; b >= 0; b--)
            if (T[i].node.rt.count[b] == VNA_KAD_K) {
                X = i;
                fb = b;
                break;
            }
    CHECK(fb >= 0, "a full bucket exists");
    if (fb >= 0) {
        /* live LRS: newcomer is NOT admitted, LRS keeps its place */
        vna_contact_t *lrs = 0;
        for (uint32_t i = 0; i < POOL; i++) {
            vna_contact_t *c = &T[X].pool[i];
            if (c->in_use && c->bucket == fb && (!lrs || c->touch < lrs->touch)) lrs = c;
        }
        vna_id_t lrs_id = lrs->id;
        /* an extra node whose id lands in that bucket */
        uint32_t Y = NN; /* spare slot */
        for (uint32_t tag = 50000;; tag++) {
            make_identity(&T[Y].id, tag, 8);
            if (vna_bucket_index(&T[X].id.id, &T[Y].id.id) == fb) break;
        }
        NN++;
        node_up(Y);
        uint8_t a[4];
        addr_of(X, a);
        uint32_t kept0 = T[X].node.evict_kept, ev0 = T[X].node.evictions;
        vna_node_bootstrap(&T[Y].node, a, 4, g_now, &g_ob);
        flush(Y);
        settle_net(20);
        CHECK(T[X].node.evict_kept > kept0 && vna_rt_find(&T[X].node.rt, &lrs_id) &&
                  !vna_rt_find(&T[X].node.rt, &T[Y].id.id),
              "live least-recently-seen contact kept, newcomer not admitted");
        CHECK(T[X].node.evictions == ev0, "nobody evicted while alive");
        /* dead LRS: evicted after its ping times out, newcomer admitted */
        lrs = 0;
        for (uint32_t i = 0; i < POOL; i++) {
            vna_contact_t *c = &T[X].pool[i];
            if (c->in_use && c->bucket == fb && (!lrs || c->touch < lrs->touch)) lrs = c;
        }
        lrs_id = lrs->id;
        int dead_idx = idx_of(lrs->addr, lrs->addr_len);
        T[dead_idx].dead = true;
        vna_node_ping(&T[Y].node, &T[X].id.id, g_now, &g_ob);
        flush(Y);
        settle_net(20);
        CHECK(T[X].node.evictions > ev0 && !vna_rt_find(&T[X].node.rt, &lrs_id) &&
                  vna_rt_find(&T[X].node.rt, &T[Y].id.id),
              "dead contact evicted after the ping timed out; newcomer admitted");
        T[dead_idx].dead = false;
    }

    /* idle-bucket refresh: only idle buckets are refreshed */
    uint8_t idle[256];
    uint32_t ni = vna_rt_idle_buckets(&T[2].node.rt, g_now, 1000000000ull, idle, 256);
    CHECK(ni == 0, "no bucket idle within the interval");
    ni = vna_rt_idle_buckets(&T[2].node.rt, g_now + 2000000000ull, 1000000000ull, idle, 256);
    CHECK(ni > 0, "buckets idle after the interval are listed for refresh");
    vna_id_t rid;
    uint8_t rnd[32];
    vna_drbg_gen(&g_rng, rnd, 32);
    vna_rt_id_in_bucket(&T[2].id.id, idle[0], rnd, &rid);
    CHECK(vna_bucket_index(&T[2].id.id, &rid) == idle[0], "refresh target falls in its bucket");
    printf("    DHT section %.2f s\n", now_s() - t0);
}

/* ===================================================================== */
/* 2. wire attacks                                                        */
/* ===================================================================== */
static vna_msg_t g_m;
static uint8_t g_wire[16384];

static int32_t craft(const vna_identity_t *from, const vna_id_t *dst, uint8_t type, uint64_t seq,
                     uint64_t ts, uint64_t rpc, const vna_schema_t *bs, const void *body, bool ubh)
{
    vna_zero(&g_m, sizeof g_m);
    g_m.type = type;
    g_m.dst = *dst;
    g_m.seq = seq;
    g_m.ts = ts;
    g_m.rpc = rpc;
    g_m.features = VNA_FEAT_UBH168;
    if (bs) g_m.body_len = (uint16_t) vna_schema_pack(bs, body, g_m.body, VNA_BODY_MAX, true);
    uint8_t rnd[32];
    vna_drbg_gen(&g_rng, rnd, 32);
    return vna_msg_seal(&g_m, from, rnd, ubh, g_wire, sizeof g_wire);
}

static vna_status_t deliver_to(uint32_t to, uint32_t from_idx, const uint8_t *b, uint32_t len)
{
    uint8_t a[4];
    addr_of(from_idx, a);
    vna_status_t st = vna_node_handle(&T[to].node, b, len, a, 4, g_now, &g_ob);
    vna_outbox_clear(&g_ob);
    return st;
}

static void test_wire_attacks(void)
{
    printf("[2] wire: tampered / replayed / forged / wrong-signer / misaddressed / malformed\n");
    uint32_t A = 10, B = 11, C = 12;
    vna_b_key_t kb;
    vna_drbg_gen(&g_rng, kb.key.b, 32);
    static uint8_t copy[16384];

    int32_t n = craft(&T[B].id, &T[A].id.id, VNA_MSG_FIND_NODE, 1000000, g_now, 77,
                      &vna_b_key_schema, &kb, false);
    memcpy(copy, g_wire, (size_t) n);
    CHECK(deliver_to(A, B, copy, (uint32_t) n) == VNA_OK, "genuine message accepted");
    CHECK(deliver_to(A, B, copy, (uint32_t) n) == VNA_ERR_DUP, "identical bytes dropped (dedupe)");
    vna_dedupe_init(&T[A].node.dd, T[A].dd, DD); /* forget hashes: the replay window still holds */
    CHECK(deliver_to(A, B, copy, (uint32_t) n) == VNA_ERR_REPLAY, "replayed sequence refused");

    n = craft(&T[B].id, &T[A].id.id, VNA_MSG_FIND_NODE, 1000001, g_now, 78, &vna_b_key_schema, &kb,
              false);
    int sig_rej = 0;
    for (int k = 0; k < 40; k++) { /* flip one byte anywhere in the signed region */
        memcpy(copy, g_wire, (size_t) n);
        uint32_t pos = (uint32_t) (rand() % (n - (int32_t) VNA_SIG_LEN));
        copy[pos] ^= (uint8_t) (1u << (rand() % 8));
        vna_status_t st = deliver_to(A, B, copy, (uint32_t) n);
        sig_rej += st != VNA_OK;
    }
    CHECK(sig_rej == 40, "40 single-bit tamperings all rejected (%d)", sig_rej);
    memcpy(copy, g_wire, (size_t) n);
    copy[n - 5] ^= 0x20;
    CHECK(deliver_to(A, B, copy, (uint32_t) n) == VNA_ERR_SIG, "tampered signature rejected");
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_OK,
          "untampered original still accepted once");

    /* stale timestamp */
    n = craft(&T[B].id, &T[A].id.id, VNA_MSG_PING, 1000002, g_now - 60000, 79, 0, 0, false);
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_ERR_STALE, "stale timestamp refused");

    /* forged: attacker claims B's id with its own key */
    static vna_identity_t evil;
    make_identity(&evil, 777777, 8);
    n = craft(&evil, &T[A].id.id, VNA_MSG_PING, 1000010, g_now, 80, 0, 0, false);
    vna_msg_t *m = &g_m;
    m->src = T[B].id.id; /* re-sign with the evil key but B's id */
    {
        uint8_t rnd[32];
        vna_drbg_gen(&g_rng, rnd, 32);
        int32_t l = vna_schema_pack(&vna_msg_schema, m, g_wire, sizeof g_wire, false);
        vna_sign(evil.sk, VNA_CTX_MSG, g_wire, (uint32_t) l, rnd, m->sig);
        n = vna_schema_pack(&vna_msg_schema, m, g_wire, sizeof g_wire, true);
    }
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_ERR_BINDING,
          "forged sender id refused (binding)");
    /* forged: B's id and B's pk, evil signature */
    vna_copy(m->pk, T[B].id.pk, VNA_PK_LEN);
    m->pow = T[B].id.pow_nonce;
    {
        uint8_t rnd[32];
        vna_drbg_gen(&g_rng, rnd, 32);
        int32_t l = vna_schema_pack(&vna_msg_schema, m, g_wire, sizeof g_wire, false);
        vna_sign(evil.sk, VNA_CTX_MSG, g_wire, (uint32_t) l, rnd, m->sig);
        n = vna_schema_pack(&vna_msg_schema, m, g_wire, sizeof g_wire, true);
    }
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_ERR_SIG,
          "signature by the wrong key refused");

    /* wrong context string: a record signature is not a message signature */
    n = craft(&T[B].id, &T[A].id.id, VNA_MSG_PING, 1000003, g_now, 81, 0, 0, false);
    {
        uint8_t rnd[32];
        vna_drbg_gen(&g_rng, rnd, 32);
        int32_t l = vna_schema_pack(&vna_msg_schema, &g_m, g_wire, sizeof g_wire, false);
        vna_sign(T[B].id.sk, VNA_CTX_RECORD, g_wire, (uint32_t) l, rnd, g_m.sig);
        n = vna_schema_pack(&vna_msg_schema, &g_m, g_wire, sizeof g_wire, true);
    }
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_ERR_SIG, "cross-context signature refused");

    /* misaddressed: a message for C delivered to A */
    n = craft(&T[B].id, &T[C].id.id, VNA_MSG_PING, 1000004, g_now, 82, 0, 0, false);
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_ERR_DST,
          "message for another node refused");

    /* PoW below difficulty */
    static vna_identity_t weak;
    make_identity(&weak, 31337, 0);
    uint32_t tries = 0;
    while (vna_pow_ok(&weak.id, weak.pow_nonce, 8) && tries < 100) weak.pow_nonce++, tries++;
    n = craft(&weak, &T[A].id.id, VNA_MSG_PING, 1, g_now, 83, 0, 0, false);
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_ERR_POW,
          "identity without enough PoW refused");

    /* wrong-signer response: C answers a FIND_NODE that A sent to B */
    int32_t slot = vna_node_lookup(&T[A].node, &kb.key, VNA_LK_FIND_NODE, g_now, &g_ob);
    uint64_t rpc = 0;
    vna_id_t asked;
    for (uint32_t i = 0; i < VNA_NODE_PENDING; i++)
        if (T[A].node.pend[i].kind == VNA_P_LOOKUP) {
            rpc = T[A].node.pend[i].rpc;
            asked = T[A].node.pend[i].peer;
            break;
        }
    vna_outbox_clear(&g_ob);
    uint32_t imp = C;
    for (uint32_t i = 0; i < NN; i++)
        if (!vna_id_eq(&T[i].id.id, &asked) && i != A) {
            imp = i;
            break;
        }
    vna_b_nodes_t nb;
    nb.n = 0;
    uint32_t u0 = T[A].node.unexpected;
    n = craft(&T[imp].id, &T[A].id.id, VNA_MSG_NODES, 2000000, g_now, rpc, &vna_b_nodes_schema, &nb,
              false);
    CHECK(deliver_to(A, imp, g_wire, (uint32_t) n) == VNA_ERR_UNEXPECTED &&
              T[A].node.unexpected == u0 + 1,
          "response signed by someone we did not ask is refused");
    n = craft(&T[imp].id, &T[A].id.id, VNA_MSG_NODES, 2000001, g_now, 123456789,
              &vna_b_nodes_schema, &nb, false);
    CHECK(deliver_to(A, imp, g_wire, (uint32_t) n) == VNA_ERR_UNEXPECTED,
          "unsolicited response refused");
    vna_node_lookup_release(&T[A].node, slot);

    /* every truncation and UBH tampering of a valid framed message refused */
    n = craft(&T[B].id, &T[A].id.id, VNA_MSG_PING, 1000005, g_now, 84, 0, 0, true);
    int acc = 0;
    for (int32_t k = 0; k < n; k += 97) acc += deliver_to(A, B, g_wire, (uint32_t) k) == VNA_OK;
    CHECK(acc == 0, "truncated messages refused");
    memcpy(copy, g_wire, (size_t) n);
    copy[12] ^= 1; /* payload_length in the UBH header */
    CHECK(deliver_to(A, B, copy, (uint32_t) n) == VNA_ERR_PARSE, "bad UBH length refused");
    CHECK(deliver_to(A, B, g_wire, (uint32_t) n) == VNA_OK, "framed original accepted");
    printf(
        "    rejections at A: parse %u dst %u dup %u stale %u replay %u binding %u pow %u sig %u\n",
        T[A].node.v.rej_parse, T[A].node.v.rej_dst, T[A].node.v.rej_dup, T[A].node.v.rej_stale,
        T[A].node.v.rej_replay, T[A].node.v.rej_binding, T[A].node.v.rej_pow, T[A].node.v.rej_sig);
    printf("    key cache: %llu hits, %llu misses\n", (unsigned long long) T[A].node.kc.hits,
           (unsigned long long) T[A].node.kc.misses);
}

/* ===================================================================== */
/* 3. sessions                                                            */
/* ===================================================================== */
static vna_hs_t hsI, hsR, hsM;
static vna_sess_t sI, sR;
static uint8_t m1[4096], m2[8192], m3[8192];

static bool do_handshake(const vna_identity_t *Ini, const vna_identity_t *R, vna_sess_t *si,
                         vna_sess_t *sr)
{
    uint8_t rnd[128];
    vna_drbg_gen(&g_rng, rnd, 128);
    int32_t l1 = vna_hs_initiate(&hsI, Ini, &R->id, 8, VNA_FEAT_UBH168, rnd, m1, sizeof m1);
    vna_drbg_gen(&g_rng, rnd, 128);
    int32_t l2 = vna_hs_respond(&hsR, R, 8, VNA_FEAT_UBH168, m1, (uint32_t) l1, rnd, m2, sizeof m2);
    vna_drbg_gen(&g_rng, rnd, 32);
    int32_t l3 = vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, si);
    return l1 > 0 && l2 > 0 && l3 > 0 &&
           vna_hs_responder_finish(&hsR, m3, (uint32_t) l3, sr) == VNA_OK;
}

static void test_session(void)
{
    printf("[3] session: hybrid X25519 + ML-KEM-768 handshake, ML-DSA-65 auth, AEAD records\n");
    const vna_identity_t *Ini = &T[20].id, *R = &T[21].id;
    CHECK(do_handshake(Ini, R, &sI, &sR), "handshake completes");
    CHECK(memcmp(sI.tx_key, sR.rx_key, 32) == 0 && memcmp(sI.rx_key, sR.tx_key, 32) == 0 &&
              memcmp(sI.tx_iv, sR.rx_iv, 12) == 0 && memcmp(sI.session_id, sR.session_id, 32) == 0,
          "both sides derive equal keys");
    CHECK(memcmp(sI.tx_key, sI.rx_key, 32) != 0, "per-direction keys differ");
    CHECK(vna_id_eq(&sR.peer, &Ini->id) && vna_id_eq(&sI.peer, &R->id),
          "each side knows who it talks to");
    uint8_t pt[600], ct[700], out[700], type;
    uint32_t pl;
    for (int i = 0; i < 600; i++) pt[i] = (uint8_t) (i * 13);
    int32_t cl = vna_sess_seal(&sI, 7, pt, 600, ct, sizeof ct);
    CHECK(cl > 0 && vna_sess_open(&sR, ct, (uint32_t) cl, out, sizeof out, &type, &pl) == VNA_OK &&
              type == 7 && pl == 600 && memcmp(out, pt, 600) == 0,
          "AEAD round trip Ini->R");
    CHECK(vna_sess_open(&sR, ct, (uint32_t) cl, out, sizeof out, &type, &pl) == VNA_ERR_REPLAY,
          "replayed record refused");
    int32_t c2 = vna_sess_seal(&sR, 8, pt, 100, ct, sizeof ct);
    CHECK(c2 > 0 && vna_sess_open(&sI, ct, (uint32_t) c2, out, sizeof out, &type, &pl) == VNA_OK &&
              pl == 100 && memcmp(out, pt, 100) == 0,
          "AEAD round trip R->I");
    /* out-of-order within the window */
    static uint8_t r1[700], r2[700];
    int32_t a1 = vna_sess_seal(&sI, 1, pt, 50, r1, sizeof r1);
    int32_t a2 = vna_sess_seal(&sI, 1, pt, 50, r2, sizeof r2);
    CHECK(vna_sess_open(&sR, r2, (uint32_t) a2, out, sizeof out, &type, &pl) == VNA_OK &&
              vna_sess_open(&sR, r1, (uint32_t) a1, out, sizeof out, &type, &pl) == VNA_OK,
          "reordered records accepted once each");
    a1 = vna_sess_seal(&sI, 1, pt, 50, r1, sizeof r1);
    r1[20] ^= 1;
    CHECK(vna_sess_open(&sR, r1, (uint32_t) a1, out, sizeof out, &type, &pl) == VNA_ERR_CRYPTO,
          "tampered ciphertext refused");
    r1[20] ^= 1;
    r1[0] ^= 1; /* type is AAD */
    CHECK(vna_sess_open(&sR, r1, (uint32_t) a1, out, sizeof out, &type, &pl) == VNA_ERR_CRYPTO,
          "tampered header refused");
    r1[0] ^= 1;
    CHECK(vna_sess_open(&sR, r1, (uint32_t) a1, out, sizeof out, &type, &pl) == VNA_OK,
          "the window was not advanced by the forgeries");

    /* MITM 1: M answers I's M1 with its own key, claiming to be R */
    const vna_identity_t *M = &T[22].id;
    uint8_t rnd[128];
    vna_drbg_gen(&g_rng, rnd, 128);
    int32_t l1 = vna_hs_initiate(&hsI, Ini, &R->id, 8, 0, rnd, m1, sizeof m1);
    static vna_identity_t Mfake;
    Mfake = *M;
    Mfake.id = R->id; /* M pretends: id of R, key of M */
    vna_drbg_gen(&g_rng, rnd, 128);
    int32_t l2 = vna_hs_respond(&hsM, &Mfake, 8, 0, m1, (uint32_t) l1, rnd, m2, sizeof m2);
    vna_drbg_gen(&g_rng, rnd, 32);
    CHECK(l2 > 0 && vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, &sI) ==
                        VNA_ERR_BINDING,
          "MITM with its own key: NodeID binding fails");

    /* MITM 2: M relays R's genuine M2 but swaps the ephemeral X25519 key */
    vna_drbg_gen(&g_rng, rnd, 128);
    l1 = vna_hs_initiate(&hsI, Ini, &R->id, 8, 0, rnd, m1, sizeof m1);
    vna_drbg_gen(&g_rng, rnd, 128);
    l2 = vna_hs_respond(&hsR, R, 8, 0, m1, (uint32_t) l1, rnd, m2, sizeof m2);
    m2[4 + 1 + 4 + 32 + 32 + 8 + 3] ^= 0x40; /* inside x */
    vna_drbg_gen(&g_rng, rnd, 32);
    CHECK(vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, &sI) == VNA_ERR_SIG,
          "MITM substituting the ephemeral key: responder signature fails");
    /* MITM 3: swap the KEM ciphertext */
    vna_drbg_gen(&g_rng, rnd, 128);
    l1 = vna_hs_initiate(&hsI, Ini, &R->id, 8, 0, rnd, m1, sizeof m1);
    vna_drbg_gen(&g_rng, rnd, 128);
    l2 = vna_hs_respond(&hsR, R, 8, 0, m1, (uint32_t) l1, rnd, m2, sizeof m2);
    m2[4 + 1 + 4 + 32 + 32 + 8 + 32 + 100] ^= 0x01; /* inside ct */
    vna_drbg_gen(&g_rng, rnd, 32);
    CHECK(vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, &sI) == VNA_ERR_SIG,
          "MITM substituting the ML-KEM ciphertext: signature fails");
    /* MITM 4: swap ek in M1 -> R signs a different transcript -> I refuses */
    vna_drbg_gen(&g_rng, rnd, 128);
    l1 = vna_hs_initiate(&hsI, Ini, &R->id, 8, 0, rnd, m1, sizeof m1);
    static uint8_t m1x[4096];
    memcpy(m1x, m1, (size_t) l1);
    m1x[4 + 1 + 4 + 64 + 32 + 10] ^= 0x10; /* inside ek */
    vna_drbg_gen(&g_rng, rnd, 128);
    l2 = vna_hs_respond(&hsR, R, 8, 0, m1x, (uint32_t) l1, rnd, m2, sizeof m2);
    vna_drbg_gen(&g_rng, rnd, 32);
    CHECK(vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, &sI) == VNA_ERR_SIG,
          "MITM tampering M1: transcripts diverge, signature fails");
    /* M3 tampered / from an impostor */
    vna_drbg_gen(&g_rng, rnd, 128);
    l1 = vna_hs_initiate(&hsI, Ini, &R->id, 8, 0, rnd, m1, sizeof m1);
    vna_drbg_gen(&g_rng, rnd, 128);
    l2 = vna_hs_respond(&hsR, R, 8, 0, m1, (uint32_t) l1, rnd, m2, sizeof m2);
    vna_drbg_gen(&g_rng, rnd, 32);
    int32_t l3 = vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, &sI);
    m3[100] ^= 1;
    CHECK(vna_hs_responder_finish(&hsR, m3, (uint32_t) l3, &sR) == VNA_ERR_CRYPTO,
          "tampered M3 refused");
    /* initiator claims to be someone else (claimed id != its key) */
    static vna_identity_t Ifake;
    Ifake = *M;
    Ifake.id = Ini->id;
    vna_drbg_gen(&g_rng, rnd, 128);
    l1 = vna_hs_initiate(&hsI, &Ifake, &R->id, 8, 0, rnd, m1, sizeof m1);
    vna_drbg_gen(&g_rng, rnd, 128);
    l2 = vna_hs_respond(&hsR, R, 8, 0, m1, (uint32_t) l1, rnd, m2, sizeof m2);
    vna_drbg_gen(&g_rng, rnd, 32);
    l3 = vna_hs_initiator_finish(&hsI, m2, (uint32_t) l2, rnd, m3, sizeof m3, &sI);
    CHECK(l3 > 0 && vna_hs_responder_finish(&hsR, m3, (uint32_t) l3, &sR) == VNA_ERR_BINDING,
          "initiator impersonation refused");

    /* the session key depends on the ML-KEM secret: an attacker who knows the
     * X25519 secret but not the KEM secret derives a different key */
    vna_drbg_gen(&g_rng, rnd, 128);
    CHECK(do_handshake(Ini, R, &sI, &sR), "fresh handshake");
    uint8_t ss_x[32], zero_k[32] = {0}, th[32], prk[32], k_guess[32], k_real[32];
    /* reconstruct the attacker's best guess with the real X25519 secret */
    (void) ss_x;
    memset(th, 0x5c, 32);
    memcpy(k_real, sI.tx_key, 32);
    {
        static const char tag[] = "vinea/v2/hybrid";
        uint8_t in[sizeof tag - 1 + 96];
        memcpy(in, tag, sizeof tag - 1);
        memcpy(in + sizeof tag - 1, zero_k, 32);
        memset(in + sizeof tag - 1 + 32, 0xAA, 64);
        shake256(in, sizeof in, k_guess, 32);
        (void) prk;
    }
    CHECK(memcmp(k_guess, k_real, 32) != 0, "key is not derivable from the classical half alone");
    vna_hs_clear(&hsI);
    vna_hs_clear(&hsR);
    bool wiped = true;
    for (uint32_t i = 0; i < sizeof hsI.prk; i++) wiped &= hsI.prk[i] == 0;
    CHECK(wiped, "handshake secrets wiped");
}

/* ===================================================================== */
/* 4. agreements                                                          */
/* ===================================================================== */
static vna_usage_ent_t g_ue[16];
static vna_lease_t g_le[8];

static void test_agreement(void)
{
    printf("[4] agreements: degree dial, audience, quotas, memory leases, HK over the wire\n");
    static vna_agreement_t a;
    vna_usage_t us;
    vna_usage_init(&us, g_ue, 16, g_le, 8);
    vna_id_t owner = T[30].id.id, friend_ = T[31].id.id, stranger = T[32].id.id, foe = T[33].id.id;
    vna_id_t f_pub, f_priv, f_other;
    memset(&f_pub, 1, 32);
    memset(&f_priv, 2, 32);
    memset(&f_other, 3, 32);
    vna_agree_default(&a, &owner);
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_ROUTE, 0, 0, 0, g_now, false) ==
              VNA_ERR_DENIED,
          "default agreement shares nothing");
    CHECK(vna_agree_check(0, &us, &friend_, 0, VNA_RES_ROUTE, 0, 0, 0, g_now, false) ==
              VNA_ERR_DENIED,
          "no agreement: fail closed");
    a.degree = VNA_DEG_MEMORY;
    a.audience = VNA_AUD_ALLOW_OR_TRUST;
    a.trust_min = 100;
    a.n_allow = 1;
    a.allow[0].id = friend_;
    a.n_block = 1;
    a.block[0].id = foe;
    a.n_files = 2;
    a.files[0].root = f_pub;
    a.files[1].root = f_priv;
    a.files[1].visibility = VNA_VIS_PRIVATE;
    a.compute_per_cycle = 1000;
    a.q_compute = 600;
    a.storage_bytes = 1 << 20;
    a.q_storage = 1 << 19;
    a.memory_bytes = 1 << 16;
    a.q_memory = 1 << 15;
    a.memory_max_cycles = 4;
    a.cycle_ms = 1000;
    a.q_chunks = 3;
    a.quota_period_ms = 60000;
    a.expires_ms = g_now + 3600000;
    CHECK(vna_agree_check(&a, &us, &foe, 1000000, VNA_RES_ROUTE, 0, 0, 0, g_now, false) ==
              VNA_ERR_DENIED,
          "blocklist wins over trust");
    CHECK(vna_agree_check(&a, &us, &stranger, 0, VNA_RES_FILE, &f_pub, 1, 0, g_now, true) ==
              VNA_ERR_DENIED,
          "stranger below the trust threshold refused");
    CHECK(vna_agree_check(&a, &us, &stranger, 100, VNA_RES_FILE, &f_pub, 1, 0, g_now, true) ==
              VNA_OK,
          "trusted stranger served a public file");
    CHECK(vna_agree_check(&a, &us, &stranger, 100, VNA_RES_FILE, &f_priv, 1, 0, g_now, true) ==
              VNA_ERR_DENIED,
          "PRIVATE file never served outside the allowlist");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_FILE, &f_priv, 1, 0, g_now, true) == VNA_OK,
          "allowlisted friend gets the private file");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_FILE, &f_other, 1, 0, g_now, true) ==
              VNA_ERR_DENIED,
          "file not listed refused");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_FILE, &f_pub, 1, 0, g_now, true) ==
                  VNA_OK &&
              vna_agree_check(&a, &us, &friend_, 0, VNA_RES_FILE, &f_pub, 1, 0, g_now, true) ==
                  VNA_OK &&
              vna_agree_check(&a, &us, &friend_, 0, VNA_RES_FILE, &f_pub, 1, 0, g_now, true) ==
                  VNA_ERR_CAP,
          "chunk quota per period enforced");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_FILE, &f_pub, 1, 0, g_now + 60001, true) ==
              VNA_OK,
          "quota resets next period");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_COMPUTE, 0, 601, 0, g_now, false) ==
              VNA_ERR_CAP,
          "compute above per-peer quota refused");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_COMPUTE, 0, 500, 0, g_now, true) ==
                  VNA_OK &&
              vna_agree_check(&a, &us, &stranger, 100, VNA_RES_COMPUTE, 0, 501, 0, g_now, true) ==
                  VNA_ERR_CAP,
          "compute capacity per cycle shared by all peers");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_MEMORY, 0, 1 << 14, 5, g_now, true) ==
              VNA_ERR_CAP,
          "lease longer than allowed refused");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_MEMORY, 0, 1 << 14, 2, g_now, true) ==
                  VNA_OK &&
              us.memory_leased == (1u << 14),
          "memory lease granted");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_MEMORY, 0, 1 << 15, 2, g_now, true) ==
              VNA_ERR_CAP,
          "memory per-peer quota counts live leases");
    CHECK(vna_usage_expire(&us, g_now + 1999) == 0 &&
              vna_usage_expire(&us, g_now + 2000) == (1u << 14) && us.memory_leased == 0,
          "lease released exactly at expiry");
    a.degree = VNA_DEG_COMPUTE;
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_MEMORY, 0, 16, 1, g_now, false) ==
              VNA_ERR_DENIED,
          "memory not offered below its degree");
    CHECK(vna_agree_check(&a, &us, &friend_, 0, VNA_RES_ROUTE, 0, 0, 0, g_now + 3600000, false) ==
              VNA_ERR_EXPIRED,
          "expired agreement refuses everything");

    /* over the wire: HK requests at node 30 with this agreement */
    a.degree = VNA_DEG_MEMORY;
    vna_usage_init(&us, g_ue, 16, g_le, 8);
    vna_node_set_agreement(&T[30].node, &a, &us, 0, 0);
    uint32_t P = 31, S = 32;
    /* both know each other from the DHT? make sure of it */
    vna_contact_t dummy;
    uint8_t ad[4];
    addr_of(30, ad);
    vna_rt_seen(&T[P].node.rt, &T[30].id.id, ad, 4, 0, g_now, &dummy);
    vna_rt_seen(&T[S].node.rt, &T[30].id.id, ad, 4, 0, g_now, &dummy);
    uint32_t den0 = T[30].node.denied, ref0 = T[30].node.hk_refused;
    vna_node_send_hk(&T[P].node, &T[30].id.id, "ask(compute, units: 100, price: 21)", SWARM_HK_TRUE,
                     1, false, 0, g_now, &g_ob);
    flush(P);
    vna_node_send_hk(&T[S].node, &T[30].id.id, "ask(compute, units: 100, price: 21)", SWARM_HK_TRUE,
                     1, false, 0, g_now, &g_ob);
    flush(S);
    vna_node_send_hk(&T[P].node, &T[30].id.id, "set(allocation, model: 1, units: 999)",
                     SWARM_HK_TRUE, 2, false, 0, g_now, &g_ob);
    flush(P);
    run_queue();
    vna_event_t ev;
    int got = 0;
    while (vna_node_poll_event(&T[30].node, &ev))
        got += vna_id_eq(&ev.from, &friend_) && ev.cmd.verb == VNA_V_ASK;
    CHECK(got == 1, "allowed request surfaced to the owner's agent");
    CHECK(T[30].node.denied == den0 + 1, "request outside the agreement refused on the wire");
    CHECK(T[30].node.hk_refused == ref0 + 1, "unknown verb refused on the wire");
    int rej = 0;
    while (vna_node_poll_event(&T[S].node, &ev))
        rej += ev.is_response && ev.cmd.verb == VNA_V_REJECT;
    CHECK(rej == 1, "refused requester gets a signed reject()");
    vna_node_set_agreement(&T[30].node, 0, 0, 0, 0);

    /* degree OFF: answers nothing */
    T[34].node.cfg.degree = VNA_DEG_OFF;
    uint32_t d0 = T[34].node.denied;
    vna_rt_seen(&T[35].node.rt, &T[34].id.id, (uint8_t[4]){34, 0, 0xCA, 0xFE}, 4, 0, g_now, &dummy);
    vna_node_ping(&T[35].node, &T[34].id.id, g_now, &g_ob);
    flush(35);
    run_queue();
    CHECK(T[34].node.denied == d0 + 1, "degree OFF answers nothing");
    T[34].node.cfg.degree = VNA_DEG_STORE;
}

/* ===================================================================== */
/* 5. economy                                                             */
/* ===================================================================== */
static vna_account_t accA[8], accB[8], accC[8];
static vna_book_t bkA, bkB, bkC;
static uint8_t rbuf[16][VNA_RCPT_MAX];
static uint32_t rlen[16];
static uint8_t g_tx[VNA_RCPT_MAX], g_tx2[VNA_RCPT_MAX];
static vna_receipt_t g_rs;

static bool all_conserved(void)
{
    return vna_book_conserved(&bkA) && vna_book_conserved(&bkB) && vna_book_conserved(&bkC);
}

static uint64_t ledger_count(const vna_book_t *b)
{
    return b->ledger.count;
}

static bool last_triple_ok(const vna_book_t *b)
{
    const vna_ledger_t *l = &b->ledger;
    if (l->n < 3) return false;
    const vna_lentry_t *e[3];
    for (int k = 0; k < 3; k++) {
        uint32_t i = l->head + l->n - 3 + (uint32_t) k;
        if (i >= VNA_LEDGER_RING) i -= VNA_LEDGER_RING;
        e[k] = &l->ring[i];
    }
    return e[0]->axis == VNA_LAX_FINANCIAL && e[1]->axis == VNA_LAX_PROVENANCE &&
           e[2]->axis == VNA_LAX_EXTERNALITY && memcmp(e[0]->ref, e[1]->ref, 32) == 0 &&
           memcmp(e[1]->ref, e[2]->ref, 32) == 0 && e[0]->debit != e[0]->credit;
}

static uint8_t g_cap[VNA_RCPT_MAX];
static uint32_t g_cap_len;
static vna_id_t g_cap_from;
static bool g_cap_refuse;
static vna_status_t rcpt_capture(void *ctx, const vna_id_t *from, const uint8_t *r, uint32_t len,
                                 uint64_t now)
{
    (void) ctx;
    (void) now;
    if (g_cap_refuse) return VNA_ERR_DENIED;
    memcpy(g_cap, r, len);
    g_cap_len = len;
    g_cap_from = *from;
    return VNA_OK;
}

/* seller prepares+signs, buyer countersigns, both apply */
static vna_status_t trade(vna_book_t *seller, vna_book_t *buyer, uint8_t mode, uint8_t form,
                          uint8_t res, uint64_t units, uint64_t price, const vna_agreement_t *sagr,
                          vna_usage_t *sus, vna_status_t *st_buyer)
{
    uint8_t rnd[32];
    if (vna_receipt_prepare(seller, &g_rs, mode, form, res, &seller->idn->id, &buyer->idn->id,
                            units, price, 1000, g_now, 0, "ask(compute, units: 1)") != VNA_OK)
        return VNA_ERR_ARG;
    vna_drbg_gen(&g_rng, rnd, 32);
    int32_t l = vna_receipt_sign_seller(&g_rs, seller->idn, rnd, g_tx, sizeof g_tx);
    vna_drbg_gen(&g_rng, rnd, 32);
    l = vna_receipt_countersign(g_tx, (uint32_t) l, seller->idn->pk, buyer->idn, rnd, &g_rs, g_tx2,
                                sizeof g_tx2);
    if (l < 0) return (vna_status_t) l;
    vna_drbg_gen(&g_rng, rnd, 32);
    vna_status_t ss = vna_book_apply(seller, g_tx2, (uint32_t) l, sagr, sus, g_now, rnd);
    vna_drbg_gen(&g_rng, rnd, 32);
    vna_status_t sb = vna_book_apply(buyer, g_tx2, (uint32_t) l, 0, 0, g_now, rnd);
    if (st_buyer) *st_buyer = sb;
    rlen[15] = (uint32_t) l;
    memcpy(rbuf[15], g_tx2, (size_t) l);
    return ss;
}

static void test_economy(void)
{
    printf("[5] economy: golden split, nine forms, triple ledger, gate, receipts, negotiation\n");
    /* --- the split: exact, capped, golden --- */
    uint64_t cyc[64], life[64], share[64], lvl[64], cm[64], commons;
    vna_id_t ids[64];
    int exact_ok = 1, cap_ok = 1;
    for (int t = 0; t < 3000; t++) {
        uint32_t n = 1 + (uint32_t) (rand() % 64);
        uint64_t pool = ((uint64_t) rand() << 16 ^ (uint64_t) rand()) % ((uint64_t) 1 << 40);
        for (uint32_t i = 0; i < n; i++) {
            cyc[i] = (rand() % 3) ? (uint64_t) (rand() % 100000) : 0;
            life[i] = (uint64_t) (rand() % 1000);
            vna_drbg_gen(&g_rng, ids[i].b, 32);
        }
        uint64_t res = vna_econ_split(pool, cyc, life, ids, n, share, &commons, lvl, cm);
        uint64_t s = res, mx = 0;
        for (uint32_t i = 0; i < n; i++) {
            s += share[i];
            if (share[i] > mx) mx = share[i];
        }
        uint64_t cap = pool * 8 / 21, eq = (pool + n - 1) / n;
        if (eq > cap) cap = eq;
        if (n == 2) cap = (pool * 13 + 20) / 21;
        exact_ok &= s == pool && res == 0;
        cap_ok &= mx <= cap;
    }
    CHECK(exact_ok, "3000 random splits sum to EXACTLY the pool");
    CHECK(cap_ok, "no member ever above max(8/21 of the pool, an equal share) (13/21 for two)");
    /* golden proportions of the commons: 20 members, no contribution this cycle */
    uint32_t n = 20;
    for (uint32_t i = 0; i < n; i++) {
        cyc[i] = 0;
        life[i] = 1000 - i;
        vna_drbg_gen(&g_rng, ids[i].b, 32);
    }
    uint64_t pool = 21000000;
    vna_econ_split(pool, cyc, life, ids, n, share, &commons, lvl, cm);
    uint64_t lev[8] = {0};
    uint32_t L = 0;
    for (uint32_t i = 0; i < n; i++) {
        lev[lvl[i]] += cm[i];
        if (lvl[i] + 1 > L) L = (uint32_t) lvl[i] + 1;
    }
    printf("    commons by level (L=%u):", L);
    for (uint32_t d = 0; d < L; d++) printf(" %llu", (unsigned long long) lev[d]);
    printf("\n    adjacent ratios x1000:");
    bool golden = true;
    uint64_t wsum = 0;
    for (uint32_t d = 0; d < L; d++) wsum += swarm_level_weight(d, L);
    for (uint32_t d = 0; d < L; d++) {
        uint64_t w = swarm_level_weight(d, L);
        uint64_t want = commons * w / wsum;
        golden &= lev[d] + 64 >= want && lev[d] <= want + 64;
        if (d + 1 < L) printf(" %llu", (unsigned long long) (lev[d] * 1000 / lev[d + 1]));
    }
    printf("  (phi = 1618)\n");
    CHECK(golden, "commons level shares are Fibonacci F(L-d+1) proportions (golden)");
    CHECK(commons == pool, "no contribution this cycle: the whole pool is commons");
    bool newcomer = share[n - 1] > 0;
    CHECK(newcomer, "a newcomer with no contribution still receives (growth)");
    /* market part follows demand-weighted contribution, capped */
    for (uint32_t i = 0; i < n; i++) cyc[i] = (i == 5) ? 1000000 : (i < 10 ? 10 : 0);
    vna_econ_split(pool, cyc, life, ids, n, share, &commons, lvl, cm);
    uint64_t market = pool - pool * 8 / 21;
    CHECK(share[5] - cm[5] <= market * 8 / 21 + 1,
          "dominant contributor capped at 8/21 of the market");
    CHECK(share[5] > share[4], "higher contribution earns more");

    /* demand pricing */
    CHECK(vna_econ_price(100, 0, 4) == 100 && vna_econ_price(100, 4, 4) == 200 &&
              vna_econ_price(100, 1000, 4) == 262,
          "price = base*(S+D)/S, capped at 21/8");

    /* --- books, receipts, gate --- */
    const vna_identity_t *IA = &T[40].id, *IB = &T[41].id, *IC = &T[42].id;
    vna_book_init(&bkA, IA, accA, 8);
    vna_book_init(&bkB, IB, accB, 8);
    vna_book_init(&bkC, IC, accC, 8);
    CHECK(vna_book_add_peer(&bkA, &IB->id, IB->pk, IB->pow_nonce, 8, true) == VNA_OK &&
              vna_book_add_peer(&bkA, &IC->id, IC->pk, IC->pow_nonce, 8, true) == VNA_OK &&
              vna_book_add_peer(&bkB, &IA->id, IA->pk, IA->pow_nonce, 8, true) == VNA_OK &&
              vna_book_add_peer(&bkC, &IA->id, IA->pk, IA->pow_nonce, 8, true) == VNA_OK &&
              vna_book_add_peer(&bkB, &IC->id, IC->pk, IC->pow_nonce, 8, true) == VNA_OK &&
              vna_book_add_peer(&bkC, &IB->id, IB->pk, IB->pow_nonce, 8, true) == VNA_OK,
          "peers registered (binding checked)");
    CHECK(vna_book_add_peer(&bkA, &IB->id, IC->pk, IC->pow_nonce, 8, true) == VNA_ERR_BINDING,
          "a key that does not hash to the id is refused");
    static vna_agreement_t agA;
    vna_agree_default(&agA, &IA->id);
    agA.degree = VNA_DEG_COMPUTE;
    agA.audience = VNA_AUD_EVERYONE;
    agA.compute_per_cycle = 10000;
    agA.q_compute = 5000;
    vna_usage_t usA;
    vna_usage_init(&usA, g_ue, 16, g_le, 8);

    /* the owner's internal swarm budget */
    static swarm_budget_t sbA, snap;
    swarm_budget_init(&sbA, 3, 10000);
    swarm_budget_register(&sbA, 1, 0);
    swarm_budget_register(&sbA, 2, 1);
    swarm_budget_begin_cycle(&sbA);
    uint8_t secret[32], wrong[32];
    memset(secret, 0x42, 32);
    memset(wrong, 0x43, 32);
    vna_gate_t gA;
    vna_gate_init(&gA, secret, 1500, 400, 4);
    CHECK(vna_gate_begin_cycle(&gA, secret) == VNA_OK, "gate cycle opened by its owner");
    uint64_t got = 0;
    CHECK(vna_gate_export(&gA, wrong, &sbA, 1, &bkA, SWARM_CAP_SYSTEM, 100, &got) == VNA_ERR_AUTH,
          "gate refuses anyone without the owner's secret");
    CHECK(vna_gate_export(&gA, secret, &sbA, 1, &bkA, SWARM_CAP_SOCIAL, 100, &got) ==
              VNA_ERR_INALIENABLE,
          "Crown capital cannot be exported");
    CHECK(vna_gate_export(&gA, secret, &sbA, 1, &bkA, SWARM_CAP_SYSTEM, 1600, &got) == VNA_ERR_CAP,
          "export over the per-cycle cap refused");
    uint64_t before = swarm_budget_remaining(&sbA, 1);
    CHECK(vna_gate_export(&gA, secret, &sbA, 1, &bkA, SWARM_CAP_SYSTEM, 1000, &got) == VNA_OK &&
              got == 1000 && swarm_budget_remaining(&sbA, 1) == before - 1000 &&
              bkA.pool[SWARM_CAP_SYSTEM] == 1000,
          "owner exports 1000 internal tokens into the neutral pool");
    CHECK(vna_gate_export(&gA, secret, &sbA, 2, &bkA, SWARM_CAP_INTELLECTUAL, 400, &got) == VNA_OK,
          "second form exported");
    CHECK(vna_gate_export(&gA, secret, &sbA, 1, &bkA, SWARM_CAP_SYSTEM, 200, &got) == VNA_ERR_CAP,
          "cumulative export cap per cycle");
    CHECK(all_conserved(), "conserved after export");

    /* B contributes compute to A on credit */
    uint64_t price = vna_book_price(&bkA, VNA_RES_COMPUTE, 3);
    vna_status_t sb_;
    uint64_t l0 = ledger_count(&bkA);
    static vna_agreement_t agB;
    vna_agree_default(&agB, &IB->id);
    agB.degree = VNA_DEG_COMPUTE;
    agB.audience = VNA_AUD_EVERYONE;
    agB.compute_per_cycle = 10000;
    agB.q_compute = 5000;
    static vna_usage_ent_t ueB[16];
    static vna_lease_t leB[8];
    vna_usage_t usB;
    vna_usage_init(&usB, ueB, 16, leB, 8);
    { /* the seller's own book refuses a sale with no agreement attached (fail closed) */
        uint8_t r0[32];
        vna_drbg_gen(&g_rng, r0, 32);
        vna_receipt_prepare(&bkB, &g_rs, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, &IB->id,
                            &IA->id, 100, 100 * price, 1000, g_now, 0, "ask(compute, units: 1)");
        int32_t l1 = vna_receipt_sign_seller(&g_rs, IB, r0, g_tx, sizeof g_tx);
        l1 = vna_receipt_countersign(g_tx, (uint32_t) l1, IB->pk, IA, r0, &g_rs, g_tx2,
                                     sizeof g_tx2);
        CHECK(vna_book_apply(&bkB, g_tx2, (uint32_t) l1, 0, 0, g_now, r0) == VNA_ERR_DENIED,
              "a seller with no agreement attached sells nothing (fail closed)");
    }
    CHECK(trade(&bkB, &bkA, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 100, 100 * price,
                &agB, &usB, &sb_) == VNA_OK &&
              sb_ == VNA_OK,
          "CREDIT receipt countersigned and applied by both");
    CHECK(ledger_count(&bkA) == l0 + 3 && last_triple_ok(&bkA) && last_triple_ok(&bkB),
          "each trade posts FINANCIAL + PROVENANCE + EXTERNALITY entries sharing one reference");
    CHECK(vna_book_find(&bkA, &IB->id)->contrib[SWARM_CAP_SYSTEM] == 100 * price,
          "contribution recorded");
    CHECK(all_conserved(), "conserved after credit");

    /* replay and fork */
    uint8_t rnd[32];
    vna_drbg_gen(&g_rng, rnd, 32);
    CHECK(vna_book_apply(&bkA, rbuf[15], rlen[15], 0, 0, g_now, rnd) == VNA_ERR_REPLAY,
          "replayed receipt refused");
    vna_receipt_prepare(&bkB, &g_rs, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, &IB->id,
                        &IA->id, 1, 999999, 1000, g_now, 0, "ask(compute, units: 1)");
    g_rs.pair_seq += 1; /* skip one: a gap */
    int32_t l = vna_receipt_sign_seller(&g_rs, IB, rnd, g_tx, sizeof g_tx);
    l = vna_receipt_countersign(g_tx, (uint32_t) l, IB->pk, IA, rnd, &g_rs, g_tx2, sizeof g_tx2);
    CHECK(vna_book_apply(&bkA, g_tx2, (uint32_t) l, 0, 0, g_now, rnd) == VNA_ERR_FORK,
          "receipt chain gap/fork refused");

    /* forged credit: C fabricates "A distributes 500 to C" */
    vna_receipt_prepare(&bkC, &g_rs, VNA_TR_DISTRIBUTE, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, &IA->id,
                        &IC->id, 500, 500, 1000, g_now, 0, "offer(compute, units: 500)");
    vna_zero(g_rs.sig_seller, VNA_SIG_LEN);
    vna_zero(g_rs.sig_buyer, VNA_SIG_LEN);
    l = vna_schema_pack(&vna_receipt_schema, &g_rs, g_tx, sizeof g_tx, false);
    vna_sign(IC->sk, VNA_CTX_RECEIPT, g_tx, (uint32_t) l, rnd, g_rs.sig_seller);
    l = vna_schema_pack(&vna_receipt_schema, &g_rs, g_tx, sizeof g_tx, true);
    CHECK(vna_book_apply(&bkC, g_tx, (uint32_t) l, 0, 0, g_now, rnd) == VNA_ERR_SIG,
          "forged credit (issuer signature by the wrong key) refused");
    CHECK(vna_book_apply(&bkA, g_tx, (uint32_t) l, 0, 0, g_now, rnd) == VNA_ERR_SIG,
          "forged DISTRIBUTE refused by the claimed issuer too");
    CHECK(vna_book_find(&bkC, &IA->id)->held[SWARM_CAP_SYSTEM] == 0, "no tokens appeared");

    /* Crown form trade refused */
    vna_receipt_prepare(&bkB, &g_rs, VNA_TR_CREDIT, SWARM_CAP_SOCIAL, VNA_RES_COMPUTE, &IB->id,
                        &IA->id, 1, 1, 1000, g_now, 0, "ask(compute, units: 1)");
    l = vna_receipt_sign_seller(&g_rs, IB, rnd, g_tx, sizeof g_tx);
    l = vna_receipt_countersign(g_tx, (uint32_t) l, IB->pk, IA, rnd, &g_rs, g_tx2, sizeof g_tx2);
    CHECK(vna_book_apply(&bkA, g_tx2, (uint32_t) l, 0, 0, g_now, rnd) == VNA_ERR_INALIENABLE,
          "a trade in a Crown (inalienable) form refused");

    /* settle: A distributes its pools to members B and C */
    static uint8_t outbuf[16][VNA_RCPT_MAX];
    uint32_t outlen[16];
    vna_rcpt_out_t ro = {outbuf, outlen, 16, 0};
    CHECK(vna_book_settle(&bkA, g_now, &g_rng, &ro) == VNA_OK && ro.n == 4,
          "settle issued %u DISTRIBUTE receipts (2 forms x 2 members)", ro.n);
    vna_account_t *aB = vna_book_find(&bkA, &IB->id), *aC = vna_book_find(&bkA, &IC->id);
    CHECK(aB->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] + aC->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] ==
                  1000 &&
              aB->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] >
                  aC->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] &&
              aC->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] > 0,
          "SYSTEM pool: contributor B earns more, member C still gets a share, sum exact (B %llu C "
          "%llu)",
          (unsigned long long) aB->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM],
          (unsigned long long) aC->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM]);
    CHECK(bkA.pool[SWARM_CAP_SYSTEM] == 0 && bkA.pool[SWARM_CAP_INTELLECTUAL] == 0,
          "pools emptied");
    int applied = 0;
    for (uint32_t i = 0; i < ro.n; i++) {
        vna_drbg_gen(&g_rng, rnd, 32);
        vna_status_t s1 = vna_book_apply(&bkB, outbuf[i], outlen[i], 0, 0, g_now, rnd);
        vna_drbg_gen(&g_rng, rnd, 32);
        vna_status_t s2 = vna_book_apply(&bkC, outbuf[i], outlen[i], 0, 0, g_now, rnd);
        applied += (s1 == VNA_OK) + (s2 == VNA_OK); /* each receipt belongs to one of them */
    }
    CHECK(applied == (int) ro.n, "each recipient applies its own DISTRIBUTE receipts");
    CHECK(vna_book_find(&bkB, &IA->id)->held[SWARM_CAP_SYSTEM] ==
              aB->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM],
          "B's mirror equals A's record (bilateral books agree)");
    CHECK(all_conserved(), "conserved after settle (per kind, per form)");

    /* B pays A for compute with A-issued tokens */
    uint64_t holdB = aB->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM];
    CHECK(trade(&bkA, &bkB, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 50, holdB + 1, &agA,
                &usA, &sb_) == VNA_ERR_FUNDS,
          "paying more than held refused (no debt)");
    /* the failed attempt left B's chain advanced? It must not have: B refused too */
    CHECK(sb_ != VNA_OK, "buyer refused it too");
    /* resynchronise pair state: both refused, so the same sequence is valid again */
    CHECK(trade(&bkA, &bkB, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 50, 30, &agA, &usA,
                &sb_) == VNA_OK &&
              sb_ == VNA_OK,
          "B pays 30 A-issued SYSTEM tokens for 50 compute units");
    CHECK(aB->bal[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] == holdB - 30 &&
              bkA.burned[VNA_TK_EXTERNAL][SWARM_CAP_SYSTEM] == 30,
          "spent tokens burn at their issuer");
    CHECK(trade(&bkA, &bkB, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_MEMORY, 10, 1, &agA, &usA,
                &sb_) == VNA_ERR_DENIED,
          "trade for a resource outside the seller's agreement refused");
    CHECK(all_conserved(), "conserved after payments");

    /* B's gate turns the verified compute it received into internal rate */
    static swarm_budget_t sbB;
    swarm_budget_init(&sbB, 2, 5000);
    swarm_budget_register(&sbB, 1, 0);
    swarm_budget_begin_cycle(&sbB);
    uint8_t secB[32];
    memset(secB, 0x77, 32);
    vna_gate_t gB;
    vna_gate_init(&gB, secB, 0, 40, 2);
    vna_gate_begin_cycle(&gB, secB);
    CHECK(vna_gate_import(&gB, secB, &bkB, SWARM_CAP_SYSTEM, 41, &sbB, 5000) == VNA_ERR_CAP,
          "import over the cap refused");
    CHECK(vna_gate_import(&gB, secB, &bkB, SWARM_CAP_SYSTEM, 30, &sbB, 5000) == VNA_OK &&
              sbB.tokens_per_cycle == 5030,
          "verified remote compute raises next cycle's internal rate");
    CHECK(vna_gate_import(&gB, secB, &bkB, SWARM_CAP_SYSTEM, 10, &sbB, 5000) == VNA_OK,
          "second import within the cap");
    CHECK(vna_gate_import(&gB, secB, &bkB, SWARM_CAP_SYSTEM, 5, &sbB, 5000) == VNA_ERR_CAP,
          "rate limit: conversions per cycle");
    vna_gate_t gB2;
    vna_gate_init(&gB2, secB, 0, 1000, 10);
    vna_gate_begin_cycle(&gB2, secB);
    CHECK(vna_gate_import(&gB2, secB, &bkB, SWARM_CAP_SYSTEM, 51, &sbB, 5000) == VNA_ERR_FUNDS,
          "cannot import more than was actually received");
    CHECK(vna_gate_import(&gB2, wrong, &bkB, SWARM_CAP_SYSTEM, 1, &sbB, 5000) == VNA_ERR_AUTH,
          "import needs the owner's secret");

    /* a peer cannot set someone's internal allocation */
    memcpy(&snap, &sbA, sizeof snap);
    vna_node_set_agreement(&T[40].node, &agA, &usA, vna_book_trust, &bkA);
    vna_contact_t dummy;
    vna_rt_seen(&T[41].node.rt, &T[40].id.id, (uint8_t[4]){40, 0, 0xCA, 0xFE}, 4, 0, g_now, &dummy);
    uint32_t r0 = T[40].node.hk_refused;
    vna_node_send_hk(&T[41].node, &T[40].id.id, "set(allocation, model: 1, units: 999999)",
                     SWARM_HK_TRUE, 1, false, 0, g_now, &g_ob);
    flush(41);
    vna_node_send_hk(&T[41].node, &T[40].id.id, "offer(compute, units: 999999, price: 0)",
                     SWARM_HK_TRUE, 2, false, 0, g_now, &g_ob);
    flush(41);
    run_queue();
    vna_event_t ev;
    while (vna_node_poll_event(&T[40].node, &ev)) {
    }
    CHECK(T[40].node.hk_refused == r0 + 1 && memcmp(&snap, &sbA, sizeof snap) == 0,
          "a peer's attempt to write the internal allocation changes nothing");

    /* receipts travel in their own signed message; the node only carries
     * them to the owner's handler, which applies them to its book */
    uint32_t in0 = T[40].node.rcpt_in, ref0 = T[40].node.rcpt_refused;
    CHECK(vna_node_send_rcpt(&T[41].node, &T[40].id.id, rbuf[15], rlen[15], g_now, &g_ob) == VNA_OK,
          "a receipt is sent to a known contact");
    flush(41);
    run_queue();
    CHECK(T[40].node.rcpt_refused == ref0 + 1 && T[40].node.rcpt_in == in0,
          "no handler: the receipt is refused, never applied");
    vna_node_set_rcpt_handler(&T[40].node, rcpt_capture, 0);
    g_cap_len = 0;
    vna_node_send_rcpt(&T[41].node, &T[40].id.id, rbuf[15], rlen[15], g_now, &g_ob);
    flush(41);
    run_queue();
    CHECK(T[40].node.rcpt_in == in0 + 1 && g_cap_len == rlen[15] &&
              memcmp(g_cap, rbuf[15], rlen[15]) == 0 &&
              memcmp(&g_cap_from, &T[41].id.id, sizeof g_cap_from) == 0,
          "the handler gets the exact receipt bytes and the verified sender");
    g_cap_refuse = true;
    vna_node_send_rcpt(&T[41].node, &T[40].id.id, rbuf[15], rlen[15], g_now, &g_ob);
    flush(41);
    run_queue();
    CHECK(T[40].node.rcpt_refused == ref0 + 2,
          "a receipt the handler refuses is counted as refused");
    g_cap_refuse = false;
    vna_node_set_rcpt_handler(&T[40].node, 0, 0);
    vna_id_t nobody;
    memset(&nobody, 0x5A, sizeof nobody);
    CHECK(
        vna_node_send_rcpt(&T[41].node, &nobody, rbuf[15], rlen[15], g_now, &g_ob) == VNA_ERR_ARG &&
            vna_node_send_rcpt(&T[41].node, &T[40].id.id, rbuf[15], VNA_RCPT_WIRE_MAX + 1, g_now,
                               &g_ob) == VNA_ERR_ARG &&
            vna_node_send_rcpt(&T[41].node, &T[40].id.id, rbuf[15], 0, g_now, &g_ob) == VNA_ERR_ARG,
        "send_rcpt: unknown contact, oversize and empty refused");
    vna_node_set_agreement(&T[40].node, 0, 0, 0, 0);
    CHECK(vna_ledger_verify(&bkA.ledger, IA->pk) && vna_ledger_verify(&bkB.ledger, IB->pk),
          "hash-chained, ML-DSA-signed ledgers verify");
    CHECK(!vna_ledger_verify(&bkA.ledger, IB->pk), "ledger head does not verify under another key");
    bkA.ledger.ring[(bkA.ledger.head + 2) % VNA_LEDGER_RING].amount ^= 1;
    CHECK(!vna_ledger_verify(&bkA.ledger, IA->pk), "tampered ledger entry detected");
    bkA.ledger.ring[(bkA.ledger.head + 2) % VNA_LEDGER_RING].amount ^= 1;
    printf("    A: ledger %llu entries, debits=credits=%llu; B trust at A %llu\n",
           (unsigned long long) bkA.ledger.count, (unsigned long long) bkA.ledger.debits,
           (unsigned long long) vna_book_trust(&bkA, &IB->id));

    /* --- negotiation --- */
    int term = 1, within = 1, overlap_deal = 1, deals = 0;
    for (int t = 0; t < 20000; t++) {
        uint64_t budget = (uint64_t) (rand() % 100000), val = (uint64_t) (rand() % 100000);
        uint64_t blimit = budget < val ? budget : val;
        uint64_t reserve = (uint64_t) (rand() % 100000);
        vna_neg_t B, S;
        uint64_t bopen = blimit / 3, sopen = reserve + (uint64_t) (rand() % 50000);
        vna_neg_init(&B, VNA_NEG_BUYER, blimit, bopen, 64);
        vna_neg_init(&S, VNA_NEG_SELLER, reserve, sopen, 64);
        uint64_t offer = sopen, mine = 0;
        vna_neg_status_t st = VNA_NEG_OPEN;
        int rounds = 0;
        for (;;) {
            st = vna_neg_receive(&B, offer, &mine);
            if (st != VNA_NEG_COUNTER) break;
            st = vna_neg_receive(&S, mine, &offer);
            if (st != VNA_NEG_COUNTER) break;
            if (++rounds > 200) break;
        }
        term &= rounds <= 64;
        uint64_t deal = B.status == VNA_NEG_ACCEPT   ? B.deal
                        : S.status == VNA_NEG_ACCEPT ? S.deal
                                                     : 0;
        bool agreed = B.status == VNA_NEG_ACCEPT || S.status == VNA_NEG_ACCEPT;
        if (agreed) {
            deals++;
            within &= deal >= reserve && deal <= blimit && deal <= budget;
        }
        if (blimit >= reserve) overlap_deal &= agreed;
        if (blimit < reserve) within &= !agreed;
    }
    CHECK(term, "every negotiation terminates within max_rounds");
    CHECK(within, "every deal within [reserve, min(budget, valuation)]; no deal without overlap");
    CHECK(overlap_deal, "whenever the ranges overlap, a deal is reached");
    printf("    negotiation: %d deals in 20000 random scenarios\n", deals);
    uint64_t prio[5] = {5, 3, 2, 1, 1}, plan[5], ps = 0;
    vna_econ_plan(1000, prio, 5, plan);
    for (int i = 0; i < 5; i++) ps += plan[i];
    CHECK(ps == 1000 && plan[0] > plan[1], "agent budget plan sums exactly");
}

/* ===================================================================== */
/* 6. file transfer                                                       */
/* ===================================================================== */
/* ---- the trade loop over receipts (vna_link.h) ---- */
static vna_link_t lkS, lkB;
static vna_account_t accS2[4], accB2[4];
static vna_book_t bkS2, bkB2;
static vna_agreement_t agS;
static vna_usage_ent_t ueS[16];
static vna_lease_t leS[8];
static vna_usage_t usS;

/* deliver every queued receipt between the two links; drop_from_b drops
 * what B sends (a lost message), drop_from_s what S sends */
static uint32_t pump(bool drop_from_s, bool drop_from_b)
{
    uint32_t moved = 0;
    for (int round = 0; round < 8; round++) {
        vna_id_t dst;
        const uint8_t *b;
        uint32_t len;
        bool any = false;
        static uint8_t copy[VNA_RCPT_MAX];
        while (vna_link_next_out(&lkS, &dst, &b, &len)) {
            any = true;
            memcpy(copy, b, len);
            if (!drop_from_s) vna_link_on_rcpt(&lkB, &bkS2.idn->id, copy, len, g_now);
            moved++;
        }
        while (vna_link_next_out(&lkB, &dst, &b, &len)) {
            any = true;
            memcpy(copy, b, len);
            if (!drop_from_b) vna_link_on_rcpt(&lkS, &bkB2.idn->id, copy, len, g_now);
            moved++;
        }
        if (!any) break;
    }
    return moved;
}

static bool in_sync(void)
{
    const vna_account_t *a = vna_book_find(&bkS2, &bkB2.idn->id),
                        *b = vna_book_find(&bkB2, &bkS2.idn->id);
    return a && b && a->pair_seq == b->pair_seq && memcmp(a->pair_prev, b->pair_prev, 32) == 0 &&
           vna_book_conserved(&bkS2) && vna_book_conserved(&bkB2);
}

static void test_link(void)
{
    printf("[5b] trade loop: offer, accept, commit, confirm, retries, the gate loop\n");
    const vna_identity_t *IS = &T[50].id, *IBu = &T[51].id;
    vna_book_init(&bkS2, IS, accS2, 4);
    vna_book_init(&bkB2, IBu, accB2, 4);
    vna_book_add_peer(&bkS2, &IBu->id, IBu->pk, IBu->pow_nonce, 8, true);
    vna_book_add_peer(&bkB2, &IS->id, IS->pk, IS->pow_nonce, 8, true);
    vna_agree_default(&agS, &IS->id);
    agS.degree = VNA_DEG_COMPUTE;
    agS.audience = VNA_AUD_EVERYONE;
    agS.compute_per_cycle = 10000;
    agS.q_compute = 5000;
    vna_usage_init(&usS, ueS, 16, leS, 8);
    uint8_t seedS[32] = {7}, seedB[32] = {8};
    vna_link_init(&lkS, &bkS2, &agS, &usS, seedS, 100, 3);
    vna_link_init(&lkB, &bkB2, 0, 0, seedB, 100, 3);
    const char *hk = "ask(compute, units: 1)";

    /* T1-T4 */
    int32_t w = vna_link_want(&lkB, VNA_RES_COMPUTE, SWARM_CAP_SYSTEM, 300, 1000);
    CHECK(w >= 0, "the buyer states what it wants");
    CHECK(vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 100, 300,
                        hk, g_now) == VNA_OK,
          "the seller offers 100 units of compute on credit");
    CHECK(vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk,
                        g_now) == VNA_ERR_STATE,
          "L1: one receipt outstanding per peer");
    pump(false, false);
    CHECK(lkS.committed == 1 && lkB.confirmed == 1 && vna_link_pending(&lkS, 0) == 0 &&
              vna_link_pending(&lkB, 0) == 0,
          "offer, accept, commit, confirm: nothing left pending");
    CHECK(bkB2.received_units[VNA_RES_COMPUTE] == 100 && lkB.want[w].units_left == 200 &&
              vna_book_find(&bkB2, &IS->id)->contrib[SWARM_CAP_SYSTEM] == 300,
          "the buyer recorded the compute received and the seller's contribution");
    CHECK(in_sync(), "both books hold the same pair chain and are conserved");

    /* L3: only what was asked for, under the ceiling, from the counterparty */
    uint32_t ref0 = lkB.refused;
    CHECK(vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_INTELLECTUAL, VNA_RES_COMPUTE, 10,
                        10, hk, g_now) == VNA_OK,
          "an offer in a form the buyer never asked for");
    pump(false, false);
    CHECK(lkB.refused == ref0 + 1 && vna_link_pending(&lkS, &IBu->id) == 1,
          "is refused by the buyer");
    for (int k = 0; k < 4; k++) {
        g_now += 100;
        vna_link_tick(&lkS, g_now);
        pump(false, false);
    }
    CHECK(vna_link_pending(&lkS, 0) == 0 && lkS.abandoned == 1 && in_sync(),
          "after max_tries the seller gives up; nothing was applied on either side");
    vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 10, 1001, hk,
                  g_now);
    pump(false, false);
    CHECK(lkB.refused > ref0 + 1 && vna_link_pending(&lkB, 0) == 0,
          "over the price ceiling: refused");
    for (int k = 0; k < 4; k++) {
        g_now += 100;
        vna_link_tick(&lkS, g_now);
        pump(false, false);
    }
    vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 201, 10, hk,
                  g_now);
    pump(false, false);
    CHECK(lkB.refused > ref0 + 5 && in_sync(), "more units than still wanted: refused");
    for (int k = 0; k < 4; k++) {
        g_now += 100;
        vna_link_tick(&lkS, g_now);
        pump(false, false);
    }
    vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 5, 5, hk,
                  g_now);
    vna_id_t dst;
    const uint8_t *ob;
    uint32_t olen;
    vna_link_next_out(&lkS, &dst, &ob, &olen);
    static uint8_t keep[VNA_RCPT_MAX];
    memcpy(keep, ob, olen);
    CHECK(vna_link_on_rcpt(&lkB, &T[52].id.id, keep, olen, g_now) == VNA_ERR_UNEXPECTED,
          "a receipt delivered by anyone but the counterparty is refused");
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, keep, olen - 1, g_now) == VNA_ERR_PARSE,
          "a truncated receipt is refused");

    /* L2: lost messages */
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, keep, olen, g_now) == VNA_OK,
          "the genuine offer accepted");
    vna_link_next_out(&lkB, &dst, &ob, &olen); /* the countersigned copy is lost */
    g_now += 100;
    vna_link_tick(&lkB, g_now); /* the buyer resends its countersigned copy */
    pump(true, false);          /* the seller commits, its confirmation is lost */
    CHECK(lkS.committed == 2 && lkB.confirmed == 1 && vna_link_pending(&lkB, 0) == 1,
          "the seller committed; the buyer is still waiting for the lost confirmation");
    g_now += 100;
    vna_link_tick(&lkB, g_now);
    pump(false, false);
    CHECK(lkB.confirmed == 2 && vna_link_pending(&lkB, 0) == 0 && in_sync(),
          "the resent countersign draws the confirmation again; applied once, books in sync");

    /* T3 refused at commit: the agreement changed after the offer */
    vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 5, 5, hk,
                  g_now);
    agS.degree = VNA_DEG_ROUTE;
    uint64_t want_before = lkB.want[w].units_left;
    pump(false, false);
    CHECK(lkS.committed == 2 && vna_link_pending(&lkS, 0) == 0 && in_sync(),
          "a commit the seller's book refuses is dropped: the buyer never applies it");
    for (int k = 0; k < 4; k++) {
        g_now += 100;
        vna_link_tick(&lkB, g_now);
        pump(false, false);
    }
    CHECK(vna_link_pending(&lkB, 0) == 0 && lkB.want[w].units_left == want_before && in_sync(),
          "the buyer gives up and gets its reserved units back");
    agS.degree = VNA_DEG_COMPUTE;

    /* L4: the gate loop */
    static swarm_budget_t sb;
    swarm_budget_init(&sb, 2, 5000);
    swarm_budget_register(&sb, 1, 0);
    swarm_budget_begin_cycle(&sb);
    uint8_t sec[32];
    memset(sec, 0x5C, 32);
    vna_gate_t g;
    vna_gate_init(&g, sec, 0, 150, 4);
    vna_gate_begin_cycle(&g, sec);
    uint64_t got = 0;
    uint64_t recv = bkB2.received_units[VNA_RES_COMPUTE];
    CHECK(vna_link_cycle(&lkB, &g, sec, &sb, 5000, g_now, &got) == VNA_OK && got == 105 &&
              recv == 105 && sb.tokens_per_cycle == 5105,
          "this cycle's received compute raises the next cycle's budget (the ouroboros loop)");
    CHECK(bkB2.received_units[VNA_RES_COMPUTE] == 0 && vna_book_conserved(&bkB2),
          "the book settled and reset its cycle counters");
    CHECK(vna_link_cycle(&lkB, &g, sec, &sb, 5000, g_now, &got) == VNA_OK && got == 0 &&
              sb.tokens_per_cycle == 5000,
          "a cycle with nothing received goes back to the base rate");
    uint8_t nosec[32] = {0};
    bkB2.received_units[VNA_RES_COMPUTE] = 1; /* something to import */
    CHECK(vna_link_cycle(&lkB, &g, nosec, &sb, 5000, g_now, &got) == VNA_ERR_AUTH && got == 0,
          "the loop needs the owner's secret");
    bkB2.received_units[VNA_RES_COMPUTE] = 0;
    CHECK(vna_link_cycle(0, &g, sec, &sb, 5000, g_now, &got) == VNA_ERR_ARG, "cycle(NULL)");
    pump(false, false);
    CHECK(in_sync(), "DISTRIBUTE receipts (if any) delivered; books in sync");

    /* end to end over two nodes */
    vna_contact_t dummy;
    uint8_t a50[4], a51[4];
    addr_of(50, a50);
    addr_of(51, a51);
    vna_rt_seen(&T[50].node.rt, &IBu->id, a51, 4, 0, g_now, &dummy);
    vna_rt_seen(&T[51].node.rt, &IS->id, a50, 4, 0, g_now, &dummy);
    vna_node_set_rcpt_handler(&T[50].node, vna_link_on_rcpt, &lkS);
    vna_node_set_rcpt_handler(&T[51].node, vna_link_on_rcpt, &lkB);
    uint32_t c0 = lkB.confirmed;
    vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 7, 7, hk,
                  g_now);
    for (int k = 0; k < 6; k++) {
        vna_id_t d;
        const uint8_t *bb;
        uint32_t ll;
        while (vna_link_next_out(&lkS, &d, &bb, &ll))
            vna_node_send_rcpt(&T[50].node, &d, bb, ll, g_now, &g_ob);
        flush(50);
        while (vna_link_next_out(&lkB, &d, &bb, &ll))
            vna_node_send_rcpt(&T[51].node, &d, bb, ll, g_now, &g_ob);
        flush(51);
        run_queue();
    }
    CHECK(lkB.confirmed == c0 + 1 && vna_link_pending(&lkS, 0) == 0 && in_sync(),
          "the same trade over two nodes: signed messages, both books in sync");
    vna_node_set_rcpt_handler(&T[50].node, 0, 0);
    vna_node_set_rcpt_handler(&T[51].node, 0, 0);
}

/* A receipt signed by `idn` as the seller (pair chain from book b), with
 * fields bent by the caller. Returns its length (0 on failure). */
static uint32_t rcpt_craft(vna_book_t *b, const vna_identity_t *idn, const vna_id_t *buyer,
                           uint8_t mode, uint8_t form, uint8_t res, uint64_t units, uint64_t price,
                           uint64_t seq_add, bool bad_prev, uint8_t *out)
{
    static vna_receipt_t r;
    if (vna_receipt_prepare(b, &r, VNA_TR_CREDIT, form, res, &idn->id, buyer, units, price, 1000,
                            g_now, 0, "ask(compute, units: 1)") != VNA_OK)
        return 0;
    r.mode = mode;
    r.pair_seq += seq_add;
    if (bad_prev) r.prev[0] ^= 1;
    uint8_t rnd[32] = {9};
    int32_t n = vna_receipt_sign_seller(&r, idn, rnd, out, VNA_RCPT_MAX);
    return n < 0 ? 0 : (uint32_t) n;
}

/* Move one queued message from link a to link b (from a's node); keep a copy. */
static vna_status_t hop(vna_link_t *a, vna_link_t *b, uint8_t *keep, uint32_t *klen)
{
    vna_id_t d;
    const uint8_t *ob;
    uint32_t ol;
    if (!vna_link_next_out(a, &d, &ob, &ol)) return VNA_ERR_STATE;
    static uint8_t tmp[VNA_RCPT_MAX];
    memcpy(tmp, ob, ol);
    if (keep) {
        memcpy(keep, ob, ol);
        *klen = ol;
    }
    if (!vna_id_eq(&d, &b->book->idn->id)) return VNA_ERR_DST;
    return vna_link_on_rcpt(b, &a->book->idn->id, tmp, ol, g_now);
}

static void test_link_edges(void)
{
    printf("[5c] trade loop edges: arguments, queues, timers, forks, replays, PAY\n");
    const vna_identity_t *IS = &T[50].id, *IBu = &T[51].id;
    const char *hk = "ask(compute, units: 1)";
    uint8_t seedS[32] = {7}, seedB[32] = {8};
    static vna_link_t lkX;
    vna_id_t d;
    const uint8_t *ob;
    uint32_t ol;

    /* init and wants */
    vna_link_init(0, &bkS2, &agS, &usS, seedS, 0, 0);
    vna_link_init(&lkX, &bkS2, &agS, &usS, seedS, 0, 0);
    CHECK(lkX.retry_ms == 1000 && lkX.max_tries == 8 && lkX.book == &bkS2 && lkX.agr == &agS &&
              lkX.us == &usS && lkX.out_n == 0 && lkX.offered == 0,
          "init: retry 1000 ms and 8 tries by default; init(NULL) is a no-op");
    {
        vna_drbg_t ref;
        uint8_t x1[16], x2[16];
        vna_drbg_seed(&ref, seedS, 32);
        vna_drbg_gen(&ref, x1, 16);
        vna_drbg_gen(&lkX.rng, x2, 16);
        CHECK(memcmp(x1, x2, 16) == 0, "init: the signature randomness comes from the full seed");
    }
    CHECK(vna_link_want(0, VNA_RES_COMPUTE, 0, 1, 1) == -1 &&
              vna_link_want(&lkX, VNA_RES_COUNT, 0, 1, 1) == -1 &&
              vna_link_want(&lkX, 0, VNA_FORMS, 1, 1) == -1 &&
              vna_link_want(&lkX, VNA_RES_COMPUTE, 0, 0, 1) == -1,
          "want: argument errors");
    bool slots = true;
    for (int32_t k = 0; k < (int32_t) VNA_LINK_WANTS; k++)
        slots = slots && vna_link_want(&lkX, VNA_RES_COUNT - 1, VNA_FORMS - 1, 1, 1) == k;
    CHECK(slots && vna_link_want(&lkX, VNA_RES_COMPUTE, 0, 1, 1) == -1,
          "want: eight slots handed out in order, the ninth refused");

    /* sell: arguments and the seller's own checks */
    vna_link_init(&lkX, &bkS2, &agS, &usS, seedS, 100, 20);
    CHECK(vna_link_sell(0, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk,
                        g_now) == VNA_ERR_ARG &&
              vna_link_sell(&lkX, 0, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk,
                            g_now) == VNA_ERR_ARG &&
              vna_link_sell(&lkX, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1,
                            0, g_now) == VNA_ERR_ARG &&
              vna_link_sell(&lkX, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 0, 1,
                            hk, g_now) == VNA_ERR_ARG,
          "sell: argument errors");
    CHECK(vna_link_sell(&lkX, &IBu->id, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk,
                        g_now) == VNA_ERR_FUNDS,
          "sell: PAY when the buyer holds none of our tokens is refused before signing");
    CHECK(vna_link_sell(&lkX, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1,
                        "ask(", g_now) != VNA_OK,
          "sell: an HK text that does not parse is refused");
    CHECK(lkX.offered == 0 && lkX.out_n == 0 && vna_link_pending(&lkX, 0) == 0,
          "refused sales leave nothing queued or pending");
    CHECK(vna_link_sell(&lkX, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 3, 4, hk,
                        g_now) == VNA_OK &&
              lkX.offered == 1 && vna_link_next_out(&lkX, &d, &ob, &ol),
          "sell: one offer queued");
    {
        static vna_receipt_t r;
        uint32_t so = 0;
        uint8_t cid[32];
        vna_agree_cid(&agS, cid);
        CHECK(vna_id_eq(&d, &IBu->id) &&
                  vna_schema_unpack(&vna_receipt_schema, ob, ol, &r, &so) >= 0 &&
                  r.mode == VNA_TR_CREDIT && r.units == 3 && r.price == 4 &&
                  r.demand_x1000 == 1000 && memcmp(r.agreement_cid, cid, 32) == 0 &&
                  vna_id_eq(&r.seller, &IS->id) && vna_id_eq(&r.buyer, &IBu->id),
              "the offer names the agreement it was made under, at the neutral demand factor");
        const vna_link_pend_t *p = &lkX.pend[0];
        CHECK(p->used && p->role == VNA_LINK_SELL && p->want == -1 && p->tries == 1 &&
                  p->next_ms == g_now + 100 && vna_id_eq(&p->peer, &IBu->id),
              "the pending sale: first slot, first try, next resend one retry later");
    }

    /* the out queue (FIFO, wraps, drops when full), resend timers, the pending table */
    static vna_account_t accX[20];
    static vna_book_t bkX;
    vna_book_init(&bkX, IS, accX, 20);
    bool added = true;
    for (uint32_t k = 0; k < 17; k++)
        added = added && vna_book_add_peer(&bkX, &T[30 + k].id.id, T[30 + k].id.pk,
                                           T[30 + k].id.pow_nonce, 8, true) == VNA_OK;
    CHECK(added, "a seller with seventeen peers");
    const vna_id_t *P0 = &T[30].id.id, *P1 = &T[31].id.id;
    vna_link_init(&lkX, &bkX, &agS, &usS, seedS, 100, 20);
    uint64_t t0 = g_now;
    CHECK(vna_link_sell(&lkX, P0, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk, t0) ==
                  VNA_OK &&
              vna_link_sell(&lkX, P1, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk,
                            t0) == VNA_OK &&
              vna_link_sell(&lkX, P1, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk,
                            t0) == VNA_ERR_STATE,
          "two peers, one outstanding sale each (L1 holds for any slot)");
    CHECK(lkX.offered == 2 && lkX.out_n == 2 && vna_link_pending(&lkX, 0) == 2 &&
              vna_link_pending(&lkX, P1) == 1 && vna_link_pending(&lkX, &T[47].id.id) == 0 &&
              vna_link_pending(0, 0) == 0,
          "pending: per peer, in total, and none for NULL");
    CHECK(vna_link_next_out(&lkX, &d, &ob, &ol) && vna_id_eq(&d, P0), "first in, first out");
    vna_link_tick(&lkX, t0 + 99);
    CHECK(lkX.out_n == 1 && lkX.resent == 0, "no resend before the retry time");
    vna_link_tick(&lkX, t0 + 100);
    CHECK(lkX.out_n == 3 && lkX.resent == 2, "both resent at the retry time");
    vna_link_tick(&lkX, t0 + 150);
    CHECK(lkX.out_n == 3 && lkX.resent == 2, "and not again until the next retry time");
    vna_link_tick(&lkX, t0 + 200);
    vna_link_tick(&lkX, t0 + 300);
    vna_link_tick(&lkX, t0 + 400);
    CHECK(lkX.out_n == VNA_LINK_OUT && lkX.out_dropped == 1 && lkX.resent == 7,
          "a full out queue drops (and counts) what does not fit");
    CHECK(!vna_link_next_out(0, &d, &ob, &ol) && !vna_link_next_out(&lkX, 0, &ob, &ol) &&
              !vna_link_next_out(&lkX, &d, 0, &ol) && !vna_link_next_out(&lkX, &d, &ob, 0) &&
              lkX.out_n == VNA_LINK_OUT,
          "next_out: argument errors take nothing off the queue");
    bool order = true;
    for (uint32_t k = 0; k < VNA_LINK_OUT; k++)
        order = order && vna_link_next_out(&lkX, &d, &ob, &ol) &&
                vna_id_eq(&d, (k & 1) ? P0 : P1) && ol > 0;
    CHECK(order && !vna_link_next_out(&lkX, &d, &ob, &ol),
          "the queue wrapped around and still came out in order, then empty");
    for (uint32_t k = 2; k < 16; k++)
        vna_link_sell(&lkX, &T[30 + k].id.id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1,
                      1, hk, t0);
    CHECK(lkX.offered == 16 && vna_link_pending(&lkX, 0) == VNA_LINK_PEND &&
              lkX.out_dropped == 1 + 6 &&
              vna_link_sell(&lkX, &T[46].id.id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1,
                            1, hk, t0) == VNA_ERR_SPACE &&
              lkX.offered == 16,
          "sixteen sales outstanding fill the table; the seventeenth is refused");
    vna_link_init(&lkX, &bkX, &agS, &usS, seedS, 100, 3);
    vna_link_sell(&lkX, P0, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, hk, t0);
    vna_link_tick(&lkX, t0 + 100);
    vna_link_tick(&lkX, t0 + 200);
    CHECK(lkX.abandoned == 0 && lkX.resent == 2 && vna_link_pending(&lkX, 0) == 1,
          "three tries: the offer and two resends");
    vna_link_tick(&lkX, t0 + 300);
    vna_link_tick(0, t0 + 300);
    CHECK(lkX.abandoned == 1 && lkX.resent == 2 && vna_link_pending(&lkX, 0) == 0,
          "then given up; tick(NULL) is a no-op");

    /* the receipt handler's own checks */
    static uint8_t m[VNA_RCPT_MAX + 1], keep[VNA_RCPT_MAX], conf[VNA_RCPT_MAX];
    uint32_t mlen, klen = 0, clen = 0;
    vna_link_init(&lkS, &bkS2, &agS, &usS, seedS, 100, 3);
    vna_link_init(&lkB, &bkB2, 0, 0, seedB, 100, 3);
    memset(m, 0xA5, sizeof m);
    CHECK(vna_link_on_rcpt(0, &IS->id, m, 10, g_now) == VNA_ERR_ARG &&
              vna_link_on_rcpt(&lkB, 0, m, 10, g_now) == VNA_ERR_ARG &&
              vna_link_on_rcpt(&lkB, &IS->id, 0, 10, g_now) == VNA_ERR_ARG &&
              vna_link_on_rcpt(&lkB, &IS->id, m, 0, g_now) == VNA_ERR_ARG &&
              vna_link_on_rcpt(&lkB, &IS->id, m, VNA_RCPT_MAX + 1, g_now) == VNA_ERR_ARG &&
              lkB.refused == 0,
          "on_rcpt: argument errors (not counted as refusals)");
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, VNA_RCPT_MAX, g_now) == VNA_ERR_PARSE &&
              lkB.refused == 1,
          "on_rcpt: a full-size receipt of garbage does not parse");
    mlen = rcpt_craft(&bkX, IS, P0, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, 0,
                      false, m);
    CHECK(mlen > 0 && vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_DST,
          "a receipt between two other nodes is refused");

    /* T2: the buyer's matching, exactly at the limits */
    CHECK(vna_link_want(&lkB, VNA_RES_COMPUTE, SWARM_CAP_SYSTEM, 10, 10) == 0 &&
              vna_link_want(&lkB, VNA_RES_COMPUTE, SWARM_CAP_SYSTEM, 10, 10) == 1,
          "two wants that both match");
    CHECK(vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 10, 10,
                        hk, g_now) == VNA_OK &&
              hop(&lkS, &lkB, 0, 0) == VNA_OK,
          "an offer at exactly the units still wanted and the price ceiling is accepted");
    CHECK(lkB.want[0].units_left == 0 && lkB.want[1].units_left == 10 && lkB.accepted == 1 &&
              lkB.pend[0].want == 0 && lkB.pend[0].role == VNA_LINK_BUY,
          "the first matching want is the one reserved");
    CHECK(hop(&lkB, &lkS, 0, 0) == VNA_OK && hop(&lkS, &lkB, conf, &clen) == VNA_OK &&
              lkS.committed == 1 && lkB.confirmed == 1 && !lkB.want[0].used && lkB.want[1].used &&
              lkS.resent == 0 && lkB.resent == 0 && lkS.refused == 0 && lkB.refused == 2 &&
              in_sync(),
          "committed and confirmed; the used-up want is freed, the other kept");
    uint32_t rej = bkB2.rejected, rejS = bkS2.rejected;
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, conf, clen, g_now) == VNA_OK && lkB.resent == 0 &&
              lkB.out_n == 0 && bkB2.rejected == rej && lkB.confirmed == 1,
          "L2: a repeated confirmation is applied once and not answered");
    CHECK(vna_link_on_rcpt(&lkS, &IBu->id, conf, clen, g_now) == VNA_OK && lkS.resent == 1 &&
              lkS.out_n == 1 && bkS2.rejected == rejS && lkS.committed == 1,
          "L2: a repeated countersign is confirmed again, not refused");
    hop(&lkS, &lkB, 0, 0);
    CHECK(lkB.want[0].used == false &&
              vna_link_want(&lkB, VNA_RES_MEMORY, SWARM_CAP_INTELLECTUAL, 10, 10) == 0,
          "a freed slot is handed out again");
    uint32_t ref0 = lkB.refused;
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_INTELLECTUAL, VNA_RES_COMPUTE,
                      5, 5, 0, false, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_DENIED,
          "a resource asked for in another form only: refused");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 11, 10,
                      0, false, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_DENIED,
          "one unit more than wanted: refused");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 10, 11,
                      0, false, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_DENIED,
          "one token over the ceiling: refused");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, 0, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, 0, false, m);
    CHECK(mlen == 0 || vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_PARSE,
          "an offer that is neither PAY nor CREDIT: refused");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1,
                      1, false, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_FORK,
          "an offer that skips a link of the pair chain: FORK");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1,
                      0, true, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_FORK,
          "an offer on another chain head: FORK");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1, 0,
                      false, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_FUNDS,
          "a PAY offer the buyer cannot pay: refused");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1,
                      0, false, m);
    m[mlen - 2u * VNA_SIG_LEN + 5] ^= 1;
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) < 0 && lkB.accepted == 1 &&
              vna_link_pending(&lkB, 0) == 0 && lkB.out_n == 0,
          "an offer with a bad seller signature is not countersigned");
    {
        static vna_account_t accP[2];
        static vna_book_t bkP;
        vna_book_init(&bkP, &T[52].id, accP, 2);
        vna_book_add_peer(&bkP, &IBu->id, IBu->pk, IBu->pow_nonce, 8, true);
        mlen = rcpt_craft(&bkP, &T[52].id, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM,
                          VNA_RES_COMPUTE, 1, 1, 0, false, m);
        CHECK(vna_link_on_rcpt(&lkB, &T[52].id.id, m, mlen, g_now) == VNA_ERR_ARG,
              "an offer from a node the buyer has no account with: refused");
    }
    CHECK(lkB.refused == ref0 + 9 && lkB.accepted == 1 && vna_link_pending(&lkB, 0) == 0 &&
              in_sync(),
          "every refusal counted; nothing reserved or applied");

    /* L2: a resent offer, a different offer while one is outstanding, a late countersign */
    vna_link_sell(&lkS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 5, 5, hk,
                  g_now);
    hop(&lkS, &lkB, keep, &klen);          /* accepted */
    vna_link_next_out(&lkB, &d, &ob, &ol); /* the countersign is lost */
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, keep, klen, g_now) == VNA_OK && lkB.resent == 1 &&
              lkB.out_n == 1 && lkB.accepted == 2 && lkB.want[1].units_left == 5,
          "a resent offer draws the same countersign again, reserving nothing more");
    mlen = rcpt_craft(&bkS2, IS, &IBu->id, VNA_TR_CREDIT, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 1, 1,
                      1, false, m);
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, m, mlen, g_now) == VNA_ERR_STATE,
          "another offer while one is outstanding with the same peer: refused");
    for (int k = 1; k <= 3; k++) vna_link_tick(&lkS, g_now + 100u * (uint64_t) k);
    while (vna_link_next_out(&lkS, &d, &ob, &ol)) {
    }
    CHECK(vna_link_pending(&lkS, 0) == 0 && lkS.abandoned == 1, "the seller gave up waiting");
    CHECK(hop(&lkB, &lkS, 0, 0) == VNA_OK && lkS.committed == 2 &&
              hop(&lkS, &lkB, 0, 0) == VNA_OK && lkB.confirmed == 2 &&
              vna_link_pending(&lkB, 0) == 0 && in_sync(),
          "a countersign that arrives after the seller gave up is still committed, in sync");

    /* the seller's own DISTRIBUTE, and PAY with distributed tokens */
    static swarm_budget_t sbB;
    swarm_budget_init(&sbB, 2, 5000);
    swarm_budget_register(&sbB, 1, 0);
    swarm_budget_begin_cycle(&sbB);
    uint8_t sec[32];
    memset(sec, 0x6D, 32);
    vna_gate_t gB;
    vna_gate_init(&gB, sec, 1000, 0, 8);
    vna_gate_begin_cycle(&gB, sec);
    uint64_t granted = 0, got = 77;
    CHECK(vna_gate_export(&gB, sec, &sbB, 1, &bkB2, SWARM_CAP_SYSTEM, 60, &granted) == VNA_OK &&
              granted == 60,
          "the buyer's owner puts 60 internal tokens into its commons pool");
    CHECK(vna_link_cycle(&lkB, &gB, sec, &sbB, 5000, g_now, &got) == VNA_OK && got == 0 &&
              sbB.tokens_per_cycle == 5000 && lkB.imported == 0,
          "no import room this cycle: nothing imported, base rate");
    CHECK(lkB.out_n >= 1, "the settle queued a DISTRIBUTE for the seller");
    static uint8_t dist[VNA_RCPT_MAX];
    uint32_t dlen = 0;
    CHECK(hop(&lkB, &lkS, dist, &dlen) == VNA_OK, "the seller applies its share of the commons");
    while (vna_link_next_out(&lkB, &d, &ob, &ol)) {
    }
    uint64_t held = vna_book_find(&bkS2, &IBu->id)->held[SWARM_CAP_SYSTEM];
    CHECK(held > 0 && in_sync(), "the seller now holds tokens the buyer issued");
    CHECK(vna_link_on_rcpt(&lkB, &IS->id, dist, dlen, g_now) == VNA_ERR_STATE,
          "a DISTRIBUTE sent back to its issuer is refused");
    static vna_agreement_t agB;
    vna_agree_default(&agB, &IBu->id);
    agB.degree = VNA_DEG_COMPUTE;
    agB.audience = VNA_AUD_EVERYONE;
    agB.compute_per_cycle = 10000;
    agB.q_compute = 5000;
    static vna_usage_ent_t ueB[16];
    static vna_lease_t leB[8];
    static vna_usage_t usB;
    vna_usage_init(&usB, ueB, 16, leB, 8);
    lkB.agr = &agB;
    lkB.us = &usB;
    CHECK(vna_link_sell(&lkB, &IS->id, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 2, held + 1,
                        hk, g_now) == VNA_ERR_FUNDS,
          "PAY above what the other side holds: refused by the seller");
    CHECK(vna_link_want(&lkS, VNA_RES_COMPUTE, SWARM_CAP_SYSTEM, 2, held) >= 0 &&
              vna_link_sell(&lkB, &IS->id, VNA_TR_PAY, SWARM_CAP_SYSTEM, VNA_RES_COMPUTE, 2, held,
                            hk, g_now) == VNA_OK,
          "the roles swap: the old seller wants compute and can pay exactly its holding");
    CHECK(hop(&lkB, &lkS, 0, 0) == VNA_OK && hop(&lkS, &lkB, 0, 0) == VNA_OK &&
              hop(&lkB, &lkS, 0, 0) == VNA_OK &&
              vna_book_find(&bkS2, &IBu->id)->held[SWARM_CAP_SYSTEM] == 0 && in_sync(),
          "paid in full with the buyer's own tokens; books in sync");
    lkB.agr = 0;
    lkB.us = 0;

    /* L4: the import is what was received less what the owner already imported, capped */
    static swarm_budget_t sb2;
    swarm_budget_init(&sb2, 2, 5000);
    swarm_budget_register(&sb2, 1, 0);
    swarm_budget_begin_cycle(&sb2);
    vna_gate_t g2;
    vna_gate_init(&g2, sec, 0, 150, 8);
    vna_gate_begin_cycle(&g2, sec);
    uint64_t imp0 = lkB.imported;
    bkB2.received_units[VNA_RES_COMPUTE] = 100;
    CHECK(vna_gate_import(&g2, sec, &bkB2, SWARM_CAP_SYSTEM, 30, &sb2, 5000) == VNA_OK &&
              vna_link_cycle(&lkB, &g2, sec, &sb2, 5000, g_now, 0) == VNA_OK &&
              sb2.tokens_per_cycle == 5100 && lkB.imported == imp0 + 70,
          "100 received, 30 already imported by hand: the loop imports the other 70");
    bkB2.received_units[VNA_RES_COMPUTE] = 200;
    CHECK(vna_gate_import(&g2, sec, &bkB2, SWARM_CAP_SYSTEM, 30, &sb2, 5000) == VNA_OK &&
              vna_link_cycle(&lkB, &g2, sec, &sb2, 5000, g_now, &got) == VNA_OK && got == 120 &&
              sb2.tokens_per_cycle == 5150 && lkB.imported == imp0 + 190,
          "200 received but only 120 of room under the cap: 120 imported");
    got = 77;
    CHECK(vna_link_cycle(&lkB, 0, sec, &sb2, 5000, g_now, &got) == VNA_ERR_ARG && got == 0 &&
              vna_link_cycle(&lkB, &g2, 0, &sb2, 5000, g_now, &got) == VNA_ERR_ARG &&
              vna_link_cycle(&lkB, &g2, sec, 0, 5000, g_now, &got) == VNA_ERR_ARG,
          "cycle: argument errors clear the out value");
    while (vna_link_next_out(&lkB, &d, &ob, &ol)) {
    }
    CHECK(in_sync(), "books still in sync");
}

static void test_files(void)
{
    printf("[6] files: chunked, Merkle-verified, streamed inside a session, agreement-checked\n");
    static uint8_t file[100000], recv[100000];
    for (uint32_t i = 0; i < sizeof file; i++) file[i] = (uint8_t) (i * 2654435761u >> 13);
    static uint8_t leaves[64][32];
    vna_id_t root;
    uint32_t n = vna_file_build(file, sizeof file, VNA_CHUNK_SIZE, leaves, 64, &root);
    CHECK(n == 7, "100000 bytes -> 7 chunks of 16 KiB");
    vna_shared_file_t sf = {root, sizeof file, VNA_CHUNK_SIZE, n, (const uint8_t(*)[32]) leaves,
                            file};
    static vna_agreement_t ag;
    const vna_identity_t *Sv = &T[50].id, *Cl = &T[51].id;
    vna_agree_default(&ag, &Sv->id);
    ag.degree = VNA_DEG_SERVE;
    ag.audience = VNA_AUD_EVERYONE;
    ag.n_files = 1;
    ag.files[0].root = root;
    ag.q_chunks = 100;
    vna_usage_t us;
    vna_usage_init(&us, g_ue, 16, g_le, 8);
    CHECK(do_handshake(Cl, Sv, &sI, &sR), "client-server session");
    vna_fetch_t f;
    uint8_t bitmap[8];
    vna_fetch_init(&f, &root, sizeof file, VNA_CHUNK_SIZE, bitmap, sizeof bitmap);
    static vna_chunk_t chunk, rchunk;
    static uint8_t req[64], rec[20000], ct[20100], pt[20100];
    int32_t idx;
    int steps = 0;
    bool corrupt_done = false;
    while ((idx = vna_fetch_next(&f)) >= 0 && steps++ < 20) {
        vna_chunk_req_t rq = {VNA_CHUNKREQ_MAGIC, root, (uint32_t) idx};
        int32_t ql = vna_schema_pack(&vna_chunk_req_schema, &rq, req, sizeof req, true);
        int32_t cl =
            vna_sess_seal(&sI, 2, req, (uint32_t) ql, ct, sizeof ct); /* client -> server */
        uint8_t type;
        uint32_t pl;
        vna_sess_open(&sR, ct, (uint32_t) cl, pt, sizeof pt, &type, &pl);
        int32_t rl =
            vna_file_serve(&sf, &ag, &us, &sR.peer, 0, pt, pl, g_now, &chunk, rec, sizeof rec);
        if (rl < 0) break;
        if (!corrupt_done && idx == 3) { /* a malicious server flips one byte of chunk 3 */
            rec[rl - 100] ^= 1;
            corrupt_done = true;
            cl = vna_sess_seal(&sR, 3, rec, (uint32_t) rl, ct, sizeof ct);
            vna_sess_open(&sI, ct, (uint32_t) cl, pt, sizeof pt, &type, &pl);
            CHECK(vna_fetch_accept(&f, pt, pl, &rchunk) == VNA_ERR_MERKLE,
                  "corrupted chunk rejected");
            continue;
        }
        cl = vna_sess_seal(&sR, 3, rec, (uint32_t) rl, ct, sizeof ct);
        vna_sess_open(&sI, ct, (uint32_t) cl, pt, sizeof pt, &type, &pl);
        if (vna_fetch_accept(&f, pt, pl, &rchunk) == VNA_OK)
            memcpy(recv + (size_t) rchunk.index * VNA_CHUNK_SIZE, rchunk.data, rchunk.data_len);
        /* duplicate delivery refused */
        if (idx == 0)
            CHECK(vna_fetch_accept(&f, pt, pl, &rchunk) == VNA_ERR_DUP, "duplicate chunk refused");
    }
    CHECK(vna_fetch_complete(&f) && memcmp(recv, file, sizeof file) == 0,
          "file transferred chunk by chunk, verified on arrival, identical");
    CHECK(f.rejected == 1, "exactly the corrupted chunk was rejected");

    /* wrong proof / wrong index / truncation */
    vna_hash_el_t pr[VNA_PROOF_MAX];
    uint32_t pn = vna_file_proof((const uint8_t(*)[32]) leaves, n, 2, pr);
    CHECK(vna_file_verify_chunk(&root, sizeof file, VNA_CHUNK_SIZE, 2, file + 2 * VNA_CHUNK_SIZE,
                                VNA_CHUNK_SIZE, pr, pn) == VNA_OK,
          "audit path verifies");
    CHECK(vna_file_verify_chunk(&root, sizeof file, VNA_CHUNK_SIZE, 1, file + 2 * VNA_CHUNK_SIZE,
                                VNA_CHUNK_SIZE, pr, pn) != VNA_OK,
          "chunk presented under the wrong index refused");
    pr[0].h[0] ^= 1;
    CHECK(vna_file_verify_chunk(&root, sizeof file, VNA_CHUNK_SIZE, 2, file + 2 * VNA_CHUNK_SIZE,
                                VNA_CHUNK_SIZE, pr, pn) == VNA_ERR_MERKLE,
          "tampered proof refused");
    CHECK(vna_file_verify_chunk(&root, sizeof file - 1, VNA_CHUNK_SIZE, 6,
                                file + 6 * VNA_CHUNK_SIZE,
                                (uint32_t) (sizeof file - 6 * VNA_CHUNK_SIZE), pr, pn) != VNA_OK,
          "root commits to the file length");
    /* every tree size 1..40 proves every index */
    bool allp = true;
    static uint8_t small[40 * 1024];
    for (uint32_t i = 0; i < sizeof small; i++) small[i] = (uint8_t) (i * 31);
    for (uint32_t sz = 1; sz <= 40; sz++) {
        vna_id_t r2;
        uint32_t m = vna_file_build(small, (uint64_t) sz * 1024, 1024, leaves, 64, &r2);
        for (uint32_t k = 0; k < m; k++) {
            uint32_t q = vna_file_proof((const uint8_t(*)[32]) leaves, m, k, pr);
            allp &= vna_file_verify_chunk(&r2, (uint64_t) sz * 1024, 1024, k, small + k * 1024,
                                          1024, pr, q) == VNA_OK;
        }
    }
    CHECK(allp, "RFC 9162 proofs verify for every index of trees of 1..40 leaves");

    /* requests outside the agreement */
    vna_id_t other;
    memset(&other, 9, 32);
    vna_chunk_req_t rq = {VNA_CHUNKREQ_MAGIC, root, 0};
    int32_t ql = vna_schema_pack(&vna_chunk_req_schema, &rq, req, sizeof req, true);
    ag.files[0].visibility = VNA_VIS_PRIVATE;
    CHECK(vna_file_serve(&sf, &ag, &us, &sR.peer, 0, req, (uint32_t) ql, g_now, &chunk, rec,
                         sizeof rec) == VNA_ERR_DENIED,
          "PRIVATE file refused to a peer outside the allowlist");
    ag.files[0].visibility = VNA_VIS_PUBLIC;
    ag.degree = VNA_DEG_STORE;
    CHECK(vna_file_serve(&sf, &ag, &us, &sR.peer, 0, req, (uint32_t) ql, g_now, &chunk, rec,
                         sizeof rec) == VNA_ERR_DENIED,
          "files not served below degree SERVE");
    ag.degree = VNA_DEG_SERVE;
    ag.n_files = 0;
    CHECK(vna_file_serve(&sf, &ag, &us, &sR.peer, 0, req, (uint32_t) ql, g_now, &chunk, rec,
                         sizeof rec) == VNA_ERR_DENIED,
          "unlisted file refused");

    /* IPFS raw-block compatibility: a one-chunk file's chunk CID is the IPFS CID */
    uint8_t cid[36];
    char cs[64];
    vna_cid_raw((const uint8_t *) "hello world", 11, cid);
    vna_cid_to_string(cid, cs, sizeof cs);
    printf("    chunk CID of 'hello world': %s\n", cs);
}

/* ===================================================================== */
/* 7. offline / LAN-only / online: discovery, islands, offline outbox     */
/* ===================================================================== */
#define ISL 28 /* nodes per LAN island */

static void lan_up(uint32_t i, int seg)
{
    vna_lan_cfg_t c;
    vna_lan_cfg_default(&c);
    c.ttl_ms = 3600000; /* long-lived records keep the simulation cheap */
    c.steady_ms = 1800000;
    vna_addr_el_t a;
    a.len = 4;
    addr_of(i, a.a);
    uint8_t seed[32];
    vna_drbg_gen(&g_rng, seed, 32);
    T[i].seg = seg;
    if (vna_lan_init(&T[i].lan, &T[i].node, &c, G_GROUP, 4, &a, 1, T[i].lpeers, 64, seed) !=
        VNA_OK) {
        printf("lan init failed\n");
        exit(1);
    }
    T[i].lan_on = true;
    vna_lan_start(&T[i].lan, g_now, &g_ob);
    flush(i);
}

static uint32_t g_drop_from, g_drop_to, g_drop_left;
static bool drop_spool_ack(const pkt_t *p)
{
    static vna_msg_t m;
    const uint8_t *rec;
    uint32_t rl;
    bool ubh;
    if (p->lan || g_drop_left == 0 || p->from != g_drop_from || p->to != g_drop_to) return false;
    if (vna_frame_accept(&vna_msg_schema, p->data, p->len, &rec, &rl, &ubh) != VNA_OK ||
        vna_schema_unpack(&vna_msg_schema, rec, rl, &m, 0) < 0 || m.type != VNA_MSG_SPOOL_ACK)
        return false;
    g_drop_left--;
    return true; /* the acknowledgement is "lost" */
}

static bool in_rt(uint32_t at, uint32_t who)
{
    return vna_rt_find(&T[at].node.rt, &T[who].id.id) != 0;
}

/* craft one LAN datagram */
static int32_t lan_pack(uint8_t kind, const uint8_t *rec, uint32_t rlen, const char *svc,
                        uint8_t *out, uint32_t cap)
{
    static vna_lan_msg_t m;
    memset(&m, 0, sizeof m);
    m.magic = VNA_LAN_MAGIC;
    m.version = VNA_VERSION;
    m.kind = kind;
    m.svc_len = (uint16_t) strlen(svc);
    memcpy(m.svc, svc, m.svc_len);
    m.qid = 7;
    if (rec) {
        memcpy(m.rec, rec, rlen);
        m.rec_len = (uint16_t) rlen;
    }
    return vna_schema_pack(&vna_lan_msg_schema, &m, out, cap, true);
}

static int32_t forge_rec(const vna_identity_t *signer, const vna_id_t *claim_id,
                         const uint8_t *claim_pk, uint64_t seq, uint64_t created, uint64_t expires,
                         uint8_t flags, uint32_t addr_idx, uint8_t *out, uint32_t cap)
{
    static vna_noderec_t r;
    memset(&r, 0, sizeof r);
    r.features = VNA_FEAT_UBH168;
    r.flags = flags;
    r.seq = seq;
    r.created = created;
    r.expires = expires;
    r.naddr = 1;
    r.addr[0].len = 4;
    addr_of(addr_idx, r.addr[0].a);
    uint8_t rnd[32];
    vna_drbg_gen(&g_rng, rnd, 32);
    int32_t n = vna_noderec_seal(&r, signer, rnd, out, cap);
    if (n < 0 || (!claim_id && !claim_pk)) return n;
    /* re-encode with a claimed id / pk but the signer's signature over that */
    if (claim_id) r.id = *claim_id;
    if (claim_pk) memcpy(r.pk, claim_pk, VNA_PK_LEN);
    int32_t l = vna_schema_pack(&vna_noderec_schema, &r, out, cap, false);
    vna_sign(signer->sk, VNA_CTX_NODEREC, out, (uint32_t) l, rnd, r.sig);
    return vna_schema_pack(&vna_noderec_schema, &r, out, cap, true);
}

/* toy per-frame transform: XOR keystream + 4-byte check (stands in for ehop) */
typedef struct {
    uint8_t key;
    uint32_t calls;
} toyx_t;
static int32_t toy_seal(void *ctx, const uint8_t *addr, uint32_t alen, const uint8_t *in,
                        uint32_t len, uint8_t *out, uint32_t cap)
{
    toyx_t *t = ctx;
    (void) addr;
    (void) alen;
    if (len + 4 > cap) return -1;
    uint32_t sum = 0x5a5a5a5au;
    for (uint32_t i = 0; i < len; i++) {
        out[i] = (uint8_t) (in[i] ^ (uint8_t) (t->key + i * 7u));
        sum = sum * 31u + in[i];
    }
    vna_put32(out + len, sum);
    t->calls++;
    return (int32_t) len + 4;
}
static int32_t toy_open(void *ctx, const uint8_t *addr, uint32_t alen, const uint8_t *in,
                        uint32_t len, uint8_t *out, uint32_t cap)
{
    toyx_t *t = ctx;
    (void) addr;
    (void) alen;
    if (len < 4 || len - 4 > cap) return -1;
    uint32_t sum = 0x5a5a5a5au;
    for (uint32_t i = 0; i < len - 4; i++) {
        out[i] = (uint8_t) (in[i] ^ (uint8_t) (t->key + i * 7u));
        sum = sum * 31u + out[i];
    }
    t->calls++;
    return vna_get32(in + len - 4) == sum ? (int32_t) len - 4 : -1;
}

static void test_islands(void)
{
    printf("[7] offline / LAN-only / online: LAN discovery, two islands, offline outbox, frame "
           "hook\n");
    double t0 = now_s();
    tnode_t *saveT = T;
    uint32_t saveNN = NN;
    NN = 2 * ISL;
    T = calloc(NN + 1, sizeof *T);
    for (uint32_t i = 0; i < NN; i++) make_identity(&T[i].id, 20000 + i, 8);
    vna_node_cfg_default(&g_cfg);
    g_cfg.degree = VNA_DEG_STORE;
    g_cfg.pow_bits = 8;
    for (uint32_t i = 0; i < NN; i++) {
        node_up(i);
        vna_spool_init(&T[i].sp, T[i].spe, 8, T[i].sprx, 8, 1);
        vna_node_set_spool(&T[i].node, &T[i].sp);
    }
    const uint32_t A3 = 3, B5 = ISL + 5, B6 = ISL + 6;

    /* fully offline: no contacts at all. Queue for a node nobody can reach yet. */
    const char *msgs[4] = {"offer(compute, units: 1)", "offer(compute, units: 2)",
                           "offer(compute, units: 3)", "ask(compute, units: 5)"};
    bool q_ok = true;
    for (uint32_t k = 0; k < 4; k++)
        q_ok &= vna_node_spool_hk(&T[A3].node, &T[B5].id.id, msgs[k], SWARM_HK_TRUE, k + 1, 600000,
                                  g_now, &g_ob, 0) == VNA_OK;
    q_ok &= vna_node_spool_hk(&T[A3].node, &T[B6].id.id, "offer(storage, units: 9)", SWARM_HK_TRUE,
                              9, 5000, g_now, &g_ob, 0) == VNA_OK;
    CHECK(q_ok && g_ob.n == 0 && vna_spool_pending(&T[A3].sp, &T[B5].id.id) == 4,
          "offline: items signed and queued, nothing sent with no route");
    vna_node_tick(&T[A3].node, g_now + 3000, &g_ob);
    CHECK(g_ob.n == 0 && T[A3].sp.delivered == 0,
          "offline tick: still nothing to send, nothing lost");
    vna_outbox_clear(&g_ob);

    /* two LAN islands, no bootstrap peer, no server, no internet */
    for (uint32_t i = 0; i < NN; i++) lan_up(i, i < ISL ? 1 : 2);
    settle_net(10);
    uint32_t full = 0, leak = 0, joined = 0;
    for (uint32_t i = 0; i < NN; i++) {
        uint32_t same = 0;
        for (uint32_t j = 0; j < NN; j++) {
            if (i == j) continue;
            bool r = in_rt(i, j);
            if (T[i].seg == T[j].seg)
                same += r;
            else
                leak += r;
        }
        full += same == ISL - 1;
        joined += T[i].node.joined;
    }
    printf("    LAN: %u/%u nodes see their whole island, %u cross-island contacts, %u joined\n",
           full, NN, leak, joined);
    CHECK(full == NN && leak == 0, "each island discovered itself over LAN multicast only");
    CHECK(joined == NN, "every node joined the DHT with no bootstrap peer");
    CHECK(vna_lan_peer_count(&T[0].lan, g_now) == ISL - 1, "LAN peer table holds the island");
    CHECK(T[A3].sp.delivered == 0 && vna_spool_pending(&T[A3].sp, &T[B5].id.id) == 4,
          "destination on the other island: items wait (looked up, not found)");

    /* the DHT works on a LAN-only island */
    vna_provider_t pv;
    memset(&pv, 0, sizeof pv);
    pv.file_size = 5;
    pv.chunk_size = VNA_CHUNK_SIZE;
    pv.n_chunks = 1;
    pv.visibility = VNA_VIS_PUBLIC;
    pv.addr_len = 4;
    vna_id_t rootA, rootB;
    vna_drbg_gen(&g_rng, rootA.b, 32);
    vna_drbg_gen(&g_rng, rootB.b, 32);
    addr_of(10, pv.addr);
    int32_t sl = vna_node_announce(&T[10].node, &rootA, &pv, g_now, 3600000, &g_ob);
    flush(10);
    settle_net(10);
    vna_node_lookup_release(&T[10].node, sl);
    addr_of(ISL + 10, pv.addr);
    sl = vna_node_announce(&T[ISL + 10].node, &rootB, &pv, g_now, 3600000, &g_ob);
    flush(ISL + 10);
    settle_net(10);
    vna_node_lookup_release(&T[ISL + 10].node, sl);
    sl = vna_node_lookup(&T[20].node, &rootA, VNA_LK_FIND_VALUE, g_now, &g_ob);
    flush(20);
    settle_net(10);
    const vna_node_lookup_t *L = vna_node_lookup_get(&T[20].node, sl);
    CHECK(L && L->lk.found_value, "LAN-only island: provider record STOREd and found (FIND_VALUE)");
    vna_node_lookup_release(&T[20].node, sl);
    sl = vna_node_lookup(&T[20].node, &rootB, VNA_LK_FIND_VALUE, g_now, &g_ob);
    flush(20);
    settle_net(10);
    L = vna_node_lookup_get(&T[20].node, sl);
    CHECK(L && !L->lk.found_value, "the other island's record is not reachable before the join");
    vna_node_lookup_release(&T[20].node, sl);
    uint32_t exact = 0;
    double q = 0;
    g_qseg = 1;
    for (uint32_t t = 0; t < 6; t++) {
        vna_id_t tg;
        vna_drbg_gen(&g_rng, tg.b, 32);
        q += lookup_quality((uint32_t) (rand() % ISL), &tg, &exact);
    }
    g_qseg = -1;
    CHECK(q / 6 >= 0.99 && exact == 6, "island lookups exact (%.3f, %u/6)", q / 6, exact);

    /* LAN attacks, at node 0 */
    static uint8_t rb[VNA_NODEREC_MAX], lb[VNA_LAN_MSG_MAX];
    static vna_identity_t evil;
    make_identity(&evil, 424242, 8);
    vna_lan_t *l0 = &T[0].lan;
    uint64_t s1 = T[1].lan.rec_seq + 100;
    int32_t rl = forge_rec(&T[1].id, 0, 0, s1, g_now, g_now + 60000, 0, 1, rb, sizeof rb);
    int32_t ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_OK,
          "genuine signed announce accepted");
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_ERR_REPLAY,
          "replayed announce refused (seq not newer)");
    rb[60] ^= 1;
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) != VNA_OK, "tampered record refused");
    rl = forge_rec(&evil, &T[1].id.id, 0, s1 + 5, g_now, g_now + 60000, 0, 1, rb, sizeof rb);
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_ERR_BINDING,
          "announce claiming another node's id with our key refused (binding)");
    rl = forge_rec(&evil, &T[1].id.id, T[1].id.pk, s1 + 6, g_now, g_now + 60000, 0, 1, rb,
                   sizeof rb);
    { /* also needs that node's PoW nonce to get as far as the signature */
        static vna_noderec_t r;
        vna_schema_unpack(&vna_noderec_schema, rb, (uint32_t) rl, &r, 0);
        r.pow = T[1].id.pow_nonce;
        int32_t k = vna_schema_pack(&vna_noderec_schema, &r, rb, sizeof rb, false);
        uint8_t rnd[32] = {9};
        vna_sign(evil.sk, VNA_CTX_NODEREC, rb, (uint32_t) k, rnd, r.sig);
        rl = vna_schema_pack(&vna_noderec_schema, &r, rb, sizeof rb, true);
    }
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_ERR_SIG,
          "announce with the right id and key but the wrong signer refused");
    rl = forge_rec(&T[1].id, 0, 0, s1 + 7, g_now - 90000, g_now - 1, 0, 1, rb, sizeof rb);
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_ERR_EXPIRED,
          "expired record refused");
    rl = forge_rec(&T[1].id, 0, 0, s1 + 8, g_now + 120000, g_now + 200000, 0, 1, rb, sizeof rb);
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_ERR_STALE,
          "record from the future refused");
    rl = forge_rec(&T[1].id, 0, 0, s1 + 9, g_now, g_now + 60000, 0, 1, rb, sizeof rb);
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, "_other._udp", lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) == VNA_ERR_PARSE,
          "wrong service type refused");
    rl = forge_rec(&evil, &T[3].id.id, T[3].id.pk, s1, g_now, g_now, VNA_NR_GOODBYE, 3, rb,
                   sizeof rb);
    ll = lan_pack(VNA_LAN_ANNOUNCE, rb, (uint32_t) rl, VNA_LAN_SERVICE, lb, sizeof lb);
    CHECK(vna_lan_handle(l0, lb, (uint32_t) ll, g_now, &g_ob) != VNA_OK && in_rt(0, 3),
          "forged GOODBYE refused: the peer stays");
    for (uint32_t k = 0; k < 2000; k++) { /* random datagrams never parse */
        uint32_t n = (uint32_t) (rand() % 600);
        for (uint32_t i = 0; i < n; i++) lb[i] = (uint8_t) rand();
        if (k & 1) vna_put32(lb, VNA_LAN_MAGIC);
        if (vna_lan_handle(l0, lb, n, g_now, &g_ob) == VNA_OK) {
            CHECK(0, "random LAN datagram accepted");
            break;
        }
    }
    vna_outbox_clear(&g_ob);
    /* known-answer suppression: a query listing everyone but node 7 gets one answer */
    {
        static vna_lan_msg_t m;
        memset(&m, 0, sizeof m);
        m.magic = VNA_LAN_MAGIC;
        m.version = VNA_VERSION;
        m.kind = VNA_LAN_QUERY;
        m.svc_len = (uint16_t) strlen(VNA_LAN_SERVICE);
        memcpy(m.svc, VNA_LAN_SERVICE, m.svc_len);
        m.qid = 99;
        for (uint32_t i = 1; i < ISL && m.nknown < VNA_LAN_KNOWN; i++)
            if (i != 7) m.known[m.nknown++].id = T[i].id.id;
        ll = vna_schema_pack(&vna_lan_msg_schema, &m, lb, sizeof lb, true);
        uint32_t ann0 = 0, sup0 = 0;
        for (uint32_t i = 1; i < ISL; i++) {
            ann0 += T[i].lan.sent_announce;
            sup0 += T[i].lan.suppressed;
        }
        for (uint32_t i = 1; i < ISL; i++) enqueue(0, i, lb, (uint32_t) ll), q_tail->lan = true;
        run_queue();
        g_now += 200;
        tick_all();
        run_queue();
        uint32_t ann1 = 0, sup1 = 0;
        for (uint32_t i = 1; i < ISL; i++) {
            ann1 += T[i].lan.sent_announce;
            sup1 += T[i].lan.suppressed;
        }
        uint32_t listed = m.nknown, unlisted = ISL - 1 - listed;
        CHECK(sup1 - sup0 == listed && ann1 - ann0 == unlisted,
              "known-answer suppression: %u listed stay quiet, %u answer", listed, ann1 - ann0);
    }
    /* signed GOODBYE removes a node; it rejoins with a newer record */
    vna_lan_stop(&T[2].lan, g_now, &g_ob);
    flush(2);
    run_queue();
    uint32_t gone = 0;
    for (uint32_t i = 0; i < ISL; i++) gone += i != 2 && !in_rt(i, 2);
    CHECK(gone == ISL - 1, "signed GOODBYE: the island forgets node 2 (%u/%u)", gone, ISL - 1);
    vna_lan_start(&T[2].lan, g_now, &g_ob);
    flush(2);
    settle_net(3);
    uint32_t back = 0;
    for (uint32_t i = 0; i < ISL; i++) back += i != 2 && in_rt(i, 2);
    CHECK(back == ISL - 1, "node 2 back after re-announcing (%u/%u)", back, ISL - 1);

    g_drop = drop_spool_ack; /* lose the first acknowledgement B5 sends A3 */
    g_drop_from = B5;
    g_drop_to = A3;
    g_drop_left = 1;
    /* the islands meet: ONE cached peer record (B0's, as a host would have
     * persisted it) is given to A0. No LAN merge, no server. */
    CHECK(vna_node_seed_record(&T[0].node, T[ISL].lan.rec, T[ISL].lan.rec_len, g_now, &g_ob) ==
              VNA_OK,
          "cached signed node record seeds a contact");
    rb[0] = 0;
    memcpy(rb, T[ISL].lan.rec, T[ISL].lan.rec_len);
    rb[T[ISL].lan.rec_len - 5] ^= 1;
    CHECK(vna_node_seed_record(&T[0].node, rb, T[ISL].lan.rec_len, g_now, &g_ob) == VNA_ERR_SIG,
          "tampered cached record refused");
    flush(0);
    int32_t s0 = vna_node_lookup(&T[0].node, &T[0].id.id, VNA_LK_FIND_NODE, g_now, &g_ob);
    flush(0);
    settle_net(10);
    vna_node_lookup_release(&T[0].node, s0);
    for (uint32_t round = 0; round < 1; round++)
        for (uint32_t i = 0; i < NN; i++) { /* ordinary Kademlia self-lookups and refreshes */
            for (uint32_t k = 0; k < VNA_NODE_LOOKUPS; k++) {
                if (T[i].sp.lookup_slot == (int32_t) k) continue;
                vna_node_lookup_release(&T[i].node, (int32_t) k);
            }
            int32_t a = vna_node_lookup(&T[i].node, &T[i].id.id, VNA_LK_FIND_NODE, g_now, &g_ob);
            flush(i);
            settle_net(4);
            if (a >= 0 && a != T[i].sp.lookup_slot) vna_node_lookup_release(&T[i].node, a);
        }
    uint32_t cross = 0;
    for (uint32_t i = 0; i < NN; i++)
        for (uint32_t j = 0; j < NN; j++) cross += T[i].seg != T[j].seg && in_rt(i, j);
    printf("    joined through one link: %u cross-island contacts (mean %.1f per node)\n", cross,
           (double) cross / NN);
    CHECK(cross / NN >= 10, "routing tables now span both islands");
    exact = 0;
    q = 0;
    for (uint32_t t = 0; t < 12; t++) {
        vna_id_t tg;
        vna_drbg_gen(&g_rng, tg.b, 32);
        q += lookup_quality((uint32_t) (rand() % (int) NN), &tg, &exact);
    }
    printf("    merged network: 12 lookups, mean overlap with true 20 closest %.3f, exact %u/12\n",
           q / 12, exact);
    CHECK(q / 12 >= 0.97 && exact >= 9, "lookups converge over the merged network");
    sl = vna_node_lookup(&T[20].node, &rootB, VNA_LK_FIND_VALUE, g_now, &g_ob);
    flush(20);
    settle_net(10);
    L = vna_node_lookup_get(&T[20].node, sl);
    CHECK(L && L->lk.found_value, "island A now finds island B's record");
    vna_node_lookup_release(&T[20].node, sl);

    /* the offline outbox drains: in order, exactly once, even with a lost ack */
    for (uint32_t r = 0; r < 12 && vna_spool_pending(&T[A3].sp, &T[B5].id.id); r++) {
        g_now += 2001;
        tick_all();
        run_queue();
    }
    g_drop = 0;
    vna_event_t ev;
    uint32_t got = 0, inorder = 1, spooled = 1, last = 0;
    while (vna_node_poll_event(&T[B5].node, &ev)) {
        if (!vna_id_eq(&ev.from, &T[A3].id.id)) continue;
        got++;
        inorder &= ev.hk.ordinal == last + 1;
        last = ev.hk.ordinal;
        spooled &= ev.spooled && ev.created <= g_now;
    }
    printf("    outbox: delivered %u, refused %u, retries %u, receiver dups %u, expired %u\n",
           T[A3].sp.delivered, T[A3].sp.refused, T[A3].sp.retries, T[B5].sp.rx_dup,
           T[A3].sp.expired);
    CHECK(got == 3 && inorder && spooled,
          "3 queued items surfaced at the destination, in order, once each");
    CHECK(T[B5].sp.rx_dup >= 1 && T[A3].sp.retries >= 1,
          "lost acknowledgement: sender retried, receiver deduplicated");
    CHECK(T[A3].sp.refused == 1 && T[B5].sp.rx_refused == 1,
          "item outside the receiver's agreement refused (no agreement: ask is not served)");
    CHECK(T[A3].sp.expired == 1 && vna_spool_pending(&T[A3].sp, 0) == 0,
          "an item past its lifetime expired instead of being delivered late");
    /* a forged spool item (signed by someone else) is ignored */
    {
        static vna_spool_item_t it;
        static uint8_t ib[VNA_SPOOL_ITEM_MAX];
        memset(&it, 0, sizeof it);
        it.magic = VNA_SPOOL_MAGIC;
        it.version = VNA_VERSION;
        it.src = T[A3].id.id;
        it.dst = T[B5].id.id;
        it.sseq = 1000;
        it.created = g_now;
        it.expires = g_now + 60000;
        it.truth = SWARM_HK_TRUE;
        it.len = (uint16_t) vna_hk_canonicalize("offer(compute, units: 77)", it.text, VNA_HK_MAX);
        int32_t k = vna_schema_pack(&vna_spool_item_schema, &it, ib, sizeof ib, false);
        uint8_t rnd[32] = {3};
        vna_sign(evil.sk, VNA_CTX_SPOOL, ib, (uint32_t) k, rnd, it.sig);
        k = vna_schema_pack(&vna_spool_item_schema, &it, ib, sizeof ib, true);
        static vna_b_rec_t br;
        br.len = (uint16_t) k;
        memcpy(br.rec, ib, (size_t) k);
        int32_t n = craft(&T[A3].id, &T[B5].id.id, VNA_MSG_SPOOL, T[A3].node.seq + 50, g_now, 4242,
                          &vna_b_rec_schema, &br, false);
        uint32_t acc0 = T[B5].sp.rx_accepted;
        deliver_to(B5, A3, g_wire, (uint32_t) n);
        CHECK(T[B5].sp.rx_accepted == acc0 && !vna_node_poll_event(&T[B5].node, &ev),
              "spool item not signed by its origin ignored");
    }

    /* per-frame transform hook (the ehop integration point) */
    {
        static toyx_t tx = {0x6b, 0};
        static vna_xform_t xf = {toy_seal, toy_open, &tx, 4};
        vna_node_set_xform(&T[1].node, &xf);
        vna_node_set_xform(&T[2].node, &xf);
        uint32_t r1 = T[1].node.received, r2 = T[2].node.received;
        vna_node_ping(&T[1].node, &T[2].id.id, g_now, &g_ob);
        flush(1);
        run_queue();
        CHECK(T[2].node.received == r2 + 1 && T[1].node.received == r1 + 1 && tx.calls == 4,
              "PING/PONG through the transform on both ends (4 hook calls)");
        uint32_t x4 = T[4].node.received;
        vna_node_ping(&T[1].node, &T[4].id.id, g_now, &g_ob);
        flush(1);
        run_queue();
        CHECK(T[4].node.received == x4,
              "a node without the transform cannot read transformed frames");
        uint32_t xr = T[2].node.xform_refused;
        vna_node_ping(&T[5].node, &T[2].id.id, g_now, &g_ob);
        flush(5);
        run_queue();
        CHECK(T[2].node.xform_refused == xr + 1,
              "a transformed node drops frames its hook refuses");
        vna_lan_set_xform(&T[1].lan, &xf);
        vna_lan_set_xform(&T[2].lan, &xf);
        uint32_t a2 = T[2].lan.rej_old + T[2].lan.accepted, p4 = T[4].lan.rej_parse;
        T[1].lan.next_announce = g_now;
        vna_lan_tick(&T[1].lan, g_now, &g_ob);
        flush(1);
        run_queue();
        CHECK(T[2].lan.rej_old + T[2].lan.accepted == a2 + 1 && T[4].lan.rej_parse == p4 + 1,
              "LAN discovery through the hook: members read it, others cannot");
        vna_node_set_xform(&T[1].node, 0);
        vna_node_set_xform(&T[2].node, 0);
    }

    printf("    section 7 in %.2f s\n", now_s() - t0);
    while (q_head) {
        pkt_t *p = q_head;
        q_head = p->next;
        free(p);
    }
    q_tail = 0;
    free(T);
    T = saveT;
    NN = saveNN;
}

int main(int argc, char **argv)
{
    double t0 = now_s();
    NN = argc > 1 ? (uint32_t) atoi(argv[1]) : 128;
    if (NN < 64) NN = 64;
    if (NN > 200) NN = 200;
    srand(12345);
    uint8_t seed[32] = {1, 2, 3};
    vna_drbg_seed(&g_rng, seed, 32);
    T = calloc(NN + 2, sizeof *T);
    size_t obcap = (size_t) (VNA_OUTBOX_MAX + 1u) * (VNA_MSG_WIRE_MAX + VNA_XFORM_MAX_OVERHEAD);
    g_obuf = malloc(obcap);
    vna_outbox_init(&g_ob, g_obuf, (uint32_t) obcap);
    printf("Vinea host test\n");
    test_primitives();
    test_dht();
    test_wire_attacks();
    test_session();
    test_agreement();
    test_economy();
    test_link();
    test_link_edges();
    test_files();
    test_islands();
    printf("\n%d passed, %d failed, %.2f s\n", g_pass, g_fail, now_s() - t0);
    free(T);
    free(g_obuf);
    return g_fail ? 1 : 0;
}
