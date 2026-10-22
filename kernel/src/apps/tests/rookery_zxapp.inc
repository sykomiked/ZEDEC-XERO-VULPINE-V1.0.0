/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Rookery's languages against the C its model runs (rookery_rules.h).
 * Slots: @logic.rookery_letter_send_ok, @logic.rookery_letter_size_ok,
 * @num.rookery_mail_addr_fault, @logic.rookery_letter_record_ok and, used
 * as they are, @num.lockbox_days_left and @num.coffer_balance. */
#include "rookery_rules.h"

APP_TEST(rookery, 16, 26)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* the C rules' own edges */
    CHECK(rookery_consent_gap(false, 0, false) == 0 && rookery_consent_gap(true, 0, false) == 1 &&
              rookery_consent_gap(false, 1, false) == 1 &&
              rookery_consent_gap(true, 5, false) == 2 && rookery_consent_gap(true, 5, true) == 0 &&
              rookery_consent_gap(false, -3, false) == 0,
          "Rookery: the scopes a letter needs that have no consent (KR5)");
    CHECK(rookery_trash_days(1760000000, 1760000000) == ROOKERY_TRASH_DAYS &&
              rookery_trash_days(1760000000, 1760000000 + 30 * 86400) == 0 &&
              rookery_trash_days(1760000000, 1760000000 + 29 * 86400 + 1) == 0 &&
              rookery_trash_days(1760000000, 1760000000 + 29 * 86400) == 1 &&
              rookery_trash_days(-1, 0) == LOCKBOX_REFUSED &&
              rookery_trash_days(LOCKBOX_STAMP_MAX - 30 * 86400, 0) >= 0 &&
              rookery_trash_days(LOCKBOX_STAMP_MAX - 30 * 86400 + 1, 0) == LOCKBOX_REFUSED,
          "Rookery: the Trash's days left (KR6)");
    CHECK(ROOKERY_HEAD + ROOKERY_SUBJECT_MAX + ROOKERY_GAP + ROOKERY_BODY_MAX ==
              ROOKERY_PAYLOAD_MAX,
          "Rookery: the largest subject and body fill web4.post's payload exactly (KR2)");

    /* letter_send_ok (KR1): Eve = KRON = C, swept near every edge */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t k = wrand(0, 3);
        a[i] = k == 0 ? wrand(-2, 1) : 1;
        b[i] = k == 1 ? wrand(-100, 2200) : wrand(0, ROOKERY_PAYLOAD_MAX);
        c[i] = k == 2 ? wrand(-2, 2) : 0;
    }
    a[0] = 1, b[0] = 0, c[0] = 0;                       /* a full payload */
    a[1] = 1, b[1] = ROOKERY_PAYLOAD_MAX, c[1] = 0;     /* an empty one */
    a[2] = 0, b[2] = 100, c[2] = 0;                     /* the card did not verify */
    a[3] = 1, b[3] = -1, c[3] = 0;                      /* one byte over */
    a[4] = 1, b[4] = ROOKERY_PAYLOAD_MAX + 1, c[4] = 0; /* no payload measure */
    a[5] = 1, b[5] = 100, c[5] = 1;                     /* personal data, no consent */
    a[6] = 1, b[6] = 100, c[6] = 2;                     /* and money */
    a[7] = -1, b[7] = 100, c[7] = -1;                   /* no answer, no count */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(rookery_letter_send_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Rookery: the C send rule's own edges (KR1)");
    zxapp_arg_t sa[3] = {
        {"verified", a, WAVE_ROWS}, {"room", b, WAVE_ROWS}, {"consentgap", c, WAVE_ROWS}};
    CHECK(wave_same(app, "letter_send_ok", sa, 3, want, WAVE_ROWS, true),
          "Rookery: letter_send_ok, Eve = KRON = C");
    {
        /* a card answer of 2 is no answer: Eve says no, KRON reads it as verified */
        static const int64_t v[2] = {1, 2}, rm[2] = {100, 100}, gp[2] = {0, 0};
        zxapp_arg_t xa[3] = {{"verified", v, 2}, {"room", rm, 2}, {"consentgap", gp, 2}};
        CHECK(wave_lp(app, "letter_send_ok", xa, 3) == ZXAPP_BOTH &&
                  !rookery_letter_send_ok(2, 100, 0),
              "Rookery: letter_send_ok with a card answer of 2: Eve no, KRON yes: BOTH");
    }

    /* letter_size_ok (KR2): Eve = C, each field at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(0, ROOKERY_SUBJECT_MAX) : wrand(-5, ROOKERY_SUBJECT_MAX + 5);
        b[i] = i % 3 ? wrand(0, ROOKERY_BODY_MAX) : wrand(-5, ROOKERY_BODY_MAX + 5);
    }
    a[0] = 0, b[0] = 0;                                  /* empty */
    a[1] = ROOKERY_SUBJECT_MAX, b[1] = ROOKERY_BODY_MAX; /* the most */
    a[2] = ROOKERY_SUBJECT_MAX + 1, b[2] = 0;            /* subject over */
    a[3] = 0, b[3] = ROOKERY_BODY_MAX + 1;               /* body over */
    a[4] = -1, b[4] = 0;                                 /* no subject measure */
    a[5] = 0, b[5] = -1;                                 /* no body measure */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(rookery_letter_size_ok(a[i], b[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Rookery: the C size limits' own edges (KR2)");
    zxapp_arg_t za[2] = {{"subject", a, WAVE_ROWS}, {"content", b, WAVE_ROWS}};
    CHECK(wave_same(app, "letter_size_ok", za, 2, want, WAVE_ROWS, true),
          "Rookery: letter_size_ok, Eve = C");

    /* mail_addr_fault (KR3): Rust = KRON = C, over counts one address can give */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        if (i % 2) { /* one '@': local and domain fill the rest */
            a[i] = wrand(3, i % 4 == 1 ? 300 : 254);
            b[i] = 1;
            c[i] = wrand(0, a[i] - 1 < 80 ? a[i] - 1 : 80);
            d[i] = a[i] - 1 - c[i];
        } else { /* any number of '@' and parts that need not fill it */
            a[i] = wrand(0, 300);
            b[i] = wrand(0, a[i] < 3 ? a[i] : 3);
            c[i] = wrand(0, a[i] - b[i]);
            d[i] = wrand(0, a[i] - b[i] - c[i]);
        }
        e[i] = wrand(0, d[i] < 70 ? d[i] : 70);
        f[i] = i % 3 ? wrand(e[i] ? 1 : 0, e[i]) : wrand(0, e[i]);
        g[i] = i % 5 ? 0 : wrand(0, a[i]);
    }
    /* size, ats, local, domain, longest, shortest, bad */
    static const int64_t EDGE[12][7] = {
        {3, 1, 1, 1, 1, 1, 0},        /* a@b: the shortest that fits */
        {254, 1, 64, 189, 63, 61, 0}, /* the longest that fits */
        {2, 1, 1, 0, 0, 0, 0},        /* too short */
        {255, 1, 64, 190, 63, 1, 0},  /* too long */
        {5, 0, 5, 0, 0, 0, 0},        /* no '@' */
        {5, 2, 1, 1, 1, 1, 0},        /* two */
        {67, 1, 65, 1, 1, 1, 0},      /* local part past 64 */
        {4, 1, 0, 3, 1, 1, 0},        /* no local part */
        {3, 1, 2, 0, 0, 0, 0},        /* no domain */
        {6, 1, 1, 4, 1, 0, 0},        /* an empty label */
        {66, 1, 1, 64, 64, 64, 0},    /* a label past 63 */
        {5, 1, 3, 1, 1, 1, 1},        /* a byte not allowed */
    };
    static const int64_t EDGE_WANT[12] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6};
    for (uint32_t k = 0; k < 12; k++) {
        a[k] = EDGE[k][0], b[k] = EDGE[k][1], c[k] = EDGE[k][2], d[k] = EDGE[k][3];
        e[k] = EDGE[k][4], f[k] = EDGE[k][5], g[k] = EDGE[k][6];
    }
    bool edges = true;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        want[i] = rookery_addr_fault(a[i], b[i], c[i], d[i], e[i], f[i], g[i]);
        if (i < 12 && want[i] != EDGE_WANT[i]) edges = false;
        if (want[i] == ROOKERY_REFUSED) edges = false; /* every row is one address's counts */
    }
    CHECK(edges, "Rookery: the C address rule's own edges (KR3)");
    zxapp_arg_t aa[7] = {{"size", a, WAVE_ROWS},    {"ats", b, WAVE_ROWS},
                         {"local", c, WAVE_ROWS},   {"domain", d, WAVE_ROWS},
                         {"longest", e, WAVE_ROWS}, {"shortest", f, WAVE_ROWS},
                         {"bad", g, WAVE_ROWS}};
    CHECK(wave_same(app, "mail_addr_fault", aa, 7, want, WAVE_ROWS, false),
          "Rookery: mail_addr_fault, Rust = KRON = C");
    {
        /* counts no address can give: Rust refuses, KRON answers */
        static const int64_t BAD[4][7] = {
            {3, 1, 1, 1, 1, 2, 0},  /* a shortest label longer than the longest */
            {3, 1, 1, 1, 1, 1, -1}, /* a negative count */
            {3, 1, 2, 2, 1, 1, 0},  /* more parts than bytes */
            {5, 1, 1, 1, 3, 1, 0},  /* a label longer than its domain */
        };
        static const char *const NAME[7] = {"size",    "ats",      "local", "domain",
                                            "longest", "shortest", "bad"};
        for (uint32_t k = 0; k < 4; k++) {
            int64_t col[7][2];
            zxapp_arg_t xa[7];
            for (uint32_t j = 0; j < 7; j++) {
                col[j][0] = EDGE[0][j];
                col[j][1] = BAD[k][j];
                xa[j].name = NAME[j];
                xa[j].v = col[j];
                xa[j].n = 2;
            }
            CHECK(wave_lp(app, "mail_addr_fault", xa, 7) == ZXAPP_BOTH &&
                      rookery_addr_fault(BAD[k][0], BAD[k][1], BAD[k][2], BAD[k][3], BAD[k][4],
                                         BAD[k][5], BAD[k][6]) == ROOKERY_REFUSED,
                  "Rookery: mail_addr_fault case %u: Rust refuses, KRON answers: BOTH", k);
        }
    }

    /* letter_record_ok (KR4): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-11, 11);
        b[i] = i % 2 ? wrand(0, UINT32_MAX) : wrand(-11000000000, 11000000000);
        c[i] = i % 3 ? wrand(1, UINT32_MAX) : wrand(-11000000000, 11000000000);
        d[i] = i % 4 ? wrand(0, ROOKERY_PAYLOAD_MAX) : wrand(-11000, 11000);
        e[i] = i % 5 ? wrand(1600000000, 2000000000) : wrand(-1100000000000, 1100000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0; /* every least */
    a[1] = ROOKERY_FOLDER_FIELD, b[1] = ROOKERY_ID_FIELD, c[1] = ROOKERY_ID_FIELD;
    d[1] = ROOKERY_SIZE_FIELD, e[1] = ROOKERY_STAMP_FIELD;                   /* every most */
    a[2] = ROOKERY_FOLDER_FIELD + 1, b[2] = 1, c[2] = 1, d[2] = 0, e[2] = 0; /* folder past i1 */
    a[3] = 4, b[3] = ROOKERY_ID_FIELD + 1, c[3] = 1, d[3] = 0, e[3] = 0;     /* thread past i10 */
    a[4] = 4, b[4] = 1, c[4] = ROOKERY_ID_FIELD + 1, d[4] = 0, e[4] = 0;     /* letter past i10 */
    a[5] = 4, b[5] = 1, c[5] = 1, d[5] = ROOKERY_SIZE_FIELD + 1, e[5] = 0;   /* size past i4 */
    a[6] = 4, b[6] = 1, c[6] = 1, d[6] = 0, e[6] = ROOKERY_STAMP_FIELD + 1;  /* stamp past i12 */
    a[7] = -9, b[7] = -1, c[7] = -1, d[7] = -ROOKERY_SIZE_FIELD, e[7] = -1;  /* sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(rookery_letter_record_ok(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Rookery: the C record's own edges (KR4)");
    zxapp_arg_t ra[5] = {{"folder", a, WAVE_ROWS},
                         {"thread", b, WAVE_ROWS},
                         {"letter", c, WAVE_ROWS},
                         {"size", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "letter_record_ok", ra, 5, want, WAVE_ROWS, true),
          "Rookery: letter_record_ok, MYRA = C");

    /* lockbox.days_left over the Trash's 30 days (KR6): C++ = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t put = wrand(1600000000, 2000000000);
        a[i] = put + (int64_t) ROOKERY_TRASH_DAYS * LOCKBOX_DAY;
        b[i] = put + wrand(0, 40 * 86400);
        want[i] = rookery_trash_days(put, b[i]);
    }
    zxapp_arg_t la[2] = {{"expires", a, WAVE_ROWS}, {"now", b, WAVE_ROWS}};
    CHECK(wave_same(app, "lockbox.days_left", la, 2, want, WAVE_ROWS, false),
          "Rookery: the Trash's days through lockbox.days_left, C++ = KRON = C");

    /* coffer.balance: what an account holds after a letter's amount left it */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-999999999999, 999999999999);
        b[i] = -wrand(1, 999999999999);
        want[i] = a[i] + b[i];
    }
    zxapp_arg_t ca[2] = {{"before", a, WAVE_ROWS}, {"moved", b, WAVE_ROWS}};
    CHECK(wave_same(app, "coffer.balance", ca, 2, want, WAVE_ROWS, false),
          "Rookery: the balance after the letter's amount, Sutra = Trebago = C");
}
