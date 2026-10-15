/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_devprof.c — the device catalogue is consistent with itself. */
#include "devprof.h"
#include <stdio.h>
#include <string.h>

static int g_fail, g_checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(c)) {                                                                                \
            g_fail++;                                                                              \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
        }                                                                                          \
    } while (0)

int main(void)
{
    /* every component sits at its own index, has a slug and evidence */
    for (int i = 0; i < DPC__COUNT; i++) {
        const dp_component_t *c = dp_component((dp_comp_t) i);
        CHECK(c && (int) c->id == i);
        CHECK(c && c->slug && c->slug[0] && c->evidence && c->evidence[0]);
        for (int j = 0; j < i; j++) CHECK(strcmp(c->slug, dp_component((dp_comp_t) j)->slug) != 0);
    }
    CHECK(dp_component(DPC__COUNT) == NULL);

    /* the spec line-up: 6 briefcase SKUs, 6 servers, 5 handheld boards */
    int lines[4] = {0};
    CHECK(dp_device_count() == 17);
    for (uint32_t i = 0; i < dp_device_count(); i++) {
        const dp_device_t *d = dp_device(i);
        CHECK(d->line >= 1 && d->line <= 3);
        lines[d->line]++;
        CHECK(d->n_needs > 0 && d->n_needs <= DP_MAX_NEEDS);
        CHECK(d->sustained_w > 0 && d->sustained_w <= d->peak_w);
        CHECK(d->ram_gb > 0 && d->cores > 0 && d->nodes > 0);
        CHECK(dp_find(d->slug) == d && dp_find_model(d->model) == d);
        /* needs are valid and unique */
        for (uint32_t a = 0; a < d->n_needs; a++) {
            CHECK((int) d->needs[a] >= 0 && d->needs[a] < DPC__COUNT);
            for (uint32_t b = 0; b < a; b++) CHECK(d->needs[a] != d->needs[b]);
        }
        /* the erasure code holds: k data + m parity, m < k unless mirrored */
        if (d->code_k) CHECK(d->code_m >= 1 && d->code_k + d->code_m <= 168);
        /* servers carry the climate controller; nothing else does */
        int climate = 0, mbox = 0;
        for (uint32_t a = 0; a < d->n_needs; a++) {
            climate += d->needs[a] == DPC_CLIMATE;
            mbox += d->needs[a] == DPC_ARBITER_MBOX;
        }
        CHECK(climate == (d->line == DP_LINE_SERVER));
        /* every device with an arbiter talks to it through the mailbox */
        CHECK(mbox == (d->arbiter != NULL));
        /* a held device is a handheld (MOBILE_COMMON s.3.1: Signet always) */
        if (d->held) CHECK(d->line == DP_LINE_HANDHELD);
        dp_tally_t t;
        dp_assess(d, &t);
        CHECK(t.built + t.hosted + t.partial + t.missing == d->n_needs);
        CHECK(dp_ready(d) == (t.partial == 0 && t.missing == 0));
    }
    CHECK(lines[DP_LINE_BRIEFCASE] == 6 && lines[DP_LINE_SERVER] == 6 &&
          lines[DP_LINE_HANDHELD] == 5);
    /* the Hoard is the 3 PB class: 12 + 8 across nodes */
    const dp_device_t *h = dp_find("hoard");
    CHECK(h && h->code_k == 12 && h->code_m == 8 && h->code_spans_nodes && h->usable_tb == 2950);
    CHECK(dp_find("nope") == NULL && dp_find(NULL) == NULL && dp_find_model("") == NULL);
    CHECK(dp_device(dp_device_count()) == NULL);
    dp_tally_t z;
    dp_assess(NULL, &z);
    CHECK(z.built == 0 && z.missing == 0 && !dp_ready(NULL));
    CHECK(strcmp(dp_state_name(DP_ST_BUILT), "built") == 0);

    printf("%s: %d checks, %d failed\n", g_fail ? "FAIL" : "ok", g_checks, g_fail);
    return g_fail != 0;
}
