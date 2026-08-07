/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_nes.c — host test: run real files through the NES machine and print the
 * behavioural observation + verdict, so we can confirm real NES games come up
 * "RUNNING" and non-NES data does not.
 *   cc -Ikernel/src/emu kernel/src/emu/cpu6502.c kernel/src/emu/nes.c \
 *      kernel/src/emu/test_nes.c -o /tmp/tnes && /tmp/tnes <file> [<file>...]
 */
#include "nes.h"
#include <stdio.h>
#include <stdlib.h>

static nes_t g_nes;

int main(int argc, char **argv){
    for (int i = 1; i < argc; i++){
        FILE *f = fopen(argv[i], "rb");
        if (!f){ printf("  open FAIL %s\n", argv[i]); continue; }
        static uint8_t buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
        const char *base = argv[i]; for (const char *p = argv[i]; *p; p++) if (*p=='/') base = p+1;
        if (!nes_is_ines(buf, (uint32_t)n)){
            printf("  [not-iNES]  %-40.40s (%zu bytes)\n", base, n);
            /* still run it to show a non-NES image does NOT come up running */
        }
        int ok = nes_load_ines(&g_nes, buf, (uint32_t)n);
        if (!ok){ printf("  [load-skip] %-40.40s\n", base); continue; }
        nes_run(&g_nes, 300000u, 2000u);
        printf("  %-8s %-40.40s ppu_w=%-5u ppu_status_rd=%-6u apu_io=%-5u nmi=%-4u illegal=%u\n",
               nes_is_running(&g_nes) ? "RUNNING" : "no",
               base, g_nes.ppu_reg_writes, g_nes.ppu_status_reads,
               g_nes.apu_io_writes, g_nes.nmis_taken, g_nes.cpu.illegal);
    }
    return 0;
}
