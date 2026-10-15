/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_allot_view.c — host tests for allot_view.h (ASan/UBSan). */
#include "allot_view.h"
#include <stdio.h>
#include <string.h>

void sha3_256(const uint8_t *in, size_t len, uint8_t out[32]);

static unsigned g_pass, g_fail;
#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (cond) {                                                                                \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static void ident(const char *tag, uint8_t i, uint8_t out[32])
{
    uint8_t b[64];
    size_t n = strlen(tag);
    memcpy(b, tag, n);
    b[n] = i;
    sha3_256(b, n + 1, out);
}

static allot_range_t g_rg[64];

int main(void)
{
    allot_config_t c;
    allot_config_default(&c);
    allot_view_t v;
    uint8_t a[32], b[32], fund[32], p[32], sec[32], cm[32];
    ident("alice", 0, a);
    ident("bob", 0, b);
    ident("fund", 0, fund);
    ident("party", 0, p);
    ident("secret", 0, sec);

    /* V2: switch off -> read-only history, no entry prompt */
    CHECK(allot_view_mode(&c) == ALLOT_VIEW_HISTORY && allot_view_mode(NULL) == ALLOT_VIEW_HISTORY,
          "V2: history mode while the switch is off");
    CHECK(allot_view_build(&c, NULL, NULL, a, &v) == ALLOT_OK && v.mode == ALLOT_VIEW_HISTORY &&
              v.action == NULL && !v.can_register && !strcmp(v.note, ALLOT_UI_HISTORY_NOTE) &&
              !strcmp(v.header, "The Sicilian Crown Allotment"),
          "V2: no action, no registration, the history note");

    /* V1: switch on -> entry view with the owner's wording */
    c.jurisdiction_permits = true;
    c.fib_index = 3; /* F(3) = 2 ballots close the epoch */
    allot_t r;
    CHECK(allot_init(&r, &c, 1, NULL, g_rg, 64) == ALLOT_OK, "init");
    allot_commitment(1, p, sec, cm);
    allot_commit(&r, p, cm);
    CHECK(allot_view_build(&c, NULL, NULL, a, &v) == ALLOT_ERR_ARG, "V1 needs the allotment");
    CHECK(allot_view_build(&c, &r, NULL, a, &v) == ALLOT_OK && v.mode == ALLOT_VIEW_ENTRY &&
              !strcmp(v.action, "Register Sortition Ballot") && v.can_register &&
              !strcmp(v.note, ALLOT_UI_NO_COST) && strstr(v.subtext, "Section 6.3") != NULL,
          "V1: the register action is offered at no cost");
    CHECK(allot_view_build(&c, &r, NULL, NULL, &v) == ALLOT_OK && !v.can_register,
          "V1: not signed in, nothing to register");
    allot_endow(&r, fund, 500);
    allot_free_entry(&r, a, NULL);
    CHECK(allot_view_build(&c, &r, NULL, a, &v) == ALLOT_OK && !v.can_register && v.ballots == 1 &&
              v.endowment == 500 && v.threshold == 2,
          "V1: alice holds her ballot; counts shown");
    CHECK(allot_view_build(&c, &r, NULL, b, &v) == ALLOT_OK && v.can_register,
          "V1: bob can still register");
    allot_free_entry(&r, b, NULL);
    CHECK(allot_view_build(&c, &r, NULL, b, &v) == ALLOT_OK && !v.can_register,
          "V1: closed epoch, nothing to register");

    /* V3: history ring */
    allot_stewardship_t rec[2];
    allot_history_t h;
    CHECK(allot_history_init(&h, rec, 0) == ALLOT_ERR_ARG &&
              allot_history_init(&h, rec, 2) == ALLOT_OK,
          "V3: init");
    allot_settlement_t s;
    allot_reveal(&r, p, sec);
    CHECK(allot_select(&r, &s) == ALLOT_OK && allot_history_note(&h, &s) == ALLOT_OK, "V3: note");
    const allot_stewardship_t *e = allot_history_at(&h, 0);
    CHECK(e && e->epoch == 1 && e->endowment == 500 && !memcmp(e->steward, s.steward, 32) &&
              !memcmp(e->link, s.link, 32) && !allot_history_at(&h, 1),
          "V3: the record copies the settlement");
    allot_settlement_t s2 = s, s3 = s;
    s2.epoch = 2;
    s3.epoch = 3;
    allot_history_note(&h, &s2);
    allot_history_note(&h, &s3);
    CHECK(h.len == 2 && allot_history_at(&h, 0)->epoch == 2 && allot_history_at(&h, 1)->epoch == 3,
          "V3: full ring drops the oldest, keeps order");
    c.jurisdiction_permits = false;
    CHECK(allot_view_build(&c, NULL, &h, a, &v) == ALLOT_OK && v.history_len == 2 && !v.action,
          "V2: read-only history listed");
    CHECK(allot_history_note(NULL, &s) == ALLOT_ERR_ARG &&
              allot_view_build(NULL, &r, &h, a, &v) == ALLOT_ERR_ARG,
          "null args");
    printf("%u passed, %u failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
