/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Moraine's languages, witnessed, row by row against moraine_rules.h, the
 * C the model runs; the parts it composes (heirloom, gatehouse, lockbox,
 * witness, warding) resolved and called; and the BOTH of guardian 2
 * approving while guardian 4 objects within the waiting period. Slots:
 * @logic.moraine_key_release_now, @logic.moraine_escrow_custody_ok,
 * @num.moraine_escrow_key_strength, @logic.moraine_guardian_node_ok;
 * and, used, @logic.heirloom_heir_release_due, @logic.gatehouse_use_ok,
 * @num.lockbox_days_left, @logic.lockbox_secret_ok, @logic.witness_chain_ok,
 * @logic.witness_audit_ok, @num.warding_key_entropy_bits,
 * @num.warding_access_grid_cell. */
#include "moraine_rules.h"

APP_TEST(moraine, 45, 210)
{
    /* the shared row generator is put back as it was at the end, so the apps
     * tested after Moraine draw the rows they drew before it was built */
    const uint64_t seed_was = WSEED;
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[8][2] = {
        {"heirloom.heir_release_due", "heirloom"}, {"gatehouse.use_ok", "gatehouse"},
        {"lockbox.days_left", "lockbox"},          {"lockbox.secret_ok", "lockbox"},
        {"witness.chain_ok", "witness"},           {"witness.audit_ok", "witness"},
        {"warding.key_entropy_bits", "warding"},   {"warding.access_grid_cell", "warding"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 6 && app->n == 45 &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Moraine: composed of 5 parts, 6 own lines and %u of theirs (%u)", app->n - 6u, app->n);
    for (uint32_t i = 0; i < 8u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Moraine: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) >= 6 && !org[0].from, "Moraine: itself and its parts");

    /* MR1: key_release_now, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 2);
        b[i] = i % 2u ? wrand(-10, 10) : wrand(-1000, MORAINE_STAMP_MAX);
        c[i] = wrand(0, 2);
        if (i == 0) a[i] = 1, b[i] = 1, c[i] = 1;                 /* the least window */
        if (i == 1) a[i] = 1, b[i] = MORAINE_STAMP_MAX, c[i] = 1; /* the longest */
        if (i == 2) a[i] = 1, b[i] = 0, c[i] = 1;                 /* the window is over */
        if (i == 3) a[i] = 0, b[i] = 5, c[i] = 1;                 /* not due */
        if (i == 4) a[i] = 1, b[i] = 5, c[i] = 0;                 /* not allowed */
        want[i] = T_(moraine_release_now(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {{"due", a, WAVE_ROWS}, {"left", b, WAVE_ROWS}, {"allowed", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "key_release_now", ra, 3, want, WAVE_ROWS, true),
          "Moraine: key_release_now, Eve = KRON = C");
    {
        static const int64_t du[2] = {1, 1}, le[2] = {5, MORAINE_STAMP_MAX + 1}, al[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"due", du, 2}, {"left", le, 2}, {"allowed", al, 2}};
        CHECK(!moraine_release_now(1, MORAINE_STAMP_MAX + 1, 1) &&
                  wave_lp(app, "key_release_now", xa, 3) == ZXAPP_BOTH,
              "Moraine: a window past year 9999: Eve refuses, KRON reads it open: BOTH");
    }

    /* MR2: escrow_custody_ok, Sutra = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        b[i] = wrand(0, MORAINE_NUMBER_MAX - 1);
        a[i] = i % 2u ? b[i] + 1 : b[i] + wrand(0, 1);
        c[i] = wrand(0, 9);
        e[i] = wrand(0, MORAINE_STAMP_MAX);
        d[i] = i % 3u ? e[i] + wrand(0, 1000) : wrand(0, MORAINE_STAMP_MAX);
        if (i == 0) a[i] = 1, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0; /* the first */
        if (i == 1)
            a[i] = MORAINE_NUMBER_MAX, b[i] = MORAINE_NUMBER_MAX - 1, c[i] = 4,
            d[i] = MORAINE_TIME_MAX, e[i] = MORAINE_TIME_MAX;            /* the last */
        if (i == 2) a[i] = 5, b[i] = 5, c[i] = 1, d[i] = 9, e[i] = 0;    /* reused */
        if (i == 3) a[i] = 7, b[i] = 5, c[i] = 1, d[i] = 9, e[i] = 0;    /* a gap */
        if (i == 4) a[i] = 6, b[i] = 5, c[i] = 5, d[i] = 9, e[i] = 0;    /* no such guardian */
        if (i == 5) a[i] = 6, b[i] = 5, c[i] = 1, d[i] = 99, e[i] = 100; /* back in time */
        want[i] = T_(moraine_custody_ok(a[i], b[i], c[i], d[i], e[i]) == 1);
    }
    zxapp_arg_t ca[5] = {{"number", a, WAVE_ROWS},
                         {"last", b, WAVE_ROWS},
                         {"guardian", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS},
                         {"prior", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              wave_same(app, "escrow_custody_ok", ca, 5, want, WAVE_ROWS, true),
          "Moraine: escrow_custody_ok, Sutra = C");
    {
        /* outside Sutra's pictures: refused, never wrapped */
        static const int64_t bad[4][5] = {{-1, 0, 0, 0, 0},
                                          {MORAINE_NUMBER_MAX + 1, MORAINE_NUMBER_MAX, 0, 0, 0},
                                          {1, 0, 10, 0, 0},
                                          {1, 0, 0, MORAINE_TIME_MAX + 1, 0}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t v[5];
            for (uint32_t k = 0; k < 5u; k++) v[k] = bad[i][k];
            zxapp_arg_t xa[5] = {{"number", &v[0], 1},
                                 {"last", &v[1], 1},
                                 {"guardian", &v[2], 1},
                                 {"stamp", &v[3], 1},
                                 {"prior", &v[4], 1}};
            CHECK(moraine_custody_ok(v[0], v[1], v[2], v[3], v[4]) == MORAINE_REFUSED &&
                      wave_lp(app, "escrow_custody_ok", xa, 5) == ZXAPP_NEITHER,
                  "Moraine: escrow_custody_ok outside its pictures (%u): refused", i);
        }
    }

    /* MR3: escrow_key_strength, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2u ? wrand(0, 300) : wrand(0, MORAINE_BITS_MAX);
        b[i] = wrand(1, MORAINE_GUARDIANS);
        c[i] = wrand(b[i], MORAINE_GUARDIANS);
        if (i == 0) a[i] = 0, b[i] = 1, c[i] = 1;                /* the least: 10 */
        if (i == 1) a[i] = 256, b[i] = 3, c[i] = 5;              /* the most: 100 */
        if (i == 2) a[i] = 63, b[i] = 3, c[i] = 5;               /* a 64 short */
        if (i == 3) a[i] = 64, b[i] = 3, c[i] = 5;               /* one whole 64 */
        if (i == 4) a[i] = MORAINE_BITS_MAX, b[i] = 5, c[i] = 5; /* the longest key */
        want[i] = moraine_key_strength(a[i], b[i], c[i]);
    }
    zxapp_arg_t sa[3] = {
        {"bits", a, WAVE_ROWS}, {"quorum", b, WAVE_ROWS}, {"guardians", c, WAVE_ROWS}};
    CHECK(want[0] == 10 && want[1] == 100 && want[2] == 40 && want[3] == 55 && want[4] == 90 &&
              wave_same(app, "escrow_key_strength", sa, 3, want, WAVE_ROWS, false),
          "Moraine: escrow_key_strength, Trebago = KRON = C");
    {
        static const int64_t bi[2] = {256, MORAINE_BITS_MAX + 1}, qu[2] = {3, 3}, gu[2] = {5, 5};
        zxapp_arg_t xa[3] = {{"bits", bi, 2}, {"quorum", qu, 2}, {"guardians", gu, 2}};
        CHECK(moraine_key_strength(MORAINE_BITS_MAX + 1, 3, 5) == MORAINE_REFUSED &&
                  wave_lp(app, "escrow_key_strength", xa, 3) == ZXAPP_BOTH,
              "Moraine: a key past 65536 bits: Trebago refuses, KRON answers: BOTH");
        static const int64_t b2[2] = {256, 256}, q2[2] = {3, 0};
        zxapp_arg_t ya[3] = {{"bits", b2, 2}, {"quorum", q2, 2}, {"guardians", gu, 2}};
        CHECK(moraine_key_strength(256, 0, 5) == MORAINE_REFUSED &&
                  wave_lp(app, "escrow_key_strength", ya, 3) == ZXAPP_BOTH,
              "Moraine: a quorum of 0: Trebago refuses, KRON answers: BOTH");
    }

    /* MR4: guardian_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-12, 12);
        c[i] = wrand(-12, 12);
        d[i] = wrand(-12, 12);
        e[i] = wrand(0, 1200000000000);
        if (i == 0)
            a[i] = MORAINE_ESCROW_MAX, b[i] = 9, c[i] = 9, d[i] = 9, e[i] = MORAINE_TIME_MAX;
        if (i == 1) a[i] = MORAINE_ESCROW_MAX + 1, b[i] = 1, c[i] = 0, d[i] = 1, e[i] = 0;
        if (i == 2) a[i] = 1, b[i] = 10, c[i] = 0, d[i] = 1, e[i] = 0;
        if (i == 3) a[i] = 1, b[i] = 1, c[i] = 0, d[i] = 1, e[i] = MORAINE_TIME_MAX + 1;
        want[i] = T_(moraine_node_ok(a[i], b[i], c[i], d[i], e[i]));
    }
    zxapp_arg_t na[5] = {{"escrow", a, WAVE_ROWS},
                         {"guardian", b, WAVE_ROWS},
                         {"parent", c, WAVE_ROWS},
                         {"rank", d, WAVE_ROWS},
                         {"added", e, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "guardian_node_ok", na, 5, want, WAVE_ROWS, true),
          "Moraine: guardian_node_ok, MYRA = C");
    {
        /* the hierarchy is a tree under the owner */
        static const int64_t tree[5] = {0, 1, 1, 2, 0}, loop[5] = {2, 1, 0, 0, 0},
                             self[5] = {0, 2, 0, 0, 0}, far[5] = {0, 0, 0, 0, 6};
        int64_t rank[5];
        CHECK(moraine_hierarchy(tree, rank) && rank[0] == 1 && rank[1] == 2 && rank[3] == 3 &&
                  rank[4] == 1 && !moraine_hierarchy(loop, rank) &&
                  !moraine_hierarchy(self, rank) && !moraine_hierarchy(far, rank),
              "Moraine: the guardian hierarchy, a tree, never a loop");
    }

    /* the parts, called through Moraine as it exports them */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        b[i] = wrand(1, MORAINE_GUARDIANS);
        c[i] = wrand(-1, 4);
        d[i] = wrand(-300000, 300000);
        e[i] = wrand(0, 1);
        if (i == 0) a[i] = 1, b[i] = 3, c[i] = 0, d[i] = 0, e[i] = 0;
        want[i] = T_(heirloom_release_due(a[i], b[i], c[i], d[i], e[i]));
    }
    {
        zxapp_arg_t ha[5] = {{"reported", a, WAVE_ROWS},
                             {"k", b, WAVE_ROWS},
                             {"spare", c, WAVE_ROWS},
                             {"over", d, WAVE_ROWS},
                             {"vetoed", e, WAVE_ROWS}};
        CHECK(want[0] == ZXAPP_TRUE &&
                  wave_same(app, "heirloom.heir_release_due", ha, 5, want, WAVE_ROWS, true),
              "Moraine: heirloom.heir_release_due, release due, Eve = Sutra");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = wrand(0, 1);
        if (i == 0) a[i] = b[i] = 1, c[i] = 0;
        want[i] = T_(gatehouse_use_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t ga[3] = {
        {"asked", a, WAVE_ROWS}, {"granted", b, WAVE_ROWS}, {"revoked", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && wave_same(app, "gatehouse.use_ok", ga, 3, want, WAVE_ROWS, true),
          "Moraine: gatehouse.use_ok, the requester allowed, Eve = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, LOCKBOX_STAMP_MAX);
        b[i] = wrand(0, LOCKBOX_STAMP_MAX);
        want[i] = lockbox_days_left(a[i], b[i]);
    }
    zxapp_arg_t da[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.days_left", da, 2, want, WAVE_ROWS, false),
          "Moraine: lockbox.days_left, a key's expiry, C++ = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1100);
        b[i] = wrand(0, 5);
        c[i] = wrand(0, 1100);
        if (i == 0) a[i] = 12, b[i] = 1, c[i] = 0;
        want[i] = T_(lockbox_secret_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t la[3] = {
        {"length", a, WAVE_ROWS}, {"classes", b, WAVE_ROWS}, {"repeats", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE &&
              wave_same(app, "lockbox.secret_ok", la, 3, want, WAVE_ROWS, true),
          "Moraine: lockbox.secret_ok, a key's phrase inside the key limits");
    {
        /* the custody register's chain: numbers 1, 2, 3 linked; a reused 3 not */
        static const int64_t seq[4] = {1, 2, 3, 3}, prev[4] = {0, 1, 2, 2}, last[4] = {0, 1, 2, 3};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = T_(witness_linked(seq[i], prev[i], last[i]));
        zxapp_arg_t wa[3] = {{"seq", seq, 4}, {"prev", prev, 4}, {"last", last, 4}};
        CHECK(w[0] == ZXAPP_TRUE && w[3] == ZXAPP_FALSE &&
                  wave_same(app, "witness.chain_ok", wa, 3, w, 4, true),
              "Moraine: witness.chain_ok over the custody register, KRON = Sutra");
        static const int64_t neg[2] = {2, -1}, one[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"seq", neg, 2}, {"prev", one, 2}, {"last", one, 2}};
        CHECK(wave_lp(app, "witness.chain_ok", xa, 3) == ZXAPP_BOTH,
              "Moraine: witness.chain_ok, a negative number: BOTH");
        static const int64_t aseq[2] = {1, WITNESS_SEQ_MAX + 1}, aprev[2] = {0, 0},
                             kind[2] = {MORAINE_KIND, MORAINE_KIND},
                             stamp[2] = {INT64_C(1700000000000), 0};
        int64_t aw[2] = {T_(witness_record_fits(1, 0, MORAINE_KIND, INT64_C(1700000000000))),
                         T_(witness_record_fits(WITNESS_SEQ_MAX + 1, 0, MORAINE_KIND, 0))};
        zxapp_arg_t aa[4] = {
            {"seq", aseq, 2}, {"prev", aprev, 2}, {"kind", kind, 2}, {"stamp", stamp, 2}};
        CHECK(aw[0] == ZXAPP_TRUE && aw[1] == ZXAPP_FALSE &&
                  wave_same(app, "witness.audit_ok", aa, 4, aw, 2, true),
              "Moraine: witness.audit_ok, a release's record");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, WARDING_CHARS_MAX);
        b[i] = wrand(1, WARDING_ALPHABET_MAX);
        want[i] = warding_entropy_bits(a[i], b[i]);
    }
    zxapp_arg_t ea[2] = {{"chars", a, WAVE_ROWS}, {"alphabet", b, WAVE_ROWS}};
    CHECK(wave_same(app, "warding.key_entropy_bits", ea, 2, want, WAVE_ROWS, false),
          "Moraine: warding.key_entropy_bits, a key's bits, Trebago = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 6);
        b[i] = wrand(1, 4);
        c[i] = wrand(0, 1);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = warding_grid_cell(a, b, c, WAVE_ROWS, i);
    zxapp_arg_t xa[3] = {{"holder", a, WAVE_ROWS}, {"item", b, WAVE_ROWS}, {"live", c, WAVE_ROWS}};
    CHECK(wave_same(app, "warding.access_grid_cell", xa, 3, want, WAVE_ROWS, false),
          "Moraine: warding.access_grid_cell, requesters by escrowed keys");

    /* the BOTH test: a claim is open and within its waiting period. Guardian
     * 2 approves (TRUE) and guardian 4 objects (FALSE): the claim's word is
     * BOTH, held. Heirloom's release due is FALSE while the objection stands
     * and the wait runs, so "release now" is FALSE: the key is neither
     * released nor lost until the rule resolves it. */
    {
        lpres_state_t word[MORAINE_GUARDIANS] = {LPRES_STATE_NEITHER, LPRES_STATE_NEITHER,
                                                 LPRES_STATE_TRUE, LPRES_STATE_NEITHER,
                                                 LPRES_STATE_FALSE};
        CHECK(moraine_word(word) == LPRES_STATE_BOTH &&
                  lpres_conjoin(moraine_word(word), LPRES_STATE_TRUE) != LPRES_STATE_TRUE,
              "Moraine: guardian 2 approves, guardian 4 objects: BOTH, never TRUE");
        static const int64_t rep[2] = {1, 1}, k[2] = {3, 3}, sp[2] = {-2, -2},
                             over[2] = {-3600, -3600}, vet[2] = {1, 0};
        int64_t dw[2] = {T_(heirloom_release_due(1, 3, -2, -3600, 1)),
                         T_(heirloom_release_due(1, 3, -2, -3600, 0))};
        zxapp_arg_t ha[5] = {{"reported", rep, 2},
                             {"k", k, 2},
                             {"spare", sp, 2},
                             {"over", over, 2},
                             {"vetoed", vet, 2}};
        CHECK(dw[0] == ZXAPP_FALSE && dw[1] == ZXAPP_FALSE &&
                  wave_same(app, "heirloom.heir_release_due", ha, 5, dw, 2, true),
              "Moraine: within the wait, one approval and an objection: not due");
        static const int64_t du[2] = {0, 0}, le[2] = {86400, 86400}, al[2] = {1, 1};
        static const int64_t no[2] = {ZXAPP_FALSE, ZXAPP_FALSE};
        zxapp_arg_t ra2[3] = {{"due", du, 2}, {"left", le, 2}, {"allowed", al, 2}};
        CHECK(wave_same(app, "key_release_now", ra2, 3, no, 2, true),
              "Moraine: held: not released now, Eve = KRON");
    }
    WSEED = seed_was;
}
