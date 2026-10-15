/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_chg_abi.c — the chiglet's gated system ABI (chg_abi.h A1-A6). */
#include <stdio.h>
#include <string.h>
#include "chg_abi.h"
#include "../robin_debanks/sha256.h"

static int pass, fail;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (c)                                                                                     \
            pass++;                                                                                \
        else {                                                                                     \
            fail++;                                                                                \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, m);                                      \
        }                                                                                          \
    } while (0)

/* a scripted rulebook and kernel */
typedef struct {
    lpres_state_t verdict[CHGA_OPS];
    int rc[CHGA_OPS];
    int runs[CHGA_OPS];
    int rules;
    chga_call_t last;
} kern_t;

static lpres_state_t k_rule(void *ctx, const chga_call_t *c)
{
    kern_t *k = ctx;
    k->rules++;
    return k->verdict[c->op];
}

static int k_run(void *ctx, const chga_call_t *c)
{
    kern_t *k = ctx;
    k->runs[c->op]++;
    k->last = *c;
    return k->rc[c->op];
}

static void kern_reset(kern_t *k)
{
    memset(k, 0, sizeof *k);
    for (uint32_t i = 0; i < CHGA_OPS; i++) k->verdict[i] = LPRES_STATE_TRUE;
}

static const uint32_t ALL = (1u << CHGA_OPS) - 1u;

static chga_call_t call(const char *text)
{
    chga_call_t c;
    memset(&c, 0xee, sizeof c);
    int rc = chga_parse(text, strlen(text), &c);
    CHECK(rc == CHGA_OK, text);
    return c;
}

/* ---- A1 A2 ---- */
static void test_names(void)
{
    static const char *const names[CHGA_OPS] = {"storage_dma_read", "framebuffer_blit",
                                                "cell_reload",      "network_pulse",
                                                "cell_quarantine",  "model_exec"};
    static const int kinds[CHGA_OPS] = {CHGA_K_LOOK,   CHGA_K_LOOK,   CHGA_K_CHANGE,
                                        CHGA_K_CHANGE, CHGA_K_CHANGE, CHGA_K_LOOK};
    static const uint8_t nargs[CHGA_OPS] = {2, 4, 1, 0, 1, 2};
    for (uint32_t i = 0; i < CHGA_OPS; i++) {
        CHECK(strcmp(chga_op_name(i), names[i]) == 0, "A1 name");
        CHECK(chga_op_find(names[i], strlen(names[i])) == (int) i, "A1 find");
        CHECK(chga_op_kind(i) == kinds[i], "A1 kind");
        CHECK(chga_op_nargs(i) == nargs[i], "A1 nargs");
    }
    CHECK(strcmp(chga_op_name(CHGA_OPS), "") == 0 && chga_op_kind(CHGA_OPS) == -1 &&
              chga_op_nargs(CHGA_OPS) == 0,
          "A1 out of range");
    CHECK(chga_op_find("cell_reloa", 10) == -1 && chga_op_find("cell_reloads", 12) == -1 &&
              chga_op_find("CELL_RELOAD", 11) == -1 && chga_op_find(NULL, 3) == -1 &&
              chga_op_find("cell_reload", 0) == -1,
          "A2 exact names only");
    CHECK(chga_op_find("cell_reloadx", 11) == CHGA_CELL_RELOAD, "A2 length bounds the match");

    chga_call_t c = call("storage_dma_read 2048 8");
    CHECK(c.op == CHGA_STORAGE_DMA_READ && c.nargs == 2 && c.arg[0] == 2048 && c.arg[1] == 8,
          "A2 two arguments");
    c = call("framebuffer_blit 0 10 640 480");
    CHECK(c.nargs == 4 && c.arg[0] == 0 && c.arg[1] == 10 && c.arg[2] == 640 && c.arg[3] == 480,
          "A2 four arguments");
    c = call("network_pulse");
    CHECK(c.op == CHGA_NETWORK_PULSE && c.nargs == 0 && c.arg[0] == 0, "A2 no arguments");
    c = call("storage_dma_read 18446744073709551615 1");
    CHECK(c.arg[0] == UINT64_MAX, "A2 the largest value");

    static const char *const bad[] = {
        "",
        "storage_dma_read",
        "storage_dma_read 1",
        "storage_dma_read 1 2 3",
        "storage_dma_read 1  2",
        "storage_dma_read 1 2 ",
        " storage_dma_read 1 2",
        "storage_dma_read 1 x",
        "storage_dma_read -1 2",
        "storage_dma_read 18446744073709551616 1",
        "storage_dma_read 99999999999999999999 1",
        "network_pulse 1",
        "network_pulse ",
        "cell_reload 1 2 3 4 5",
        "format_disk 1",
    };
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        chga_call_t d = {0};
        d.arg[0] = 77;
        CHECK(chga_parse(bad[i], strlen(bad[i]), &d) == CHGA_E_ARG && d.arg[0] == 77,
              "A2 refused, call untouched");
    }
    CHECK(chga_parse(NULL, 3, &c) == CHGA_E_ARG &&
              chga_parse("network_pulse", 13, NULL) == CHGA_E_ARG,
          "A2 null");
    CHECK(chga_parse("cell_reload 12", 13, &c) == CHGA_OK && c.arg[0] == 1,
          "A2 the length bounds the text");
}

/* ---- A3 ---- */
static void test_gate(void)
{
    chga_session_t s;
    kern_t k;
    chga_result_t r;
    kern_reset(&k);
    CHECK(chga_init(&s, ALL, k_rule, &k, k_run, &k) == CHGA_OK, "init");

    /* LOOK, granted, rule TRUE: runs */
    chga_call_t c = call("storage_dma_read 100 4");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN &&
              r.verdict == LPRES_STATE_TRUE && r.rc == 0 && r.seq == 1,
          "A3 a granted LOOK call runs");
    CHECK(k.runs[CHGA_STORAGE_DMA_READ] == 1 && k.last.arg[0] == 100 && k.last.arg[1] == 4,
          "A3 the kernel got the call");

    /* CHANGE from the chiglet: held even when everything says TRUE */
    c = call("cell_reload 3");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_HELD_CALL &&
              r.verdict == LPRES_STATE_TRUE && k.runs[CHGA_CELL_RELOAD] == 0,
          "A3 a CHANGE from the chiglet is held");
    /* the same CHANGE typed by the person runs */
    CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == CHGA_RAN &&
              k.runs[CHGA_CELL_RELOAD] == 1,
          "A3 a CHANGE the person typed runs");

    /* not granted: refused for the chiglet, still the person's own */
    chga_set_grants(&s, ALL & ~(1u << CHGA_STORAGE_DMA_READ));
    c = call("storage_dma_read 1 1");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_REFUSED &&
              r.verdict == LPRES_STATE_FALSE && k.runs[CHGA_STORAGE_DMA_READ] == 1,
          "A3 an ungranted op is refused");
    CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == CHGA_RAN,
          "A3 grants bind the chiglet, not the person");
    chga_set_grants(&s, ALL);
    chga_set_grants(NULL, 0); /* no crash */

    /* each bit is its own op */
    chga_set_grants(&s, 1u << CHGA_FRAMEBUFFER_BLIT);
    c = call("framebuffer_blit 0 0 1 1");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN,
          "A3 the op's own bit");
    c = call("storage_dma_read 1 1");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_REFUSED,
          "A3 another op's bit does not grant");
    chga_set_grants(&s, ALL);

    /* bounds, low and high, on every argument */
    static const struct {
        const char *text;
        uint8_t outcome;
    } b[] = {
        {"storage_dma_read 0 1", CHGA_RAN},
        {"storage_dma_read 0 8", CHGA_RAN},
        {"storage_dma_read 0 0", CHGA_REFUSED},
        {"storage_dma_read 0 9", CHGA_REFUSED},
        {"framebuffer_blit 16383 16383 16384 16384", CHGA_RAN},
        {"framebuffer_blit 16384 0 1 1", CHGA_REFUSED},
        {"framebuffer_blit 0 16384 1 1", CHGA_REFUSED},
        {"framebuffer_blit 0 0 0 1", CHGA_REFUSED},
        {"framebuffer_blit 0 0 1 0", CHGA_REFUSED},
        {"framebuffer_blit 0 0 16385 1", CHGA_REFUSED},
        {"framebuffer_blit 0 0 1 16385", CHGA_REFUSED},
        {"cell_reload 63", CHGA_RAN},
        {"cell_reload 64", CHGA_REFUSED},
        {"cell_quarantine 0", CHGA_RAN},
        {"cell_quarantine 64", CHGA_REFUSED},
    };
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) {
        c = call(b[i].text);
        CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == b[i].outcome,
              b[i].text);
    }

    /* the rulebook */
    kern_reset(&k);
    k.verdict[CHGA_STORAGE_DMA_READ] = LPRES_STATE_FALSE;
    c = call("storage_dma_read 1 1");
    CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == CHGA_REFUSED &&
              k.runs[CHGA_STORAGE_DMA_READ] == 0 && k.rules == 1,
          "A3 a rule that says FALSE refuses even the person");
    k.verdict[CHGA_STORAGE_DMA_READ] = LPRES_STATE_NEITHER;
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_HELD_CALL &&
              r.verdict == LPRES_STATE_NEITHER,
          "A3 no rule speaks: held as not sure");
    CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == CHGA_HELD_CALL,
          "A3 not sure holds the person's call too");
    k.verdict[CHGA_STORAGE_DMA_READ] = LPRES_STATE_BOTH;
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_HELD_CALL &&
              r.verdict == LPRES_STATE_BOTH,
          "A3 a dispute is held and shown");
    k.verdict[CHGA_STORAGE_DMA_READ] = (lpres_state_t) 9;
    CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == CHGA_REFUSED,
          "A3 a broken rulebook never authorises");
    k.verdict[CHGA_STORAGE_DMA_READ] = LPRES_STATE_NEITHER;
    c = call("storage_dma_read 0 9");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_REFUSED,
          "A3 out of bounds refuses whatever the rule says");

    /* A6: a failing handler is recorded, the session goes on */
    kern_reset(&k);
    k.rc[CHGA_FRAMEBUFFER_BLIT] = -7;
    c = call("framebuffer_blit 0 0 1 1");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_FAILED &&
              r.rc == -7,
          "A6 a failed handler is recorded");
    k.rc[CHGA_FRAMEBUFFER_BLIT] = 0;
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN,
          "A6 the next call still runs");

    /* malformed */
    chga_call_t m = call("cell_reload 1");
    m.nargs = 2;
    CHECK(chga_submit(&s, &m, CHGA_FROM_PERSON, &r) == CHGA_E_ARG, "wrong argument count");
    m = call("cell_reload 1");
    m.op = CHGA_OPS;
    CHECK(chga_submit(&s, &m, CHGA_FROM_PERSON, &r) == CHGA_E_ARG, "unknown op");
    m = call("cell_reload 1");
    CHECK(chga_submit(&s, &m, 2, &r) == CHGA_E_ARG, "unknown source");
    CHECK(chga_submit(NULL, &m, CHGA_FROM_PERSON, &r) == CHGA_E_ARG &&
              chga_submit(&s, NULL, CHGA_FROM_PERSON, &r) == CHGA_E_ARG,
          "null");
    uint64_t before = s.seq;
    CHECK(chga_submit(&s, &m, CHGA_FROM_PERSON, NULL) == CHGA_OK && s.seq == before + 1,
          "a NULL result is allowed");
    CHECK(chga_init(&s, 0, NULL, &k, k_run, &k) == CHGA_E_ARG &&
              chga_init(&s, 0, k_rule, &k, NULL, &k) == CHGA_E_ARG &&
              chga_init(NULL, 0, k_rule, &k, k_run, &k) == CHGA_E_ARG,
          "init needs a rulebook and a kernel");
}

/* ---- A4 ---- */
static void test_hold(void)
{
    chga_session_t s;
    kern_t k;
    chga_result_t r, a;
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);

    chga_call_t c = call("cell_quarantine 5");
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(r.outcome == CHGA_HELD_CALL && r.seq == 1, "A4 held");
    const chga_held_t *h = chga_held(&s, r.seq);
    CHECK(h && h->seq == 1 && h->call.op == CHGA_CELL_QUARANTINE && h->call.arg[0] == 5 &&
              memcmp(h->digest, r.digest, CHGA_DIGEST) == 0,
          "A4 the held call and its digest");
    /* the digest is SHA-256 of (seq LE, op, nargs, args LE) */
    uint8_t buf[18] = {1, 0, 0, 0, 0, 0, 0, 0, CHGA_CELL_QUARANTINE, 1, 5, 0, 0, 0, 0, 0, 0, 0};
    uint8_t want[32];
    sha256(buf, sizeof buf, want);
    CHECK(memcmp(r.digest, want, 32) == 0, "A4 digest layout");
    CHECK(chga_held(&s, 2) == NULL && chga_held(NULL, 1) == NULL, "A4 no such held call");

    uint8_t wrong[CHGA_DIGEST];
    memcpy(wrong, r.digest, CHGA_DIGEST);
    wrong[7] ^= 1u;
    CHECK(chga_accept(&s, 1, wrong, 8, &a) == CHGA_E_DIGEST && k.runs[CHGA_CELL_QUARANTINE] == 0,
          "A4 a wrong digest runs nothing");
    memcpy(wrong, r.digest, CHGA_DIGEST);
    wrong[0] ^= 0x80u;
    CHECK(chga_accept(&s, 1, wrong, 32, &a) == CHGA_E_DIGEST, "A4 every byte is compared");
    CHECK(chga_accept(&s, 1, r.digest, 7, &a) == CHGA_E_ARG, "A4 too short a digest");
    CHECK(chga_accept(&s, 1, r.digest, 33, &a) == CHGA_E_ARG, "A4 too long a digest");
    CHECK(chga_accept(&s, 9, r.digest, 8, &a) == CHGA_E_NOHELD, "A4 unknown seq");
    CHECK(chga_accept(NULL, 1, r.digest, 8, &a) == CHGA_E_ARG &&
              chga_accept(&s, 1, NULL, 8, &a) == CHGA_E_ARG,
          "A4 null");
    CHECK(chga_accept(&s, 1, r.digest, 8, &a) == CHGA_OK && a.outcome == CHGA_RAN && a.seq == 1 &&
              k.runs[CHGA_CELL_QUARANTINE] == 1 && k.last.arg[0] == 5,
          "A4 the exact digest runs it");
    CHECK(chga_held(&s, 1) == NULL && chga_accept(&s, 1, r.digest, 8, &a) == CHGA_E_NOHELD,
          "A4 it runs once");

    /* accept answers "not sure" and a dispute */
    k.verdict[CHGA_NETWORK_PULSE] = LPRES_STATE_BOTH;
    c = call("network_pulse");
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(chga_accept(&s, r.seq, r.digest, 32, &a) == CHGA_OK && a.outcome == CHGA_RAN &&
              a.verdict == LPRES_STATE_BOTH,
          "A4 the person settles a dispute");
    /* a failing handler after accept */
    k.verdict[CHGA_NETWORK_PULSE] = LPRES_STATE_TRUE;
    k.rc[CHGA_NETWORK_PULSE] = 3;
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(chga_accept(&s, r.seq, r.digest, 8, &a) == CHGA_OK && a.outcome == CHGA_FAILED &&
              a.rc == 3,
          "A6 an accepted call that fails is recorded");
    k.rc[CHGA_NETWORK_PULSE] = 0;

    /* the gate is checked again at accept */
    c = call("cell_reload 1");
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    chga_set_grants(&s, ALL & ~(1u << CHGA_CELL_RELOAD));
    CHECK(chga_accept(&s, r.seq, r.digest, 8, &a) == CHGA_OK && a.outcome == CHGA_REFUSED &&
              k.runs[CHGA_CELL_RELOAD] == 0 && chga_held(&s, r.seq) == NULL,
          "A4 a grant withdrawn since refuses it");
    chga_set_grants(&s, ALL);
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    k.verdict[CHGA_CELL_RELOAD] = LPRES_STATE_FALSE;
    CHECK(chga_accept(&s, r.seq, r.digest, 8, &a) == CHGA_OK && a.outcome == CHGA_REFUSED,
          "A4 a rule that now says FALSE refuses it");
    k.verdict[CHGA_CELL_RELOAD] = LPRES_STATE_TRUE;

    /* refuse */
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(chga_refuse(&s, r.seq + 1) == CHGA_E_NOHELD && chga_refuse(NULL, r.seq) == CHGA_E_ARG,
          "A4 refuse needs a held call");
    CHECK(chga_refuse(&s, r.seq) == CHGA_OK && chga_held(&s, r.seq) == NULL &&
              chga_accept(&s, r.seq, r.digest, 8, &a) == CHGA_E_NOHELD,
          "A4 refusing drops it");

    /* A4 bound: CHGA_HELD slots, then backpressure; a freed slot is reused */
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    chga_result_t first;
    int rc = CHGA_OK;
    for (uint32_t i = 0; i < CHGA_HELD && rc == CHGA_OK; i++) {
        rc = chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
        if (i == 0) first = r;
    }
    CHECK(rc == CHGA_OK && r.outcome == CHGA_HELD_CALL, "A4 fills the held slots");
    uint64_t seq = s.seq;
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_E_FULL && s.seq == seq,
          "A5 held table full: nothing written");
    chga_call_t look = call("storage_dma_read 1 1");
    CHECK(chga_submit(&s, &look, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN,
          "A5 a call that runs needs no held slot");
    CHECK(chga_refuse(&s, first.seq) == CHGA_OK &&
              chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_HELD_CALL,
          "A4 a freed slot is reused");
}

/* ---- A5 ---- */
static void test_journal(void)
{
    chga_session_t s;
    kern_t k;
    chga_result_t r;
    chga_rec_t rec;
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    CHECK(!chga_pop(&s, &rec) && !chga_pop(NULL, &rec) && !chga_pop(&s, NULL), "A5 empty");

    chga_call_t look = call("storage_dma_read 7 2");
    chga_call_t change = call("cell_reload 2");
    k.rc[CHGA_STORAGE_DMA_READ] = 0;
    chga_submit(&s, &look, CHGA_FROM_CHIGLET, &r);
    chga_submit(&s, &change, CHGA_FROM_CHIGLET, &r);
    chga_result_t held = r;
    chga_set_grants(&s, 0);
    chga_submit(&s, &look, CHGA_FROM_CHIGLET, &r);
    chga_set_grants(&s, ALL);
    chga_accept(&s, held.seq, held.digest, 8, &r);
    chga_submit(&s, &change, CHGA_FROM_CHIGLET, &r);
    chga_refuse(&s, r.seq);
    k.rc[CHGA_STORAGE_DMA_READ] = 5;
    chga_submit(&s, &look, CHGA_FROM_PERSON, &r);

    static const struct {
        uint64_t seq;
        uint8_t op, from, outcome;
        lpres_state_t v;
        int32_t rc;
    } want[] = {
        {1, CHGA_STORAGE_DMA_READ, CHGA_FROM_CHIGLET, CHGA_RAN, LPRES_STATE_TRUE, 0},
        {2, CHGA_CELL_RELOAD, CHGA_FROM_CHIGLET, CHGA_HELD_CALL, LPRES_STATE_TRUE, 0},
        {3, CHGA_STORAGE_DMA_READ, CHGA_FROM_CHIGLET, CHGA_REFUSED, LPRES_STATE_FALSE, 0},
        {2, CHGA_CELL_RELOAD, CHGA_FROM_PERSON, CHGA_RAN, LPRES_STATE_TRUE, 0},
        {4, CHGA_CELL_RELOAD, CHGA_FROM_CHIGLET, CHGA_HELD_CALL, LPRES_STATE_TRUE, 0},
        {4, CHGA_CELL_RELOAD, CHGA_FROM_PERSON, CHGA_DROPPED, LPRES_STATE_FALSE, 0},
        {5, CHGA_STORAGE_DMA_READ, CHGA_FROM_PERSON, CHGA_FAILED, LPRES_STATE_TRUE, 5},
    };
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        memset(&rec, 0xcc, sizeof rec);
        bool got = chga_pop(&s, &rec);
        CHECK(got && rec.seq == want[i].seq && rec.op == want[i].op && rec.from == want[i].from &&
                  rec.outcome == want[i].outcome && rec.verdict == want[i].v &&
                  rec.rc == want[i].rc,
              "A5 journal record, in order");
    }
    CHECK(!chga_pop(&s, &rec), "A5 drained");

    /* full journal: backpressure on every path, nothing runs or changes */
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    chga_submit(&s, &change, CHGA_FROM_CHIGLET, &held);
    chga_submit(&s, &change, CHGA_FROM_CHIGLET, &r);
    chga_result_t held2 = r;
    int rc = CHGA_OK;
    while (rc == CHGA_OK && s.head - s.tail < CHGA_JOURNAL)
        rc = chga_submit(&s, &look, CHGA_FROM_CHIGLET, &r);
    CHECK(rc == CHGA_OK && s.head - s.tail == CHGA_JOURNAL, "A5 the journal fills");
    int runs = k.runs[CHGA_STORAGE_DMA_READ];
    uint64_t seq = s.seq;
    CHECK(chga_submit(&s, &look, CHGA_FROM_CHIGLET, &r) == CHGA_E_FULL &&
              k.runs[CHGA_STORAGE_DMA_READ] == runs && s.seq == seq,
          "A5 full: a call is not run");
    chga_set_grants(&s, 0);
    CHECK(chga_submit(&s, &look, CHGA_FROM_CHIGLET, &r) == CHGA_E_FULL && s.seq == seq,
          "A5 full: not even a refusal is written");
    chga_set_grants(&s, ALL);
    CHECK(chga_accept(&s, held.seq, held.digest, 8, &r) == CHGA_E_FULL &&
              chga_held(&s, held.seq) != NULL && k.runs[CHGA_CELL_RELOAD] == 0,
          "A5 full: an accept waits");
    CHECK(chga_refuse(&s, held2.seq) == CHGA_E_FULL && chga_held(&s, held2.seq) != NULL,
          "A5 full: a refuse waits");
    CHECK(chga_pop(&s, &rec) && rec.seq == held.seq, "A5 draining one record frees one slot");
    CHECK(chga_accept(&s, held.seq, held.digest, 8, &r) == CHGA_OK && r.outcome == CHGA_RAN,
          "A5 then the accept goes through");
    /* the ring wraps: drain all, refill past the end */
    while (chga_pop(&s, &rec)) {
    }
    for (uint32_t i = 0; i < CHGA_JOURNAL + 3; i++) {
        chga_submit(&s, &look, CHGA_FROM_PERSON, &r);
        CHECK(chga_pop(&s, &rec) && rec.seq == r.seq, "A5 wraps around");
    }
}

/* ---- A7 ---- */
static void test_model_exec(void)
{
    chga_session_t s;
    kern_t k;
    chga_result_t r;
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    /* domain 0x60, place 600, item 4, base, format 0, language 0 */
    const uint64_t coord = 0x6002580400000000ull;
    chga_call_t c = {CHGA_MODEL_EXEC, 2, {coord, 9, 0, 0}};
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN &&
              k.runs[CHGA_MODEL_EXEC] == 1,
          "A7 a LOOK call: the chiglet runs a model without holding");
    chga_call_t t;
    CHECK(chga_parse("model_exec 6918188751797616640 9", 32, &t) == CHGA_OK &&
              t.op == CHGA_MODEL_EXEC && t.arg[0] == coord && t.arg[1] == 9,
          "A7 typed as text");
    /* the domain byte bounds the coordinate: 0x5f.. and 0x61.. refuse */
    static const uint64_t outside[] = {0x5FFFFFFFFFFFFFFFull, 0x6100000000000000ull, 0, UINT64_MAX,
                                       0x1002000100000001ull};
    for (size_t i = 0; i < sizeof outside / sizeof outside[0]; i++) {
        chga_call_t o = {CHGA_MODEL_EXEC, 2, {outside[i], 9, 0, 0}};
        CHECK(chga_submit(&s, &o, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == CHGA_REFUSED,
              "A7 only domain 0x60 is a model coordinate");
    }
    static const uint64_t edges[] = {0x6000000000000000ull, 0x60FFFFFFFFFFFFFFull};
    for (size_t i = 0; i < 2; i++) {
        chga_call_t o = {CHGA_MODEL_EXEC, 2, {edges[i], 1, 0, 0}};
        CHECK(chga_submit(&s, &o, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN,
              "A7 the whole domain");
    }
    chga_call_t z = {CHGA_MODEL_EXEC, 2, {coord, 0, 0, 0}};
    CHECK(chga_submit(&s, &z, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_REFUSED,
          "A7 event 0 is no event");
    z.arg[1] = UINT64_MAX;
    CHECK(chga_submit(&s, &z, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_RAN,
          "A7 any later event");
    /* a model with no cell: the handler refuses, the session goes on (A6) */
    k.rc[CHGA_MODEL_EXEC] = 2;
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_FAILED &&
              r.rc == 2,
          "A7 no model at that coordinate is a failure, not a guess");
    chga_set_grants(&s, ALL & ~(1u << CHGA_MODEL_EXEC));
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_OK && r.outcome == CHGA_REFUSED,
          "A7 a grant binds it like any call");
}

/* ---- A8 ---- */
static void head_of(const uint8_t prev[32], const chga_rec_t *rec, uint8_t out[32])
{
    /* the chain written out independently of chg_abi.c */
    uint8_t b[CHGA_REC_BYTES];
    size_t n = 0;
    for (int i = 0; i < 8; i++) b[n++] = (uint8_t) (rec->seq >> (8 * i));
    b[n++] = rec->op;
    b[n++] = rec->from;
    b[n++] = rec->outcome;
    b[n++] = (uint8_t) rec->verdict;
    for (int i = 0; i < 4; i++) b[n++] = (uint8_t) ((uint32_t) rec->rc >> (8 * i));
    b[n++] = rec->nargs;
    for (uint32_t a = 0; a < CHGA_ARGS; a++)
        for (int i = 0; i < 8; i++)
            b[n++] = (uint8_t) ((a < rec->nargs ? rec->arg[a] : 0) >> (8 * i));
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) "zxv/chga/event/v1", 17);
    sha256_update(&c, prev, 32);
    sha256_update(&c, b, n);
    sha256_final(&c, out);
}

static void test_chain(void)
{
    CHECK(CHGA_REC_BYTES == 49, "A8 record size");
    {
        /* the byte layout itself, with every byte of seq and the arguments distinct */
        chga_rec_t v = {.seq = 0x0807060504030201ull,
                        .op = 5,
                        .from = 1,
                        .outcome = 2,
                        .verdict = LPRES_STATE_BOTH,
                        .rc = -2,
                        .nargs = 2,
                        .arg = {0x1112131415161718ull, 0x2122232425262728ull, 9, 9}};
        static const uint8_t want[CHGA_REC_BYTES] = {
            1,    2,    3,    4,    5,    6,    7,    8,    5,    1,    2,    3,
            0xfe, 0xff, 0xff, 0xff, 2,    0x18, 0x17, 0x16, 0x15, 0x14, 0x13, 0x12,
            0x11, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0};
        uint8_t got[CHGA_REC_BYTES];
        chga_rec_encode(&v, got);
        CHECK(memcmp(got, want, sizeof want) == 0, "A8 the record's bytes, little-endian");
    }
    chga_session_t s;
    kern_t k;
    chga_result_t r, held;
    chga_rec_t rec;
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    uint8_t zero[32] = {0};
    CHECK(memcmp(chga_chain_head(&s), zero, 32) == 0 && chga_chain_head(NULL) == NULL,
          "A8 the chain starts at zero");
    chga_call_t read = call("storage_dma_read 7 2"), blit = call("framebuffer_blit 1 2 3 4");
    chga_call_t reload = call("cell_reload 63");
    k.rc[CHGA_FRAMEBUFFER_BLIT] = -7;
    chga_submit(&s, &read, CHGA_FROM_CHIGLET, &r);
    chga_submit(&s, &blit, CHGA_FROM_PERSON, &r);
    chga_submit(&s, &reload, CHGA_FROM_CHIGLET, &held);
    chga_accept(&s, held.seq, held.digest, 32, &r);
    chga_submit(&s, &reload, CHGA_FROM_CHIGLET, &held);
    chga_refuse(&s, held.seq);
    uint8_t h[32] = {0};
    chga_rec_t recs[6];
    int n = 0;
    while (chga_pop(&s, &rec)) {
        uint8_t want[32];
        head_of(h, &rec, want);
        CHECK(memcmp(rec.head, want, 32) == 0, "A8 each record carries the head after it");
        memcpy(h, want, 32);
        if (n < 6) recs[n] = rec;
        n++;
    }
    CHECK(n == 6 && memcmp(h, chga_chain_head(&s), 32) == 0, "A8 the session head is the last");
    CHECK(recs[1].nargs == 4 && recs[1].arg[3] == 4 && recs[1].rc == -7 && recs[0].arg[0] == 7 &&
              recs[0].arg[1] == 2 && recs[0].arg[2] == 0,
          "A8 the record keeps the call and the handler's code");
    /* chga_chain_next is the same fold, and every field counts */
    uint8_t a[32], b[32];
    chga_chain_next(zero, &recs[0], a);
    CHECK(memcmp(a, recs[0].head, 32) == 0, "A8 chga_chain_next");
    chga_rec_t m = recs[1];
    uint8_t enc0[CHGA_REC_BYTES], enc1[CHGA_REC_BYTES];
    chga_rec_encode(&m, enc0);
    for (int f = 0; f < 11; f++) {
        m = recs[1];
        switch (f) {
        case 0:
            m.seq ^= 1ull << 63;
            break;
        case 1:
            m.op ^= 1;
            break;
        case 2:
            m.from ^= 1;
            break;
        case 3:
            m.outcome ^= 1;
            break;
        case 4:
            m.verdict = LPRES_STATE_BOTH;
            break;
        case 5:
            m.rc ^= INT32_MIN;
            break;
        case 6:
            m.nargs = 3;
            break;
        case 7:
            m.arg[0] ^= 1ull << 63;
            break;
        case 8:
            m.arg[1] ^= 1;
            break;
        case 9:
            m.arg[2] ^= 1ull << 56;
            break;
        default:
            m.arg[3] ^= 1ull << 40;
            break;
        }
        chga_rec_encode(&m, enc1);
        chga_chain_next(recs[0].head, &m, b);
        CHECK(memcmp(enc0, enc1, sizeof enc0) != 0 && memcmp(b, recs[1].head, 32) != 0,
              "A8 every field is in the record");
    }
    m = recs[1];
    m.arg[3] = 99;
    m.nargs = 3; /* an argument past nargs is not part of the call */
    chga_rec_t m2 = m;
    m2.arg[3] = 0;
    chga_rec_encode(&m, enc0);
    chga_rec_encode(&m2, enc1);
    CHECK(memcmp(enc0, enc1, sizeof enc0) == 0, "A8 unused arguments encode as 0");
    chga_chain_next(zero, &recs[1], b);
    CHECK(memcmp(b, recs[1].head, 32) != 0, "A8 the previous head counts");
    /* init restarts the chain */
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    CHECK(memcmp(chga_chain_head(&s), zero, 32) == 0, "A8 init restarts the chain");
}

/* edges the mutation run found untested */
static void test_edges(void)
{
    chga_session_t s;
    kern_t k;
    chga_result_t r, a;
    chga_rec_t rec;
    kern_reset(&k);
    chga_init(&s, ALL, k_rule, &k, k_run, &k);

    CHECK(chga_op_find("xell_reload", 11) == -1, "A2 the first letter counts");
    static const struct {
        const char *text;
        uint8_t outcome;
    } b[] = {
        {"cell_reload 0", CHGA_RAN},
        {"cell_quarantine 63", CHGA_RAN},
    };
    for (size_t i = 0; i < sizeof b / sizeof b[0]; i++) {
        chga_call_t c = call(b[i].text);
        CHECK(chga_submit(&s, &c, CHGA_FROM_PERSON, &r) == CHGA_OK && r.outcome == b[i].outcome,
              b[i].text);
    }

    /* the digest covers every byte of seq and of every argument */
    k.verdict[CHGA_STORAGE_DMA_READ] = LPRES_STATE_NEITHER;
    s.seq = 0x1122334455667787ull;
    chga_call_t c = call("storage_dma_read 72623859790382856 3");
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, NULL) == CHGA_OK, "A4 held with no result");
    const chga_held_t *h = chga_held(&s, 0x1122334455667788ull);
    uint8_t buf[26] = {0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, CHGA_STORAGE_DMA_READ,
                       2,    0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
                       3,    0,    0,    0,    0,    0,    0,    0};
    uint8_t want[32];
    sha256(buf, sizeof buf, want);
    CHECK(h && memcmp(h->digest, want, 32) == 0, "A4 digest over the whole seq and both args");

    /* results carry rc 0 when nothing ran */
    memset(&r, 0x7f, sizeof r);
    c = call("cell_reload 99");
    chga_submit(&s, &c, CHGA_FROM_PERSON, &r);
    CHECK(r.outcome == CHGA_REFUSED && r.rc == 0, "A3 a refusal has rc 0");
    memset(&r, 0x7f, sizeof r);
    c = call("cell_reload 9");
    chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(r.outcome == CHGA_HELD_CALL && r.rc == 0, "A4 a held call has rc 0");
    k.verdict[CHGA_CELL_RELOAD] = LPRES_STATE_FALSE;
    memset(&a, 0x7f, sizeof a);
    CHECK(chga_accept(&s, r.seq, r.digest, 8, &a) == CHGA_OK && a.outcome == CHGA_REFUSED &&
              a.rc == 0,
          "A4 refused at accept has rc 0");
    rec.rc = 99;
    while (chga_pop(&s, &rec)) {
    }
    CHECK(rec.seq == r.seq && rec.outcome == CHGA_REFUSED && rec.rc == 0 &&
              rec.from == CHGA_FROM_PERSON,
          "A5 the refused accept is journaled with rc 0");

    /* init resets a used session */
    k.verdict[CHGA_CELL_RELOAD] = LPRES_STATE_TRUE;
    for (uint32_t i = 0; i < CHGA_HELD; i++) chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r) == CHGA_E_FULL, "held full before init");
    chga_init(&s, ALL, k_rule, &k, k_run, &k);
    CHECK(s.seq == 0 && s.head == s.tail && !chga_pop(&s, &rec), "init empties the journal");
    int rc = CHGA_OK;
    for (uint32_t i = 0; i < CHGA_HELD && rc == CHGA_OK; i++)
        rc = chga_submit(&s, &c, CHGA_FROM_CHIGLET, &r);
    CHECK(rc == CHGA_OK && r.outcome == CHGA_HELD_CALL && r.seq == CHGA_HELD,
          "init frees every held slot");
}

int main(void)
{
    test_names();
    test_gate();
    test_hold();
    test_journal();
    test_model_exec();
    test_chain();
    test_edges();
    printf("%d/%d checks passed\n", pass, pass + fail);
    return fail ? 1 : 0;
}
