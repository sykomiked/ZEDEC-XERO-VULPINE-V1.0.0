/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* smoke.c - libzxv-econ standalone: three firms owe each other in a
 * circle plus one extra debt; compress nets it to one leg, route finds
 * the cheap corridor, decompress settles it with the fee. */
#include <stdio.h>
#include "zxv_econ.h"

static zxv_engine_t E;

int main(void)
{
    const uint8_t seed[32] = {42};
    uint32_t a, b, c;
    if (zxv_engine_init(&E, "EUR", 978, 2, seed) || zxv_engine_add_member(&E, &a) ||
        zxv_engine_add_member(&E, &b) || zxv_engine_add_member(&E, &c))
        return 1;
    zxv_corridor_t k1 = {a, b, 30, 1000000}, k2 = {b, c, 20, 1000000}, k3 = {a, c, 90, 1000000};
    if (zxv_engine_add_corridor(&E, &k1) || zxv_engine_add_corridor(&E, &k2) ||
        zxv_engine_add_corridor(&E, &k3) || zxv_engine_fund(&E, a, 100000, 1))
        return 2;
    zxv_obligation_t book[4] = {{a, b, 5000}, {b, c, 5000}, {c, a, 5000}, {a, c, 2500}};
    zxv_obligation_t legs[4];
    uint32_t n, path[3], hops;
    uint64_t cost, fees;
    if (zxv_compress(&E, book, 4, legs, 4, &n) || n != 1 || legs[0].amount != 2500) return 3;
    if (zxv_route(&E, legs[0].from, legs[0].to, legs[0].amount, path, 3, &hops, &cost) ||
        hops != 3 || cost != 50)
        return 4;
    if (zxv_decompress(&E, legs, n, 2, &fees) || !zxv_engine_check(&E)) return 5;
    /* the same circle as a treaty, run end to end */
    static const char treaty[] = "TREATY CIRCLE EUR SETL\nPARTY A 100000 0 1/3\nPARTY B 0 0 1/3\n"
                                 "PARTY C 0 0 1/3\nOWE A B 5000\nOWE B C 5000\nOWE C A 5000\n"
                                 "OWE A C 2500\nEND\n";
    static zxv_engine_t W;
    zxt_treaty_t t;
    zxt_result_t r;
    if (zxt_parse_sutra(treaty, sizeof treaty - 1, &t, NULL) || zxt_exec(&t, &W, seed, &r) ||
        r.n_leg != 1 || r.leg[0].amount != 2500 || r.balance[2] != 2500)
        return 7;
    printf("gross 17500 -> 1 leg of %llu via %u hops (cost %llu ppm), fee %llu\n",
           (unsigned long long) legs[0].amount, hops, (unsigned long long) cost,
           (unsigned long long) fees);
    return zxv_engine_balance(&E, c) == 2500 && zxv_engine_balance(&E, a) == 100000 - 2500 - fees
               ? 0
               : 6;
}
