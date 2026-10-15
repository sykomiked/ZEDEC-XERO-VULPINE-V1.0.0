/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxp_driver.c — runs every C and x86-64 projection test_zl_proj wrote and
 * checks each against what zl_run did: output, fuel, status and the
 * trapping instruction, exactly. */
#include <stdio.h>
#include <string.h>
#include "zxp_case.h"

static zxp_io_t io;
static char out[4096];

int main(void)
{
    unsigned fails = 0, runs = 0;
    for (unsigned i = 0; i < zxp_ncases; i++) {
        const zxp_case_t *k = &zxp_cases[i];
        for (int t = 0; t < 2; t++) {
            memset(out, 0x55, sizeof out);
            io.fuel = k->fuel;
            io.in = k->in;
            io.nin = k->nin;
            io.out = out;
            io.cap = k->cap;
            for (unsigned j = 0; j < ZL_MAX_SLOTS; j++) io.mem[j] = 0x5a5a; /* zxp_begin zeroes */
            int s = (t ? k->x : k->c)(&io);
            runs++;
            if (s != k->status || io.used != k->used || io.trap_pc != k->trap_pc ||
                io.len != k->len || memcmp(out, k->out, k->len) ||
                (k->len < k->cap && out[k->len] != 0)) {
                fails++;
                printf("[FAIL] %s (%s): status %d/%d used %llu/%llu pc %u/%u printed [%.*s] want "
                       "[%.*s]\n",
                       k->what, t ? "x86-64" : "C", s, k->status, (unsigned long long) io.used,
                       (unsigned long long) k->used, io.trap_pc, k->trap_pc, (int) io.len, out,
                       (int) k->len, k->out);
            }
        }
    }
    printf("%u runs, %u failures\n", runs, fails);
    return fails ? 1 : 0;
}
