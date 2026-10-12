/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_edgeclus.c — the edge cluster (edgeclus.h): advert codec and its
 * tamper cases, freshness and expiry, sequence and equivocation handling,
 * order independence under permutation, the Fibonacci budget with the
 * no-monopoly cap and exact conservation, the scheduler's fit rules and
 * estimates, and failover. Expected numbers are worked by hand from the
 * rules in edgeclus.h and swarm_budget.h, not read back from the code.
 * Signatures are real ML-DSA-87 (pqsec) from fixed seeds. */
#include <stdio.h>
#include <string.h>
#include "edgeclus.h"

static int passes = 0, failures = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            passes++;                                                                              \
            printf("[PASS] %s\n", msg);                                                            \
        } else {                                                                                   \
            failures++;                                                                            \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)

#define NID 34u
static vna_identity_t ids[NID];
static uint8_t buf[EC_ADVERT_WIRE_MAX + 16];
static ec_cluster_t cl, cl2;
static ec_cycle_t cy, cy2;

static void make_ids(void)
{
    for (uint32_t i = 0; i < NID; i++) {
        uint8_t seed[32];
        for (uint32_t k = 0; k < 32; k++) seed[k] = (uint8_t) (i * 31u + k + 1u);
        vna_identity_create(&ids[i], seed, 0, 1);
    }
}

static void set_model(ec_model_t *m, const char *name, uint8_t q, uint32_t mem, uint32_t pre,
                      uint32_t dec)
{
    memset(m, 0, sizeof *m);
    strncpy(m->name, name, EC_NAME_LEN);
    m->quant = q;
    m->mem_mb = mem;
    m->prefill_mtps = pre;
    m->decode_mtps = dec;
}

/* One-model advert: "llama-3.2-3b" Q4_K_M at the given decode rate. */
static void basic(ec_advert_t *a, uint64_t seq, uint64_t issued, uint32_t dec)
{
    memset(a, 0, sizeof *a);
    a->seq = seq;
    a->issued_ms = issued;
    a->ttl_ms = 60000;
    a->free_mem_mb = 4096;
    a->queue_depth = 0;
    a->price = 10;
    a->n_models = 1;
    set_model(&a->models[0], "llama-3.2-3b", EC_Q4_K_M, 2000, 50000, dec);
}

static int32_t seal(ec_advert_t *a, uint32_t who, uint8_t *out)
{
    return ec_advert_seal(a, &ids[who], NULL, out, EC_ADVERT_WIRE_MAX);
}

/* ---------------- codec ---------------- */

static void test_codec(void)
{
    ec_advert_t a, b;
    basic(&a, 7, 1000, 9000);
    a.n_models = 2;
    set_model(&a.models[1], "qwen2.5-0.5b", EC_Q8_0, 600, 200000, 30000);
    a.queue_depth = 3;
    a.price = 0x0102030405060708ull;
    a.flags = EC_F_DRAINING;
    int32_t n = seal(&a, 0, buf);
    CHECK(n == (int32_t) (84 + 2 * 45 + 2592 + 4627), "advert length = 84 + 45 x models + pk + sig");
    CHECK(ec_advert_body_len(2) == 174 && ec_advert_body_len(0) == 0 && ec_advert_body_len(9) == 0,
          "body length rule and its bounds");
    CHECK(buf[0] == 'X' && buf[1] == 'Z' && buf[2] == 'E' && buf[3] == 'C' && buf[4] == 1 &&
              buf[5] == EC_F_DRAINING && buf[6] == 2 && buf[7] == 0,
          "header bytes: magic XZEC, version 1, flags, count, reserved");
    CHECK(buf[76] == 0x08 && buf[83] == 0x01, "price is little-endian at offset 76");
    CHECK(memcmp(buf + 8, ids[0].id.b, 32) == 0, "node id is the Vinea NodeID of the signer");
    uint8_t h[32], h2[32];
    CHECK(ec_advert_open(buf, (uint32_t) n, 0, &b, h) == EC_OK, "open accepts the sealed advert");
    CHECK(memcmp(&a, &b, sizeof a) == 0, "every field round-trips");
    vna_sha3(buf, 174, h2);
    CHECK(memcmp(h, h2, 32) == 0, "body hash is SHA3-256 of the body");
    static uint8_t again[EC_ADVERT_WIRE_MAX];
    CHECK(seal(&a, 0, again) == n && memcmp(buf, again, (size_t) n) == 0,
          "deterministic ML-DSA (rnd NULL) gives identical bytes");
    uint8_t rnd[32];
    memset(rnd, 0x5A, 32);
    CHECK(ec_advert_seal(&a, &ids[0], rnd, again, sizeof again) == n &&
              memcmp(buf + 174 + 2592, again + 174 + 2592, 4627) != 0 &&
              ec_advert_open(again, (uint32_t) n, 0, &b, NULL) == EC_OK,
          "hedged signature differs and still verifies");
    CHECK(ec_advert_seal(&a, &ids[0], NULL, again, (uint32_t) n - 1) == -1,
          "seal refuses a buffer one byte short");
}

static void test_seal_rules(void)
{
    ec_advert_t a;
    int bad = 0;
    basic(&a, 1, 0, 1000); a.ttl_ms = 0; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.ttl_ms = EC_MAX_TTL_MS + 1; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.n_models = 0; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.n_models = 9; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.flags = 0x02; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].quant = EC_Q_ANY; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].quant = 9; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].prefill_mtps = 0; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].decode_mtps = 0; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); memset(a.models[0].name, 0, EC_NAME_LEN); bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].name[3] = ' '; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].name[2] = 0x7F; bad += seal(&a, 0, buf) == -1;
    basic(&a, 1, 0, 1000); a.models[0].name[20] = 'x'; bad += seal(&a, 0, buf) == -1; /* after NUL */
    basic(&a, 1, 0, 1000); a.n_models = 2; a.models[1] = a.models[0]; bad += seal(&a, 0, buf) == -1;
    CHECK(bad == 14, "seal refuses all 14 field-rule violations (A1)");
    basic(&a, 1, 0, 1000); a.ttl_ms = EC_MAX_TTL_MS; a.n_models = 2; a.models[1] = a.models[0];
    a.models[1].quant = EC_Q8_0;
    memset(a.models[0].name, 'n', EC_NAME_LEN); /* full 32 bytes, no NUL */
    a.models[1].name[0] = '~';
    CHECK(seal(&a, 0, buf) > 0, "boundaries accepted: ttl at max, 32-byte name, same name other quant");
    CHECK(ec_advert_seal(NULL, &ids[0], NULL, buf, sizeof buf) == -1 &&
              ec_advert_seal(&a, NULL, NULL, buf, sizeof buf) == -1 &&
              ec_advert_seal(&a, &ids[0], NULL, NULL, sizeof buf) == -1,
          "seal refuses NULL arguments");
}

static void test_tamper(void)
{
    ec_advert_t a, b;
    basic(&a, 3, 500, 4000);
    int32_t n = seal(&a, 1, buf);
    uint32_t bl = 84 + 45;
    static uint8_t t[EC_ADVERT_WIRE_MAX + 4];
    struct {
        uint32_t off;
        ec_status_t want;
        const char *what;
    } cases[] = {
        {48, EC_ERR_SIG, "seq"},
        {56, EC_ERR_SIG, "issued_ms"},
        {64, EC_ERR_SIG, "ttl_ms (stays in range)"},
        {68, EC_ERR_SIG, "free memory"},
        {72, EC_ERR_SIG, "queue depth"},
        {76, EC_ERR_SIG, "price"},
        {84 + 37, EC_ERR_SIG, "prefill rate"},
        {84 + 41, EC_ERR_SIG, "decode rate"},
        {84 + 33, EC_ERR_SIG, "model memory"},
        {40, EC_ERR_SIG, "pow nonce"},
        {8, EC_ERR_BINDING, "node id"},
        {bl + 100, EC_ERR_BINDING, "public key"},
        {bl + 2592 + 10, EC_ERR_SIG, "signature start"},
        {bl + 2592 + 4626, EC_ERR_SIG, "signature last byte"},
        {0, EC_ERR_PARSE, "magic"},
        {7, EC_ERR_PARSE, "reserved byte"},
    };
    int ok = 0;
    for (uint32_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        memcpy(t, buf, (size_t) n);
        t[cases[i].off] ^= 0x01;
        ec_status_t st = ec_advert_open(t, (uint32_t) n, 0, &b, NULL);
        if (st == cases[i].want)
            ok++;
        else
            printf("  tamper %s: got %d want %d\n", cases[i].what, st, cases[i].want);
    }
    CHECK(ok == 16, "each of 16 single-bit tampers is refused with the right reason");
    memcpy(t, buf, (size_t) n);
    t[4] = 2;
    CHECK(ec_advert_open(t, (uint32_t) n, 0, &b, NULL) == EC_ERR_PARSE, "version 2 refused");
    t[4] = 1;
    t[5] = 0x80;
    CHECK(ec_advert_open(t, (uint32_t) n, 0, &b, NULL) == EC_ERR_PARSE, "unknown flag refused");
    memcpy(t, buf, (size_t) n);
    t[84 + 32] = 0;
    CHECK(ec_advert_open(t, (uint32_t) n, 0, &b, NULL) == EC_ERR_PARSE, "quant 0 on the wire refused");
    memcpy(t, buf, (size_t) n);
    t[6] = 2;
    CHECK(ec_advert_open(t, (uint32_t) n, 0, &b, NULL) == EC_ERR_PARSE,
          "model count that disagrees with the length refused");
    CHECK(ec_advert_open(buf, (uint32_t) n - 1, 0, &b, NULL) == EC_ERR_PARSE &&
              ec_advert_open(buf, 83, 0, &b, NULL) == EC_ERR_PARSE,
          "truncated advert refused");
    memcpy(t, buf, (size_t) n);
    t[n] = 0;
    CHECK(ec_advert_open(t, (uint32_t) n + 1, 0, &b, NULL) == EC_ERR_PARSE, "trailing byte refused");
    /* a signature made under another context does not transfer */
    memcpy(t, buf, (size_t) n);
    vna_sign(ids[1].sk, VNA_CTX_MSG, t, bl, NULL, t + bl + 2592);
    CHECK(ec_advert_open(t, (uint32_t) n, 0, &b, NULL) == EC_ERR_SIG,
          "Vinea message-context signature over the same body refused");
    /* key swap: another node's key and signature over this body */
    memcpy(t, buf, (size_t) n);
    memcpy(t + bl, ids[2].pk, 2592);
    vna_sign(ids[2].sk, EC_CTX_ADVERT, t, bl, NULL, t + bl + 2592);
    CHECK(ec_advert_open(t, (uint32_t) n, 0, &b, NULL) == EC_ERR_BINDING,
          "valid signature by another key over this node id refused (binding)");
    CHECK(ec_advert_open(NULL, (uint32_t) n, 0, &b, NULL) == EC_ERR_ARG &&
              ec_advert_open(buf, (uint32_t) n, 0, NULL, NULL) == EC_ERR_ARG,
          "open refuses NULL arguments");
    /* proof of work: ids[1] has nonce 0, at 12 bits it almost surely fails */
    bool meets = vna_pow_ok(&ids[1].id, 0, 12);
    CHECK(ec_advert_open(buf, (uint32_t) n, 12, &b, NULL) == (meets ? EC_OK : EC_ERR_POW) && !meets,
          "advert below the cluster proof-of-work difficulty refused");
    vna_identity_t *p = &ids[NID - 1];
    uint8_t seed[32];
    memset(seed, 0xA5, 32);
    vna_identity_create(p, seed, 8, 1u << 20);
    basic(&a, 1, 0, 1000);
    n = seal(&a, NID - 1, buf);
    CHECK(ec_advert_open(buf, (uint32_t) n, 8, &b, NULL) == EC_OK && vna_pow_ok(&p->id, p->pow_nonce, 8),
          "advert meeting an 8-bit proof of work accepted at 8 bits");
}

/* ---------------- table ---------------- */

static ec_status_t put(ec_cluster_t *c, ec_advert_t *a, uint32_t who, uint64_t now)
{
    static uint8_t b[EC_ADVERT_WIRE_MAX];
    int32_t n = seal(a, who, b);
    return ec_cluster_ingest(c, b, (uint32_t) n, now);
}

static void test_freshness(void)
{
    ec_advert_t a;
    ec_cluster_init(&cl, 0, 2000, 1000);
    basic(&a, 1, 12001, 1000);
    CHECK(put(&cl, &a, 0, 10000) == EC_ERR_FUTURE, "issued 2001 ms ahead with 2000 ms skew refused");
    basic(&a, 1, 12000, 1000);
    CHECK(put(&cl, &a, 0, 10000) == EC_OK, "issued exactly skew ahead accepted");
    basic(&a, 1, 1000, 1000);
    a.ttl_ms = 9000;
    CHECK(put(&cl, &a, 1, 10000) == EC_ERR_EXPIRED, "advert at issued + ttl (exclusive end) refused");
    a.ttl_ms = 9001;
    CHECK(put(&cl, &a, 1, 10000) == EC_OK, "advert one ms before its end accepted");
    CHECK(cl.n == 2, "two live adverts held");
    CHECK(ec_cluster_expire(&cl, 10000) == 0, "nothing expires at 10000");
    CHECK(ec_cluster_expire(&cl, 10001) == 1 && cl.n == 1 && ec_cluster_find(&cl, &ids[1].id) < 0,
          "the 9001 ms advert expires at exactly 10001");
    CHECK(ec_cluster_find(&cl, &ids[0].id) == 0, "the other stays");
    ec_cycle_begin(&cl, 72000, 1000, &cy);
    CHECK(cy.alloc[0] == 0 && cy.unassigned == 1000,
          "an advert dead at cycle time gets no budget even before expire runs");
    CHECK(ec_cluster_expire(&cl, 72000) == 1 && cl.n == 0, "and expire then drops it");
    CHECK(ec_cluster_init(&cl, 33, 0, 500) == EC_ERR_ARG && ec_cluster_init(&cl, 0, 0, 0) == EC_ERR_ARG &&
              ec_cluster_init(&cl, 0, 0, 1001) == EC_ERR_ARG && ec_cluster_init(NULL, 0, 0, 1) == EC_ERR_ARG,
          "init refuses pow > 32 and a cap outside 1..1000");
}

static void test_seq_and_equivocation(void)
{
    ec_advert_t a;
    ec_cluster_init(&cl, 0, 1000, 1000);
    basic(&a, 5, 100, 1000);
    CHECK(put(&cl, &a, 0, 1000) == EC_OK, "first advert accepted");
    uint64_t g = cl.gen;
    CHECK(put(&cl, &a, 0, 1000) == EC_OK && cl.gen == g, "identical bytes again: accepted, no change");
    basic(&a, 4, 200, 1000);
    CHECK(put(&cl, &a, 0, 1000) == EC_ERR_STALE && cl.nodes[0].ad.seq == 5, "older seq refused");
    basic(&a, 6, 200, 7777);
    CHECK(put(&cl, &a, 0, 1000) == EC_OK && cl.nodes[0].ad.seq == 6 &&
              cl.nodes[0].ad.models[0].decode_mtps == 7777,
          "newer seq replaces");
    /* equivocation: two different seq-9 adverts, both orders */
    ec_advert_t x, y;
    static uint8_t bx[EC_ADVERT_WIRE_MAX], by[EC_ADVERT_WIRE_MAX];
    basic(&x, 9, 300, 1111);
    basic(&y, 9, 300, 2222);
    int32_t nx = seal(&x, 0, bx), ny = seal(&y, 0, by);
    uint8_t hx[32], hy[32];
    vna_sha3(bx, 129, hx);
    vna_sha3(by, 129, hy);
    uint32_t want = memcmp(hx, hy, 32) < 0 ? 1111u : 2222u;
    ec_cluster_init(&cl, 0, 1000, 1000);
    ec_status_t s1 = ec_cluster_ingest(&cl, bx, (uint32_t) nx, 1000);
    ec_status_t s2 = ec_cluster_ingest(&cl, by, (uint32_t) ny, 1000);
    uint32_t got1 = cl.nodes[0].ad.models[0].decode_mtps;
    ec_cluster_init(&cl2, 0, 1000, 1000);
    ec_status_t s3 = ec_cluster_ingest(&cl2, by, (uint32_t) ny, 1000);
    ec_status_t s4 = ec_cluster_ingest(&cl2, bx, (uint32_t) nx, 1000);
    uint32_t got2 = cl2.nodes[0].ad.models[0].decode_mtps;
    CHECK(got1 == want && got2 == want, "equivocation keeps the lower body hash in either order");
    CHECK(s1 == EC_OK && s3 == EC_OK && cl.equivocations == 1 && cl2.equivocations == 1,
          "equivocation counted once per conflicting arrival");
    CHECK((want == 1111u) ? (s2 == EC_ERR_EQUIVOCATION && s4 == EC_OK)
                          : (s2 == EC_OK && s4 == EC_ERR_EQUIVOCATION),
          "the losing copy is reported as EC_ERR_EQUIVOCATION");
}

/* ---------------- determinism ---------------- */

static uint64_t lcg_state = 0x36A9u;
static uint32_t lcg(uint32_t n)
{
    lcg_state = lcg_state * 6364136223846793005ull + 1442695040888963407ull;
    return (uint32_t) ((lcg_state >> 33) % n);
}

#define NBLOB 12u
static uint8_t blobs[NBLOB][EC_ADVERT_WIRE_MAX];
static int32_t blen[NBLOB];

static void build_blobs(void)
{
    ec_advert_t a;
    uint32_t k = 0;
    for (uint32_t i = 0; i < 7; i++) { /* seven nodes, seq 1 */
        basic(&a, 1, 1000, 3000 + 1000 * (i % 4)); /* repeated speeds: ties by id */
        a.queue_depth = i % 3;
        a.price = 20 - i;
        a.free_mem_mb = 1000 + 500 * i;
        if (i == 6) a.flags = EC_F_DRAINING;
        blen[k] = seal(&a, i, blobs[k]);
        k++;
    }
    basic(&a, 2, 1500, 9500); /* node 0 updates */
    blen[k] = seal(&a, 0, blobs[k]);
    k++;
    basic(&a, 3, 1600, 2500); /* node 2 updates twice */
    blen[k] = seal(&a, 2, blobs[k]);
    k++;
    basic(&a, 2, 1550, 8800);
    blen[k] = seal(&a, 2, blobs[k]);
    k++;
    basic(&a, 3, 1600, 4100); /* equivocation pair on node 3 */
    blen[k] = seal(&a, 3, blobs[k]);
    k++;
    basic(&a, 3, 1600, 4200);
    blen[k] = seal(&a, 3, blobs[k]);
    k++;
}

static void reqs_fill(ec_req_t *r, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        memset(&r[i], 0, sizeof r[i]);
        r[i].req_id = 100 + i * 7;
        strcpy(r[i].model, "llama-3.2-3b");
        r[i].quant = (i % 3 == 0) ? EC_Q4_K_M : EC_Q_ANY;
        r[i].prompt_tokens = 100 + 37 * i;
        r[i].max_new_tokens = 50 + 11 * i;
        r[i].kv_mem_mb = 150;
        r[i].latency_ms = (i % 4 == 0) ? 0 : 400000;
    }
}

static void test_permutation(void)
{
    build_blobs();
    static ec_req_t rq[20], rq2[20];
    static ec_assign_t ref[20], got[20];
    reqs_fill(rq, 20);
    uint32_t perm[NBLOB];
    int same_tab = 0, same_cycle = 0, same_sched = 0, rounds = 0;
    for (int round = 0; round < 120; round++) {
        for (uint32_t i = 0; i < NBLOB; i++) perm[i] = i;
        if (round == 1)
            for (uint32_t i = 0; i < NBLOB; i++) perm[i] = NBLOB - 1 - i;
        else if (round > 1)
            for (uint32_t i = NBLOB - 1; i > 0; i--) {
                uint32_t j = lcg(i + 1), t = perm[i];
                perm[i] = perm[j];
                perm[j] = t;
            }
        ec_cluster_t *c = round == 0 ? &cl : &cl2;
        ec_cycle_t *y = round == 0 ? &cy : &cy2;
        ec_cluster_init(c, 0, 1000, 450);
        for (uint32_t i = 0; i < NBLOB; i++) ec_cluster_ingest(c, blobs[perm[i]], (uint32_t) blen[perm[i]], 2000);
        ec_cycle_begin(c, 2000, 100000, y);
        ec_assign_t *out = round == 0 ? ref : got;
        if (round == 0) {
            ec_schedule(c, y, rq, 20, out);
            continue;
        }
        /* also permute the request list */
        uint32_t rp[20];
        for (uint32_t i = 0; i < 20; i++) rp[i] = i;
        for (uint32_t i = 19; i > 0; i--) {
            uint32_t j = lcg(i + 1), t = rp[i];
            rp[i] = rp[j];
            rp[j] = t;
        }
        for (uint32_t i = 0; i < 20; i++) rq2[i] = rq[rp[i]];
        static ec_assign_t tmp[20];
        ec_schedule(c, y, rq2, 20, tmp);
        for (uint32_t i = 0; i < 20; i++) got[rp[i]] = tmp[i];
        rounds++;
        bool tab = cl.n == cl2.n;
        for (uint32_t i = 0; tab && i < cl.n; i++)
            tab = memcmp(&cl.nodes[i].ad, &cl2.nodes[i].ad, sizeof cl.nodes[i].ad) == 0 &&
                  memcmp(cl.nodes[i].body_hash, cl2.nodes[i].body_hash, 32) == 0;
        same_tab += tab;
        same_cycle += memcmp(cy.alloc, cy2.alloc, sizeof cy.alloc) == 0 && cy.unassigned == cy2.unassigned &&
                      memcmp(cy.level, cy2.level, sizeof cy.level) == 0;
        same_sched += memcmp(ref, got, sizeof ref) == 0 && memcmp(cy.used, cy2.used, sizeof cy.used) == 0;
    }
    CHECK(rounds == 119 && same_tab == 119, "table identical under 119 permutations of advert arrival");
    CHECK(same_cycle == 119, "allotments and levels identical under every permutation");
    CHECK(same_sched == 119, "assignments identical under permuted adverts and permuted request lists");
    CHECK(cl.n == 7 && cl.nodes[ec_cluster_find(&cl, &ids[0].id)].ad.seq == 2 &&
              cl.nodes[ec_cluster_find(&cl, &ids[2].id)].ad.seq == 3,
          "highest seq per node kept");
    int assigned = 0;
    for (uint32_t i = 0; i < 20; i++) assigned += ref[i].node >= 0;
    CHECK(assigned >= 10, "the permutation workload actually assigns work");
    int32_t dr = ec_cluster_find(&cl, &ids[6].id);
    bool none_on_draining = true;
    for (uint32_t i = 0; i < 20; i++) none_on_draining &= ref[i].node != dr;
    CHECK(cy.alloc[dr] == 0 && cy.level[dr] == 0xFF && none_on_draining,
          "draining node gets no budget and no work (A9)");
}

static void test_table_full(void)
{
    /* 33 nodes into 32 slots, in two orders: the 32 lowest ids stay */
    ec_advert_t a;
    uint32_t hi = 0;
    for (uint32_t i = 1; i < 33; i++)
        if (memcmp(ids[i].id.b, ids[hi].id.b, 32) > 0) hi = i;
    ec_cluster_init(&cl, 0, 1000, 1000);
    ec_cluster_init(&cl2, 0, 1000, 1000);
    int full_seen = 0;
    for (uint32_t i = 0; i < 33; i++) {
        basic(&a, 1, 0, 1000 + i);
        ec_status_t s = put(&cl, &a, i, 10);
        if (s == EC_ERR_FULL) full_seen++;
    }
    for (uint32_t k = 0; k < 33; k++) {
        uint32_t i = 32 - k;
        basic(&a, 1, 0, 1000 + i);
        put(&cl2, &a, i, 10);
    }
    bool sorted = true;
    for (uint32_t i = 1; i < cl.n; i++) sorted &= memcmp(cl.nodes[i - 1].ad.node.b, cl.nodes[i].ad.node.b, 32) < 0;
    CHECK(cl.n == 32 && cl2.n == 32 && sorted, "table holds 32 entries, sorted by node id");
    CHECK(ec_cluster_find(&cl, &ids[hi].id) < 0 && ec_cluster_find(&cl2, &ids[hi].id) < 0,
          "the highest id is the one left out, in either order");
    bool same = true;
    for (uint32_t i = 0; i < 32; i++) same &= memcmp(&cl.nodes[i].ad, &cl2.nodes[i].ad, sizeof cl.nodes[i].ad) == 0;
    CHECK(same, "both orders give the same table");
    CHECK(full_seen == (hi == 32 ? 1 : 0), "EC_ERR_FULL only when the newcomer is the highest id");
    basic(&a, 1, 0, 1);
    CHECK(put(&cl, &a, hi, 10) == EC_ERR_FULL, "highest id refused while the table is full");
    /* budget over a full table: six Fibonacci levels */
    ec_cycle_begin(&cl, 10, 87000, &cy);
    uint32_t per_level[8] = {0};
    uint64_t sum = 0;
    for (uint32_t i = 0; i < 32; i++) {
        per_level[cy.level[i]]++;
        sum += cy.alloc[i];
    }
    CHECK(per_level[0] == 1 && per_level[1] == 2 && per_level[2] == 3 && per_level[3] == 5 &&
              per_level[4] == 8 && per_level[5] == 13,
          "32 nodes fill levels 1, 2, 3, 5, 8, 13");
    CHECK(sum + cy.unassigned == 87000 && cy.unassigned == 0, "full table: allotments sum to the budget");
    /* L = 6: weights F(7..2) = 13, 8, 5, 3, 2, 1; sum 32; the fastest gets 13/32 */
    int32_t top = -1;
    for (uint32_t i = 0; i < 32; i++)
        if (cy.level[i] == 0) top = (int32_t) i;
    /* level shares 35343.75, 21750, 13593.75, 8156.25, 5437.5, 2718.75: floors leave 3 tokens,
       which go to the three .75 remainders, lowest level first: level 0 -> 35344 */
    CHECK(top >= 0 && cy.alloc[top] == 35344, "level 0 gets 13/32 of the budget (largest remainder: 35344)");
}

/* ---------------- budget ---------------- */

static void three_nodes(uint32_t cap_permille, uint32_t d0, uint32_t d1, uint32_t d2)
{
    ec_advert_t a;
    ec_cluster_init(&cl, 0, 1000, cap_permille);
    basic(&a, 1, 0, d0);
    put(&cl, &a, 10, 1);
    basic(&a, 1, 0, d1);
    put(&cl, &a, 11, 1);
    basic(&a, 1, 0, d2);
    put(&cl, &a, 12, 1);
}

static uint64_t alloc_of(uint32_t who)
{
    return cy.alloc[ec_cluster_find(&cl, &ids[who].id)];
}

static void test_budget(void)
{
    /* A fastest, B and C next: L = 2, weights 2 : 1, level 1 split equally */
    three_nodes(1000, 9000, 5000, 4000);
    ec_cycle_begin(&cl, 1, 1200, &cy);
    CHECK(alloc_of(10) == 800 && alloc_of(11) == 200 && alloc_of(12) == 200 && cy.unassigned == 0,
          "1200 tokens over 3 nodes: 800 / 200 / 200 (weights 2 : 1, equal inside level)");
    CHECK(cy.level[ec_cluster_find(&cl, &ids[10].id)] == 0 && cy.level[ec_cluster_find(&cl, &ids[11].id)] == 1,
          "fastest node ranked to level 0");
    three_nodes(400, 9000, 5000, 4000);
    ec_cycle_begin(&cl, 1, 1200, &cy);
    CHECK(alloc_of(10) == 480 && alloc_of(11) == 360 && alloc_of(12) == 360 && cy.unassigned == 0,
          "cap 40%: 480 / 360 / 360 (cut 320 shared in proportion 200 : 200)");
    three_nodes(400, 9000, 5000, 4000);
    ec_cycle_begin(&cl, 1, 1201, &cy);
    /* cap = floor(1201*0.4) = 480. Fibonacci: 1201*2/3 = 800.67 -> 800 + rem; level 1 400.33 ->
       400 (+1 by largest remainder to level 0: rems 2/3 vs 1/3) -> L0 801, L1 400 = 200/200.
       Clip 801 -> 480, cut 321 over (200, 200): 160.5 each -> 161 to the lower index, 160. */
    uint64_t b = alloc_of(11), c = alloc_of(12);
    int32_t ib = ec_cluster_find(&cl, &ids[11].id), ic = ec_cluster_find(&cl, &ids[12].id);
    CHECK(alloc_of(10) == 480 && b + c == 721 && (ib < ic ? (b == 361 && c == 360) : (b == 360 && c == 361)),
          "odd budget: the leftover token goes to the lower table index");
    /* ties on speed rank by node id */
    three_nodes(1000, 5000, 5000, 5000);
    ec_cycle_begin(&cl, 1, 1200, &cy);
    CHECK(cy.alloc[0] == 800 && cy.alloc[1] == 200 && cy.alloc[2] == 200,
          "equal speeds: the lowest node id takes level 0");
    /* six nodes, L = 3, weights 3 : 2 : 1 -> 9000 / 3000 3000 / 1000 1000 1000 of 18000; cap 40%
       = 7200 clips 1800, shared in proportion 3 : 3 : 1 : 1 : 1 -> +600 +600 +200 +200 +200 */
    {
        ec_advert_t s6;
        static const uint32_t sp[6] = {9000, 8000, 7000, 3000, 2000, 1000};
        ec_cluster_init(&cl, 0, 1000, 400);
        for (uint32_t i = 0; i < 6; i++) {
            basic(&s6, 1, 0, sp[i]);
            put(&cl, &s6, 25 + i, 1);
        }
        ec_cycle_begin(&cl, 1, 18000, &cy);
        CHECK(alloc_of(25) == 7200 && alloc_of(26) == 3600 && alloc_of(27) == 3600 && alloc_of(28) == 1200 &&
                  alloc_of(29) == 1200 && alloc_of(30) == 1200 && cy.unassigned == 0,
              "cap cut shared in proportion to holdings: 7200 / 3600 3600 / 1200 1200 1200");
    }
    /* two nodes, cap below half: what cannot be placed is reported, not handed out */
    ec_advert_t a;
    ec_cluster_init(&cl, 0, 1000, 400);
    basic(&a, 1, 0, 9000);
    put(&cl, &a, 10, 1);
    basic(&a, 1, 0, 3000);
    put(&cl, &a, 11, 1);
    ec_cycle_begin(&cl, 1, 1000, &cy);
    CHECK(alloc_of(10) == 400 && alloc_of(11) == 400 && cy.unassigned == 200,
          "2 nodes x cap 400 < 1000: each 400, 200 unassigned (A5, A6)");
    ec_cycle_begin(&cl, 1, 0, &cy);
    CHECK(cy.alloc[0] == 0 && cy.alloc[1] == 0 && cy.unassigned == 0, "zero budget allots nothing");
    CHECK(ec_cycle_begin(&cl, 1, SWARM_MAX_TOKENS_PER_CYCLE + 1, &cy) == EC_ERR_ARG &&
              ec_cycle_begin(&cl, 1, SWARM_MAX_TOKENS_PER_CYCLE, &cy) == EC_OK,
          "budget limit is swarm_budget's 2^48");
    ec_cluster_init(&cl, 0, 1000, 400);
    ec_cycle_begin(&cl, 1, 999, &cy);
    CHECK(cy.unassigned == 999, "empty cluster: the whole budget is unassigned");

    /* sweep: conservation and the cap over many shapes */
    int cons = 0, capok = 0, full = 0, total = 0;
    for (uint32_t nn = 1; nn <= 9; nn++) {
        ec_cluster_init(&cl, 0, 1000, 1000);
        for (uint32_t i = 0; i < nn; i++) {
            basic(&a, 1, 0, 1000 + 997u * ((i * 7u) % 5u));
            put(&cl, &a, 13 + i, 1);
        }
        for (uint32_t k = 0; k < 40; k++) {
            uint64_t T = (k < 5) ? k : (uint64_t) lcg(1000000) + (k % 3 == 0 ? (1ull << 40) : 0);
            cl.cap_permille = 1 + lcg(1000);
            ec_cycle_begin(&cl, 1, T, &cy);
            uint64_t sum = 0, mx = 0, cap = T * cl.cap_permille / 1000;
            for (uint32_t i = 0; i < cl.n; i++) {
                sum += cy.alloc[i];
                if (cy.alloc[i] > mx) mx = cy.alloc[i];
            }
            total++;
            cons += sum + cy.unassigned == T;
            capok += mx <= cap;
            full += (cap * nn >= T) ? (cy.unassigned == 0) : (cy.unassigned == T - cap * nn);
        }
    }
    CHECK(cons == total, "conservation: sum(alloc) + unassigned == budget in all 360 sweeps");
    CHECK(capok == total, "no node over the cap in any sweep (no monopoly)");
    CHECK(full == total, "unassigned is 0 when nodes x cap covers the budget, else exactly the shortfall");
}

/* ---------------- scheduling ---------------- */

static void sched_cluster(void)
{
    ec_advert_t a;
    ec_cluster_init(&cl, 0, 1000, 1000);
    /* node 20: 3b Q4 fast-ish; node 21: 3b Q4 and Q8, slower, cheaper; node 22: 8b only */
    basic(&a, 1, 0, 10000);
    a.models[0].prefill_mtps = 100000;
    a.free_mem_mb = 1000;
    a.price = 30;
    put(&cl, &a, 20, 1);
    basic(&a, 1, 0, 5000);
    a.models[0].prefill_mtps = 50000;
    a.n_models = 2;
    set_model(&a.models[1], "llama-3.2-3b", EC_Q8_0, 3500, 40000, 4000);
    a.free_mem_mb = 2000;
    a.price = 5;
    put(&cl, &a, 21, 1);
    basic(&a, 1, 0, 3000);
    set_model(&a.models[0], "llama-3.1-8b", EC_Q4_K_M, 5000, 20000, 3000);
    a.queue_depth = 2;
    put(&cl, &a, 22, 1);
}

static ec_req_t mkreq(uint32_t id, const char *model, uint8_t q, uint32_t p, uint32_t g, uint32_t kv,
                      uint32_t lat)
{
    ec_req_t r;
    memset(&r, 0, sizeof r);
    r.req_id = id;
    strncpy(r.model, model, EC_NAME_LEN);
    r.quant = q;
    r.prompt_tokens = p;
    r.max_new_tokens = g;
    r.kv_mem_mb = kv;
    r.latency_ms = lat;
    return r;
}

static void test_schedule(void)
{
    sched_cluster();
    int32_t n20 = ec_cluster_find(&cl, &ids[20].id), n21 = ec_cluster_find(&cl, &ids[21].id),
            n22 = ec_cluster_find(&cl, &ids[22].id);
    ec_cycle_begin(&cl, 1, 1000000, &cy);
    ec_req_t r[8];
    ec_assign_t o[8];
    /* 1000 prompt at 100 tok/s = 10000 ms, 200 new at 10 tok/s = 20000 ms */
    r[0] = mkreq(1, "llama-3.2-3b", EC_Q_ANY, 1000, 200, 100, 0);
    CHECK(ec_schedule(&cl, &cy, r, 1, o) == EC_OK && o[0].node == n20 && o[0].est_ms == 30000 &&
              o[0].why == EC_WHY_OK && memcmp(o[0].node_id.b, ids[20].id.b, 32) == 0,
          "fastest fitting node chosen, estimate 10000 + 20000 = 30000 ms");
    CHECK(cy.used[n20] == 1200 && cy.mem_used[n20] == 100 && cy.queued[n20] == 1,
          "accounting: tokens, memory and queue charged to the node");
    /* the same request again: node 20 now 2 x 30000 = 60000; node 21: Q4 at 20000 + 40000 = 60000
       tie on estimate -> lower price (21) */
    r[0] = mkreq(2, "llama-3.2-3b", EC_Q_ANY, 1000, 200, 100, 0);
    ec_schedule(&cl, &cy, r, 1, o);
    CHECK(o[0].node == n21 && o[0].est_ms == 60000 && o[0].model_idx == 0,
          "equal estimates: the cheaper node wins; its faster-decoding quant picked");
    /* Q8 requested: only node 21's Q8 model; 1000/40 = 25000 + 200/4 = 50000 -> 75000, queue 1 -> x2 */
    r[0] = mkreq(3, "llama-3.2-3b", EC_Q8_0, 1000, 200, 100, 0);
    ec_schedule(&cl, &cy, r, 1, o);
    CHECK(o[0].node == n21 && o[0].model_idx == 1 && o[0].est_ms == 150000,
          "requested quantisation honoured, queue counted (75000 x 2)");
    /* ceil: 1 prompt token at 3 tok/s = 333.3 -> 334 ms; 1 at 7 tok/s = 142.9 -> 143; queue 2 -> x3 */
    ec_cluster_init(&cl2, 0, 1000, 1000);
    ec_advert_t a;
    basic(&a, 1, 0, 7000);
    a.models[0].prefill_mtps = 3000;
    a.queue_depth = 2;
    put(&cl2, &a, 23, 1);
    ec_cycle_begin(&cl2, 1, 100, &cy2);
    r[0] = mkreq(4, "llama-3.2-3b", EC_Q_ANY, 1, 1, 0, 0);
    ec_schedule(&cl2, &cy2, r, 1, o);
    CHECK(o[0].node == 0 && o[0].est_ms == (334 + 143) * 3, "estimates round each part up, then x (1 + queue)");
    r[0] = mkreq(5, "llama-3.2-3b", EC_Q_ANY, 1, 1, 0, (334 + 143) * 4 - 1);
    ec_schedule(&cl2, &cy2, r, 1, o);
    CHECK(o[0].node == -1 && o[0].why == EC_WHY_LATENCY, "one ms under the estimate misses the latency target");
    r[0] = mkreq(6, "llama-3.2-3b", EC_Q_ANY, 1, 1, 0, (334 + 143) * 4);
    ec_schedule(&cl2, &cy2, r, 1, o);
    CHECK(o[0].node == 0 && o[0].est_ms == (334 + 143) * 4, "exactly the target fits");
    r[0] = mkreq(7, "llama-3.2-3b", EC_Q_ANY, 50, 47, 0, 0);
    ec_schedule(&cl2, &cy2, r, 1, o);
    CHECK(o[0].node == -1 && o[0].why == EC_WHY_NO_BUDGET, "97 tokens with 96 left in the allotment: no budget");
    r[0] = mkreq(8, "llama-3.2-3b", EC_Q_ANY, 50, 46, 0, 0);
    ec_schedule(&cl2, &cy2, r, 1, o);
    CHECK(o[0].node == 0 && cy2.used[0] == 100, "96 tokens exactly use up the allotment");
    /* EC_Q_ANY: of two quants of the model, the faster-decoding one, wherever it is listed */
    ec_cluster_init(&cl2, 0, 1000, 1000);
    basic(&a, 1, 0, 3000);
    a.models[0].quant = EC_Q8_0;
    a.n_models = 2;
    set_model(&a.models[1], "llama-3.2-3b", EC_Q4_0, 1800, 50000, 6000);
    put(&cl2, &a, 23, 1);
    ec_cycle_begin(&cl2, 1, 1000, &cy2);
    r[0] = mkreq(9, "llama-3.2-3b", EC_Q_ANY, 10, 10, 0, 0);
    ec_schedule(&cl2, &cy2, r, 1, o);
    CHECK(o[0].node == 0 && o[0].model_idx == 1, "EC_Q_ANY picks the faster-decoding quant (listed second)");
    /* reasons */
    ec_cycle_begin(&cl, 1, 1000000, &cy);
    r[0] = mkreq(10, "mistral-7b", EC_Q_ANY, 10, 10, 0, 0);
    r[1] = mkreq(11, "llama-3.2-3b", EC_F16, 10, 10, 0, 0);
    r[2] = mkreq(12, "llama-3.1-8b", EC_Q_ANY, 10, 10, 4097, 0);
    r[3] = mkreq(13, "llama-3.1-8b", EC_Q_ANY, 10, 10, 4096, 0);
    r[4] = mkreq(14, "llama-3.1-8b", EC_Q_ANY, 10, 10, 1, 0);
    ec_schedule(&cl, &cy, r, 5, o);
    CHECK(o[0].node == -1 && o[0].why == EC_WHY_NO_MODEL, "unknown model: EC_WHY_NO_MODEL");
    CHECK(o[1].node == -1 && o[1].why == EC_WHY_NO_MODEL, "model present but not in that quant: no model");
    CHECK(o[2].node == -1 && o[2].why == EC_WHY_NO_MEMORY, "4097 MB working memory on a 4096 MB node: no memory");
    CHECK(o[3].node == n22 && o[4].node == -1 && o[4].why == EC_WHY_NO_MEMORY,
          "memory is charged within the cycle: the next 1 MB no longer fits");
    /* argument checks */
    r[0] = mkreq(1, "x", EC_Q_ANY, 1, 1, 0, 0);
    r[1] = mkreq(1, "x", EC_Q_ANY, 1, 1, 0, 0);
    CHECK(ec_schedule(&cl, &cy, r, 2, o) == EC_ERR_ARG, "duplicate req_id refused");
    CHECK(ec_schedule(&cl, &cy, r, EC_MAX_REQS + 1, o) == EC_ERR_ARG, "more than EC_MAX_REQS refused");
    ec_advert_t b2;
    basic(&b2, 1, 0, 100);
    put(&cl, &b2, 24, 1);
    CHECK(ec_schedule(&cl, &cy, r, 1, o) == EC_ERR_CYCLE, "table changed since the cycle began: EC_ERR_CYCLE");
}

/* ---------------- failover ---------------- */

static void test_failover(void)
{
    sched_cluster();
    int32_t n20 = ec_cluster_find(&cl, &ids[20].id), n21 = ec_cluster_find(&cl, &ids[21].id),
            n22 = ec_cluster_find(&cl, &ids[22].id);
    cl.cap_permille = 600;
    ec_cycle_begin(&cl, 100, 90000, &cy);
    ec_req_t r[6];
    ec_assign_t o[6], before[6];
    for (uint32_t i = 0; i < 5; i++) r[i] = mkreq(40 - i, "llama-3.2-3b", EC_Q_ANY, 100, 50, 10, 0);
    r[5] = mkreq(50, "llama-3.1-8b", EC_Q_ANY, 100, 50, 10, 0);
    ec_schedule(&cl, &cy, r, 6, o);
    memcpy(before, o, sizeof o);
    uint32_t on20 = 0;
    for (uint32_t i = 0; i < 6; i++) on20 += o[i].node == n20;
    CHECK(on20 >= 1 && o[5].node == n22, "work spread before the failure");
    uint32_t moved = 99;
    CHECK(ec_failover(&cl, &cy, &ids[20].id, r, 6, o, &moved) == EC_OK && moved == on20,
          "failover moves exactly the requests that were on the failed node");
    bool stayed = true, rehomed = true;
    for (uint32_t i = 0; i < 6; i++) {
        if (before[i].node != n20)
            stayed &= memcmp(&before[i], &o[i], sizeof o[i]) == 0;
        else
            rehomed &= o[i].node == n21;
    }
    CHECK(stayed, "requests on healthy nodes do not move");
    CHECK(rehomed, "moved requests land on the other node that has the model");
    uint64_t sum = 0;
    for (uint32_t i = 0; i < cl.n; i++) sum += cy.alloc[i];
    CHECK(cy.alloc[n20] == 0 && sum + cy.unassigned == 90000, "failed node's allotment returned; budget conserved");
    uint64_t cap = 90000u * 600u / 1000u;
    CHECK(cy.alloc[n21] <= cap && cy.alloc[n22] <= cap, "returned tokens respect the cap");
    CHECK(cy.used[n21] == 750 && cy.queued[n21] == 5,
          "all five 3b requests now charged to node 21");
    CHECK(ec_failover(&cl, &cy, &ids[0].id, r, 6, o, &moved) == EC_ERR_UNKNOWN && moved == 0,
          "unknown node: EC_ERR_UNKNOWN");
    ec_cycle_begin(&cl, 200, 90000, &cy);
    CHECK(cy.alloc[n20] == 0 && cy.level[n20] == 0xFF, "a failed node gets nothing next cycle");
    ec_advert_t a;
    basic(&a, 2, 100, 10000); /* issued at the failure time, not after */
    CHECK(put(&cl, &a, 20, 200) == EC_OK && cl.nodes[n20].failed, "newer advert issued at the failure time: still failed");
    basic(&a, 3, 101, 10000);
    CHECK(put(&cl, &a, 20, 200) == EC_OK && !cl.nodes[n20].failed, "newer advert issued after the failure revives it");
    ec_cycle_begin(&cl, 200, 90000, &cy);
    CHECK(cy.alloc[n20] > 0, "revived node gets budget again");
    /* failing the only node with the model leaves the work unassigned with a reason */
    for (uint32_t i = 0; i < 6; i++) o[i] = (ec_assign_t){0};
    ec_schedule(&cl, &cy, r, 6, o);
    ec_failover(&cl, &cy, &ids[22].id, r, 6, o, &moved);
    CHECK(moved == 1 && o[5].node == -1 && o[5].why == EC_WHY_NO_MODEL,
          "no other node has the model: left unassigned, reason no model");
    cy2 = cy;
    cy2.gen ^= 1u;
    CHECK(ec_failover(&cl, &cy2, &ids[21].id, r, 6, o, &moved) == EC_ERR_CYCLE,
          "failover with a stale cycle refused");
}

int main(void)
{
    make_ids();
    test_codec();
    test_seal_rules();
    test_tamper();
    test_freshness();
    test_seq_and_equivocation();
    test_permutation();
    test_table_full();
    test_budget();
    test_schedule();
    test_failover();
    printf("%d checks passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
