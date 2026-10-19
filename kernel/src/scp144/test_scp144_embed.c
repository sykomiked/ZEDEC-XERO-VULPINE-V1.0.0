/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_scp144_embed.c — host unit test of the SCP-144 boot self-check
 * (scp144_embed.c): the embedded 144-dimensional lattice ROM must agree
 * with the kernel's own recomputation of the tables, the matrix and the
 * digest, and the improved SCP-144 live checks and the fractal seed chain
 * must pass. This is the same POST the kernel runs before [BOOT_OK] on
 * every architecture, on the host so CI guards the generator agreement.
 *   cc -I<build>/gen -Isrc/scp144 ... (see the kernel Makefile target) */
#include "scp144_embed.h"
#include <stdio.h>

static void say(const char *s)
{
    puts(s);
}

int main(void)
{
    static uint8_t shadow[1024];
    uint32_t bad = scp144_heal_audit(shadow, sizeof shadow);
    bool ok = scp144_boot_selfcheck(say);
    printf("heal audit: %u byte%s differ from the ROM (0 = intact)\n",
           bad, bad == 1u ? "" : "s");
    if (bad != 0u) ok = false;
    printf("test_scp144_embed: %s\n", ok ? "all checks passed" : "FAILURES");
    return ok ? 0 : 1;
}