/* boot_banner.h — ZXV serial-console boot identity (ANSI art)
 *
 * The framebuffer logo headers (zede_logo.h, n36n9_logo.h, ...) hold
 * 250x250 RGBA pixel data, which cannot render on the serial console
 * that every real boot of this kernel uses (-nographic). This header
 * provides the console-native identity: ANSI art in the house style —
 * navy silhouette, white blueprint linework — so the 36N9 dragon and
 * the ZEDEC XERO VULPINE fox actually appear at boot.
 *
 * House palette (ANSI 256):
 *   navy fill    38;5;25   — the deep blueprint navy of the logos
 *   navy deep    38;5;17   — shadow / depth
 *   white line   38;5;255  — the fine white blueprint linework
 *   gold accent  38;5;178  — ASCW crown accent (heraldic exception)
 *
 * Set ZXV_BANNER_COLOR=0 at build time for a plain-ASCII banner on
 * terminals that do not honour ANSI SGR.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_BOOT_BANNER_H
#define ZXV_BOOT_BANNER_H

#ifndef ZXV_BANNER_COLOR
#define ZXV_BANNER_COLOR 1
#endif

#if ZXV_BANNER_COLOR
#define BN_NAVY   "\033[38;5;25m"
#define BN_DEEP   "\033[38;5;17m"
#define BN_LINE   "\033[38;5;255m"
#define BN_GOLD   "\033[38;5;178m"
#define BN_DIM    "\033[38;5;245m"
#define BN_RST    "\033[0m"
#else
#define BN_NAVY   ""
#define BN_DEEP   ""
#define BN_LINE   ""
#define BN_GOLD   ""
#define BN_DIM    ""
#define BN_RST    ""
#endif

/* 36N9 GENETICS — the code-dragon, rampant, wings spread.
 * Silhouette in navy; the white linework is the blueprint grid that
 * crosses the body in the original mark. */
#define ZXV_BANNER_36N9 \
BN_LINE "      /\\                                            /\\\n" BN_RST \
BN_NAVY "     /  \\__" BN_LINE "/|" BN_NAVY "\\        " BN_LINE "___" BN_NAVY "        " BN_LINE "/|" BN_NAVY "\\__/  \\\n" BN_RST \
BN_NAVY "    <  " BN_LINE "\\" BN_NAVY " 0110 " BN_LINE "\\" BN_NAVY "___" BN_LINE "/" BN_NAVY " 1001 " BN_LINE "\\___/" BN_NAVY " 0110 " BN_LINE "/" BN_NAVY "  >\n" BN_RST \
BN_NAVY "     \\  " BN_LINE "\\" BN_NAVY "1001" BN_LINE "|" BN_NAVY "  " BN_DEEP "><" BN_NAVY "  " BN_LINE "|" BN_NAVY "0110" BN_LINE "|" BN_NAVY "  " BN_DEEP "><" BN_NAVY "  " BN_LINE "|" BN_NAVY "1001" BN_LINE "/" BN_NAVY "  /\n" BN_RST \
BN_NAVY "      \\__" BN_LINE "\\" BN_NAVY "__" BN_LINE "|" BN_NAVY "__" BN_LINE "/" BN_NAVY "\\__" BN_LINE "|" BN_NAVY "__" BN_LINE "|" BN_NAVY "__" BN_LINE "/" BN_NAVY "\\__" BN_LINE "|" BN_NAVY "__" BN_LINE "/" BN_NAVY "__/\n" BN_RST \
BN_NAVY "         " BN_LINE "\\" BN_NAVY "  " BN_DEEP "\\\\" BN_NAVY "  " BN_LINE "/" BN_NAVY " 36N9 " BN_LINE "\\" BN_NAVY "  " BN_DEEP "//" BN_NAVY "  " BN_LINE "/\n" BN_RST \
BN_NAVY "          \\__" BN_DEEP "\\\\" BN_NAVY "_" BN_LINE "/" BN_NAVY "_______" BN_LINE "\\" BN_NAVY "_" BN_DEEP "//" BN_NAVY "__/\n" BN_RST \
BN_NAVY "             " BN_LINE "\\" BN_NAVY "__" BN_DEEP "V" BN_NAVY "___" BN_DEEP "V" BN_NAVY "__" BN_LINE "/\n" BN_RST \
BN_DIM  "            3 6 N 9   G E N E T I C S\n" BN_RST

/* ZEDEC XERO VULPINE — the circuit fox, head-on, ears raised, with the
 * orbital node ring of the original mark. */
#define ZXV_BANNER_ZEDEC \
BN_NAVY "        " BN_LINE "/\\" BN_NAVY "              " BN_LINE "/\\\n" BN_RST \
BN_NAVY "       " BN_LINE "/" BN_NAVY " " BN_LINE "\\" BN_NAVY "____" BN_LINE "o" BN_NAVY "__" BN_LINE "o" BN_NAVY "____" BN_LINE "/" BN_NAVY " " BN_LINE "\\\n" BN_RST \
BN_NAVY "      " BN_LINE "/" BN_NAVY "  " BN_LINE "\\" BN_NAVY "  " BN_DEEP "\\\\" BN_NAVY "    " BN_DEEP "//" BN_NAVY "  " BN_LINE "/" BN_NAVY "  " BN_LINE "\\\n" BN_RST \
BN_LINE "     o" BN_NAVY "___" BN_LINE "\\" BN_NAVY "__" BN_DEEP "\\\\" BN_NAVY "__" BN_DEEP "//" BN_NAVY "__" BN_LINE "/" BN_NAVY "___" BN_LINE "o\n" BN_RST \
BN_NAVY "      \\   " BN_LINE "<" BN_NAVY "0" BN_LINE ">" BN_NAVY "  " BN_LINE "<" BN_NAVY "0" BN_LINE ">" BN_NAVY "   /\n" BN_RST \
BN_NAVY "       \\___" BN_LINE "\\" BN_NAVY "__" BN_DEEP "/\\" BN_NAVY "__" BN_LINE "/" BN_NAVY "___/\n" BN_RST \
BN_NAVY "           \\_" BN_DEEP "\\/" BN_NAVY "_/\n" BN_RST \
BN_NAVY "             " BN_LINE "\\/\n" BN_RST \
BN_DIM  "     ZEDEC  XERO  VULPINE\n" BN_RST

#endif /* ZXV_BOOT_BANNER_H */
