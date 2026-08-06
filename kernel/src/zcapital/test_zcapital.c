/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_zcapital.c — every assertion is anchored to an EXTERNAL truth: the
 * proposal Sec 3.5 jurisdiction table, the inalienability rule ("you cannot buy
 * a language"), a value-conservation identity, and a hand-computed 11% gratuity.
 * None of it asserts "the code returned what the code returned". */
#include <stdio.h>
#include <assert.h>
#include "zcapital.h"

static int g_asserts = 0;
#define CHECK(cond) do { \
    g_asserts++; \
    if (!(cond)) { printf("FAIL line %d: %s\n", __LINE__, #cond); return 1; } \
} while (0)

/* Q32.32/double equality good enough for exact-integer anchors. */
static int sr_eq(surplus_real_t a, surplus_real_t b) { return SR_CMP(a, b) == 0; }

int main(void) {
    /* ---- Anchor 1: jurisdiction table maps all 9 forms per Sec 3.5 ---- */
    /* Ministry: Financial, Manufactured, Intellectual, Human */
    CHECK(zcap_who_owns_this(ZCAP_FINANCIAL)    == ZCAP_AUTH_MINISTRY);
    CHECK(zcap_who_owns_this(ZCAP_MANUFACTURED) == ZCAP_AUTH_MINISTRY);
    CHECK(zcap_who_owns_this(ZCAP_INTELLECTUAL) == ZCAP_AUTH_MINISTRY);
    CHECK(zcap_who_owns_this(ZCAP_HUMAN)        == ZCAP_AUTH_MINISTRY);
    /* Crown: Social, Natural, Cultural, Spiritual */
    CHECK(zcap_who_owns_this(ZCAP_SOCIAL)    == ZCAP_AUTH_CROWN);
    CHECK(zcap_who_owns_this(ZCAP_NATURAL)   == ZCAP_AUTH_CROWN);
    CHECK(zcap_who_owns_this(ZCAP_CULTURAL)  == ZCAP_AUTH_CROWN);
    CHECK(zcap_who_owns_this(ZCAP_SPIRITUAL) == ZCAP_AUTH_CROWN);
    /* Co-Juris: System */
    CHECK(zcap_who_owns_this(ZCAP_SYSTEM)    == ZCAP_AUTH_COJURIS);

    /* ---- Anchor 2: priceability — false for the 4 Crown forms, true else ---- */
    CHECK(zcap_is_priceable(ZCAP_FINANCIAL)    == true);
    CHECK(zcap_is_priceable(ZCAP_MANUFACTURED) == true);
    CHECK(zcap_is_priceable(ZCAP_INTELLECTUAL) == true);
    CHECK(zcap_is_priceable(ZCAP_HUMAN)        == true);
    CHECK(zcap_is_priceable(ZCAP_SYSTEM)       == true);
    CHECK(zcap_is_priceable(ZCAP_SOCIAL)    == false);
    CHECK(zcap_is_priceable(ZCAP_NATURAL)   == false);
    CHECK(zcap_is_priceable(ZCAP_CULTURAL)  == false);
    CHECK(zcap_is_priceable(ZCAP_SPIRITUAL) == false);

    /* ---- Anchor 3: buying a Crown form is refused, ZCAP_INALIENABLE ---- */
    {
        zcap_vec_t v = {0};
        v.bal[ZCAP_FINANCIAL] = SR_FROM_INT(1000);
        zcap_result_t r = zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_CULTURAL,
                                        SR_FROM_INT(10));
        CHECK(r == ZCAP_INALIENABLE);
        /* refusal leaves the holding untouched */
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], SR_FROM_INT(1000)));
        CHECK(sr_eq(v.bal[ZCAP_CULTURAL], SR_ZERO));
    }

    /* ---- Anchor 4: a valid exchange returns OK and CONSERVES the sum ---- */
    {
        zcap_vec_t v = {0};
        v.bal[ZCAP_FINANCIAL]    = SR_FROM_INT(100);
        v.bal[ZCAP_MANUFACTURED] = SR_FROM_INT(30);
        surplus_real_t sum_before = SR_ADD(v.bal[ZCAP_FINANCIAL],
                                           v.bal[ZCAP_MANUFACTURED]);
        zcap_result_t r = zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_MANUFACTURED,
                                        SR_FROM_INT(40));
        CHECK(r == ZCAP_OK);
        /* moved to the token: 100-40=60, 30+40=70 */
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], SR_FROM_INT(60)));
        CHECK(sr_eq(v.bal[ZCAP_MANUFACTURED], SR_FROM_INT(70)));
        /* conservation identity: the sum is invariant */
        surplus_real_t sum_after = SR_ADD(v.bal[ZCAP_FINANCIAL],
                                          v.bal[ZCAP_MANUFACTURED]);
        CHECK(sr_eq(sum_before, sum_after));
    }

    /* ---- Anchor 5: overdraw is refused, ZCAP_INSUFFICIENT ---- */
    {
        zcap_vec_t v = {0};
        v.bal[ZCAP_FINANCIAL] = SR_FROM_INT(5);
        zcap_result_t r = zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_MANUFACTURED,
                                        SR_FROM_INT(6));
        CHECK(r == ZCAP_INSUFFICIENT);
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], SR_FROM_INT(5)));  /* untouched */
        CHECK(sr_eq(v.bal[ZCAP_MANUFACTURED], SR_ZERO));
    }

    /* ---- Anchor 6: gratuity is exactly 11%, principal untouched ---- */
    {
        surplus_real_t principal = SR_FROM_INT(100);
        surplus_real_t tip = zcap_gratuity(principal);
        CHECK(sr_eq(tip, SR_FROM_INT(11)));            /* hand-computed 11% of 100 */
        CHECK(sr_eq(principal, SR_FROM_INT(100)));     /* principal never deducted */
    }

    /* ---- Anchor 6b: SELLING a Crown form is refused too, both directions ---- */
    {
        zcap_vec_t v = {0};
        v.bal[ZCAP_CULTURAL] = SR_FROM_INT(1000);  /* even if you "hold" it... */
        zcap_result_t r = zcap_exchange(&v, ZCAP_CULTURAL, ZCAP_FINANCIAL,
                                        SR_FROM_INT(10));
        CHECK(r == ZCAP_INALIENABLE);              /* ...you cannot sell a language */
        CHECK(sr_eq(v.bal[ZCAP_CULTURAL], SR_FROM_INT(1000)));  /* untouched */
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], SR_ZERO));
    }

    /* ---- Anchor 6c: a NEGATIVE amount cannot mint value (the reverse-move hole) ---- */
    {
        zcap_vec_t v = {0};
        v.bal[ZCAP_FINANCIAL]    = SR_FROM_INT(100);
        v.bal[ZCAP_MANUFACTURED] = SR_FROM_INT(30);
        zcap_result_t r = zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_MANUFACTURED,
                                        SR_FROM_INT(-40));
        CHECK(r == ZCAP_INSUFFICIENT);
        /* the holding is left EXACTLY as it was — no inflation, no negative balance */
        CHECK(sr_eq(v.bal[ZCAP_FINANCIAL], SR_FROM_INT(100)));
        CHECK(sr_eq(v.bal[ZCAP_MANUFACTURED], SR_FROM_INT(30)));
    }

    /* ---- Anchor 7: market pairs a compatible offer+seek, else false ---- */
    {
        zmarket_t m;
        zmarket_init(&m);
        zcap_commitment_t c;

        /* empty book: no match, and out must be left untouched */
        c.form = ZCAP_SYSTEM; c.units = SR_FROM_INT(-1);
        c.giver = 999; c.taker = 999;
        CHECK(zmarket_match(&m, ZCAP_INTELLECTUAL, &c) == false);
        CHECK(c.giver == 999 && c.taker == 999);  /* untouched on no-match */

        /* one seek but no offer: still no match */
        zmarket_seek(&m, ZCAP_INTELLECTUAL, SR_FROM_INT(7), 20);
        CHECK(zmarket_match(&m, ZCAP_INTELLECTUAL, &c) == false);

        /* add the offer: now they pair, FIFO, with the right form/units/parties */
        int32_t oi = zmarket_offer(&m, ZCAP_INTELLECTUAL, SR_FROM_INT(7), 10);
        CHECK(oi >= 0);
        CHECK(zmarket_match(&m, ZCAP_INTELLECTUAL, &c) == true);
        CHECK(c.form == ZCAP_INTELLECTUAL);
        CHECK(sr_eq(c.units, SR_FROM_INT(7)));
        CHECK(c.giver == 10);   /* the offerer gives */
        CHECK(c.taker == 20);   /* the seeker takes */

        /* both consumed: a second match finds no counterpart */
        CHECK(zmarket_match(&m, ZCAP_INTELLECTUAL, &c) == false);
    }

    printf("zcapital: all %d assertions passed.\n", g_asserts);
    return 0;
}
