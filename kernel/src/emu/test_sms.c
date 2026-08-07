/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_sms.c — host test: no args runs the self-check; else run each file
 * through the SMS machine and print the running verdict + observation.
 *   cc -Ikernel/src/emu kernel/src/emu/cpu_z80.c kernel/src/emu/sms.c \
 *      kernel/src/emu/test_sms.c -o /tmp/tsms && /tmp/tsms [file...]
 */
#include "sms.h"
#include <stdio.h>

static sms_t g;

int main(int argc, char **argv){
    if (argc == 1){
        int ok = sms_selfcheck();
        printf("sms_selfcheck: %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }
    for (int i = 1; i < argc; i++){
        FILE *f = fopen(argv[i], "rb");
        if (!f){ printf("  open FAIL %s\n", argv[i]); continue; }
        static uint8_t buf[SMS_ROM_MAX];
        size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
        const char *base = argv[i]; for (const char *p = argv[i]; *p; p++) if (*p=='/') base = p+1;
        if (!sms_load(&g, buf, (uint32_t)n)){ printf("  [load-skip] %s\n", base); continue; }
        sms_run(&g, 3000000u, 15000u);
        printf("  %-8s %-40.40s vdp_reg_w=%-5u vdp_status_rd=%-6u vdp_data_w=%-6u frame_irq=%-4u illegal=%u\n",
               sms_is_running(&g) ? "RUNNING" : "no",
               base, g.vdp_reg_writes, g.vdp_status_reads, g.vdp_data_writes, g.frame_ints, g.cpu.illegal);
    }
    return 0;
}
