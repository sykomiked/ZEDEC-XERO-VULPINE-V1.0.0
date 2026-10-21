/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_zcapital.c: differential probe for kernel/src/zcapital/zcapital.c.
 * Built twice by probes.json: Q32.32 (the kernel) and -DTEST_HOST (double). */
#include <string.h>
#include "probe_common.h"
#include "zcapital.h"

static unsigned long long bits(surplus_real_t v) {
    unsigned long long u = 0;
    memcpy(&u, &v, sizeof v < sizeof u ? sizeof v : sizeof u);
    return u;
}

static zmarket_t mk;

static void dump_book(const char *tag) {
    PROBE_STATE(tag, mk);
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f++)
        printf(" f%u oc%u sc%u", f, mk.offer_count[f], mk.seek_count[f]);
    printf("\n");
}

int main(void) {
    printf("sizeof real %zu\n", sizeof(surplus_real_t));
    for (int f = -1; f <= (int)ZCAP_FORM_COUNT + 1; f++)
        printf("form %d owner %d priceable %d\n", f, (int)zcap_who_owns_this((zcap_form_t)f),
               (int)zcap_is_priceable((zcap_form_t)f));
    static const int64_t amts[] = {-5, -1, 0, 1, 2, 7, 99, 100, 101, 1000};
    for (unsigned a = 0; a < sizeof amts / sizeof amts[0]; a++)
        for (int from = -1; from <= (int)ZCAP_FORM_COUNT; from++)
            for (int to = -1; to <= (int)ZCAP_FORM_COUNT; to++) {
                zcap_vec_t v;
                for (uint32_t i = 0; i < ZCAP_FORM_COUNT; i++) v.bal[i] = SR_FROM_INT(100 + i);
                int r = (int)zcap_exchange(&v, (zcap_form_t)from, (zcap_form_t)to, SR_FROM_INT(amts[a]));
                printf("xchg %lld %d->%d r%d", (long long)amts[a], from, to, r);
                for (uint32_t i = 0; i < ZCAP_FORM_COUNT; i++) printf(" %llx", bits(v.bal[i]));
                printf("\n");
            }
    printf("xchg null %d\n", (int)zcap_exchange(NULL, ZCAP_FINANCIAL, ZCAP_HUMAN, SR_ONE));
    for (int64_t g = -3; g <= 300; g += 37) printf("grat %lld %llx\n", (long long)g, bits(zcap_gratuity(SR_FROM_INT(g))));

    memset(&mk, 0x5A, sizeof mk);
    zmarket_init(&mk);
    dump_book("init");
    zmarket_init(NULL);
    printf("null %d %d %d\n", zmarket_offer(NULL, 0, SR_ONE, 1), zmarket_seek(NULL, 0, SR_ONE, 1),
           (int)zmarket_match(NULL, 0, NULL));

    /* fill one form's books to the brim and past it, then drain */
    for (uint32_t f = 0; f < ZCAP_FORM_COUNT; f += 4) {
        for (uint32_t i = 0; i < ZMARKET_SLOTS + 2; i++)
            printf("full f%u o%u %d s%u %d\n", f, i, zmarket_offer(&mk, f, SR_FROM_INT(1 + i % 3), 10 + i),
                   i, zmarket_seek(&mk, f, SR_FROM_INT(2), 10 + (i + 1) % 5));
        dump_book("full");
        zcap_commitment_t c;
        for (int n = 0; n < 80; n++) {
            memset(&c, 0, sizeof c);
            bool ok = zmarket_match(&mk, f, &c);
            printf("drain f%u %d %d form%d u%llx g%u t%u\n", f, n, (int)ok, (int)c.form, bits(c.units), c.giver, c.taker);
            if (!ok) break;
        }
        dump_book("drained");
        for (uint32_t i = 0; i < ZMARKET_SLOTS + 2; i++)
            printf("refill f%u %d %d\n", f, zmarket_offer(&mk, f, SR_ONE, 7), zmarket_seek(&mk, f, SR_ONE, 7));
        dump_book("refill");
    }

    /* a full book, partly matched, then one more post: compact() runs with
     * both live and spent entries; a full seek book where no seek can match */
    memset(&mk, 0x5A, sizeof mk);
    zmarket_init(&mk);
    for (uint32_t i = 0; i < ZMARKET_SLOTS; i++) {
        zmarket_offer(&mk, 2, SR_FROM_INT(1), 20 + i);
        zmarket_seek(&mk, 2, SR_FROM_INT(1), 1);
        zmarket_seek(&mk, 3, SR_FROM_INT(5), 9);
    }
    zmarket_offer(&mk, 3, SR_FROM_INT(5), 9);
    for (int n = 0; n < 5; n++) {
        zcap_commitment_t c = {0};
        printf("part %d %d %llx\n", n, (int)zmarket_match(&mk, 2, &c), bits(c.units));
    }
    zmarket_seek(&mk, 4, SR_FROM_INT(5), 50); /* the slot just past form 3's seek book */
    zcap_commitment_t c3 = {0};
    printf("wash %d %u %u\n", (int)zmarket_match(&mk, 3, &c3), c3.giver, c3.taker);
    printf("compact o%d s%d\n", zmarket_offer(&mk, 2, SR_FROM_INT(3), 99), zmarket_seek(&mk, 2, SR_FROM_INT(3), 98));
    dump_book("compact");
    for (uint32_t i = 0; i < ZMARKET_SLOTS; i++)
        printf(" e%u %d %u %llx/%d %u %llx", i, (int)mk.offers[2][i].active, mk.offers[2][i].party,
               bits(mk.offers[2][i].units), (int)mk.seeks[2][i].active, mk.seeks[2][i].party,
               bits(mk.seeks[2][i].units));
    printf("\n");

    /* random walks, every form, bad forms and bad units included */
    for (uint64_t seed = 1; seed <= 40; seed++) {
        probe_seed(seed);
        memset(&mk, 0x5A, sizeof mk);
        zmarket_init(&mk);
        for (int step = 0; step < 400; step++) {
            uint32_t op = probe_below(5);
            int form = (int)probe_below(ZCAP_FORM_COUNT + 2) - 1;
            int64_t u = (int64_t)probe_below(12) - 2;
            uint32_t party = probe_below(4);
            zcap_commitment_t c = {0};
            if (op < 2) printf("w%llu o %d %lld %u -> %d\n", (unsigned long long)seed, form, (long long)u, party,
                               zmarket_offer(&mk, (zcap_form_t)form, SR_FROM_INT(u), party));
            else if (op < 4) printf("w%llu s %d %lld %u -> %d\n", (unsigned long long)seed, form, (long long)u, party,
                                    zmarket_seek(&mk, (zcap_form_t)form, SR_FROM_INT(u), party));
            else {
                bool ok = zmarket_match(&mk, (zcap_form_t)form, probe_below(8) ? &c : NULL);
                printf("w%llu m %d -> %d %d %llx %u %u\n", (unsigned long long)seed, form, (int)ok, (int)c.form,
                       bits(c.units), c.giver, c.taker);
            }
        }
        dump_book("walk");
    }
    return 0;
}
