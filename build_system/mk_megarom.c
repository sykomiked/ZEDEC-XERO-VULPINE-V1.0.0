/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_megarom.c — HOST authoring tool for the boot MegaROM (MEGAROM.TVL).
 *
 * It links the REAL kernel container source (kernel/src/tolvovina/tvl_rom.c)
 * and calls tvl_rom_build_boot() — the same build_probe() the kernel uses to
 * make the blob it registers at boot. So the file this writes is byte-for-byte
 * the image the target validates: one derivation, two carriers. It then parses
 * its own output through tvl_rom_parse() and refuses to write anything the
 * validator would reject — the file is proven parseable before it hits the ESP.
 *
 * Build: build_system/mk_efi_disc.sh compiles this natively against the kernel
 * sources (freestanding modules that are also valid host C). It is NOT part of
 * any kernel image. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "tvl_rom.h"
#include "trispace.h"   /* TRI_POSITIVE — the role build_probe() writes */

int main(int argc, char **argv)
{
    static uint8_t buf[4096];
    tvl_rom_t r;
    uint32_t n;
    FILE *f;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <out.tvl>\n", argv[0]);
        return 2;
    }

    n = tvl_rom_build_boot(buf, (uint32_t)sizeof buf);
    if (n == 0u) {
        fprintf(stderr, "mk_megarom: build_boot returned 0 (buffer too small)\n");
        return 1;
    }

    /* Prove parseability on the host with the SAME validator the kernel runs. */
    memset(&r, 0, sizeof r);
    if (tvl_rom_parse(buf, n, (uint16_t)TRI_POSITIVE, &r) != TVL_OK || !r.valid) {
        fprintf(stderr, "mk_megarom: self-parse REFUSED the image — not written\n");
        return 1;
    }

    f = fopen(argv[1], "wb");
    if (!f) { perror("fopen"); return 1; }
    if (fwrite(buf, 1, n, f) != n) { perror("fwrite"); fclose(f); return 1; }
    fclose(f);

    fprintf(stderr,
            "mk_megarom: wrote %s (%u bytes) — TVUL v%u, %u sections, "
            "%u holds / %u gates, WORLD @section %u; parse OK\n",
            argv[1], n, (unsigned)r.version, (unsigned)r.section_count,
            (unsigned)r.hold_count, (unsigned)r.gate_count,
            (unsigned)r.world_index);
    return 0;
}
