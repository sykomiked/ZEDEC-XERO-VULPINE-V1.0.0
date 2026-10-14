/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_aftp.c — host tests for ap_log (the twin audit chain) and AFTP (the FTP
 * twin). Every rule in ap_log.h and aftp.h has a check that tries to break it.
 * Build: make -C kernel test-aproto */
#include <stdio.h>
#include <string.h>

#include "aftp.h"

static int g_pass, g_fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static vna_drbg_t g_rng;
static void rnd(uint8_t *out, uint32_t n)
{
    vna_drbg_gen(&g_rng, out, n);
}

/* ---- a log sink that keeps every entry, and can be told to refuse ---- */
#define STORE_MAX 4096u
typedef struct {
    uint8_t e[STORE_MAX][AP_ENTRY_LEN];
    uint32_t n;
    bool refuse;
} store_t;
static int sink(void *ctx, const uint8_t *entry, uint32_t len)
{
    store_t *s = ctx;
    if (s->refuse || s->n >= STORE_MAX || len != AP_ENTRY_LEN) return -1;
    memcpy(s->e[s->n++], entry, len);
    return 0;
}

/* Replay a store from genesis; returns the number of entries that fed. */
static uint32_t replay(const store_t *s, const vna_id_t *owner, ap_audit_t *a)
{
    ap_audit_init(a, owner);
    uint32_t k = 0;
    while (k < s->n && ap_audit_feed(a, s->e[k], AP_ENTRY_LEN, 0) == VNA_OK) k++;
    return k;
}

/* Count entries of one event and tag. */
static uint32_t count(const store_t *s, uint16_t ev, uint8_t tag)
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < s->n; i++) {
        ap_entry_t e;
        if (vna_schema_unpack(&ap_entry_schema, s->e[i], AP_ENTRY_LEN, &e, 0) < 0) continue;
        k += e.event == ev && e.tag == tag;
    }
    return k;
}

static vna_identity_t A, B, C, M; /* publisher/server, receiver, honest peer, malicious peer */
static store_t SA, SB;
static ap_log_t LA, LB;
static aftp_t FA, FB;

static void make_id(vna_identity_t *id, uint8_t s)
{
    uint8_t seed[32];
    memset(seed, s, 32);
    CHECK(vna_identity_create(id, seed, 0, 1) == VNA_OK, "identity %u", s);
}

/* ------------------------------------------------------------------ */
static void test_log(void)
{
    printf("[1] ap_log: hash chain, sink failure, checkpoints, auditor, forks\n");
    static store_t s;
    ap_log_t l;
    memset(&s, 0, sizeof s);
    ap_log_init(&l, &A.id, sink, &s);
    uint8_t g[32];
    ap_log_genesis(&A.id, g);
    CHECK(l.seq == 0 && memcmp(l.head, g, 32) == 0, "starts at genesis (L2)");
    vna_id_t p = B.id, sub;
    memset(&sub, 7, sizeof sub);
    CHECK(vna_schema_max_len(&ap_entry_schema) == AP_ENTRY_LEN, "entry has a fixed length");
    for (uint32_t i = 0; i < 10; i++)
        CHECK(ap_log_append(&l, AP_TWIN_AFTP, 1, (uint8_t) (i & 3), &p, &sub, i * 100u,
                            -(int32_t) (i % 3), 1000 + i) == VNA_OK,
              "append %u", i);
    CHECK(l.seq == 10 && s.n == 10, "ten entries, ten stored");
    CHECK(l.by_tag[0] == 3 && l.by_tag[1] == 3 && l.by_tag[2] == 2 && l.by_tag[3] == 2,
          "per-tag counts");
    CHECK(ap_log_append(&l, AP_TWIN_AFTP, 1, 4, 0, 0, 0, 0, 2000) == VNA_ERR_ARG,
          "tag above BOTH refused");
    CHECK(ap_log_append(&l, AP_TWIN_AFTP, 1, 1, 0, 0, 0, 5, 2000) == VNA_ERR_ARG,
          "positive status refused");
    CHECK(ap_log_append(0, AP_TWIN_AFTP, 1, 1, 0, 0, 0, 0, 2000) == VNA_ERR_ARG, "NULL log");
    CHECK(l.seq == 10, "refused appends do not advance");

    /* L3: time never goes backwards */
    CHECK(ap_log_append(&l, AP_TWIN_AFTP, 2, 1, 0, 0, 0, 0, 5) == VNA_OK && l.last_time == 1009,
          "an earlier clock is recorded as the last time");
    ap_entry_t e;
    CHECK(vna_schema_unpack(&ap_entry_schema, s.e[10], AP_ENTRY_LEN, &e, 0) > 0 && e.time == 1009 &&
              e.seq == 11 && e.code == 0,
          "entry 11 carries the clamped time");
    CHECK(vna_schema_unpack(&ap_entry_schema, s.e[2], AP_ENTRY_LEN, &e, 0) > 0 && e.code == 2 &&
              e.tag == 2 && e.amount == 200 && vna_id_eq(&e.peer, &B.id),
          "entry 3 fields round-trip");

    /* L4: sink failure */
    uint8_t h0[32];
    memcpy(h0, l.head, 32);
    s.refuse = true;
    CHECK(ap_log_append(&l, AP_TWIN_AFTP, 1, 1, 0, 0, 0, 0, 3000) == VNA_ERR_SPACE && l.seq == 11 &&
              memcmp(h0, l.head, 32) == 0 && l.sink_failures == 1 && s.n == 11,
          "a refusing sink leaves the log unchanged (no audit, no action)");
    s.refuse = false;

    /* L6: auditor */
    ap_audit_t au;
    CHECK(replay(&s, &A.id, &au) == 11 && au.seq == 11 && memcmp(au.head, l.head, 32) == 0,
          "auditor replays to the live head");
    ap_audit_t a2;
    ap_audit_init(&a2, &B.id);
    CHECK(ap_audit_feed(&a2, s.e[0], AP_ENTRY_LEN, 0) == VNA_ERR_PARSE, "wrong owner refused");
    /* tamper, drop, reorder, duplicate, truncate */
    static store_t t;
    uint32_t caught = 0, trials = 0;
    for (uint32_t i = 0; i < 11; i++)
        for (uint32_t b = 0; b < AP_ENTRY_LEN; b += 7) {
            t = s;
            t.e[i][b] ^= 0x01;
            trials++;
            ap_audit_t x;
            uint32_t fed = replay(&t, &A.id, &x);
            caught += fed < 11 || memcmp(x.head, l.head, 32) != 0;
        }
    CHECK(caught == trials, "every single-bit change detected (%u of %u)", caught, trials);
    t = s;
    memmove(t.e[4], t.e[5], (size_t) 6 * AP_ENTRY_LEN);
    t.n = 10;
    CHECK(replay(&t, &A.id, &au) == 4, "a dropped entry stops the replay");
    t = s;
    memcpy(t.e[3], s.e[4], AP_ENTRY_LEN);
    memcpy(t.e[4], s.e[3], AP_ENTRY_LEN);
    CHECK(replay(&t, &A.id, &au) == 3, "a reordering stops the replay");
    t = s;
    memcpy(t.e[5], s.e[4], AP_ENTRY_LEN);
    CHECK(replay(&t, &A.id, &au) == 5, "a duplicated entry stops the replay");
    CHECK(ap_audit_feed(&au, s.e[5], AP_ENTRY_LEN - 1, 0) == VNA_ERR_PARSE &&
              ap_audit_feed(&au, s.e[5], AP_ENTRY_LEN + 1, 0) == VNA_ERR_PARSE,
          "truncated or extended entry refused");
    /* an entry that keeps prev but moves time back */
    {
        ap_log_t l2;
        static store_t s2;
        memset(&s2, 0, sizeof s2);
        ap_log_init(&l2, &A.id, sink, &s2);
        ap_log_append(&l2, AP_TWIN_AFTP, 1, 1, 0, 0, 0, 0, 500);
        ap_entry_t f;
        f.magic = AP_LOG_MAGIC;
        f.owner = A.id;
        f.seq = 2;
        memcpy(f.prev, l2.head, 32);
        f.time = 499;
        f.twin = 21;
        f.event = 1;
        f.tag = 1;
        memset(&f.peer, 0, 32);
        memset(&f.subject, 0, 32);
        f.amount = 0;
        f.code = 0;
        vna_schema_pack(&ap_entry_schema, &f, s2.e[1], AP_ENTRY_LEN, true);
        s2.n = 2;
        CHECK(replay(&s2, &A.id, &au) == 1, "a forged entry with time going back refused");
    }

    /* L5: checkpoints */
    static uint8_t cp[AP_HEAD_MAX], cp2[AP_HEAD_MAX];
    uint8_t r[32];
    rnd(r, 32);
    int32_t cl = ap_log_checkpoint(&l, &A, r, 4000, cp, sizeof cp);
    CHECK(cl == (int32_t) AP_HEAD_MAX, "checkpoint signed (%d bytes)", cl);
    CHECK(ap_log_checkpoint(&l, &B, r, 4000, cp2, sizeof cp2) == VNA_ERR_ARG,
          "only the owner signs its log's checkpoint");
    CHECK(ap_log_checkpoint(&l, &A, r, 4000, cp2, 100) == VNA_ERR_SPACE, "small buffer");
    ap_head_t h, h2;
    CHECK(ap_head_open(cp, (uint32_t) cl, A.pk, &h) == VNA_OK && h.seq == 11 &&
              memcmp(h.head, l.head, 32) == 0,
          "checkpoint verifies");
    CHECK(ap_head_open(cp, (uint32_t) cl, B.pk, &h2) == VNA_ERR_BINDING, "wrong key: binding");
    cp[60] ^= 1;
    CHECK(ap_head_open(cp, (uint32_t) cl, A.pk, &h2) == VNA_ERR_SIG, "changed head: signature");
    cp[60] ^= 1;
    CHECK(ap_head_open(cp, (uint32_t) cl - 1, A.pk, &h2) == VNA_ERR_PARSE, "truncated");
    replay(&s, &A.id, &au);
    CHECK(ap_audit_matches(&au, &h) == AP_TAG_T, "replay reaches the checkpoint");
    /* a fork: same seq, different history */
    ap_log_t lf;
    ap_log_init(&lf, &A.id, 0, 0);
    for (uint32_t i = 0; i < 11; i++) ap_log_append(&lf, AP_TWIN_AFTP, 9, 1, 0, 0, 0, 0, i);
    rnd(r, 32);
    int32_t fl = ap_log_checkpoint(&lf, &A, r, 4000, cp2, sizeof cp2);
    CHECK(fl > 0 && ap_head_open(cp2, (uint32_t) fl, A.pk, &h2) == VNA_OK, "fork checkpoint");
    CHECK(ap_head_compare(&h, &h2) == AP_TAG_B, "same seq, different head: BOTH (fork)");
    CHECK(ap_head_compare(&h, &h) == AP_TAG_T, "same checkpoint: T");
    CHECK(ap_audit_matches(&au, &h2) == AP_TAG_F, "replay contradicts the forked checkpoint");
    ap_log_append(&lf, AP_TWIN_AFTP, 9, 1, 0, 0, 0, 0, 20);
    fl = ap_log_checkpoint(&lf, &A, r, 4000, cp2, sizeof cp2);
    ap_head_open(cp2, (uint32_t) fl, A.pk, &h2);
    CHECK(ap_head_compare(&h, &h2) == AP_TAG_N && ap_audit_matches(&au, &h2) == AP_TAG_N,
          "different seq: cannot tell (N)");
    printf("    entry %u bytes, checkpoint %u bytes\n", AP_ENTRY_LEN, AP_HEAD_MAX);
}

/* ------------------------------------------------------------------ */
static void test_paths(void)
{
    printf("[2] paths (D2): relative, no traversal, no controls, valid UTF-8\n");
    static const char *good[] = {"a",
                                 "readme.txt",
                                 "docs/readme.txt",
                                 "a/b/c/d",
                                 ".hidden",
                                 "..x",
                                 "x..",
                                 "a.b/c..d",
                                 "caf\xc3\xa9",
                                 "\xe2\x82\xac 5",
                                 "\xf0\x9f\xa6\x8a fox",
                                 "\xf4\x8f\xbf\xbf",
                                 "sp ace"};
    static const char *bad[] = {"/a",
                                "a/",
                                "a//b",
                                ".",
                                "..",
                                "a/./b",
                                "a/../b",
                                "../a",
                                "a/..",
                                "a\\b",
                                "a\nb",
                                "a\tb",
                                "a\x7f",
                                "\xc0\xaf",
                                "\xe0\x80\xaf",
                                "\xed\xa0\x80",
                                "\xf4\x90\x80\x80",
                                "\xf5\x80\x80\x80",
                                "\x80",
                                "\xc3",
                                "\xc2\x85",
                                "x\xe2\x80\xaetxt.exe",
                                "\xe2\x81\xa6x",
                                "\xff"};
    uint32_t ok = 0;
    for (uint32_t i = 0; i < sizeof good / sizeof *good; i++) {
        bool r = aftp_path_ok((const uint8_t *) good[i], (uint32_t) strlen(good[i]));
        ok += r;
        if (!r) printf("    refused good path %u\n", i);
    }
    CHECK(ok == sizeof good / sizeof *good, "every good path accepted");
    uint32_t no = 0;
    for (uint32_t i = 0; i < sizeof bad / sizeof *bad; i++) {
        bool r = aftp_path_ok((const uint8_t *) bad[i], (uint32_t) strlen(bad[i]));
        no += !r;
        if (r) printf("    accepted bad path %u\n", i);
    }
    CHECK(no == sizeof bad / sizeof *bad, "every bad path refused");
    uint8_t nul[3] = {'a', 0, 'b'};
    CHECK(!aftp_path_ok(nul, 3), "NUL refused");
    CHECK(!aftp_path_ok((const uint8_t *) "", 0) && !aftp_path_ok(0, 1), "empty / NULL refused");
    uint8_t longp[AFTP_NAME_MAX + 1];
    memset(longp, 'x', sizeof longp);
    CHECK(aftp_path_ok(longp, AFTP_NAME_MAX) && !aftp_path_ok(longp, AFTP_NAME_MAX + 1),
          "128 bytes accepted, 129 refused");
    /* exhaustive two-byte sweep: no accepted pair is a control or malformed */
    uint32_t bad_ok = 0;
    for (uint32_t x = 0; x < 256; x++)
        for (uint32_t y = 0; y < 256; y++) {
            uint8_t p[2] = {(uint8_t) x, (uint8_t) y};
            if (!aftp_path_ok(p, 2)) continue;
            bool ascii = x >= 0x20 && x < 0x7f && y >= 0x20 && y < 0x7f && x != '/' && y != '/' &&
                         x != '\\' && y != '\\' && !(x == '.' && y == '.');
            bool two = x >= 0xC2 && x <= 0xDF && y >= 0x80 && y <= 0xBF && !(x == 0xC2 && y < 0xA0);
            bad_ok += !(ascii || two);
        }
    CHECK(bad_ok == 0, "two-byte sweep: only printable ASCII or valid non-control UTF-8 (%u)",
          bad_ok);
}

/* ------------------------------------------------------------------ */
static void entry(aftp_entry_t *e, const char *name, uint8_t fill, uint64_t size)
{
    memset(e, 0, sizeof *e);
    e->name_len = (uint16_t) strlen(name);
    memcpy(e->name, name, e->name_len);
    memset(&e->root, fill, 32);
    e->size = size;
    e->chunk_size = VNA_CHUNK_SIZE;
    e->kind = AFTP_KIND_DATA;
    e->tag = AP_TAG_T;
}

static aftp_dir_t g_dir, g_dir2;
static uint8_t g_buf[24000], g_buf2[24000];

static void test_dir(void)
{
    printf("[3] listings (D1-D5): signed, bounded, ordered, rollback and equivocation\n");
    uint64_t now = 1000000;
    uint8_t r[32];
    memset(&g_dir, 0, sizeof g_dir);
    memset(&g_dir.share, 0x51, 32);
    g_dir.seq = 5;
    g_dir.created = now;
    g_dir.expires = now + 3600000;
    g_dir.tag = AP_TAG_T;
    g_dir.n = 3;
    entry(&g_dir.e[0], "docs/a.txt", 1, 100);
    entry(&g_dir.e[1], "docs/b.txt", 2, 200);
    entry(&g_dir.e[2], "model.bin", 3, 1u << 20);
    rnd(r, 32);
    uint32_t log0 = SA.n;
    int32_t dl = aftp_dir_sign(&FA, &g_dir, r, now, g_buf, sizeof g_buf);
    CHECK(dl > 0 && SA.n == log0 + 1 && count(&SA, AFTP_EV_DIR_OUT, AP_TAG_T) >= 1,
          "listing signed and logged (%d bytes)", dl);
    CHECK(vna_schema_max_len(&aftp_dir_schema) <= VNA_SESS_MAX_PT,
          "a full listing fits one session record (%u <= %u)", vna_schema_max_len(&aftp_dir_schema),
          VNA_SESS_MAX_PT);
    CHECK(vna_schema_max_len(&aftp_offer_schema) <= VNA_SESS_MAX_PT &&
              vna_schema_max_len(&vna_chunk_schema) <= VNA_SESS_MAX_PT,
          "an offer and a full chunk each fit one session record");

    /* D1 */
    CHECK(aftp_dir_open(g_buf, (uint32_t) dl, A.pk, now, &g_dir2) == VNA_OK && g_dir2.n == 3 &&
              vna_id_eq(&g_dir2.publisher, &A.id),
          "listing opens");
    CHECK(aftp_dir_open(g_buf, (uint32_t) dl, B.pk, now, &g_dir2) == VNA_ERR_BINDING,
          "another key: binding refused");
    CHECK(aftp_dir_open(g_buf, (uint32_t) dl, A.pk, now + 3600000, &g_dir2) == VNA_ERR_EXPIRED,
          "expired refused");
    CHECK(aftp_dir_open(g_buf, (uint32_t) dl, A.pk, now - AFTP_SKEW_MS - 1, &g_dir2) ==
              VNA_ERR_STALE,
          "created in the future refused");
    CHECK(aftp_dir_open(g_buf, (uint32_t) dl, A.pk, now - AFTP_SKEW_MS, &g_dir2) == VNA_OK,
          "inside the skew accepted");
    uint32_t sigfail = 0, tried = 0;
    for (int32_t i = 0; i < dl; i += 37) {
        memcpy(g_buf2, g_buf, (size_t) dl);
        g_buf2[i] ^= 0x10;
        tried++;
        sigfail += aftp_dir_open(g_buf2, (uint32_t) dl, A.pk, now, &g_dir2) != VNA_OK;
    }
    CHECK(sigfail == tried, "every changed byte refused (%u of %u)", sigfail, tried);
    CHECK(aftp_dir_open(g_buf, (uint32_t) dl - 1, A.pk, now, &g_dir2) == VNA_ERR_PARSE,
          "truncated refused");

    /* D2-D4 at signing (a publisher cannot sign a bad listing) */
    aftp_dir_t d = g_dir;
    entry(&d.e[1], "docs/../../etc/passwd", 2, 200);
    CHECK(aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2) == VNA_ERR_PARSE,
          "traversal path refused at signing");
    d = g_dir;
    entry(&d.e[1], "docs/a.txt", 2, 200);
    CHECK(aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2) == VNA_ERR_PARSE,
          "duplicate path refused (D3)");
    d = g_dir;
    entry(&d.e[0], "zz", 2, 200);
    CHECK(aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2) == VNA_ERR_PARSE,
          "unsorted paths refused (D3)");
    d = g_dir;
    d.e[2].chunk_size = 3000;
    CHECK(aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2) == VNA_ERR_PARSE,
          "chunk size not a power of two refused (D4)");
    d = g_dir;
    d.e[2].size = (uint64_t) VNA_FILE_MAX_CHUNKS * VNA_CHUNK_SIZE + 1;
    CHECK(aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2) == VNA_ERR_PARSE,
          "too many chunks refused (D4)");
    d = g_dir;
    d.expires = d.created;
    CHECK(aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2) == VNA_ERR_ARG,
          "a listing that is born expired refused");
    /* a hostile publisher signs a traversal path by hand: receivers refuse it */
    {
        d = g_dir;
        entry(&d.e[0], "../x", 1, 100);
        d.magic = AFTP_DIR_MAGIC;
        d.publisher = M.id;
        int32_t m = vna_schema_pack(&aftp_dir_schema, &d, g_buf2, sizeof g_buf2, false);
        vna_sign(M.sk, AFTP_CTX_DIR, g_buf2, (uint32_t) m, r, d.sig);
        m = vna_schema_pack(&aftp_dir_schema, &d, g_buf2, sizeof g_buf2, true);
        uint8_t tag = 9;
        CHECK(m > 0 &&
                  aftp_dir_learn(&FB, g_buf2, (uint32_t) m, M.pk, now, &g_dir2, &tag) ==
                      VNA_ERR_PARSE &&
                  tag == AP_TAG_F,
              "a correctly signed listing with a traversal path is refused by the receiver");
    }

    /* D5 */
    uint8_t tag = 9;
    uint32_t b0 = SB.n;
    CHECK(aftp_dir_learn(&FB, g_buf, (uint32_t) dl, A.pk, now, &g_dir2, &tag) == VNA_OK &&
              tag == AP_TAG_T && SB.n == b0 + 1,
          "first listing learned (T) and logged");
    CHECK(aftp_dir_learn(&FB, g_buf, (uint32_t) dl, A.pk, now, &g_dir2, &tag) == VNA_ERR_DUP &&
              tag == AP_TAG_T,
          "same listing again: duplicate");
    /* re-signed with fresh randomness: different bytes, same content -> still a duplicate */
    d = g_dir;
    rnd(r, 32);
    int32_t dl2 = aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2);
    CHECK(dl2 > 0 && memcmp(g_buf, g_buf2, (size_t) dl) != 0 &&
              aftp_dir_learn(&FB, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_ERR_DUP,
          "same content re-signed is a duplicate, not a fork");
    /* equivocation: same seq, other content */
    d = g_dir;
    d.e[0].size = 101;
    rnd(r, 32);
    dl2 = aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2);
    CHECK(aftp_dir_learn(&FB, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_ERR_FORK &&
              tag == AP_TAG_B && aftp_dir_equivocated(&FB, &A.id, &g_dir.share) &&
              FB.equivocations == 1,
          "same seq, different content: BOTH, share marked");
    CHECK(aftp_dir_learn(&FB, g_buf, (uint32_t) dl, A.pk, now, &g_dir2, &tag) == VNA_ERR_DUP,
          "the first listing is the one kept");
    /* newer */
    d = g_dir;
    d.seq = 6;
    d.n = 2;
    rnd(r, 32);
    dl2 = aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2);
    CHECK(aftp_dir_learn(&FB, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_OK &&
              tag == AP_TAG_T && g_dir2.n == 2,
          "newer seq replaces (DELE is a new listing)");
    CHECK(aftp_dir_equivocated(&FB, &A.id, &g_dir.share), "equivocation mark stays");
    CHECK(aftp_dir_learn(&FB, g_buf, (uint32_t) dl, A.pk, now, &g_dir2, &tag) == VNA_ERR_REPLAY &&
              tag == AP_TAG_F,
          "older seq after a newer one: rollback refused (F)");
    /* another share of the same publisher is independent */
    d = g_dir;
    memset(&d.share, 0x52, 32);
    d.seq = 1;
    rnd(r, 32);
    dl2 = aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2);
    CHECK(aftp_dir_learn(&FB, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_OK &&
              !aftp_dir_equivocated(&FB, &A.id, &d.share),
          "another share: independent sequence");
    /* L4 through AFTP: the log refuses, nothing is learned */
    d = g_dir;
    memset(&d.share, 0x53, 32);
    rnd(r, 32);
    dl2 = aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2);
    SB.refuse = true;
    CHECK(aftp_dir_learn(&FB, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_ERR_SPACE,
          "log refuses: learn fails");
    SB.refuse = false;
    CHECK(aftp_dir_learn(&FB, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_OK,
          "and the same listing is new afterwards (nothing was kept)");
    /* the known table fills: N */
    aftp_t tmp = FB;
    for (uint32_t i = 0; i < AFTP_KNOWN; i++) tmp.known[i].used = true;
    d = g_dir;
    memset(&d.share, 0x54, 32);
    rnd(r, 32);
    dl2 = aftp_dir_sign(&FA, &d, r, now, g_buf2, sizeof g_buf2);
    CHECK(aftp_dir_learn(&tmp, g_buf2, (uint32_t) dl2, A.pk, now, &g_dir2, &tag) == VNA_ERR_SPACE &&
              tag == AP_TAG_N,
          "full table: N, not F");

    /* directory requests */
    uint8_t q[128];
    aftp_dir_req_t rq;
    int32_t ql = aftp_dir_req_encode(&A.id, &g_dir.share, 5, q, sizeof q);
    CHECK(ql > 0 && aftp_dir_req_decode(q, (uint32_t) ql, &rq) == VNA_OK && rq.have_seq == 5 &&
              vna_id_eq(&rq.publisher, &A.id),
          "LIST request round-trips");
    CHECK(aftp_dir_req_decode(q, (uint32_t) ql - 1, &rq) == VNA_ERR_PARSE &&
              aftp_dir_req_encode(&A.id, &g_dir.share, 5, q, 10) == VNA_ERR_SPACE,
          "truncated request, small buffer");
}

/* ------------------------------------------------------------------ */
#define FILE_LEN 100000u
#define CS       1024u
static uint8_t g_file[FILE_LEN], g_bad[FILE_LEN], g_recv[FILE_LEN];
static uint8_t g_leaves[128][32];
static vna_id_t g_root;
static vna_agreement_t g_ag;
static vna_usage_ent_t g_ue[16];
static vna_lease_t g_le[4];
static vna_usage_t g_us;
static vna_chunk_t g_sc, g_rc;

/* Serve a request as `who`: A and C honestly, M with one byte flipped. */
static int32_t serve_as(const vna_identity_t *who, const vna_id_t *peer, const uint8_t *req,
                        uint32_t rl, uint64_t now, uint8_t *out, uint32_t cap)
{
    vna_shared_file_t sf = {
        g_root, FILE_LEN, CS, 0, (const uint8_t(*)[32]) g_leaves, who == &M ? g_bad : g_file};
    sf.n = vna_file_nchunks(FILE_LEN, CS);
    if (who == &A)
        return aftp_serve(&FA, &sf, &g_ag, &g_us, peer, 0, req, rl, now, &g_sc, out, cap);
    return vna_file_serve(&sf, &g_ag, &g_us, peer, 0, req, rl, now, &g_sc, out, cap);
}

static void test_get(void)
{
    printf("[4] multi-source get (G1-G5) and serving (S1)\n");
    for (uint32_t i = 0; i < FILE_LEN; i++) g_file[i] = (uint8_t) (i * 2654435761u >> 11);
    memcpy(g_bad, g_file, FILE_LEN);
    for (uint32_t i = 0; i < FILE_LEN; i += CS) g_bad[i + 5] ^= 0x40; /* every chunk wrong */
    uint32_t n = vna_file_build(g_file, FILE_LEN, CS, g_leaves, 128, &g_root);
    CHECK(n == 98, "100000 bytes in 1 KiB chunks: 98");
    vna_agree_default(&g_ag, &A.id);
    g_ag.degree = VNA_DEG_SERVE;
    g_ag.audience = VNA_AUD_EVERYONE;
    g_ag.n_files = 1;
    g_ag.files[0].root = g_root;
    g_ag.q_chunks = 100000;
    g_ag.expires_ms = UINT64_MAX;
    g_ag.quota_period_ms = 3600000;
    vna_usage_init(&g_us, g_ue, 16, g_le, 4);

    uint64_t now = 2000000;
    static aftp_get_t g;
    uint8_t bitmap[13];
    CHECK(aftp_get_start(&FB, &g, &g_root, FILE_LEN, CS, bitmap, 12, false, 8, 500, now) ==
              VNA_ERR_ARG,
          "bitmap too small refused");
    CHECK(aftp_get_start(&FB, &g, &g_root, FILE_LEN, CS, bitmap, 13, false, 0, 500, now) ==
                  VNA_ERR_ARG &&
              aftp_get_start(&FB, &g, &g_root, FILE_LEN, CS, bitmap, 13, false, AFTP_WINDOW + 1,
                             500, now) == VNA_ERR_ARG,
          "window out of range refused");
    CHECK(aftp_get_start(&FB, &g, &g_root, FILE_LEN, CS, bitmap, 13, false, 8, 500, now) == VNA_OK,
          "get started");
    uint8_t req[128];
    vna_id_t to;
    CHECK(aftp_get_next(&g, now, &to, req, sizeof req) == VNA_ERR_STATE,
          "no sources: nothing can be asked");
    CHECK(aftp_get_add_source(&g, &A.id) == VNA_OK && aftp_get_add_source(&g, &C.id) == VNA_OK &&
              aftp_get_add_source(&g, &M.id) == VNA_OK &&
              aftp_get_add_source(&g, &A.id) == VNA_ERR_DUP,
          "three sources, duplicate refused");

    static uint8_t rec[24000];
    uint32_t steps = 0, by[3] = {0, 0, 0}, unsolicited_tried = 0, ok_chunks = 0;
    bool drop_seen = false, late_tried = false;
    while (!aftp_get_done(&g) && steps++ < 2000) {
        int32_t ql = aftp_get_next(&g, now, &to, req, sizeof req);
        CHECK(ql >= 0, "next %d", ql);
        if (ql <= 0) break;
        const vna_identity_t *who = vna_id_eq(&to, &A.id) ? &A : vna_id_eq(&to, &C.id) ? &C : &M;
        by[who == &A ? 0 : who == &C ? 1 : 2]++;
        int32_t rl = serve_as(who, &B.id, req, (uint32_t) ql, now, rec, sizeof rec);
        CHECK(rl > 0, "served %d", rl);
        if (who == &C && steps == 7) { /* C goes silent once: G4 */
            now += 600;
            continue;
        }
        if (!unsolicited_tried && who == &A) {
            /* the same chunk delivered by C, who was not asked for it (G1) */
            CHECK(aftp_get_on_chunk(&g, &C.id, rec, (uint32_t) rl, now, &g_rc) ==
                      VNA_ERR_UNEXPECTED,
                  "a chunk from a source not asked for that index is refused");
            vna_id_t stranger;
            memset(&stranger, 0xEE, 32);
            CHECK(aftp_get_on_chunk(&g, &stranger, rec, (uint32_t) rl, now, &g_rc) ==
                      VNA_ERR_UNEXPECTED,
                  "a chunk from a stranger is refused");
            unsolicited_tried = 1;
        }
        vna_status_t st = aftp_get_on_chunk(&g, &to, rec, (uint32_t) rl, now, &g_rc);
        if (who == &M) {
            CHECK(st == VNA_ERR_MERKLE, "bad chunk fails the proof (%d)", st);
            CHECK(g.src[2].dropped, "the source that sent it is dropped (G3)");
            drop_seen = true;
            CHECK(aftp_get_on_chunk(&g, &M.id, rec, (uint32_t) rl, now, &g_rc) ==
                      VNA_ERR_UNEXPECTED,
                  "a dropped source is no longer heard");
        } else {
            CHECK(st == VNA_OK, "good chunk accepted (%d)", st);
            if (st == VNA_OK) {
                memcpy(g_recv + (uint64_t) g_rc.index * CS, g_rc.data, g_rc.data_len);
                ok_chunks++;
                if (!late_tried) {
                    CHECK(aftp_get_on_chunk(&g, &to, rec, (uint32_t) rl, now, &g_rc) == VNA_ERR_DUP,
                          "the same chunk twice is a duplicate");
                    late_tried = true;
                }
            }
        }
        now += 1;
    }
    CHECK(aftp_get_done(&g), "file complete after %u steps", steps);
    CHECK(memcmp(g_recv, g_file, FILE_LEN) == 0, "received bytes equal the file");
    CHECK(drop_seen && by[2] == 1, "the malicious source was asked once (%u)", by[2]);
    CHECK(g.src[1].timeouts == 1 && !g.src[1].dropped, "the slow source timed out, kept (G4)");
    CHECK(by[0] > 20 && by[1] > 20, "work spread over the honest sources (%u, %u)", by[0], by[1]);
    CHECK(ok_chunks == n, "every chunk counted exactly once (%u)", ok_chunks);
    CHECK(aftp_get_next(&g, now, &to, req, sizeof req) == 0, "done: nothing more to ask");
    CHECK(aftp_get_on_chunk(&g, &A.id, rec, 10, now, &g_rc) == VNA_ERR_STATE, "done: no chunks");

    /* G5: the receiver's log names each source */
    CHECK(count(&SB, AFTP_EV_CHUNK_IN, AP_TAG_T) == n, "one T entry per verified chunk");
    CHECK(count(&SB, AFTP_EV_SOURCE_DROP, AP_TAG_F) == 1, "one source drop logged");
    CHECK(count(&SB, AFTP_EV_TIMEOUT, AP_TAG_N) == 1, "one timeout logged");
    CHECK(count(&SB, AFTP_EV_GET_DONE, AP_TAG_T) == 1, "completion logged");
    CHECK(count(&SB, AFTP_EV_CHUNK_IN, AP_TAG_F) == 3, "three refused deliveries logged");
    /* S1: A's log has one SERVE per chunk it served */
    CHECK(count(&SA, AFTP_EV_SERVE, AP_TAG_T) == by[0] && FA.served == by[0],
          "server logged every chunk it served (%u)", by[0]);
    ap_audit_t au;
    CHECK(replay(&SA, &A.id, &au) == SA.n && au.seq == LA.seq && memcmp(au.head, LA.head, 32) == 0,
          "server's whole log replays");
    CHECK(replay(&SB, &B.id, &au) == SB.n && memcmp(au.head, LB.head, 32) == 0,
          "receiver's whole log replays");

    /* S1: agreement refusal is logged; log refusal blocks the chunk */
    vna_chunk_req_t rq = {VNA_CHUNKREQ_MAGIC, g_root, 0};
    int32_t ql = vna_schema_pack(&vna_chunk_req_schema, &rq, req, sizeof req, true);
    vna_id_t blocked;
    memset(&blocked, 0x77, 32);
    g_ag.n_block = 1;
    g_ag.block[0].id = blocked;
    uint32_t f0 = count(&SA, AFTP_EV_SERVE, AP_TAG_F);
    CHECK(serve_as(&A, &blocked, req, (uint32_t) ql, now, rec, sizeof rec) == VNA_ERR_DENIED &&
              count(&SA, AFTP_EV_SERVE, AP_TAG_F) == f0 + 1,
          "agreement refusal logged (F)");
    g_ag.n_block = 0;
    SA.refuse = true;
    CHECK(serve_as(&A, &B.id, req, (uint32_t) ql, now, rec, sizeof rec) == VNA_ERR_SPACE,
          "log refuses: chunk not sent");
    SA.refuse = false;

    /* REST: resume from a bitmap with half the chunks */
    static aftp_get_t g2;
    uint8_t half[13];
    memset(half, 0, sizeof half);
    for (uint32_t i = 0; i < n; i += 2) half[i >> 3] = (uint8_t) (half[i >> 3] | (1u << (i & 7u)));
    uint8_t junk[13];
    memcpy(junk, half, 13);
    junk[12] |= 0x80; /* bit 103, past chunk 97 */
    CHECK(aftp_get_start(&FB, &g2, &g_root, FILE_LEN, CS, junk, 13, true, 4, 500, now) ==
              VNA_ERR_ARG,
          "resume bitmap with bits past the end refused");
    CHECK(aftp_get_start(&FB, &g2, &g_root, FILE_LEN, CS, half, 13, true, 4, 500, now) == VNA_OK &&
              g2.f.received == 49,
          "resume keeps 49 chunks");
    aftp_get_add_source(&g2, &C.id);
    uint32_t asked = 0;
    while (!aftp_get_done(&g2) && asked < 200) {
        int32_t l = aftp_get_next(&g2, now, &to, req, sizeof req);
        if (l <= 0) break;
        vna_chunk_req_t x;
        vna_schema_unpack(&vna_chunk_req_schema, req, (uint32_t) l, &x, 0);
        CHECK(!(x.index & 1u) == 0, "only missing chunks are asked (%u)", x.index);
        asked++;
        int32_t rl = serve_as(&C, &B.id, req, (uint32_t) l, now, rec, sizeof rec);
        aftp_get_on_chunk(&g2, &to, rec, (uint32_t) rl, now, &g_rc);
    }
    CHECK(aftp_get_done(&g2) && asked == 49, "resumed get asks exactly the 49 missing");

    /* window: never more than `window` in flight */
    static aftp_get_t g3;
    uint8_t bm3[13];
    aftp_get_start(&FB, &g3, &g_root, FILE_LEN, CS, bm3, 13, false, 3, 500, now);
    aftp_get_add_source(&g3, &A.id);
    int32_t sent = 0;
    while (aftp_get_next(&g3, now, &to, req, sizeof req) > 0) sent++;
    CHECK(sent == 3, "window of 3: three requests, then wait");
    CHECK(aftp_get_next(&g3, now + 500, &to, req, sizeof req) > 0 && g3.src[0].timeouts == 3,
          "after the timeout all three are asked again");
    /* a malformed chunk drops its source and every request it held */
    aftp_get_add_source(&g3, &C.id);
    uint8_t junkrec[40];
    memset(junkrec, 0xAB, sizeof junkrec);
    CHECK(aftp_get_on_chunk(&g3, &A.id, junkrec, sizeof junkrec, now, &g_rc) == VNA_ERR_PARSE &&
              g3.src[0].dropped && g3.src[0].inflight == 0,
          "malformed chunk: source dropped (G3)");
    int32_t ql2 = aftp_get_next(&g3, now + 500, &to, req, sizeof req);
    CHECK(ql2 > 0 && vna_id_eq(&to, &C.id), "the remaining source takes over");
    /* log refuses a verified chunk: not counted, still asked */
    {
        int32_t rl = serve_as(&C, &B.id, req, (uint32_t) ql2, now, rec, sizeof rec);
        uint32_t before = g3.f.received;
        SB.refuse = true;
        CHECK(aftp_get_on_chunk(&g3, &C.id, rec, (uint32_t) rl, now, &g_rc) == VNA_ERR_SPACE &&
                  g3.f.received == before,
              "log refuses: chunk not counted");
        SB.refuse = false;
        CHECK(aftp_get_on_chunk(&g3, &C.id, rec, (uint32_t) rl, now, &g_rc) == VNA_OK &&
                  g3.f.received == before + 1,
              "the retry lands");
    }
}

/* ------------------------------------------------------------------ */
static void test_offer(void)
{
    printf("[5] offers (O1, O2): a push is a pull the receiver consents to\n");
    uint64_t now = 3000000;
    aftp_entry_t f;
    entry(&f, "gift/report.txt", 0, FILE_LEN);
    f.root = g_root;
    f.chunk_size = CS;
    static uint8_t ob[12000], ob2[12000];
    uint8_t r[32];
    rnd(r, 32);
    const char *note = "for you\nread me";
    int32_t ol = aftp_offer_make(&FA, &B.id, &f, (const uint8_t *) note, (uint16_t) strlen(note),
                                 42, r, now, ob, sizeof ob);
    CHECK(ol > 0 && count(&SA, AFTP_EV_OFFER_OUT, AP_TAG_N) == 1, "offer signed and logged");
    int32_t slot = 9;
    CHECK(aftp_offer_receive(&FB, ob, (uint32_t) ol, A.pk, now, &slot) == VNA_OK && slot >= 0 &&
              FB.inbox[slot].used && FB.inbox[slot].note_len == strlen(note),
          "offer waits in the inbox");
    CHECK(count(&SB, AFTP_EV_OFFER_IN, AP_TAG_N) == 1, "logged as N (pending)");
    int32_t s2;
    CHECK(aftp_offer_receive(&FB, ob, (uint32_t) ol, A.pk, now + 10, &s2) == VNA_ERR_REPLAY &&
              s2 == -1,
          "the same offer twice: replay refused");
    aftp_t fc;
    ap_log_t lc;
    ap_log_init(&lc, &C.id, 0, 0);
    aftp_init(&fc, &C, &lc);
    CHECK(aftp_offer_receive(&fc, ob, (uint32_t) ol, A.pk, now, &s2) == VNA_ERR_DST,
          "an offer for someone else refused");
    CHECK(aftp_offer_receive(&FB, ob, (uint32_t) ol, M.pk, now, &s2) == VNA_ERR_BINDING,
          "an offer checked under another key refused");
    rnd(r, 32);
    ol = aftp_offer_make(&FA, &B.id, &f, 0, 0, 43, r, now, ob2, sizeof ob2);
    CHECK(aftp_offer_receive(&FB, ob2, (uint32_t) ol, A.pk, now + AFTP_SKEW_MS + 1, &s2) ==
              VNA_ERR_STALE,
          "an old offer refused");
    ob2[ol - 5] ^= 1;
    CHECK(aftp_offer_receive(&FB, ob2, (uint32_t) ol, A.pk, now, &s2) == VNA_ERR_SIG,
          "a changed offer refused");
    CHECK(count(&SB, AFTP_EV_OFFER_IN, AP_TAG_F) == 4, "four refusals logged (F)");
    /* a sender cannot offer a traversal path or a control-laden note */
    aftp_entry_t evil = f;
    evil.name_len = 5;
    memcpy(evil.name, "../.x", 5);
    CHECK(aftp_offer_make(&FA, &B.id, &evil, 0, 0, 44, r, now, ob2, sizeof ob2) == VNA_ERR_PARSE,
          "traversal path refused when offering");
    CHECK(aftp_offer_make(&FA, &B.id, &f, (const uint8_t *) "a\x1b[2J", 5, 44, r, now, ob2,
                          sizeof ob2) == VNA_ERR_PARSE,
          "terminal escape in the note refused");
    /* decline */
    rnd(r, 32);
    ol = aftp_offer_make(&FA, &B.id, &f, 0, 0, 45, r, now, ob2, sizeof ob2);
    int32_t s3;
    CHECK(aftp_offer_receive(&FB, ob2, (uint32_t) ol, A.pk, now, &s3) == VNA_OK &&
              aftp_offer_decline(&FB, (uint32_t) s3, now) == VNA_OK && !FB.inbox[s3].used &&
              count(&SB, AFTP_EV_OFFER_DECLINE, AP_TAG_F) == 1,
          "decline frees the slot and logs F");
    CHECK(aftp_offer_decline(&FB, (uint32_t) s3, now) == VNA_ERR_ARG, "decline twice refused");
    /* accept: a get with the sender as source, nothing fetched until asked */
    static aftp_get_t g;
    uint8_t bm[13];
    CHECK(aftp_offer_accept(&FB, (uint32_t) slot, &g, bm, 13, 4, 500, now) == VNA_OK &&
              g.nsrc == 1 && vna_id_eq(&g.src[0].id, &A.id) && !FB.inbox[slot].used &&
              g.f.received == 0 && count(&SB, AFTP_EV_OFFER_TAKE, AP_TAG_T) == 1,
          "accept starts a get from the sender");
    /* inbox fills: further offers refused (CAP), not dropped silently */
    uint32_t got = 0;
    vna_status_t last = VNA_OK;
    for (uint32_t i = 0; i < AFTP_INBOX + 1; i++) {
        rnd(r, 32);
        ol = aftp_offer_make(&FA, &B.id, &f, 0, 0, 100 + i, r, now, ob2, sizeof ob2);
        last = aftp_offer_receive(&FB, ob2, (uint32_t) ol, A.pk, now, &s3);
        got += last == VNA_OK;
    }
    CHECK(got == AFTP_INBOX && last == VNA_ERR_CAP, "inbox of %u, then refused", AFTP_INBOX);
    ap_audit_t au;
    CHECK(replay(&SB, &B.id, &au) == SB.n && memcmp(au.head, LB.head, 32) == 0,
          "receiver's log still replays end to end");
}

int main(void)
{
    uint8_t seed[32];
    memset(seed, 0x5A, 32);
    vna_drbg_seed(&g_rng, seed, 32);
    make_id(&A, 1);
    make_id(&B, 2);
    make_id(&C, 3);
    make_id(&M, 4);
    ap_log_init(&LA, &A.id, sink, &SA);
    ap_log_init(&LB, &B.id, sink, &SB);
    CHECK(aftp_init(&FA, &A, &LA) == VNA_OK && aftp_init(&FB, &B, &LB) == VNA_OK, "init");
    CHECK(aftp_init(&FA, &A, &LB) == VNA_ERR_ARG, "a node logs only to its own log");
    aftp_init(&FA, &A, &LA);
    test_log();
    test_paths();
    test_dir();
    test_get();
    test_offer();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
