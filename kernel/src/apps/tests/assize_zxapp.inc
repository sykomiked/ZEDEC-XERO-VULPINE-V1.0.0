/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Assize's languages, witnessed, row by row against assize_rules.h, the C
 * the model runs; the parts it composes (witness, broadsheet, sealwax,
 * twelvefold, tallystick) resolved and called; and the BOTH of two
 * witnesses who place one event at two sites. Slots:
 * @logic.assize_filing_accepted, @num.assize_response_days_left,
 * @num.assize_docket_number, @num.assize_claim_evidence_state,
 * @logic.assize_dispute_node_ok; and, used, @logic.witness_chain_ok,
 * @logic.witness_audit_ok, @num.witness_kind_count,
 * @logic.broadsheet_well_formed, @logic.broadsheet_note_ok,
 * @logic.sealwax_sign_ready, @logic.sealwax_field_record_ok,
 * @logic.twelvefold_lp_entail_kept, @num.tallystick_next_doc_number. */
#include "assize_rules.h"

APP_TEST(assize, 34, 178)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* compose: five parts, every part entry it calls resolves to its part */
    static const char *const used[9][2] = {{"witness.chain_ok", "witness"},
                                           {"witness.audit_ok", "witness"},
                                           {"witness.kind_count", "witness"},
                                           {"broadsheet.well_formed", "broadsheet"},
                                           {"broadsheet.note_ok", "broadsheet"},
                                           {"sealwax.sign_ready", "sealwax"},
                                           {"sealwax.field_record_ok", "sealwax"},
                                           {"twelvefold.lp_entail_kept", "twelvefold"},
                                           {"tallystick.next_doc_number", "tallystick"}};
    CHECK(app->resolved && app->nuse == 5 && app->composed == 8 && app->n == 34,
          "Assize: 5 parts used, 8 composed with theirs, 8 own lines and 26 of theirs (%u, %u)",
          app->composed, app->n);
    for (uint32_t i = 0; i < 9u; i++) {
        const zxapp_logic_t *L = zxapp_find(app, used[i][0]);
        CHECK(L && L->origin && !strcmp(L->origin->id, used[i][1]) && L->loaded &&
                  zxapp_contract(app, used[i][0]),
              "Assize: %s resolves to %s and loads", used[i][0], used[i][1]);
    }
    zxapp_origin_t org[16];
    uint32_t norg = zxapp_provenance(app, org, 16);
    CHECK(norg == 6 && !org[0].from, "Assize: itself and its five parts (%u)", norg);

    /* AR1: filing_accepted, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 2);
        b[i] = i % 2u ? wrand(-30, 30) : wrand(-5, ASSIZE_LEFT_MAX);
        c[i] = wrand(0, ASSIZE_PAGES_MAX + 2);
        d[i] = wrand(-1, ASSIZE_EXHIBITS_MAX + 1);
        if (i == 0) a[i] = 1, b[i] = 0, c[i] = 1, d[i] = 0; /* the last day, least */
        if (i == 1)
            a[i] = 1, b[i] = ASSIZE_LEFT_MAX, c[i] = ASSIZE_PAGES_MAX,
            d[i] = ASSIZE_EXHIBITS_MAX;                                        /* the most */
        if (i == 2) a[i] = 1, b[i] = -1, c[i] = 10, d[i] = 2;                  /* a day late */
        if (i == 3) a[i] = 0, b[i] = 5, c[i] = 10, d[i] = 2;                   /* not a party */
        if (i == 4) a[i] = 1, b[i] = 5, c[i] = ASSIZE_PAGES_MAX + 1, d[i] = 2; /* too long */
        if (i == 5) a[i] = 1, b[i] = 5, c[i] = 10, d[i] = ASSIZE_EXHIBITS_MAX + 1;
        want[i] = T_(assize_filing_accepted(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t fa[4] = {{"party", a, WAVE_ROWS},
                         {"left", b, WAVE_ROWS},
                         {"pages", c, WAVE_ROWS},
                         {"exhibits", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              wave_same(app, "filing_accepted", fa, 4, want, WAVE_ROWS, true),
          "Assize: filing_accepted, Eve = KRON = C");
    {
        static const int64_t party[2] = {1, 1}, left[2] = {5, ASSIZE_LEFT_MAX + 1},
                             pages[2] = {1, 1}, ex[2] = {0, 0};
        zxapp_arg_t xa[4] = {
            {"party", party, 2}, {"left", left, 2}, {"pages", pages, 2}, {"exhibits", ex, 2}};
        CHECK(!assize_filing_accepted(1, left[1], 1, 0) &&
                  wave_lp(app, "filing_accepted", xa, 4) == ZXAPP_BOTH,
              "Assize: a deadline past 9999 days: Eve refuses, KRON answers: BOTH");
    }

    /* AR2: response_days_left, Trebago = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, ASSIZE_DAY_MAX);
        b[i] = i % 2u ? a[i] - wrand(-60, 60) : wrand(0, ASSIZE_DAY_MAX);
        if (b[i] < 0) b[i] = 0;
        if (b[i] > ASSIZE_DAY_MAX) b[i] = ASSIZE_DAY_MAX;
        if (i == 0) a[i] = 0, b[i] = 0;              /* zero */
        if (i == 1) a[i] = ASSIZE_DAY_MAX, b[i] = 0; /* the most */
        if (i == 2) a[i] = 0, b[i] = ASSIZE_DAY_MAX; /* the least */
        if (i == 3) a[i] = 20000, b[i] = 20001;      /* a day past */
        CHECK(assize_days_left(a[i], b[i], &want[i]), "Assize: AR2 row %u answers", i);
    }
    zxapp_arg_t ra[2] = {{"deadline", a, WAVE_ROWS}, {"today", b, WAVE_ROWS}};
    CHECK(want[1] == ASSIZE_DAY_MAX && want[2] == -ASSIZE_DAY_MAX && want[3] == -1 &&
              wave_same(app, "response_days_left", ra, 2, want, WAVE_ROWS, false),
          "Assize: response_days_left, Trebago = KRON = C");
    {
        static const int64_t dl[2] = {20030, ASSIZE_DAY_MAX + 1}, td[2] = {20000, 0},
                             dl2[2] = {20030, 20030}, td2[2] = {20000, -1};
        int64_t w = 0;
        zxapp_arg_t x1[2] = {{"deadline", dl, 2}, {"today", td, 2}};
        zxapp_arg_t x2[2] = {{"deadline", dl2, 2}, {"today", td2, 2}};
        CHECK(!assize_days_left(dl[1], td[1], &w) && !assize_days_left(dl2[1], td2[1], &w) &&
                  wave_lp(app, "response_days_left", x1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "response_days_left", x2, 2) == ZXAPP_BOTH,
              "Assize: a day past 999999 or below 0: Trebago refuses, KRON answers: BOTH");
    }

    /* AR3: docket_number, one row a call (ALGOL reads one), Sutra = ALGOL = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t cn = wrand(1, ASSIZE_CASE_MAX), sq = wrand(1, ASSIZE_SEQ_MAX);
        if (i == 0) cn = 1, sq = 1;                                     /* the least */
        if (i == 1) cn = ASSIZE_CASE_MAX, sq = ASSIZE_SEQ_MAX;          /* the most */
        if (i == 2) cn = 178, sq = assize_next_seq(1);                  /* after the opening */
        if (i == 3) cn = 178, sq = assize_next_seq(ASSIZE_SEQ_MAX - 1); /* the docket's last */
        int64_t w = assize_docket_number(cn, sq);
        zxapp_arg_t da[2] = {{"caseno", &cn, 1}, {"seq", &sq, 1}};
        CHECK(w > 0 && wave_same(app, "docket_number", da, 2, &w, 1, false),
              "Assize: docket_number row %u, Sutra = ALGOL = C (%lld)", i, (long long) w);
    }
    {
        /* case 0 or filing 0: Sutra's pictures hold them, ALGOL's proof refuses: held */
        static const int64_t held[2][2] = {{178, 0}, {0, 7}};
        for (uint32_t i = 0; i < 2u; i++) {
            int64_t cn = held[i][0], sq = held[i][1];
            zxapp_arg_t da[2] = {{"caseno", &cn, 1}, {"seq", &sq, 1}};
            CHECK(assize_docket_number(cn, sq) == ASSIZE_REFUSED &&
                      wave_lp(app, "docket_number", da, 2) == ZXAPP_BOTH,
                  "Assize: docket number of nothing (%u): Sutra answers, ALGOL refuses: BOTH", i);
        }
        /* past the pictures: both refuse */
        static const int64_t bad[4][2] = {{1000000, 1}, {-1, 1}, {178, 10000}, {178, -1}};
        for (uint32_t i = 0; i < 4u; i++) {
            int64_t cn = bad[i][0], sq = bad[i][1];
            zxapp_arg_t xa[2] = {{"caseno", &cn, 1}, {"seq", &sq, 1}};
            CHECK(assize_docket_number(cn, sq) == ASSIZE_REFUSED &&
                      wave_lp(app, "docket_number", xa, 2) == ZXAPP_NEITHER,
                  "Assize: docket_number past the pictures (%u): both refuse", i);
        }
        CHECK(assize_next_seq(ASSIZE_SEQ_MAX) == ASSIZE_REFUSED &&
                  assize_next_seq(-1) == ASSIZE_REFUSED && assize_next_seq(0) == 1,
              "Assize: a full docket refuses the next seq; a seq is never reused");
    }

    /* AR4: claim_evidence_state, KRON = C, and the same as lpres_kjoin over each claim */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 4u ? 1 : wrand(1, 12);
        b[i] = wrand(ASSIZE_SILENT, ASSIZE_CONTRADICTS);
    }
    a[4] = ASSIZE_CLAIM_MAX, b[4] = ASSIZE_SILENT; /* the most, alone and silent: NEITHER */
    b[0] = ASSIZE_SUPPORTS, b[1] = ASSIZE_CONTRADICTS;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        lpres_state_t k = LPRES_STATE_NEITHER;
        for (uint32_t j = 0; j < WAVE_ROWS; j++)
            if (a[j] == a[i] && b[j] != ASSIZE_SILENT)
                k = lpres_kjoin(k, b[j] == ASSIZE_SUPPORTS ? LPRES_STATE_TRUE : LPRES_STATE_FALSE);
        want[i] = assize_claim_state(a, b, WAVE_ROWS, i);
        CHECK(want[i] == (int64_t) k, "Assize: AR4 row %u is lpres_kjoin of its claim", i);
    }
    zxapp_arg_t ea[2] = {{"claim", a, WAVE_ROWS}, {"says", b, WAVE_ROWS}};
    CHECK(want[0] == LPRES_STATE_BOTH && want[4] == LPRES_STATE_NEITHER &&
              wave_same(app, "claim_evidence_state", ea, 2, want, WAVE_ROWS, false),
          "Assize: claim_evidence_state, KRON = C");
    {
        static const int64_t cl[3] = {5, 5, 6}, sy[3] = {1, 1, 2};
        CHECK(assize_claim_state(cl, sy, 3, 0) == LPRES_STATE_TRUE &&
                  assize_claim_state(cl, sy, 3, 2) == LPRES_STATE_FALSE &&
                  assize_claim_state(cl, sy, 3, 3) == ASSIZE_REFUSED,
              "Assize: one side's evidence alone is TRUE or FALSE");
        static const int64_t bad[2] = {5, 0}, bsy[2] = {1, 3};
        CHECK(assize_claim_state(bad, sy, 2, 0) == ASSIZE_REFUSED &&
                  assize_claim_state(cl, bsy, 2, 0) == ASSIZE_REFUSED,
              "Assize: a claim 0 or a says past 2 is refused");
    }

    /* AR5: dispute_node_ok, MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000, 1200000);
        b[i] = wrand(-12000, 12000);
        c[i] = wrand(0, 12);
        d[i] = wrand(0, 120);
        e[i] = wrand(0, 1200000);
        f[i] = wrand(0, 12000);
        if (i == 0)
            a[i] = ASSIZE_CASE_MAX, b[i] = ASSIZE_SEQ_MAX, c[i] = 9, d[i] = 99, e[i] = 999999,
            f[i] = 9999;                                                        /* the most */
        if (i == 1) a[i] = 0, b[i] = 0, c[i] = 0, d[i] = 0, e[i] = 0, f[i] = 0; /* zero */
        if (i == 2) a[i] = 1, b[i] = 1, c[i] = 1, d[i] = 100, e[i] = 1, f[i] = 1;
        if (i == 3) a[i] = -999999, b[i] = 1, c[i] = 1, d[i] = 1, e[i] = 1, f[i] = 10000;
        want[i] = T_(assize_node_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    }
    zxapp_arg_t na[6] = {{"caseno", a, WAVE_ROWS}, {"seq", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},   {"party", d, WAVE_ROWS},
                         {"filed", e, WAVE_ROWS},  {"claim", f, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE &&
              wave_same(app, "dispute_node_ok", na, 6, want, WAVE_ROWS, true),
          "Assize: dispute_node_ok, MYRA = C");

    /* the parts, called through Assize as it exports them */
    {
        /* the docket's lines, each linked to the one before */
        static const int64_t seq[4] = {1, 2, 3, 3}, prev[4] = {0, 1, 2, 1}, last[4] = {0, 1, 2, 3};
        int64_t w[4];
        for (uint32_t i = 0; i < 4u; i++) w[i] = T_(witness_linked(seq[i], prev[i], last[i]));
        zxapp_arg_t ca[3] = {{"seq", seq, 4}, {"prev", prev, 4}, {"last", last, 4}};
        CHECK(w[3] == ZXAPP_FALSE && wave_same(app, "witness.chain_ok", ca, 3, w, 4, true),
              "Assize: witness.chain_ok over the docket, KRON = Sutra");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, 9999);
        b[i] = a[i] - wrand(1, 3);
        c[i] = wrand(0, 11);
        d[i] = wrand(0, ASSIZE_DAY_MAX) * 86400000;
        if (i == 0) a[i] = WITNESS_SEQ_MAX, b[i] = 0, c[i] = 6, d[i] = WITNESS_STAMP_MAX;
        if (i == 1) a[i] = WITNESS_SEQ_MAX + 1, b[i] = 0, c[i] = 1, d[i] = 0;
        want[i] = T_(witness_record_fits(a[i], b[i], c[i], d[i]));
    }
    zxapp_arg_t aa[4] = {{"seq", a, WAVE_ROWS},
                         {"prev", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "witness.audit_ok", aa, 4, want, WAVE_ROWS, true),
          "Assize: witness.audit_ok, each docket line as a record");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 6);
    {
        int64_t ev = 3, k = witness_kind_count(a, WAVE_ROWS, ev);
        zxapp_arg_t ka[2] = {{"kind", a, WAVE_ROWS}, {"want", &ev, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &k, 1, false),
              "Assize: witness.kind_count, the evidence lodged, KRON = Trebago");
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 1);
        d[i] = wrand(1, 1000000);
        b[i] = wrand(-2, d[i] + 2);
        c[i] = d[i] - b[i];
        want[i] = T_(broadsheet_well_formed(a[i], b[i], c[i]) == 1);
    }
    zxapp_arg_t wa[3] = {{"header", a, WAVE_ROWS}, {"off", b, WAVE_ROWS}, {"room", c, WAVE_ROWS}};
    CHECK(wave_same(app, "broadsheet.well_formed", wa, 3, want, WAVE_ROWS, true),
          "Assize: broadsheet.well_formed, a lodged PDF's startxref");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        /* inside every range, or past a field's digits: MYRA and Eve agree on both */
        a[i] = wrand(1, 10000);
        b[i] = i % 7u ? wrand(1, 250) : 1000000;
        c[i] = wrand(0, 100000);
        d[i] = wrand(0, 100000);
        e[i] = wrand(1, 3);
        f[i] = wrand(0, ASSIZE_DAY_MAX) * 86400000;
        if (i == 0) a[i] = 2, b[i] = 1, c[i] = 0, d[i] = 0, e[i] = 1, f[i] = 0;
        if (i == 1) a[i] = 9999, b[i] = 250, c[i] = 99999, d[i] = 99999, e[i] = 3, f[i] = 0;
        if (i == 2) a[i] = 10000, b[i] = 1, c[i] = 0, d[i] = 0, e[i] = 1, f[i] = 0;
        want[i] = T_(broadsheet_note_ok(a[i], b[i], c[i], d[i], e[i], f[i]) == 1);
    }
    zxapp_arg_t oa[6] = {{"note", a, WAVE_ROWS}, {"page", b, WAVE_ROWS}, {"x", c, WAVE_ROWS},
                         {"y", d, WAVE_ROWS},    {"kind", e, WAVE_ROWS}, {"stamp", f, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "broadsheet.note_ok", oa, 6, want, WAVE_ROWS, true),
          "Assize: broadsheet.note_ok, an annotation on a page of evidence");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 40);
        b[i] = i % 4u ? wrand(0, 3) : 0;
        c[i] = i % 3u ? 0 : wrand(0, 2);
        if (i == 0) a[i] = 7, b[i] = 0, c[i] = 0; /* a lodging form */
        if (i == 1) a[i] = SEALWAX_FIELDS, b[i] = 0, c[i] = 0;
        if (i == 2) a[i] = SEALWAX_FIELDS + 1, b[i] = 0, c[i] = 0;
        want[i] = T_(sealwax_sign_ready(a[i], b[i], c[i]));
    }
    zxapp_arg_t sa[3] = {
        {"fields", a, WAVE_ROWS}, {"missing", b, WAVE_ROWS}, {"outside", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              wave_same(app, "sealwax.sign_ready", sa, 3, want, WAVE_ROWS, true),
          "Assize: sealwax.sign_ready, a form ready to lodge");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 3);
        b[i] = wrand(1, 8);
        c[i] = wrand(1, 3);
        d[i] = wrand(0, 1);
        e[i] = wrand(0, 999999);
        f[i] = e[i] + wrand(0, 999999);
        g[i] = i % 5u ? 0 : 1000;
        want[i] = T_(sealwax_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    }
    zxapp_arg_t fr[7] = {{"form", a, WAVE_ROWS}, {"field", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS}, {"required", d, WAVE_ROWS},
                         {"lo", e, WAVE_ROWS},   {"hi", f, WAVE_ROWS},
                         {"len", g, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_FALSE && want[1] == ZXAPP_TRUE &&
              wave_same(app, "sealwax.field_record_ok", fr, 7, want, WAVE_ROWS, true),
          "Assize: sealwax.field_record_ok, the forms' field definitions");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, 3);
        b[i] = wrand(TWELVEFOLD_CODE_T, TWELVEFOLD_CODE_F);
        want[i] = T_(twelvefold_kept(a[i], b[i]) == 1);
    }
    zxapp_arg_t ta[2] = {{"premise", a, WAVE_ROWS}, {"conclusion", b, WAVE_ROWS}};
    CHECK(wave_same(app, "twelvefold.lp_entail_kept", ta, 2, want, WAVE_ROWS, true),
          "Assize: twelvefold.lp_entail_kept, whether a finding follows, Sutra = KRON");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, ASSIZE_SEQ_MAX - 1);
        want[i] = tstk_next_number(a[i]);
    }
    zxapp_arg_t xa[1] = {{"last", a, WAVE_ROWS}};
    CHECK(wave_same(app, "tallystick.next_doc_number", xa, 1, want, WAVE_ROWS, false),
          "Assize: tallystick.next_doc_number, the docket's next seq");

    /* AR6, the BOTH test: one claim asserting site 11; witness 1 places the
     * event at site 11, witness 2 at site 12: BOTH, never merged */
    static const int64_t two_sites[2] = {11, 12}, one_site[2] = {11, 11}, elsewhere[2] = {12, 13};
    lpres_state_t st = assize_sites_state(11, two_sites, 2);
    CHECK(st == LPRES_STATE_BOTH && (int) st == 3, "Assize: two sites for one event: BOTH (3)");
    {
        int64_t cl[2] = {4, 4},
                sy[2] = {assize_says(11, two_sites[0]), assize_says(11, two_sites[1])};
        zxapp_arg_t ba[2] = {{"claim", cl, 2}, {"says", sy, 2}};
        int64_t w[2] = {LPRES_STATE_BOTH, LPRES_STATE_BOTH};
        CHECK(wave_same(app, "claim_evidence_state", ba, 2, w, 2, false),
              "Assize: the evidence matrix says BOTH for that claim, each row kept");
    }
    CHECK(assize_sites_state(11, one_site, 2) == LPRES_STATE_TRUE &&
              assize_sites_state(11, elsewhere, 2) == LPRES_STATE_FALSE &&
              assize_sites_state(11, one_site, 0) == LPRES_STATE_NEITHER,
          "Assize: one site TRUE, only elsewhere FALSE, no witness NEITHER");
    CHECK(twelvefold_kept(LPRES_STATE_BOTH, TWELVEFOLD_CODE_T) == 1 &&
              twelvefold_kept(LPRES_STATE_BOTH, TWELVEFOLD_CODE_F) == 0,
          "Assize: from BOTH an upheld finding follows, a dismissal does not (advice only)");
}
