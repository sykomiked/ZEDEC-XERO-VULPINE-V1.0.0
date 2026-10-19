/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mk_megarom.c — HOST authoring tool for the boot MegaROM array + selection.
 *
 * It links the REAL kernel container source (kernel/src/tolvovina/tvl_rom.c)
 * and calls tvl_rom_build_world() — the same build_probe()-family derivation
 * the kernel uses to make the blob it registers at boot. So every file this
 * writes is byte-for-byte the image the target validates: one derivation,
 * two carriers. It then parses its own output through tvl_rom_parse() and
 * refuses to write anything the validator would reject — every file is proven
 * parseable before it hits the ESP.
 *
 * The installer image ships the WHOLE array: MEGAROM.TVL (the canonical boot
 * cartridge), MEGAROM-SERVER.TVL (headless), MEGAROM-USER.TVL (desktop GUI)
 * and MEGAROM-FOUNDRY.TVL (dev toolchain). Choosing which world a machine
 * boots happens ONCE, at configuration: `mk_megarom --select <world>
 * <rom.tvl> <bootsel.tvl>` writes a sealed BOOTSEL.TVL committing that
 * cartridge's digest, and the loader refuses any cartridge that does not
 * match it — no casual switching by file rename or boot flag.
 *
 * Build: build_system/mk_efi_disc.sh compiles this natively against the kernel
 * sources (freestanding modules that are also valid host C). It is NOT part
 * of any kernel image. */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "tvl_rom.h"
#include "trispace.h"   /* TRI_POSITIVE — the role build_probe() writes */

#define MK_ROM_CAP (TVL_HDR_SIZE + 32 * TVL_SEC_SIZE + 4096u)

static int author_world(const char *out, uint32_t world){
    static uint8_t buf[MK_ROM_CAP];
    tvl_rom_t r;
    uint32_t n;
    FILE *f;

    n = tvl_rom_build_world(world, buf, (uint32_t)sizeof buf);
    if (n == 0u) {
        fprintf(stderr, "mk_megarom: world %u build returned 0\n", world);
        return 1;
    }

    /* Prove parseability on the host with the SAME validator the kernel runs. */
    memset(&r, 0, sizeof r);
    if (tvl_rom_parse(buf, n, (uint16_t)TRI_POSITIVE, &r) != TVL_OK || !r.valid) {
        fprintf(stderr, "mk_megarom: self-parse REFUSED world %u — not written\n", world);
        return 1;
    }

    f = fopen(out, "wb");
    if (!f) { perror("fopen"); return 1; }
    if (fwrite(buf, 1, n, f) != n) { perror("fwrite"); fclose(f); return 1; }
    fclose(f);

    fprintf(stderr,
            "mk_megarom: wrote %s (%u bytes) — TVUL v%u, %u sections, "
            "%u holds / %u gates, WORLD @section %u; parse OK\n",
            out, (unsigned)n, (unsigned)r.version, (unsigned)r.section_count,
            (unsigned)r.hold_count, (unsigned)r.gate_count,
            (unsigned)r.world_index);
    return 0;
}

/* usage:
 *   mk_megarom <out.tvl>                     boot cartridge (compat)
 *   mk_megarom <world> <out.tvl>             one named world
 *   mk_megarom --all <dir>                   every world + BOOTSEL.TVL (user)
 *   mk_megarom --select <world> <rom> <sel>  sealed selection over a ROM
 */
int main(int argc, char **argv)
{
    static uint8_t selbuf[TVL_SEL_SIZE + 64];
    tvl_sel_t sel;
    FILE *f;

    if (argc == 2) return author_world(argv[1], TVL_WORLD_BOOT);

    if (argc == 3 && strcmp(argv[1], "--all") == 0){
        char path[512];
        uint8_t rom[MK_ROM_CAP];
        uint32_t world, len, sel_len;
        for (world = 0; world < TVL_WORLD_COUNT; world++){
            const char *name = tvl_rom_world_name(world);
            if (!name) return 1;
            snprintf(path, sizeof path, "%s/%s", argv[2], name);
            if (author_world(path, world)) return 1;
            if (world == TVL_WORLD_USER){
                len = tvl_rom_build_world(world, rom, (uint32_t)sizeof rom);
                if (len == 0u) return 1;
                sel_len = tvl_sel_build(world, rom, len, selbuf, (uint32_t)sizeof selbuf);
                if (sel_len != TVL_SEL_SIZE) return 1;
                snprintf(path, sizeof path, "%s/BOOTSEL.TVL", argv[2]);
                f = fopen(path, "wb");
                if (!f) { perror("fopen"); return 1; }
                if (fwrite(selbuf, 1, sel_len, f) != sel_len) { perror("fwrite"); fclose(f); return 1; }
                fclose(f);
                fprintf(stderr, "mk_megarom: wrote %s — sealed selection for %s\n",
                        path, name);
            }
        }
        return 0;
    }

    if (argc == 5 && strcmp(argv[1], "--select") == 0){
        uint32_t world = (uint32_t)atoi(argv[2]);
        FILE *rf = fopen(argv[3], "rb");
        uint8_t rom[MK_ROM_CAP];
        long rl;
        uint32_t sel_len;
        if (!rf) { perror("fopen rom"); return 1; }
        rl = fread(rom, 1, sizeof rom, rf);
        fclose(rf);
        if (rl <= 0 || world >= TVL_WORLD_COUNT) return 1;
        sel_len = tvl_sel_build(world, rom, (uint32_t)rl, selbuf, (uint32_t)sizeof selbuf);
        if (sel_len != TVL_SEL_SIZE) return 1;
        if (tvl_sel_parse(selbuf, sel_len, &sel) != TVL_OK || !sel.valid) return 1;
        f = fopen(argv[4], "wb");
        if (!f) { perror("fopen"); return 1; }
        if (fwrite(selbuf, 1, sel_len, f) != sel_len) { perror("fwrite"); fclose(f); return 1; }
        fclose(f);
        fprintf(stderr, "mk_megarom: wrote %s — BOOTSEL v%u world %u sealed\n",
                argv[4], (unsigned)sel.version, (unsigned)sel.world_id);
        return 0;
    }

    if (argc == 3){
        static const struct { const char *n; uint32_t w; } names[] = {
            { "boot", TVL_WORLD_BOOT }, { "server", TVL_WORLD_SERVER },
            { "user", TVL_WORLD_USER }, { "foundry", TVL_WORLD_FOUNDRY },
        };
        for (uint32_t i = 0; i < 4u; i++)
            if (strcmp(argv[1], names[i].n) == 0)
                return author_world(argv[2], names[i].w);
        fprintf(stderr, "mk_megarom: unknown world '%s' (boot|server|user|foundry)\n", argv[1]);
        return 2;
    }

    fprintf(stderr, "usage: mk_megarom <out.tvl> | <world> <out.tvl> | "
                    "--all <dir> | --select <world> <rom> <sel>\n");
    return 2;
}