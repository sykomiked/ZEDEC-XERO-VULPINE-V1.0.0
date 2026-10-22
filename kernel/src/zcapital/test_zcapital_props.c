/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zcapital_props.c — property tests for the nine capital forms
 * (zcapital.c): exchange conserves, the Crown forms never move, and the
 * offer/seek book never fills more than was posted or across one party.
 *
 * Built twice by the Makefile: with TEST_HOST (surplus_real_t is double) and
 * without (Q32.32 integers, what the kernel runs). Every amount here is a
 * whole number of units, so both representations are exact and the same
 * expectations hold. */
#include <stdio.h>
#include <string.h>
#include "zcapital.h"

static int failures = 0;
static unsigned long checks = 0;
#define CHECK(cond, msg)                                                                           \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(cond)) {                                                                             \
            if (failures < 40) printf("  FAIL: %s (line %d)\n", msg, __LINE__);                    \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

static uint64_t rs = 0x9FB21C651E98DF25ULL;
static uint32_t rnd(uint32_t n)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return n ? (uint32_t) (rs % n) : 0;
}

#define U(x) SR_FROM_INT(x)
static int eq(surplus_real_t a, surplus_real_t b)
{
    return SR_CMP(a, b) == 0;
}
static int crown(uint32_t f)
{
    return f >= ZCAP_SOCIAL && f <= ZCAP_SPIRITUAL;
}

static void test_forms(void)
{
    static const zcap_authority_t who[ZCAP_FORM_COUNT] = {
        ZCAP_AUTH_MINISTRY, ZCAP_AUTH_MINISTRY, ZCAP_AUTH_MINISTRY,
        ZCAP_AUTH_MINISTRY, ZCAP_AUTH_CROWN,    ZCAP_AUTH_CROWN,
        ZCAP_AUTH_CROWN,    ZCAP_AUTH_CROWN,    ZCAP_AUTH_COJURIS};
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT + 3; f++) {
        zcap_form_t form = (zcap_form_t) f;
        CHECK(zcap_who_owns_this(form) == (f < ZCAP_FORM_COUNT ? who[f] : ZCAP_AUTH_COJURIS),
              "jurisdiction table");
        CHECK(zcap_is_priceable(form) == (f < ZCAP_FORM_COUNT && !crown(f)),
              "priceable = not Crown, and in range");
        CHECK(zcap_form_is_crown(f) == crown(f), "zcap_form_is_crown agrees");
    }
    CHECK(eq(zcap_gratuity(U(100)), U(11)) && eq(zcap_gratuity(U(0)), U(0)) &&
              eq(zcap_gratuity(U(1000)), U(110)),
          "gratuity is 11%");
}

static void test_exchange(void)
{
    for (int round = 0; round < 200000; round++) {
        zcap_vec_t v;
        int64_t total = 0, want[ZCAP_FORM_COUNT];
        for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++) {
            want[f] = rnd(1000);
            v.bal[f] = U(want[f]);
            total += want[f];
        }
        uint32_t from = rnd(ZCAP_FORM_COUNT + 1), to = rnd(ZCAP_FORM_COUNT + 1);
        int64_t amt = (int64_t) rnd(1200) - 100;
        zcap_result_t r = zcap_exchange(&v, (zcap_form_t) from, (zcap_form_t) to, U(amt));
        zcap_result_t expect;
        if (from >= ZCAP_FORM_COUNT || to >= ZCAP_FORM_COUNT)
            expect = ZCAP_INALIENABLE;
        else if (amt < 0)
            expect = ZCAP_INSUFFICIENT;
        else if (crown(from) || crown(to))
            expect = ZCAP_INALIENABLE;
        else if (want[from] < amt)
            expect = ZCAP_INSUFFICIENT;
        else
            expect = ZCAP_OK;
        CHECK(r == expect, "exchange outcome = rule order: range, sign, Crown, balance");
        if (r == ZCAP_OK) {
            want[from] -= amt;
            want[to] += amt;
        }
        int64_t after = 0;
        for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++) {
            CHECK(eq(v.bal[f], U(want[f])), "balances = reference (a refusal moves nothing)");
            after += want[f];
        }
        CHECK(after == total, "exchange conserves the total");
    }
    zcap_vec_t v = {{0}};
    v.bal[ZCAP_FINANCIAL] = U(5);
    CHECK(zcap_exchange(0, ZCAP_FINANCIAL, ZCAP_HUMAN, U(1)) == ZCAP_INSUFFICIENT, "NULL vector");
    CHECK(zcap_exchange(&v, ZCAP_FINANCIAL, ZCAP_HUMAN, U(5)) == ZCAP_OK &&
              eq(v.bal[ZCAP_FINANCIAL], U(0)) && eq(v.bal[ZCAP_HUMAN], U(5)),
          "the whole balance can move");
    CHECK(zcap_exchange(&v, ZCAP_HUMAN, ZCAP_HUMAN, U(5)) == ZCAP_OK && eq(v.bal[ZCAP_HUMAN], U(5)),
          "to itself: no change");
    CHECK(zcap_exchange(&v, ZCAP_HUMAN, ZCAP_SYSTEM, U(0)) == ZCAP_OK, "zero moves nothing");
    CHECK(zcap_exchange(&v, ZCAP_HUMAN, ZCAP_SOCIAL, U(-1)) == ZCAP_INSUFFICIENT,
          "a negative amount is refused before the Crown check");
}

/* the book: per form, offers and seeks; a match fills min(offer, seek) of the
 * first live offer against the first live seek of a different party */
typedef struct {
    int64_t u;
    uint32_t p;
    int live;
} slot_t;

static void test_market(void)
{
    static zmarket_t m;
    for (int round = 0; round < 3000; round++) {
        zmarket_init(&m);
        uint32_t form = rnd(ZCAP_FORM_COUNT);
        slot_t off[ZMARKET_SLOTS], sk[ZMARKET_SLOTS];
        uint32_t no = 0, ns = 0;
        int64_t posted_o[8] = {0}, posted_s[8] = {0}, given[8] = {0}, taken[8] = {0};
        for (int step = 0; step < 120; step++) {
            uint32_t op = rnd(3), party = rnd(4);
            int64_t u = (int64_t) rnd(50) - 2;
            if (op < 2) {
                int is_offer = op == 0;
                slot_t *book = is_offer ? off : sk;
                uint32_t *n = is_offer ? &no : &ns;
                int32_t idx = is_offer ? zmarket_offer(&m, (zcap_form_t) form, U(u), party)
                                       : zmarket_seek(&m, (zcap_form_t) form, U(u), party);
                if (u <= 0) {
                    CHECK(idx == -1, "no zero or negative posts");
                    continue;
                }
                if (*n >= ZMARKET_SLOTS) { /* compact: keep live entries in order */
                    uint32_t w = 0;
                    for (uint32_t r = 0; r < *n; r++)
                        if (book[r].live) book[w++] = book[r];
                    *n = w;
                }
                if (*n >= ZMARKET_SLOTS) {
                    CHECK(idx == -1, "a full book of live entries refuses");
                    continue;
                }
                CHECK(idx == (int32_t) *n, "post lands at the next slot");
                book[*n].u = u;
                book[*n].p = party;
                book[*n].live = 1;
                (*n)++;
                if (is_offer)
                    posted_o[party] += u;
                else
                    posted_s[party] += u;
            } else {
                zcap_commitment_t c;
                int oi = -1, si = -1;
                for (uint32_t i = 0; i < no && oi < 0; i++)
                    if (off[i].live) oi = (int) i;
                if (oi >= 0)
                    for (uint32_t i = 0; i < ns && si < 0; i++)
                        if (sk[i].live && sk[i].p != off[oi].p) si = (int) i;
                bool got = zmarket_match(&m, (zcap_form_t) form, &c);
                CHECK(got == (oi >= 0 && si >= 0), "match iff first offer has a foreign seek");
                if (!got) continue;
                int64_t fill = off[oi].u < sk[si].u ? off[oi].u : sk[si].u;
                CHECK(c.form == (zcap_form_t) form && eq(c.units, U(fill)) &&
                          c.giver == off[oi].p && c.taker == sk[si].p,
                      "commitment = min(offer, seek) between the two parties");
                CHECK(c.giver != c.taker, "never a self-trade");
                off[oi].u -= fill;
                sk[si].u -= fill;
                if (!off[oi].u) off[oi].live = 0;
                if (!sk[si].u) sk[si].live = 0;
                given[c.giver] += fill;
                taken[c.taker] += fill;
            }
            CHECK(m.offer_count[form] == no && m.seek_count[form] == ns, "counts = reference");
            for (uint32_t i = 0; i < no; i++)
                CHECK(m.offers[form][i].active == off[i].live &&
                          (!off[i].live || (eq(m.offers[form][i].units, U(off[i].u)) &&
                                            m.offers[form][i].party == off[i].p)),
                      "offer book = reference");
            for (uint32_t i = 0; i < ns; i++)
                CHECK(m.seeks[form][i].active == sk[i].live &&
                          (!sk[i].live || (eq(m.seeks[form][i].units, U(sk[i].u)) &&
                                           m.seeks[form][i].party == sk[i].p)),
                      "seek book = reference");
        }
        for (uint32_t p = 0; p < 4; p++)
            CHECK(given[p] <= posted_o[p] && taken[p] <= posted_s[p],
                  "nobody gives more than offered or takes more than sought");
        for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++)
            if (f != form)
                CHECK(m.offer_count[f] == 0 && m.seek_count[f] == 0, "forms are separate");
    }
    zcap_commitment_t c;
    zmarket_init(0);
    zmarket_init(&m);
    CHECK(zmarket_offer(0, ZCAP_HUMAN, U(1), 1) == -1 && zmarket_seek(0, ZCAP_HUMAN, U(1), 1) == -1,
          "post NULL");
    CHECK(zmarket_offer(&m, (zcap_form_t) ZCAP_FORM_COUNT, U(1), 1) == -1 &&
              zmarket_seek(&m, (zcap_form_t) ZCAP_FORM_COUNT, U(1), 1) == -1,
          "post past the forms");
    CHECK(!zmarket_match(0, ZCAP_HUMAN, &c) && !zmarket_match(&m, ZCAP_HUMAN, 0) &&
              !zmarket_match(&m, (zcap_form_t) ZCAP_FORM_COUNT, &c),
          "match NULL / past the forms");
    CHECK(!zmarket_match(&m, ZCAP_HUMAN, &c), "an empty book matches nothing");
    zmarket_seek(&m, ZCAP_HUMAN, U(3), 1);
    CHECK(!zmarket_match(&m, ZCAP_HUMAN, &c), "a seek with no offer matches nothing");
}

/* the edges of the book: a dirty struct, a full book compacted while the next
 * form's book holds live posts, a full book of wash trades, a spent book */
static void test_book_edges(void)
{
    static zmarket_t m;
    memset(&m, 0xA5, sizeof m);
    zmarket_init(&m);
    int clean = 1;
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++) {
        clean &= m.offer_count[f] == 0 && m.seek_count[f] == 0;
        for (uint32_t i = 0; i < ZMARKET_SLOTS; i++)
            clean &= !m.offers[f][i].active && eq(m.offers[f][i].units, U(0)) &&
                     m.offers[f][i].party == 0 && !m.seeks[f][i].active &&
                     eq(m.seeks[f][i].units, U(0)) && m.seeks[f][i].party == 0;
    }
    CHECK(clean, "init clears every entry of every form, whatever the memory held");

    /* form 2: 32 offers (parties 20..51), 32 seeks by party 1; form 3 has a
     * live offer in slot 0, right after form 2's last slot */
    for (uint32_t i = 0; i < ZMARKET_SLOTS; i++) {
        zmarket_offer(&m, ZCAP_INTELLECTUAL, U(1), 20 + i);
        zmarket_seek(&m, ZCAP_INTELLECTUAL, U(1), 1);
    }
    CHECK(zmarket_offer(&m, ZCAP_HUMAN, U(4), 77) == 0, "the next form's first post");
    zcap_commitment_t c;
    for (int k = 0; k < 5; k++) zmarket_match(&m, ZCAP_INTELLECTUAL, &c);
    CHECK(zmarket_offer(&m, ZCAP_INTELLECTUAL, U(3), 99) == 27,
          "a full book compacts: 27 live, the new post at 27");
    int order = 1;
    for (uint32_t i = 0; i < 27; i++)
        order &=
            m.offers[ZCAP_INTELLECTUAL][i].active && m.offers[ZCAP_INTELLECTUAL][i].party == 25 + i;
    CHECK(order && m.offers[ZCAP_INTELLECTUAL][27].party == 99 &&
              m.offer_count[ZCAP_INTELLECTUAL] == 28,
          "compaction keeps the live posts in order and nothing else");
    int cleared = 1;
    for (uint32_t i = 28; i < ZMARKET_SLOTS; i++)
        cleared &= !m.offers[ZCAP_INTELLECTUAL][i].active &&
                   m.offers[ZCAP_INTELLECTUAL][i].party == 0 &&
                   eq(m.offers[ZCAP_INTELLECTUAL][i].units, U(0));
    CHECK(cleared, "the slots compaction frees are cleared");
    CHECK(m.offers[ZCAP_HUMAN][0].active && m.offers[ZCAP_HUMAN][0].party == 77 &&
              eq(m.offers[ZCAP_HUMAN][0].units, U(4)) && m.offer_count[ZCAP_HUMAN] == 1,
          "compaction never touches the next form's book");

    /* form 6: a full seek book of one party's wash seeks; form 7 holds a
     * foreign seek just past it */
    for (uint32_t i = 0; i < ZMARKET_SLOTS; i++) zmarket_seek(&m, ZCAP_CULTURAL, U(5), 9);
    zmarket_offer(&m, ZCAP_CULTURAL, U(5), 9);
    zmarket_seek(&m, ZCAP_SPIRITUAL, U(5), 50);
    CHECK(!zmarket_match(&m, ZCAP_CULTURAL, &c), "a full book of wash seeks matches nothing");
    CHECK(m.seeks[ZCAP_SPIRITUAL][0].active && eq(m.seeks[ZCAP_SPIRITUAL][0].units, U(5)),
          "and never reaches into the next form's seeks");

    /* form 0: every offer spent; form 1's first offer is live */
    for (uint32_t i = 0; i < ZMARKET_SLOTS; i++) {
        zmarket_offer(&m, ZCAP_FINANCIAL, U(1), 30);
        zmarket_seek(&m, ZCAP_FINANCIAL, U(1), 31);
    }
    for (uint32_t i = 0; i < ZMARKET_SLOTS; i++) zmarket_match(&m, ZCAP_FINANCIAL, &c);
    zmarket_offer(&m, ZCAP_MANUFACTURED, U(2), 40);
    zmarket_seek(&m, ZCAP_FINANCIAL, U(1), 31); /* compacts the spent seeks, then posts */
    CHECK(m.offer_count[ZCAP_FINANCIAL] == ZMARKET_SLOTS && !zmarket_match(&m, ZCAP_FINANCIAL, &c),
          "a book of spent offers matches nothing, whatever lies past it");
    CHECK(m.offers[ZCAP_MANUFACTURED][0].active, "the next form's offer is untouched");
    CHECK(!zmarket_match(&m, (zcap_form_t) ZCAP_FORM_COUNT, &c),
          "match past the forms, books full");
}

int main(void)
{
    printf("test_zcapital_props: nine capital forms, exchange and book\n");
    test_forms();
    test_exchange();
    test_market();
    test_book_edges();
    printf("%lu checks, %d failures\n", checks, failures);
    if (failures) {
        printf("[FAIL] test_zcapital_props\n");
        return 1;
    }
    printf("[PASS] test_zcapital_props\n");
    return 0;
}
