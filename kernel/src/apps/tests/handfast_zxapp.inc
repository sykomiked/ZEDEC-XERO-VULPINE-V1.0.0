/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Handfast's languages, witnessed, row by row against handfast_rules.h,
 * the C the model runs; the parts it composes (witness, commons, kindred,
 * sealwax, furrow, trickle) resolved and called; and the BOTH of a clause
 * one party agreed and another disputed. Slots:
 * @logic.handfast_agreement_ready, @num.handfast_obligation_share,
 * @logic.handfast_clause_limits_ok, @num.handfast_clause_party_state,
 * @logic.handfast_clause_node_ok; and, used, @logic.witness_chain_ok,
 * @logic.witness_audit_ok, @logic.commons_room_post_ok,
 * @logic.commons_message_text_ok, @logic.kindred_contact_fields_ok,
 * @logic.kindred_contact_record_ok, @logic.sealwax_sign_ready,
 * @logic.sealwax_field_record_ok, @num.furrow_money_cell,
 * @num.trickle_stream_due. */
#include "handfast_rules.h"

APP_TEST(handfast, 47, 164)
{
    /* the shared row generator is put back as it was at the end, so the apps
     * tested after Handfast draw the rows they drew before it was built */
    const uint64_t seed_was = WSEED;
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: six parts, every part entry it calls resolves to its part */
    static const char *const used[10][2] = {
        {"witness.chain_ok", "witness"},          {"witness.audit_ok", "witness"},
        {"commons.room_post_ok", "commons"},      {"commons.message_text_ok", "commons"},
        {"kindred.contact_fields_ok", "kindred"}, {"kindred.contact_record_ok", "kindred"},
        {"sealwax.sign_ready", "sealwax"},        {"sealwax.field_record_ok", "sealwax"},
        {"furrow.money_cell", "furrow"},          {"trickle.stream_due", "trickle"}};
    CHECK(app->resolved && app->nuse == 6 && app->composed == 8 && app->n == 47 &&
              app->n <= ZXAPP_LOGIC_MAX && app->nuse <= ZXAPP_USES,
          "Handfast: composed of 6 parts, 8 own lines and %u of theirs (%u)", app->n - 8u, app->n);
    for (uint32_t i = 0; i < 10u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Handfast: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[8];
    CHECK(zxapp_provenance(app, org, 8) == 7 && !org[0].from, "Handfast: itself and six parts");

    /* HR3: agreement_ready, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 18);
        b[i] = wrand(0, 66);
        c[i] = i % 2u ? 0 : wrand(0, 3);
        if (i == 0) a[i] = 2, b[i] = 1, c[i] = 0;   /* the least */
        if (i == 1) a[i] = 16, b[i] = 64, c[i] = 0; /* the most */
        if (i == 2) a[i] = 17, b[i] = 5, c[i] = 0;  /* a party past */
        if (i == 3) a[i] = 3, b[i] = 65, c[i] = 0;  /* a clause past */
        if (i == 4) a[i] = 3, b[i] = 5, c[i] = 1;   /* one cell not agreed */
        want[i] = T_(handfast_ready_rule(a[i], b[i], c[i]));
    }
    zxapp_arg_t ra[3] = {
        {"parties", a, WAVE_ROWS}, {"clauses", b, WAVE_ROWS}, {"unagreed", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE &&
              wave_same(app, "agreement_ready", ra, 3, want, WAVE_ROWS, true),
          "Handfast: agreement_ready, Eve = KRON = C");
    {
        static const int64_t p[2] = {3, 3}, k[2] = {5, 5}, u[2] = {0, -1};
        zxapp_arg_t xa[3] = {{"parties", p, 2}, {"clauses", k, 2}, {"unagreed", u, 2}};
        CHECK(!handfast_ready_rule(3, 5, -1) &&
                  wave_lp(app, "agreement_ready", xa, 3) == ZXAPP_BOTH,
              "Handfast: a count below zero: Eve refuses, KRON reads it as none: BOTH");
    }

    /* HR4: obligation_share, one row a call (ALGOL reads one), Sutra = Trebago = ALGOL = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t am = i % 3u ? wrand(0, 1000000) : wrand(0, HANDFAST_AMOUNT_MAX),
                ob = wrand(1, HANDFAST_OBLIGORS), w = 0;
        int64_t po = wrand(1, ob);
        if (i == 0) am = 0, ob = 1, po = 1;                                 /* zero */
        if (i == 1) am = HANDFAST_AMOUNT_MAX, ob = 7, po = 1;               /* max, rounded up */
        if (i == 2) am = HANDFAST_AMOUNT_MAX, ob = 7, po = 7;               /* the last part */
        if (i == 3) am = 1000, ob = 3, po = 1;                              /* 334 */
        if (i == 4) am = 1, ob = HANDFAST_OBLIGORS, po = HANDFAST_OBLIGORS; /* nothing over */
        w = handfast_share(am, ob, po);
        CHECK(w != HANDFAST_REFUSED, "Handfast: HR4 row %u answers", i);
        zxapp_arg_t sa[3] = {{"amount", &am, 1}, {"obligors", &ob, 1}, {"pos", &po, 1}};
        CHECK(wave_same(app, "obligation_share", sa, 3, &w, 1, false),
              "Handfast: obligation_share row %u, Sutra = Trebago = ALGOL = C (%lld)", i,
              (long long) w);
    }
    {
        /* the parts of one amount sum to it exactly */
        int64_t s = 0;
        for (int64_t p = 1; p <= 7; p++) s += handfast_share(HANDFAST_AMOUNT_MAX, 7, p);
        CHECK(s == HANDFAST_AMOUNT_MAX && handfast_share(1000, 3, 1) == 334 &&
                  handfast_share(1000, 3, 3) == 333,
              "Handfast: the parts sum to the amount, the first ones a unit more");
        /* obligors past Sutra's 9(2): Sutra and ALGOL refuse, Trebago answers */
        int64_t am = 1000, ob = 100, po = 1;
        zxapp_arg_t xa[3] = {{"amount", &am, 1}, {"obligors", &ob, 1}, {"pos", &po, 1}};
        CHECK(handfast_share(am, ob, po) == HANDFAST_REFUSED &&
                  wave_lp(app, "obligation_share", xa, 3) == ZXAPP_BOTH,
              "Handfast: 100 obligors: BOTH");
        /* outside every language's domain: refused by all */
        static const int64_t bad[4][3] = {
            {-1, 3, 1}, {HANDFAST_AMOUNT_MAX + 1, 3, 1}, {1000, 0, 1}, {1000, 3, 4}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t x = bad[i][0], y = bad[i][1], z = bad[i][2];
            zxapp_arg_t ba[3] = {{"amount", &x, 1}, {"obligors", &y, 1}, {"pos", &z, 1}};
            CHECK(handfast_share(x, y, z) == HANDFAST_REFUSED &&
                      wave_lp(app, "obligation_share", ba, 3) == ZXAPP_NEITHER,
                  "Handfast: obligation_share outside its domain (%u): refused", i);
        }
    }

    /* HR1: clause_limits_ok, Eve = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, 18);
        b[i] = wrand(-10, HANDFAST_TEXT_MAX + 10);
        if (i < 4u) a[i] = (int64_t[]){2, 16, 1, 17}[i], b[i] = 100;
        if (i == 4) a[i] = 5, b[i] = 0;
        if (i == 5) a[i] = 5, b[i] = HANDFAST_TEXT_MAX;
        if (i == 6) a[i] = 5, b[i] = HANDFAST_TEXT_MAX + 1;
        want[i] = T_(handfast_limits_ok(a[i], b[i]));
    }
    zxapp_arg_t la[2] = {{"parties", a, WAVE_ROWS}, {"bytes", b, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE &&
              want[6] == ZXAPP_FALSE &&
              wave_same(app, "clause_limits_ok", la, 2, want, WAVE_ROWS, true),
          "Handfast: clause_limits_ok, Eve = C");

    /* HR2: clause_party_state, KRON = C, over a whole matrix of cells */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 12);
        b[i] = wrand(0, 2);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = handfast_cell_state(a, b, WAVE_ROWS, i);
    zxapp_arg_t ma[2] = {{"clause", a, WAVE_ROWS}, {"mark", b, WAVE_ROWS}};
    CHECK(wave_same(app, "clause_party_state", ma, 2, want, WAVE_ROWS, false),
          "Handfast: clause_party_state, KRON = C");

    /* HR5: clause_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000000, 1200000000);
        b[i] = wrand(-1200, 1200);
        c[i] = wrand(0, 1200000);
        d[i] = wrand(0, 1200);
        e[i] = wrand(0, 12000);
        f[i] = wrand(0, 12);
        if (i == 0)
            a[i] = HANDFAST_DEAL_MAX, b[i] = 999, c[i] = HANDFAST_REV_MAX, d[i] = 999, e[i] = 9999,
            f[i] = 3;                                                              /* max */
        if (i == 1) a[i] = 1, b[i] = 1000, c[i] = 1, d[i] = 0, e[i] = 1, f[i] = 0; /* past */
        want[i] = T_(handfast_node_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t na[6] = {{"deal", a, WAVE_ROWS},  {"clause", b, WAVE_ROWS},
                         {"rev", c, WAVE_ROWS},   {"parent", d, WAVE_ROWS},
                         {"bytes", e, WAVE_ROWS}, {"state", f, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "clause_node_ok", na, 6, want, WAVE_ROWS, true),
          "Handfast: clause_node_ok, MYRA = C");

    /* the parts, called through Handfast as it exports them */
    {
        /* a clause's revisions on the journal, linked */
        static const int64_t seq[4] = {5, 9, 14, 14}, prev[4] = {0, 5, 9, 9},
                             last[4] = {0, 5, 9, 14};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = T_(witness_linked(seq[i], prev[i], last[i]));
        zxapp_arg_t wa[3] = {{"seq", seq, 4}, {"prev", prev, 4}, {"last", last, 4}};
        CHECK(w[3] == ZXAPP_FALSE && wave_same(app, "witness.chain_ok", wa, 3, w, 4, true),
              "Handfast: witness.chain_ok over a clause's revisions, KRON = Sutra");
        static const int64_t neg[2] = {2, -1}, one[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"seq", neg, 2}, {"prev", one, 2}, {"last", one, 2}};
        CHECK(wave_lp(app, "witness.chain_ok", xa, 3) == ZXAPP_BOTH,
              "Handfast: witness.chain_ok, a negative seq: BOTH");
        static const int64_t aseq[2] = {5, WITNESS_SEQ_MAX + 1}, aprev[2] = {0, 0},
                             kind[2] = {HANDFAST_KIND, HANDFAST_KIND}, stamp[2] = {0, 0};
        int64_t aw[2] = {T_(witness_record_fits(5, 0, HANDFAST_KIND, 0)),
                         T_(witness_record_fits(WITNESS_SEQ_MAX + 1, 0, HANDFAST_KIND, 0))};
        zxapp_arg_t aa[4] = {
            {"seq", aseq, 2}, {"prev", aprev, 2}, {"kind", kind, 2}, {"stamp", stamp, 2}};
        CHECK(aw[0] == ZXAPP_TRUE && aw[1] == ZXAPP_FALSE &&
                  wave_same(app, "witness.audit_ok", aa, 4, aw, 2, true),
              "Handfast: witness.audit_ok, a revision's journal record");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1), b[i] = wrand(0, 1), c[i] = wrand(0, 1);
        if (i == 0) a[i] = b[i] = c[i] = 1;
        want[i] = T_(commons_room_post_ok(a[i], b[i], c[i]));
    }
    zxapp_arg_t pa[3] = {
        {"member", a, WAVE_ROWS}, {"joined", b, WAVE_ROWS}, {"enabled", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE &&
              wave_same(app, "commons.room_post_ok", pa, 3, want, WAVE_ROWS, true),
          "Handfast: commons.room_post_ok, may a note go to the room");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] =
            i < 4u ? (int64_t[]){0, 1, COMMONS_TEXT_MAX, COMMONS_TEXT_MAX + 1}[i] : wrand(-5, 1100);
        want[i] = T_(commons_message_text_ok(a[i]));
    }
    zxapp_arg_t ta[1] = {{"bytes", a, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE && want[2] == ZXAPP_TRUE &&
              wave_same(app, "commons.message_text_ok", ta, 1, want, WAVE_ROWS, true),
          "Handfast: commons.message_text_ok, a note's length");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1, KINDRED_NAME_MAX + 2);
        b[i] = c[i] = d[i] = 0;
        want[i] = T_(kindred_fields_ok(a[i], 0, 0, 0));
    }
    zxapp_arg_t ka[4] = {{"namelen", a, WAVE_ROWS},
                         {"phone", b, WAVE_ROWS},
                         {"mail", c, WAVE_ROWS},
                         {"note", d, WAVE_ROWS}};
    CHECK(wave_same(app, "kindred.contact_fields_ok", ka, 4, want, WAVE_ROWS, true),
          "Handfast: kindred.contact_fields_ok, a party's name, Eve = KRON");
    {
        int64_t codes = 0, nm = kindred_name_code("Ada", 3, &codes);
        static int64_t ct[2] = {7, KINDRED_CONTACT_MAX + 1}, zero[2] = {0, 0}, name[2];
        name[0] = nm, name[1] = nm;
        int64_t w[2] = {T_(kindred_record_ok(7, 0, 0, nm, 0, 0)),
                        T_(kindred_record_ok(KINDRED_CONTACT_MAX + 1, 0, 0, nm, 0, 0))};
        zxapp_arg_t ca[6] = {{"contact", ct, 2}, {"card", zero, 2},     {"node", zero, 2},
                             {"name", name, 2},  {"verified", zero, 2}, {"stamp", zero, 2}};
        CHECK(w[0] == ZXAPP_TRUE && w[1] == ZXAPP_FALSE &&
                  wave_same(app, "kindred.contact_record_ok", ca, 6, w, 2, true),
              "Handfast: kindred.contact_record_ok, a party as a contact");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 34), b[i] = wrand(0, 2), c[i] = wrand(0, 2);
        if (i == 0) a[i] = 2, b[i] = 0, c[i] = 0;
        want[i] = T_(sealwax_sign_ready(a[i], b[i], c[i]));
    }
    zxapp_arg_t ga[3] = {
        {"fields", a, WAVE_ROWS}, {"missing", b, WAVE_ROWS}, {"outside", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE &&
              wave_same(app, "sealwax.sign_ready", ga, 3, want, WAVE_ROWS, true),
          "Handfast: sealwax.sign_ready, the signature page");
    {
        static const int64_t fm[2] = {164, 1000000000}, fl[2] = {3, 3},
                             kd[2] = {SEALWAX_AMOUNT, SEALWAX_AMOUNT}, rq[2] = {1, 1},
                             lo[2] = {0, 0}, hi[2] = {SEALWAX_BOUND_MAX, SEALWAX_BOUND_MAX},
                             ln[2] = {0, 0};
        int64_t w[2] = {
            T_(sealwax_record_fits(164, 3, SEALWAX_AMOUNT, 1, 0, SEALWAX_BOUND_MAX, 0)),
            T_(sealwax_record_fits(1000000000, 3, SEALWAX_AMOUNT, 1, 0, SEALWAX_BOUND_MAX, 0))};
        zxapp_arg_t fa[7] = {{"form", fm, 2},     {"field", fl, 2}, {"kind", kd, 2},
                             {"required", rq, 2}, {"lo", lo, 2},    {"hi", hi, 2},
                             {"len", ln, 2}};
        CHECK(w[0] == ZXAPP_TRUE && w[1] == ZXAPP_FALSE &&
                  wave_same(app, "sealwax.field_record_ok", fa, 7, w, 2, true),
              "Handfast: sealwax.field_record_ok, an obligation's amount field");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, SEALWAX_UNITS_MAX);
        b[i] = wrand(0, 99);
        want[i] = sealwax_amount_minor(a[i], b[i]);
    }
    zxapp_arg_t ua[2] = {{"units", a, WAVE_ROWS}, {"cents", b, WAVE_ROWS}};
    CHECK(wave_same(app, "furrow.money_cell", ua, 2, want, WAVE_ROWS, false),
          "Handfast: furrow.money_cell, an amount as entered, Sutra = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, TRK_TOTAL_MAX);
        c[i] = wrand(1, 360);
        b[i] = wrand(1, c[i]);
        d[i] = wrand(0, a[i] * b[i] / c[i]);
        want[i] = trk_stream_due(a[i], b[i], c[i], d[i]);
    }
    zxapp_arg_t sa[4] = {{"total", a, WAVE_ROWS},
                         {"tick", b, WAVE_ROWS},
                         {"ticks", c, WAVE_ROWS},
                         {"paid", d, WAVE_ROWS}};
    CHECK(wave_same(app, "trickle.stream_due", sa, 4, want, WAVE_ROWS, false),
          "Handfast: trickle.stream_due, instalment n of N, Sutra = Trebago");

    /* the BOTH test: party A agrees clause 4, party B disputes it; clauses
     * 1 and 2 both agreed. Clause 4 is BOTH and held in escrow, the agreed
     * clauses go ahead, and the agreement is not ready to sign. */
    {
        static const int64_t cl[6] = {1, 1, 2, 2, 4, 4}, mk[6] = {1, 1, 1, 1, 1, 2};
        static const int64_t st[6] = {1, 1, 1, 1, 3, 3};
        zxapp_arg_t xa[2] = {{"clause", cl, 6}, {"mark", mk, 6}};
        CHECK(wave_same(app, "clause_party_state", xa, 2, st, 6, false) &&
                  handfast_cell_state(cl, mk, 6, 4) == LPRES_STATE_BOTH &&
                  handfast_cell_state(cl, mk, 6, 0) == LPRES_STATE_TRUE,
              "Handfast: clause 4 agreed by A and disputed by B is BOTH; 1 and 2 are TRUE");
        CHECK(lpres_kjoin(LPRES_STATE_TRUE, LPRES_STATE_FALSE) == LPRES_STATE_BOTH &&
                  lpres_conjoin(LPRES_STATE_BOTH, LPRES_STATE_TRUE) != LPRES_STATE_TRUE &&
                  !handfast_ready_rule(2, 3, 1),
              "Handfast: BOTH is never TRUE, so the agreement is not ready to sign");
    }
    WSEED = seed_was;
}
