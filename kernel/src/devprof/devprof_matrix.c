/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* devprof_matrix.c — print the device gap matrix (Markdown) from devprof.c.
 * Host tool: make -C kernel device-matrix > docs/DEVICE_COMPAT_MATRIX_TABLES.md */
#include "devprof.h"
#include <stdio.h>

static const char *line_name(dp_line_t l)
{
    return l == DP_LINE_BRIEFCASE ? "Briefcase" : l == DP_LINE_SERVER ? "Server" : "Handheld";
}

int main(void)
{
    uint32_t n = dp_device_count();
    printf("## Summary by device\n\n");
    printf("| Device | Line | Host ISA / boot | Arbiter | Needs | Built | Hosted | Partial | "
           "Missing |\n|---|---|---|---|---|---|---|---|---|\n");
    for (uint32_t i = 0; i < n; i++) {
        const dp_device_t *d = dp_device(i);
        dp_tally_t t;
        dp_assess(d, &t);
        printf("| %s | %s%s | %s / %s | %s | %u | %u | %u | %u | %u |\n", d->name,
               line_name(d->line), d->institutional ? ", institutional" : "", dp_isa_name(d->isa),
               dp_boot_name(d->boot), d->arbiter ? d->arbiter : "none (EC)", d->n_needs, t.built,
               t.hosted, t.partial, t.missing);
    }
    printf("\n## Gaps by device\n\nEach list is what the device's spec needs and the tree "
           "does not yet have (partial or missing).\n\n");
    for (uint32_t i = 0; i < n; i++) {
        const dp_device_t *d = dp_device(i);
        printf("- **%s**:", d->name);
        int first = 1;
        for (uint32_t a = 0; a < d->n_needs; a++) {
            const dp_component_t *c = dp_component(d->needs[a]);
            if (c->state == DP_ST_BUILT || c->state == DP_ST_HOSTED) continue;
            printf("%s %s (%s)", first ? "" : ",", c->slug, dp_state_name(c->state));
            first = 0;
        }
        printf("%s\n", first ? " none" : "");
    }
    printf("\n## Components\n\n| Component | What | State | Needed by | Evidence |\n"
           "|---|---|---|---|---|\n");
    for (int k = 0; k < DPC__COUNT; k++) {
        const dp_component_t *c = dp_component((dp_comp_t) k);
        uint32_t users = 0;
        for (uint32_t i = 0; i < n; i++) {
            const dp_device_t *d = dp_device(i);
            for (uint32_t a = 0; a < d->n_needs; a++) users += d->needs[a] == c->id;
        }
        printf("| `%s` | %s | %s | %u of %u | %s |\n", c->slug, c->what, dp_state_name(c->state),
               users, n, c->evidence);
    }
    return 0;
}
