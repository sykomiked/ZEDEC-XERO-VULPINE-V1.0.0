/* tvl_bringup.c — run the TOL VOVINA UPAAH LOT self-checks for real.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * Each module is exercised with its REAL self-check and the line is printed
 * from the RESULT, never from the fact that the file is in the Makefile.
 * Nothing here fabricates a success. See tvl_bringup.h for why it exists.
 *
 * Note on the two failure bitmasks: tvl_raster.h and tvl_stereo.h both define
 * TVL_CHK_* constants, but the NAMES are disjoint (DIV/MORTON/COVERAGE/DEPTH/
 * OCCLUDE/PERSP/TRISPACE vs NEUTRAL_ZERO/MIRROR/ROUNDTRIP/FLAT_IDENTICAL/
 * SEPARATES/ANTISYMMETRY) — only the bit VALUES overlap, and they index two
 * separate masks, so both headers include cleanly in one translation unit.
 */
#include "tvl_bringup.h"

#include "tvl_geom.h"
#include "tvl_raster.h"
#include "tvl_stereo.h"
#include "tvl_rom.h"

/* ---- local, libc-free number -> text, appended into a small buffer --------
 * Same shape as the helpers in boot_features.c; duplicated rather than shared
 * because those are static to that translation unit and this file must not
 * depend upward on bootfeat. */
static char *u2s(char *p, unsigned v)
{
    char tmp[12]; int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = (char)('0' + v % 10); v /= 10; }
    while (n) *p++ = tmp[--n];
    *p = 0; return p;
}

static char *scat(char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }

/* hex of a failure bitmask — masks read better in hex than decimal, because a
 * mask is a set of named bits, not a quantity. */
static char *x2s(char *p, unsigned v)
{
    static const char *H = "0123456789ABCDEF";
    int started = 0;
    for (int s = 28; s >= 0; s -= 4) {
        unsigned d = (v >> s) & 0xFu;
        if (d || started || s == 0) { *p++ = H[d]; started = 1; }
    }
    *p = 0; return p;
}

unsigned tvl_bringup(tvl_puts_t puts)
{
    if (!puts) return 0;
    unsigned ok = 0;
    char line[160];

    puts("[FEAT] TOL VOVINA UPAAH LOT — the geometry engine orients the graphics...\n");

    /* 1) tvl_geom — the orientation frame. Runs FIRST: the raster and the
     *    stereo channel are both oriented by directions this module derives
     *    from e8_icosian(), so a broken frame invalidates everything after it.
     *    Returns a PROBLEM COUNT (0 = healthy), matching zphi_selfcheck. */
    {
        uint32_t problems = tvl_geom_selfcheck();
        char *p;
        if (problems == 0) {
            p = scat(line, "  [OK] tvl_geom: icosahedral frame exact — ");
            p = u2s(p, tvl_frame_size(TVL_AXIS_5FOLD));
            p = scat(p, "/");
            p = u2s(p, tvl_frame_size(TVL_AXIS_3FOLD));
            p = scat(p, "/");
            p = u2s(p, tvl_frame_size(TVL_AXIS_2FOLD));
            p = scat(p, " dirs, ");
            p = u2s(p, tvl_rot_count());
            p = scat(p, " rotations, Z[phi] projection\n");
            ok++;
        } else {
            p = scat(line, "  [??] tvl_geom: ");
            p = u2s(p, (unsigned)problems);
            p = scat(p, " problem(s) in the orientation frame\n");
        }
        puts(line);
    }

    /* 2) tvl_raster — z-buffered, perspective-correct, Morton-addressed. The
     *    mask names which of the 7 checks broke rather than just saying 0. */
    {
        int pass = tvl_raster_selfcheck();
        uint32_t mask = tvl_raster_last_failures();
        char *p;
        if (pass && mask == 0) {
            p = scat(line, "  [OK] tvl_raster: 7/7 — exact int64 coverage, Q16.16 planes, Z-order texels via zo_encode2\n");
            ok++;
        } else {
            p = scat(line, "  [??] tvl_raster: failed checks mask=0x");
            p = x2s(p, (unsigned)mask);
            p = scat(p, "\n");
        }
        puts(line);
    }

    /* 3) tvl_stereo — complementary-CHANNEL depth (one ordinary framebuffer,
     *    no headset, degrades to flat). S+ protrudes, S- recedes, S0 sits at
     *    exactly zero parallax on the screen plane. */
    {
        uint32_t mask = 0;
        int pass = tvl_stereo_selfcheck_detail(&mask);
        char *p;
        if (pass && mask == 0) {
            p = scat(line, "  [OK] tvl_stereo: S+ protrudes / S0 zero-parallax / S- recedes; split+merge lossless\n");
            ok++;
        } else {
            p = scat(line, "  [??] tvl_stereo: failed properties mask=0x");
            p = x2s(p, (unsigned)mask);
            p = scat(p, "\n");
        }
        puts(line);
    }

    /* 4) tvl_rom — the ROM is the hard instruction set; the RAM requirement is
     *    DERIVED from the image, never declared. The self-check also requires
     *    a battery of malformed images to be refused for the right reason. */
    {
        int pass = tvl_rom_selfcheck();
        if (pass) {
            puts("  [OK] tvl_rom: TVUL container validates; RAM requirement derived from content (no ram_bytes field), malformed images refused\n");
            ok++;
        } else {
            puts("  [??] tvl_rom: container self-check did not pass\n");
        }
    }

    /* 5) Register the SHIPPED boot MegaROM. This runs AFTER the self-check so
     *    the module's one-registration latch is free (the probe released it).
     *    It embeds the exact container bytes the ESP carries as MEGAROM.TVL and
     *    takes a real MR_KIND_GAME slot — "media boots -> kernel up -> the
     *    MegaROM container registers". The blob is EMBEDDED, not yet read off
     *    the disc filesystem; the identical file is on the ESP for that
     *    follow-on. Registration success is printed from the RESULT. */
    {
        int slot = tvl_rom_register_boot();
        char *p;
        if (slot >= 0) {
            p = scat(line, "  [OK] megarom: 'TOL VOVINA UPAAH LOT' registered as MR_KIND_GAME cartridge, slot ");
            p = u2s(p, (unsigned)slot);
            p = scat(p, " (container embedded; identical bytes staged on ESP as MEGAROM.TVL)\n");
        } else {
            p = scat(line, "  [??] megarom: boot MegaROM did not register\n");
        }
        puts(line);
    }

    {
        char *p = scat(line, "[FEAT] TOL VOVINA UPAAH LOT: ");
        p = u2s(p, ok); p = scat(p, "/4 modules self-checked\n");
        puts(line);
    }
    return ok;
}
