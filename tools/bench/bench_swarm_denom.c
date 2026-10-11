/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* bench_swarm_denom.c — what each octane buys, measured: token pieces held,
 * bits to send the wallet's piece list, and time per transfer.
 * Build: gcc -std=c11 -O2 -Ikernel/src/swarm tools/bench/bench_swarm_denom.c \
 *            kernel/src/swarm/swarm_denom.c kernel/src/swarm/swarm_budget.c -o /tmp/bench_denom */
#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <time.h>
#include "swarm_denom.h"

/* Piece list on the wire: for each non-empty tier, code(tier gap) + code(count),
 * then code(0) count terminator is implied by a leading code(number of tiers). */
static uint32_t list_bits(const swarm_wallet_t *w)
{
    uint8_t buf[2048] = {0};
    uint32_t pos = 0, nz = 0, last = 0;
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++) nz += w->count[k] != 0;
    swarm_denom_code_put(buf, sizeof buf * 8, &pos, nz);
    for (uint32_t k = 0; k < SWARM_DENOM_TIERS; k++) {
        if (!w->count[k]) continue;
        swarm_denom_code_put(buf, sizeof buf * 8, &pos, k - last);
        swarm_denom_code_put(buf, sizeof buf * 8, &pos, w->count[k]);
        last = k;
    }
    return pos;
}

static double ns_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e9 + t.tv_nsec;
}

int main(void)
{
    const uint64_t amounts[] = {100, 1000, 4096, 100000, 1000000, 1000000000ULL};
    const struct { const char *name; uint32_t o; } g[] = {
        {"PLAIN", SWARM_OCTANE_PLAIN},     {"REGULAR", SWARM_OCTANE_REGULAR},
        {"MIDGRADE", SWARM_OCTANE_MIDGRADE}, {"PREMIUM", SWARM_OCTANE_PREMIUM},
        {"RACING", SWARM_OCTANE_RACING},   {"MAX", SWARM_OCTANE_MAX}};
    printf("| amount | grade | pieces | piece-list bits | amount-only bits |\n|---|---|---|---|---|\n");
    for (unsigned a = 0; a < sizeof amounts / sizeof *amounts; a++) {
        uint8_t b[16];
        uint32_t p = 0;
        swarm_denom_code_put(b, 128, &p, amounts[a]);
        for (unsigned i = 0; i < 6; i++) {
            swarm_wallet_t w;
            swarm_denom_init(&w, 1, g[i].o);
            swarm_denom_set(&w, amounts[a]);
            printf("| %llu | %s | %llu | %u | %u |\n", (unsigned long long) amounts[a], g[i].name,
                   (unsigned long long) swarm_denom_pieces(&w), list_bits(&w), p);
        }
    }
    printf("\n| grade | ns per transfer (1,000,000 tokens, 1e6 transfers) |\n|---|---|\n");
    for (unsigned i = 0; i < 6; i++) {
        swarm_wallet_t x, y;
        swarm_denom_init(&x, 1, g[i].o);
        swarm_denom_init(&y, 2, g[i].o);
        swarm_denom_set(&x, 1000000);
        double t0 = ns_now();
        for (uint32_t r = 0; r < 1000000; r++) {
            uint64_t amt = 1 + (r * 7919u) % 5000u;
            if (swarm_denom_transfer(&x, &y, amt) != SWARM_OK) swarm_denom_transfer(&y, &x, amt);
        }
        double t1 = ns_now();
        uint64_t vx, vy;
        swarm_denom_value(&x, &vx);
        swarm_denom_value(&y, &vy);
        printf("| %s | %.1f%s |\n", g[i].name, (t1 - t0) / 1e6, vx + vy == 1000000 ? "" : " CONSERVATION BROKEN");
    }
    return 0;
}
