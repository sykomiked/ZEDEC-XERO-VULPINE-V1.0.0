/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tables and test data derived from the Unicode Character Database / CLDR,
 * Copyright (c) Unicode, Inc., under the Unicode License v3
 * (https://www.unicode.org/license.txt). */
/* test_zui_shape.c — complex scripts at the character level (zui_shape.h
 * S1..S9): Arabic joining and lam-alef, grapheme clusters (with the whole
 * Unicode GraphemeBreakTest carried in this file) and Devanagari syllables. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zui_bidi.h"
#include "zui_shape.h"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static int f0, c0;
static void begin(void)
{
    f0 = fails;
    c0 = checks;
}
static void end(const char *name)
{
    printf("[%s] %s: %d/%d checks\n", fails > f0 ? "FAIL" : "PASS", name,
           checks - c0 - (fails - f0), checks - c0);
}

// clang-format off
/* GraphemeBreakTest-15.1.0.txt, all 1187 lines: code points in hex, "/" a
 * boundary, "+" none (the leading and trailing boundaries are left out). */
static const char *const UCD_GRAPHEME[] = {
    "0020/0020",
    "0020+0308/0020",
    "0020/000D",
    "0020+0308/000D",
    "0020/000A",
    "0020+0308/000A",
    "0020/0001",
    "0020+0308/0001",
    "0020+034F",
    "0020+0308+034F",
    "0020/1F1E6",
    "0020+0308/1F1E6",
    "0020/0600",
    "0020+0308/0600",
    "0020+0A03",
    "0020+0308+0A03",
    "0020/1100",
    "0020+0308/1100",
    "0020/1160",
    "0020+0308/1160",
    "0020/11A8",
    "0020+0308/11A8",
    "0020/AC00",
    "0020+0308/AC00",
    "0020/AC01",
    "0020+0308/AC01",
    "0020+0900",
    "0020+0308+0900",
    "0020+0903",
    "0020+0308+0903",
    "0020/0904",
    "0020+0308/0904",
    "0020/0D4E",
    "0020+0308/0D4E",
    "0020/0915",
    "0020+0308/0915",
    "0020/231A",
    "0020+0308/231A",
    "0020+0300",
    "0020+0308+0300",
    "0020+093C",
    "0020+0308+093C",
    "0020+094D",
    "0020+0308+094D",
    "0020+200D",
    "0020+0308+200D",
    "0020/0378",
    "0020+0308/0378",
    "000D/0020",
    "000D/0308/0020",
    "000D/000D",
    "000D/0308/000D",
    "000D+000A",
    "000D/0308/000A",
    "000D/0001",
    "000D/0308/0001",
    "000D/034F",
    "000D/0308+034F",
    "000D/1F1E6",
    "000D/0308/1F1E6",
    "000D/0600",
    "000D/0308/0600",
    "000D/0A03",
    "000D/0308+0A03",
    "000D/1100",
    "000D/0308/1100",
    "000D/1160",
    "000D/0308/1160",
    "000D/11A8",
    "000D/0308/11A8",
    "000D/AC00",
    "000D/0308/AC00",
    "000D/AC01",
    "000D/0308/AC01",
    "000D/0900",
    "000D/0308+0900",
    "000D/0903",
    "000D/0308+0903",
    "000D/0904",
    "000D/0308/0904",
    "000D/0D4E",
    "000D/0308/0D4E",
    "000D/0915",
    "000D/0308/0915",
    "000D/231A",
    "000D/0308/231A",
    "000D/0300",
    "000D/0308+0300",
    "000D/093C",
    "000D/0308+093C",
    "000D/094D",
    "000D/0308+094D",
    "000D/200D",
    "000D/0308+200D",
    "000D/0378",
    "000D/0308/0378",
    "000A/0020",
    "000A/0308/0020",
    "000A/000D",
    "000A/0308/000D",
    "000A/000A",
    "000A/0308/000A",
    "000A/0001",
    "000A/0308/0001",
    "000A/034F",
    "000A/0308+034F",
    "000A/1F1E6",
    "000A/0308/1F1E6",
    "000A/0600",
    "000A/0308/0600",
    "000A/0A03",
    "000A/0308+0A03",
    "000A/1100",
    "000A/0308/1100",
    "000A/1160",
    "000A/0308/1160",
    "000A/11A8",
    "000A/0308/11A8",
    "000A/AC00",
    "000A/0308/AC00",
    "000A/AC01",
    "000A/0308/AC01",
    "000A/0900",
    "000A/0308+0900",
    "000A/0903",
    "000A/0308+0903",
    "000A/0904",
    "000A/0308/0904",
    "000A/0D4E",
    "000A/0308/0D4E",
    "000A/0915",
    "000A/0308/0915",
    "000A/231A",
    "000A/0308/231A",
    "000A/0300",
    "000A/0308+0300",
    "000A/093C",
    "000A/0308+093C",
    "000A/094D",
    "000A/0308+094D",
    "000A/200D",
    "000A/0308+200D",
    "000A/0378",
    "000A/0308/0378",
    "0001/0020",
    "0001/0308/0020",
    "0001/000D",
    "0001/0308/000D",
    "0001/000A",
    "0001/0308/000A",
    "0001/0001",
    "0001/0308/0001",
    "0001/034F",
    "0001/0308+034F",
    "0001/1F1E6",
    "0001/0308/1F1E6",
    "0001/0600",
    "0001/0308/0600",
    "0001/0A03",
    "0001/0308+0A03",
    "0001/1100",
    "0001/0308/1100",
    "0001/1160",
    "0001/0308/1160",
    "0001/11A8",
    "0001/0308/11A8",
    "0001/AC00",
    "0001/0308/AC00",
    "0001/AC01",
    "0001/0308/AC01",
    "0001/0900",
    "0001/0308+0900",
    "0001/0903",
    "0001/0308+0903",
    "0001/0904",
    "0001/0308/0904",
    "0001/0D4E",
    "0001/0308/0D4E",
    "0001/0915",
    "0001/0308/0915",
    "0001/231A",
    "0001/0308/231A",
    "0001/0300",
    "0001/0308+0300",
    "0001/093C",
    "0001/0308+093C",
    "0001/094D",
    "0001/0308+094D",
    "0001/200D",
    "0001/0308+200D",
    "0001/0378",
    "0001/0308/0378",
    "034F/0020",
    "034F+0308/0020",
    "034F/000D",
    "034F+0308/000D",
    "034F/000A",
    "034F+0308/000A",
    "034F/0001",
    "034F+0308/0001",
    "034F+034F",
    "034F+0308+034F",
    "034F/1F1E6",
    "034F+0308/1F1E6",
    "034F/0600",
    "034F+0308/0600",
    "034F+0A03",
    "034F+0308+0A03",
    "034F/1100",
    "034F+0308/1100",
    "034F/1160",
    "034F+0308/1160",
    "034F/11A8",
    "034F+0308/11A8",
    "034F/AC00",
    "034F+0308/AC00",
    "034F/AC01",
    "034F+0308/AC01",
    "034F+0900",
    "034F+0308+0900",
    "034F+0903",
    "034F+0308+0903",
    "034F/0904",
    "034F+0308/0904",
    "034F/0D4E",
    "034F+0308/0D4E",
    "034F/0915",
    "034F+0308/0915",
    "034F/231A",
    "034F+0308/231A",
    "034F+0300",
    "034F+0308+0300",
    "034F+093C",
    "034F+0308+093C",
    "034F+094D",
    "034F+0308+094D",
    "034F+200D",
    "034F+0308+200D",
    "034F/0378",
    "034F+0308/0378",
    "1F1E6/0020",
    "1F1E6+0308/0020",
    "1F1E6/000D",
    "1F1E6+0308/000D",
    "1F1E6/000A",
    "1F1E6+0308/000A",
    "1F1E6/0001",
    "1F1E6+0308/0001",
    "1F1E6+034F",
    "1F1E6+0308+034F",
    "1F1E6+1F1E6",
    "1F1E6+0308/1F1E6",
    "1F1E6/0600",
    "1F1E6+0308/0600",
    "1F1E6+0A03",
    "1F1E6+0308+0A03",
    "1F1E6/1100",
    "1F1E6+0308/1100",
    "1F1E6/1160",
    "1F1E6+0308/1160",
    "1F1E6/11A8",
    "1F1E6+0308/11A8",
    "1F1E6/AC00",
    "1F1E6+0308/AC00",
    "1F1E6/AC01",
    "1F1E6+0308/AC01",
    "1F1E6+0900",
    "1F1E6+0308+0900",
    "1F1E6+0903",
    "1F1E6+0308+0903",
    "1F1E6/0904",
    "1F1E6+0308/0904",
    "1F1E6/0D4E",
    "1F1E6+0308/0D4E",
    "1F1E6/0915",
    "1F1E6+0308/0915",
    "1F1E6/231A",
    "1F1E6+0308/231A",
    "1F1E6+0300",
    "1F1E6+0308+0300",
    "1F1E6+093C",
    "1F1E6+0308+093C",
    "1F1E6+094D",
    "1F1E6+0308+094D",
    "1F1E6+200D",
    "1F1E6+0308+200D",
    "1F1E6/0378",
    "1F1E6+0308/0378",
    "0600+0020",
    "0600+0308/0020",
    "0600/000D",
    "0600+0308/000D",
    "0600/000A",
    "0600+0308/000A",
    "0600/0001",
    "0600+0308/0001",
    "0600+034F",
    "0600+0308+034F",
    "0600+1F1E6",
    "0600+0308/1F1E6",
    "0600+0600",
    "0600+0308/0600",
    "0600+0A03",
    "0600+0308+0A03",
    "0600+1100",
    "0600+0308/1100",
    "0600+1160",
    "0600+0308/1160",
    "0600+11A8",
    "0600+0308/11A8",
    "0600+AC00",
    "0600+0308/AC00",
    "0600+AC01",
    "0600+0308/AC01",
    "0600+0900",
    "0600+0308+0900",
    "0600+0903",
    "0600+0308+0903",
    "0600+0904",
    "0600+0308/0904",
    "0600+0D4E",
    "0600+0308/0D4E",
    "0600+0915",
    "0600+0308/0915",
    "0600+231A",
    "0600+0308/231A",
    "0600+0300",
    "0600+0308+0300",
    "0600+093C",
    "0600+0308+093C",
    "0600+094D",
    "0600+0308+094D",
    "0600+200D",
    "0600+0308+200D",
    "0600+0378",
    "0600+0308/0378",
    "0A03/0020",
    "0A03+0308/0020",
    "0A03/000D",
    "0A03+0308/000D",
    "0A03/000A",
    "0A03+0308/000A",
    "0A03/0001",
    "0A03+0308/0001",
    "0A03+034F",
    "0A03+0308+034F",
    "0A03/1F1E6",
    "0A03+0308/1F1E6",
    "0A03/0600",
    "0A03+0308/0600",
    "0A03+0A03",
    "0A03+0308+0A03",
    "0A03/1100",
    "0A03+0308/1100",
    "0A03/1160",
    "0A03+0308/1160",
    "0A03/11A8",
    "0A03+0308/11A8",
    "0A03/AC00",
    "0A03+0308/AC00",
    "0A03/AC01",
    "0A03+0308/AC01",
    "0A03+0900",
    "0A03+0308+0900",
    "0A03+0903",
    "0A03+0308+0903",
    "0A03/0904",
    "0A03+0308/0904",
    "0A03/0D4E",
    "0A03+0308/0D4E",
    "0A03/0915",
    "0A03+0308/0915",
    "0A03/231A",
    "0A03+0308/231A",
    "0A03+0300",
    "0A03+0308+0300",
    "0A03+093C",
    "0A03+0308+093C",
    "0A03+094D",
    "0A03+0308+094D",
    "0A03+200D",
    "0A03+0308+200D",
    "0A03/0378",
    "0A03+0308/0378",
    "1100/0020",
    "1100+0308/0020",
    "1100/000D",
    "1100+0308/000D",
    "1100/000A",
    "1100+0308/000A",
    "1100/0001",
    "1100+0308/0001",
    "1100+034F",
    "1100+0308+034F",
    "1100/1F1E6",
    "1100+0308/1F1E6",
    "1100/0600",
    "1100+0308/0600",
    "1100+0A03",
    "1100+0308+0A03",
    "1100+1100",
    "1100+0308/1100",
    "1100+1160",
    "1100+0308/1160",
    "1100/11A8",
    "1100+0308/11A8",
    "1100+AC00",
    "1100+0308/AC00",
    "1100+AC01",
    "1100+0308/AC01",
    "1100+0900",
    "1100+0308+0900",
    "1100+0903",
    "1100+0308+0903",
    "1100/0904",
    "1100+0308/0904",
    "1100/0D4E",
    "1100+0308/0D4E",
    "1100/0915",
    "1100+0308/0915",
    "1100/231A",
    "1100+0308/231A",
    "1100+0300",
    "1100+0308+0300",
    "1100+093C",
    "1100+0308+093C",
    "1100+094D",
    "1100+0308+094D",
    "1100+200D",
    "1100+0308+200D",
    "1100/0378",
    "1100+0308/0378",
    "1160/0020",
    "1160+0308/0020",
    "1160/000D",
    "1160+0308/000D",
    "1160/000A",
    "1160+0308/000A",
    "1160/0001",
    "1160+0308/0001",
    "1160+034F",
    "1160+0308+034F",
    "1160/1F1E6",
    "1160+0308/1F1E6",
    "1160/0600",
    "1160+0308/0600",
    "1160+0A03",
    "1160+0308+0A03",
    "1160/1100",
    "1160+0308/1100",
    "1160+1160",
    "1160+0308/1160",
    "1160+11A8",
    "1160+0308/11A8",
    "1160/AC00",
    "1160+0308/AC00",
    "1160/AC01",
    "1160+0308/AC01",
    "1160+0900",
    "1160+0308+0900",
    "1160+0903",
    "1160+0308+0903",
    "1160/0904",
    "1160+0308/0904",
    "1160/0D4E",
    "1160+0308/0D4E",
    "1160/0915",
    "1160+0308/0915",
    "1160/231A",
    "1160+0308/231A",
    "1160+0300",
    "1160+0308+0300",
    "1160+093C",
    "1160+0308+093C",
    "1160+094D",
    "1160+0308+094D",
    "1160+200D",
    "1160+0308+200D",
    "1160/0378",
    "1160+0308/0378",
    "11A8/0020",
    "11A8+0308/0020",
    "11A8/000D",
    "11A8+0308/000D",
    "11A8/000A",
    "11A8+0308/000A",
    "11A8/0001",
    "11A8+0308/0001",
    "11A8+034F",
    "11A8+0308+034F",
    "11A8/1F1E6",
    "11A8+0308/1F1E6",
    "11A8/0600",
    "11A8+0308/0600",
    "11A8+0A03",
    "11A8+0308+0A03",
    "11A8/1100",
    "11A8+0308/1100",
    "11A8/1160",
    "11A8+0308/1160",
    "11A8+11A8",
    "11A8+0308/11A8",
    "11A8/AC00",
    "11A8+0308/AC00",
    "11A8/AC01",
    "11A8+0308/AC01",
    "11A8+0900",
    "11A8+0308+0900",
    "11A8+0903",
    "11A8+0308+0903",
    "11A8/0904",
    "11A8+0308/0904",
    "11A8/0D4E",
    "11A8+0308/0D4E",
    "11A8/0915",
    "11A8+0308/0915",
    "11A8/231A",
    "11A8+0308/231A",
    "11A8+0300",
    "11A8+0308+0300",
    "11A8+093C",
    "11A8+0308+093C",
    "11A8+094D",
    "11A8+0308+094D",
    "11A8+200D",
    "11A8+0308+200D",
    "11A8/0378",
    "11A8+0308/0378",
    "AC00/0020",
    "AC00+0308/0020",
    "AC00/000D",
    "AC00+0308/000D",
    "AC00/000A",
    "AC00+0308/000A",
    "AC00/0001",
    "AC00+0308/0001",
    "AC00+034F",
    "AC00+0308+034F",
    "AC00/1F1E6",
    "AC00+0308/1F1E6",
    "AC00/0600",
    "AC00+0308/0600",
    "AC00+0A03",
    "AC00+0308+0A03",
    "AC00/1100",
    "AC00+0308/1100",
    "AC00+1160",
    "AC00+0308/1160",
    "AC00+11A8",
    "AC00+0308/11A8",
    "AC00/AC00",
    "AC00+0308/AC00",
    "AC00/AC01",
    "AC00+0308/AC01",
    "AC00+0900",
    "AC00+0308+0900",
    "AC00+0903",
    "AC00+0308+0903",
    "AC00/0904",
    "AC00+0308/0904",
    "AC00/0D4E",
    "AC00+0308/0D4E",
    "AC00/0915",
    "AC00+0308/0915",
    "AC00/231A",
    "AC00+0308/231A",
    "AC00+0300",
    "AC00+0308+0300",
    "AC00+093C",
    "AC00+0308+093C",
    "AC00+094D",
    "AC00+0308+094D",
    "AC00+200D",
    "AC00+0308+200D",
    "AC00/0378",
    "AC00+0308/0378",
    "AC01/0020",
    "AC01+0308/0020",
    "AC01/000D",
    "AC01+0308/000D",
    "AC01/000A",
    "AC01+0308/000A",
    "AC01/0001",
    "AC01+0308/0001",
    "AC01+034F",
    "AC01+0308+034F",
    "AC01/1F1E6",
    "AC01+0308/1F1E6",
    "AC01/0600",
    "AC01+0308/0600",
    "AC01+0A03",
    "AC01+0308+0A03",
    "AC01/1100",
    "AC01+0308/1100",
    "AC01/1160",
    "AC01+0308/1160",
    "AC01+11A8",
    "AC01+0308/11A8",
    "AC01/AC00",
    "AC01+0308/AC00",
    "AC01/AC01",
    "AC01+0308/AC01",
    "AC01+0900",
    "AC01+0308+0900",
    "AC01+0903",
    "AC01+0308+0903",
    "AC01/0904",
    "AC01+0308/0904",
    "AC01/0D4E",
    "AC01+0308/0D4E",
    "AC01/0915",
    "AC01+0308/0915",
    "AC01/231A",
    "AC01+0308/231A",
    "AC01+0300",
    "AC01+0308+0300",
    "AC01+093C",
    "AC01+0308+093C",
    "AC01+094D",
    "AC01+0308+094D",
    "AC01+200D",
    "AC01+0308+200D",
    "AC01/0378",
    "AC01+0308/0378",
    "0900/0020",
    "0900+0308/0020",
    "0900/000D",
    "0900+0308/000D",
    "0900/000A",
    "0900+0308/000A",
    "0900/0001",
    "0900+0308/0001",
    "0900+034F",
    "0900+0308+034F",
    "0900/1F1E6",
    "0900+0308/1F1E6",
    "0900/0600",
    "0900+0308/0600",
    "0900+0A03",
    "0900+0308+0A03",
    "0900/1100",
    "0900+0308/1100",
    "0900/1160",
    "0900+0308/1160",
    "0900/11A8",
    "0900+0308/11A8",
    "0900/AC00",
    "0900+0308/AC00",
    "0900/AC01",
    "0900+0308/AC01",
    "0900+0900",
    "0900+0308+0900",
    "0900+0903",
    "0900+0308+0903",
    "0900/0904",
    "0900+0308/0904",
    "0900/0D4E",
    "0900+0308/0D4E",
    "0900/0915",
    "0900+0308/0915",
    "0900/231A",
    "0900+0308/231A",
    "0900+0300",
    "0900+0308+0300",
    "0900+093C",
    "0900+0308+093C",
    "0900+094D",
    "0900+0308+094D",
    "0900+200D",
    "0900+0308+200D",
    "0900/0378",
    "0900+0308/0378",
    "0903/0020",
    "0903+0308/0020",
    "0903/000D",
    "0903+0308/000D",
    "0903/000A",
    "0903+0308/000A",
    "0903/0001",
    "0903+0308/0001",
    "0903+034F",
    "0903+0308+034F",
    "0903/1F1E6",
    "0903+0308/1F1E6",
    "0903/0600",
    "0903+0308/0600",
    "0903+0A03",
    "0903+0308+0A03",
    "0903/1100",
    "0903+0308/1100",
    "0903/1160",
    "0903+0308/1160",
    "0903/11A8",
    "0903+0308/11A8",
    "0903/AC00",
    "0903+0308/AC00",
    "0903/AC01",
    "0903+0308/AC01",
    "0903+0900",
    "0903+0308+0900",
    "0903+0903",
    "0903+0308+0903",
    "0903/0904",
    "0903+0308/0904",
    "0903/0D4E",
    "0903+0308/0D4E",
    "0903/0915",
    "0903+0308/0915",
    "0903/231A",
    "0903+0308/231A",
    "0903+0300",
    "0903+0308+0300",
    "0903+093C",
    "0903+0308+093C",
    "0903+094D",
    "0903+0308+094D",
    "0903+200D",
    "0903+0308+200D",
    "0903/0378",
    "0903+0308/0378",
    "0904/0020",
    "0904+0308/0020",
    "0904/000D",
    "0904+0308/000D",
    "0904/000A",
    "0904+0308/000A",
    "0904/0001",
    "0904+0308/0001",
    "0904+034F",
    "0904+0308+034F",
    "0904/1F1E6",
    "0904+0308/1F1E6",
    "0904/0600",
    "0904+0308/0600",
    "0904+0A03",
    "0904+0308+0A03",
    "0904/1100",
    "0904+0308/1100",
    "0904/1160",
    "0904+0308/1160",
    "0904/11A8",
    "0904+0308/11A8",
    "0904/AC00",
    "0904+0308/AC00",
    "0904/AC01",
    "0904+0308/AC01",
    "0904+0900",
    "0904+0308+0900",
    "0904+0903",
    "0904+0308+0903",
    "0904/0904",
    "0904+0308/0904",
    "0904/0D4E",
    "0904+0308/0D4E",
    "0904/0915",
    "0904+0308/0915",
    "0904/231A",
    "0904+0308/231A",
    "0904+0300",
    "0904+0308+0300",
    "0904+093C",
    "0904+0308+093C",
    "0904+094D",
    "0904+0308+094D",
    "0904+200D",
    "0904+0308+200D",
    "0904/0378",
    "0904+0308/0378",
    "0D4E+0020",
    "0D4E+0308/0020",
    "0D4E/000D",
    "0D4E+0308/000D",
    "0D4E/000A",
    "0D4E+0308/000A",
    "0D4E/0001",
    "0D4E+0308/0001",
    "0D4E+034F",
    "0D4E+0308+034F",
    "0D4E+1F1E6",
    "0D4E+0308/1F1E6",
    "0D4E+0600",
    "0D4E+0308/0600",
    "0D4E+0A03",
    "0D4E+0308+0A03",
    "0D4E+1100",
    "0D4E+0308/1100",
    "0D4E+1160",
    "0D4E+0308/1160",
    "0D4E+11A8",
    "0D4E+0308/11A8",
    "0D4E+AC00",
    "0D4E+0308/AC00",
    "0D4E+AC01",
    "0D4E+0308/AC01",
    "0D4E+0900",
    "0D4E+0308+0900",
    "0D4E+0903",
    "0D4E+0308+0903",
    "0D4E+0904",
    "0D4E+0308/0904",
    "0D4E+0D4E",
    "0D4E+0308/0D4E",
    "0D4E+0915",
    "0D4E+0308/0915",
    "0D4E+231A",
    "0D4E+0308/231A",
    "0D4E+0300",
    "0D4E+0308+0300",
    "0D4E+093C",
    "0D4E+0308+093C",
    "0D4E+094D",
    "0D4E+0308+094D",
    "0D4E+200D",
    "0D4E+0308+200D",
    "0D4E+0378",
    "0D4E+0308/0378",
    "0915/0020",
    "0915+0308/0020",
    "0915/000D",
    "0915+0308/000D",
    "0915/000A",
    "0915+0308/000A",
    "0915/0001",
    "0915+0308/0001",
    "0915+034F",
    "0915+0308+034F",
    "0915/1F1E6",
    "0915+0308/1F1E6",
    "0915/0600",
    "0915+0308/0600",
    "0915+0A03",
    "0915+0308+0A03",
    "0915/1100",
    "0915+0308/1100",
    "0915/1160",
    "0915+0308/1160",
    "0915/11A8",
    "0915+0308/11A8",
    "0915/AC00",
    "0915+0308/AC00",
    "0915/AC01",
    "0915+0308/AC01",
    "0915+0900",
    "0915+0308+0900",
    "0915+0903",
    "0915+0308+0903",
    "0915/0904",
    "0915+0308/0904",
    "0915/0D4E",
    "0915+0308/0D4E",
    "0915/0915",
    "0915+0308/0915",
    "0915/231A",
    "0915+0308/231A",
    "0915+0300",
    "0915+0308+0300",
    "0915+093C",
    "0915+0308+093C",
    "0915+094D",
    "0915+0308+094D",
    "0915+200D",
    "0915+0308+200D",
    "0915/0378",
    "0915+0308/0378",
    "231A/0020",
    "231A+0308/0020",
    "231A/000D",
    "231A+0308/000D",
    "231A/000A",
    "231A+0308/000A",
    "231A/0001",
    "231A+0308/0001",
    "231A+034F",
    "231A+0308+034F",
    "231A/1F1E6",
    "231A+0308/1F1E6",
    "231A/0600",
    "231A+0308/0600",
    "231A+0A03",
    "231A+0308+0A03",
    "231A/1100",
    "231A+0308/1100",
    "231A/1160",
    "231A+0308/1160",
    "231A/11A8",
    "231A+0308/11A8",
    "231A/AC00",
    "231A+0308/AC00",
    "231A/AC01",
    "231A+0308/AC01",
    "231A+0900",
    "231A+0308+0900",
    "231A+0903",
    "231A+0308+0903",
    "231A/0904",
    "231A+0308/0904",
    "231A/0D4E",
    "231A+0308/0D4E",
    "231A/0915",
    "231A+0308/0915",
    "231A/231A",
    "231A+0308/231A",
    "231A+0300",
    "231A+0308+0300",
    "231A+093C",
    "231A+0308+093C",
    "231A+094D",
    "231A+0308+094D",
    "231A+200D",
    "231A+0308+200D",
    "231A/0378",
    "231A+0308/0378",
    "0300/0020",
    "0300+0308/0020",
    "0300/000D",
    "0300+0308/000D",
    "0300/000A",
    "0300+0308/000A",
    "0300/0001",
    "0300+0308/0001",
    "0300+034F",
    "0300+0308+034F",
    "0300/1F1E6",
    "0300+0308/1F1E6",
    "0300/0600",
    "0300+0308/0600",
    "0300+0A03",
    "0300+0308+0A03",
    "0300/1100",
    "0300+0308/1100",
    "0300/1160",
    "0300+0308/1160",
    "0300/11A8",
    "0300+0308/11A8",
    "0300/AC00",
    "0300+0308/AC00",
    "0300/AC01",
    "0300+0308/AC01",
    "0300+0900",
    "0300+0308+0900",
    "0300+0903",
    "0300+0308+0903",
    "0300/0904",
    "0300+0308/0904",
    "0300/0D4E",
    "0300+0308/0D4E",
    "0300/0915",
    "0300+0308/0915",
    "0300/231A",
    "0300+0308/231A",
    "0300+0300",
    "0300+0308+0300",
    "0300+093C",
    "0300+0308+093C",
    "0300+094D",
    "0300+0308+094D",
    "0300+200D",
    "0300+0308+200D",
    "0300/0378",
    "0300+0308/0378",
    "093C/0020",
    "093C+0308/0020",
    "093C/000D",
    "093C+0308/000D",
    "093C/000A",
    "093C+0308/000A",
    "093C/0001",
    "093C+0308/0001",
    "093C+034F",
    "093C+0308+034F",
    "093C/1F1E6",
    "093C+0308/1F1E6",
    "093C/0600",
    "093C+0308/0600",
    "093C+0A03",
    "093C+0308+0A03",
    "093C/1100",
    "093C+0308/1100",
    "093C/1160",
    "093C+0308/1160",
    "093C/11A8",
    "093C+0308/11A8",
    "093C/AC00",
    "093C+0308/AC00",
    "093C/AC01",
    "093C+0308/AC01",
    "093C+0900",
    "093C+0308+0900",
    "093C+0903",
    "093C+0308+0903",
    "093C/0904",
    "093C+0308/0904",
    "093C/0D4E",
    "093C+0308/0D4E",
    "093C/0915",
    "093C+0308/0915",
    "093C/231A",
    "093C+0308/231A",
    "093C+0300",
    "093C+0308+0300",
    "093C+093C",
    "093C+0308+093C",
    "093C+094D",
    "093C+0308+094D",
    "093C+200D",
    "093C+0308+200D",
    "093C/0378",
    "093C+0308/0378",
    "094D/0020",
    "094D+0308/0020",
    "094D/000D",
    "094D+0308/000D",
    "094D/000A",
    "094D+0308/000A",
    "094D/0001",
    "094D+0308/0001",
    "094D+034F",
    "094D+0308+034F",
    "094D/1F1E6",
    "094D+0308/1F1E6",
    "094D/0600",
    "094D+0308/0600",
    "094D+0A03",
    "094D+0308+0A03",
    "094D/1100",
    "094D+0308/1100",
    "094D/1160",
    "094D+0308/1160",
    "094D/11A8",
    "094D+0308/11A8",
    "094D/AC00",
    "094D+0308/AC00",
    "094D/AC01",
    "094D+0308/AC01",
    "094D+0900",
    "094D+0308+0900",
    "094D+0903",
    "094D+0308+0903",
    "094D/0904",
    "094D+0308/0904",
    "094D/0D4E",
    "094D+0308/0D4E",
    "094D/0915",
    "094D+0308/0915",
    "094D/231A",
    "094D+0308/231A",
    "094D+0300",
    "094D+0308+0300",
    "094D+093C",
    "094D+0308+093C",
    "094D+094D",
    "094D+0308+094D",
    "094D+200D",
    "094D+0308+200D",
    "094D/0378",
    "094D+0308/0378",
    "200D/0020",
    "200D+0308/0020",
    "200D/000D",
    "200D+0308/000D",
    "200D/000A",
    "200D+0308/000A",
    "200D/0001",
    "200D+0308/0001",
    "200D+034F",
    "200D+0308+034F",
    "200D/1F1E6",
    "200D+0308/1F1E6",
    "200D/0600",
    "200D+0308/0600",
    "200D+0A03",
    "200D+0308+0A03",
    "200D/1100",
    "200D+0308/1100",
    "200D/1160",
    "200D+0308/1160",
    "200D/11A8",
    "200D+0308/11A8",
    "200D/AC00",
    "200D+0308/AC00",
    "200D/AC01",
    "200D+0308/AC01",
    "200D+0900",
    "200D+0308+0900",
    "200D+0903",
    "200D+0308+0903",
    "200D/0904",
    "200D+0308/0904",
    "200D/0D4E",
    "200D+0308/0D4E",
    "200D/0915",
    "200D+0308/0915",
    "200D/231A",
    "200D+0308/231A",
    "200D+0300",
    "200D+0308+0300",
    "200D+093C",
    "200D+0308+093C",
    "200D+094D",
    "200D+0308+094D",
    "200D+200D",
    "200D+0308+200D",
    "200D/0378",
    "200D+0308/0378",
    "0378/0020",
    "0378+0308/0020",
    "0378/000D",
    "0378+0308/000D",
    "0378/000A",
    "0378+0308/000A",
    "0378/0001",
    "0378+0308/0001",
    "0378+034F",
    "0378+0308+034F",
    "0378/1F1E6",
    "0378+0308/1F1E6",
    "0378/0600",
    "0378+0308/0600",
    "0378+0A03",
    "0378+0308+0A03",
    "0378/1100",
    "0378+0308/1100",
    "0378/1160",
    "0378+0308/1160",
    "0378/11A8",
    "0378+0308/11A8",
    "0378/AC00",
    "0378+0308/AC00",
    "0378/AC01",
    "0378+0308/AC01",
    "0378+0900",
    "0378+0308+0900",
    "0378+0903",
    "0378+0308+0903",
    "0378/0904",
    "0378+0308/0904",
    "0378/0D4E",
    "0378+0308/0D4E",
    "0378/0915",
    "0378+0308/0915",
    "0378/231A",
    "0378+0308/231A",
    "0378+0300",
    "0378+0308+0300",
    "0378+093C",
    "0378+0308+093C",
    "0378+094D",
    "0378+0308+094D",
    "0378+200D",
    "0378+0308+200D",
    "0378/0378",
    "0378+0308/0378",
    "000D+000A/0061/000A/0308",
    "0061+0308",
    "0020+200D/0646",
    "0646+200D/0020",
    "1100+1100",
    "AC00+11A8/1100",
    "AC01+11A8/1100",
    "1F1E6+1F1E7/1F1E8/0062",
    "0061/1F1E6+1F1E7/1F1E8/0062",
    "0061/1F1E6+1F1E7+200D/1F1E8/0062",
    "0061/1F1E6+200D/1F1E7+1F1E8/0062",
    "0061/1F1E6+1F1E7/1F1E8+1F1E9/0062",
    "0061+200D",
    "0061+0308/0062",
    "0061+0903/0062",
    "0061/0600+0062",
    "1F476+1F3FF/1F476",
    "0061+1F3FF/1F476",
    "0061+1F3FF/1F476+200D+1F6D1",
    "1F476+1F3FF+0308+200D+1F476+1F3FF",
    "1F6D1+200D+1F6D1",
    "0061+200D/1F6D1",
    "2701+200D+2701",
    "0061+200D/2701",
    "0915/0924",
    "0915+094D+0924",
    "0915+094D+094D+0924",
    "0915+094D+200D+0924",
    "0915+093C+200D+094D+0924",
    "0915+093C+094D+200D+0924",
    "0915+094D+0924+094D+092F",
    "0915+094D/0061",
    "0061+094D/0924",
    "003F+094D/0924",
    "0915+094D+094D+0924",
};
// clang-format on

#define N(a) ((uint32_t) (sizeof(a) / sizeof((a)[0])))

static bool same(const uint32_t *a, const uint32_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return false;
    return true;
}

static void test_joining(void)
{
    begin();
    static const struct {
        uint32_t cp;
        zui_joining_t t;
    } T[] = {
        {0x0627, ZUI_JOIN_R},   {0x0628, ZUI_JOIN_D},  {0x0644, ZUI_JOIN_D},  {0x0640, ZUI_JOIN_C},
        {0x200D, ZUI_JOIN_C},   {0x200C, ZUI_JOIN_U},  {0x064E, ZUI_JOIN_T},  {0x0670, ZUI_JOIN_T},
        {0x0621, ZUI_JOIN_U},   {0x0710, ZUI_JOIN_R},  {0x0712, ZUI_JOIN_D},  {0x07CA, ZUI_JOIN_D},
        {0x1820, ZUI_JOIN_D},   {'a', ZUI_JOIN_U},     {0x0301, ZUI_JOIN_T},  {0x0629, ZUI_JOIN_R},
        {0x0648, ZUI_JOIN_R},   {0x06CC, ZUI_JOIN_D},  {0x0698, ZUI_JOIN_R},  {0x0686, ZUI_JOIN_D},
        {0x06AF, ZUI_JOIN_D},   {0x06A9, ZUI_JOIN_D},  {0x0780, ZUI_JOIN_U},  {0x05D0, ZUI_JOIN_U},
        {0x0660, ZUI_JOIN_U},   {0x00AD, ZUI_JOIN_T},  {0x0840, ZUI_JOIN_R},  {0x0841, ZUI_JOIN_D},
        {0x110000, ZUI_JOIN_U}, {0x10AC0, ZUI_JOIN_D}, {0x1E900, ZUI_JOIN_D}, {0xA840, ZUI_JOIN_D},
    };
    for (uint32_t k = 0; k < N(T); k++)
        CHECK(zui_shape_joining(T[k].cp) == T[k].t, "joining U+%04X = %d want %d",
              (unsigned) T[k].cp, zui_shape_joining(T[k].cp), T[k].t);
    end("test_joining");
}

static void test_presentation(void)
{
    begin();
    static const uint32_t P[][5] = {
        {0x0628, 0xFE8F, 0xFE90, 0xFE91, 0xFE92}, {0x0627, 0xFE8D, 0xFE8E, 0xFE8D, 0xFE8E},
        {0x0644, 0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0}, {0x0645, 0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4},
        {0x0633, 0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4}, {0x0647, 0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC},
        {0x0629, 0xFE93, 0xFE94, 0xFE93, 0xFE94}, {0x06CC, 0xFBFC, 0xFBFD, 0xFBFE, 0xFBFF},
        {0x067E, 0xFB56, 0xFB57, 0xFB58, 0xFB59}, {0x064A, 0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4},
        {0x0639, 0xFEC9, 0xFECA, 0xFECB, 0xFECC}, {0x0643, 0xFED9, 0xFEDA, 0xFEDB, 0xFEDC},
        {0x06AF, 0xFB92, 0xFB93, 0xFB94, 0xFB95}, {0x0686, 0xFB7A, 0xFB7B, 0xFB7C, 0xFB7D},
        {0x0698, 0xFB8A, 0xFB8B, 0xFB8A, 0xFB8B}, {0x062F, 0xFEA9, 0xFEAA, 0xFEA9, 0xFEAA},
        {0x0621, 0xFE80, 0xFE80, 0xFE80, 0xFE80}, {0x0626, 0xFE89, 0xFE8A, 0xFE8B, 0xFE8C},
    };
    for (uint32_t k = 0; k < N(P); k++)
        for (int f = ZUI_FORM_ISOL; f <= ZUI_FORM_MEDI; f++)
            CHECK(zui_shape_presentation(P[k][0], (zui_form_t) f) == P[k][f],
                  "U+%04X form %d -> %04X want %04X", (unsigned) P[k][0], f,
                  (unsigned) zui_shape_presentation(P[k][0], (zui_form_t) f), (unsigned) P[k][f]);
    CHECK(zui_shape_presentation(0x0628, ZUI_FORM_NONE) == 0x0628, "no form");
    CHECK(zui_shape_presentation('a', ZUI_FORM_INIT) == 'a', "latin");
    CHECK(zui_shape_presentation(0x0712, ZUI_FORM_INIT) == 0x0712, "syriac has none");
    CHECK(zui_shape_presentation(0x0628, (zui_form_t) 9) == 0x0628, "bad form");
    end("test_presentation");
}

static void test_forms(void)
{
    begin();
    uint8_t f[8];
    static const uint32_t BB[] = {0x0628, 0x0628};
    zui_shape_forms(BB, 2, f);
    CHECK(f[0] == ZUI_FORM_INIT && f[1] == ZUI_FORM_FINA, "beh beh");
    static const uint32_t BBB[] = {0x0628, 0x0628, 0x0628};
    zui_shape_forms(BBB, 3, f);
    CHECK(f[0] == ZUI_FORM_INIT && f[1] == ZUI_FORM_MEDI && f[2] == ZUI_FORM_FINA, "beh x3");
    static const uint32_t AB[] = {0x0627, 0x0628};
    zui_shape_forms(AB, 2, f);
    CHECK(f[0] == ZUI_FORM_ISOL && f[1] == ZUI_FORM_ISOL, "alef does not join after");
    static const uint32_t BA[] = {0x0628, 0x0627};
    zui_shape_forms(BA, 2, f);
    CHECK(f[0] == ZUI_FORM_INIT && f[1] == ZUI_FORM_FINA, "beh alef");
    static const uint32_t BHB[] = {0x0628, 0x064E, 0x0628};
    zui_shape_forms(BHB, 3, f);
    CHECK(f[0] == ZUI_FORM_INIT && f[1] == ZUI_FORM_NONE && f[2] == ZUI_FORM_FINA,
          "harakat are transparent");
    static const uint32_t BNB[] = {0x0628, 0x200C, 0x0628};
    zui_shape_forms(BNB, 3, f);
    CHECK(f[0] == ZUI_FORM_ISOL && f[1] == ZUI_FORM_NONE && f[2] == ZUI_FORM_ISOL, "ZWNJ breaks");
    static const uint32_t BJ[] = {0x0628, 0x200D};
    zui_shape_forms(BJ, 2, f);
    CHECK(f[0] == ZUI_FORM_INIT && f[1] == ZUI_FORM_NONE, "ZWJ joins");
    static const uint32_t JB[] = {0x200D, 0x0628};
    zui_shape_forms(JB, 2, f);
    CHECK(f[1] == ZUI_FORM_FINA, "ZWJ before joins");
    static const uint32_t TB[] = {0x0640, 0x0628, 0x0640};
    zui_shape_forms(TB, 3, f);
    CHECK(f[0] == ZUI_FORM_NONE && f[1] == ZUI_FORM_MEDI, "tatweel joins both sides");
    static const uint32_t SY[] = {0x0712, 0x0712, 0x0710};
    zui_shape_forms(SY, 3, f);
    CHECK(f[0] == ZUI_FORM_INIT && f[1] == ZUI_FORM_MEDI && f[2] == ZUI_FORM_FINA, "syriac");
    static const uint32_t SP[] = {0x0628, ' ', 0x0628};
    zui_shape_forms(SP, 3, f);
    CHECK(f[0] == ZUI_FORM_ISOL && f[2] == ZUI_FORM_ISOL, "space breaks");
    static const uint32_t H[] = {0x0628, 0x0621, 0x0628};
    zui_shape_forms(H, 3, f);
    CHECK(f[0] == ZUI_FORM_ISOL && f[1] == ZUI_FORM_NONE && f[2] == ZUI_FORM_ISOL,
          "hamza does not join");
    end("test_forms");
}

static void test_arabic(void)
{
    begin();
    uint32_t o[16];
    uint16_t m[16];
    static const uint32_t SALAM[] = {0x0633, 0x0644, 0x0627, 0x0645};
    static const uint32_t SALAM_W[] = {0xFEB3, 0xFEFC, 0xFEE1};
    uint32_t n = zui_shape_arabic(SALAM, 4, o, m, 16);
    CHECK(n == 3 && same(o, SALAM_W, 3), "salam: lam-alef final, meem isolated");
    CHECK(m[0] == 0 && m[1] == 1 && m[2] == 3, "salam map");
    static const uint32_t BAYT[] = {0x0628, 0x064A, 0x062A};
    static const uint32_t BAYT_W[] = {0xFE91, 0xFEF4, 0xFE96};
    n = zui_shape_arabic(BAYT, 3, o, NULL, 16);
    CHECK(n == 3 && same(o, BAYT_W, 3), "bayt");
    static const uint32_t MHMD[] = {0x0645, 0x062D, 0x0645, 0x062F};
    static const uint32_t MHMD_W[] = {0xFEE3, 0xFEA4, 0xFEE4, 0xFEAA};
    n = zui_shape_arabic(MHMD, 4, o, NULL, 16);
    CHECK(n == 4 && same(o, MHMD_W, 4), "muhammad");
    static const uint32_t LA[] = {0x0644, 0x0627};
    n = zui_shape_arabic(LA, 2, o, m, 16);
    CHECK(n == 1 && o[0] == 0xFEFB && m[0] == 0, "la isolated");
    static const uint32_t LAM_MADDA[] = {0x0644, 0x0622};
    n = zui_shape_arabic(LAM_MADDA, 2, o, NULL, 16);
    CHECK(n == 1 && o[0] == 0xFEF5, "lam alef madda");
    static const uint32_t LAM_HB[] = {0x0644, 0x0625};
    n = zui_shape_arabic(LAM_HB, 2, o, NULL, 16);
    CHECK(n == 1 && o[0] == 0xFEF9, "lam alef hamza below");
    static const uint32_t BLA[] = {0x0628, 0x0644, 0x0623};
    static const uint32_t BLA_W[] = {0xFE91, 0xFEF8};
    n = zui_shape_arabic(BLA, 3, o, m, 16);
    CHECK(n == 2 && same(o, BLA_W, 2) && m[1] == 1, "beh + lam alef hamza final");
    static const uint32_t ALA[] = {0x0627, 0x0644, 0x0627, 0x0628};
    static const uint32_t ALA_W[] = {0xFE8D, 0xFEFB, 0xFE8F};
    n = zui_shape_arabic(ALA, 4, o, NULL, 16);
    CHECK(n == 3 && same(o, ALA_W, 3), "after alef the ligature is isolated; nothing joins it");
    static const uint32_t LFA[] = {0x0644, 0x064E, 0x0627};
    static const uint32_t LFA_W[] = {0xFEDF, 0x064E, 0xFE8E};
    n = zui_shape_arabic(LFA, 3, o, NULL, 16);
    CHECK(n == 3 && same(o, LFA_W, 3), "a mark between lam and alef: no ligature");
    static const uint32_t LL[] = {0x0644, 0x0644};
    static const uint32_t LL_W[] = {0xFEDF, 0xFEDE};
    n = zui_shape_arabic(LL, 2, o, NULL, 16);
    CHECK(n == 2 && same(o, LL_W, 2), "lam lam is no ligature");
    static const uint32_t LAT[] = {'a', 0x0628, 'b'};
    static const uint32_t LAT_W[] = {'a', 0xFE8F, 'b'};
    n = zui_shape_arabic(LAT, 3, o, NULL, 16);
    CHECK(n == 3 && same(o, LAT_W, 3), "latin around");
    static const uint32_t SY[] = {0x0712, 0x0710};
    n = zui_shape_arabic(SY, 2, o, NULL, 16);
    CHECK(n == 2 && same(o, SY, 2), "syriac kept (no presentation forms)");
    n = zui_shape_arabic(MHMD, 4, o, NULL, 2);
    CHECK(n == 2 && o[1] == 0xFEA4, "cap");
    CHECK(zui_shape_arabic(MHMD, 0, o, NULL, 16) == 0, "empty");
    static const uint32_t FA[] = {0x0641, 0x0627, 0x0631, 0x0633, 0x06CC}; /* farsi */
    static const uint32_t FA_W[] = {0xFED3, 0xFE8E, 0xFEAD, 0xFEB3, 0xFBFD};
    n = zui_shape_arabic(FA, 5, o, NULL, 16);
    CHECK(n == 5 && same(o, FA_W, 5), "farsi");
    static const uint32_t UR[] = {0x0627, 0x0631, 0x062F, 0x0648}; /* urdu */
    static const uint32_t UR_W[] = {0xFE8D, 0xFEAD, 0xFEA9, 0xFEED};
    n = zui_shape_arabic(UR, 4, o, NULL, 16);
    CHECK(n == 4 && same(o, UR_W, 4), "urdu: right-joiners stay isolated");

    char out[64];
    const char *salam = "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85";
    uint32_t w = zui_shape_utf8(salam, (uint32_t) strlen(salam), out, sizeof out);
    CHECK(w == 9 && !strcmp(out, "\xEF\xBA\xB3\xEF\xBB\xBC\xEF\xBB\xA1"), "utf8 salam");
    w = zui_shape_utf8("\xD9\x84\xD8\xA7", 4, out, sizeof out);
    CHECK(w == 3 && !strcmp(out, "\xEF\xBB\xBB"), "utf8 la");
    w = zui_shape_utf8("\xD8\xA8\xD9\x8E\xD8\xA8", 6, out, sizeof out);
    CHECK(!strcmp(out, "\xEF\xBA\x91\xD9\x8E\xEF\xBA\x90"), "utf8 harakat");
    w = zui_shape_utf8("abc", 3, out, sizeof out);
    CHECK(w == 3 && !strcmp(out, "abc"), "utf8 latin");
    w = zui_shape_utf8(salam, (uint32_t) strlen(salam), out, 5);
    CHECK(w == 3 && !strcmp(out, "\xEF\xBA\xB3"), "utf8 cut at a whole character");
    CHECK(zui_shape_utf8(salam, 8, out, 0) == 0, "utf8 no room");
    /* shape, then bidi: what a plain drawer is given */
    static zui_bidi_t bd;
    zui_shape_utf8(salam, (uint32_t) strlen(salam), out, sizeof out);
    zui_bidi_paragraph(&bd, out, (uint32_t) strlen(out), ZUI_BIDI_AUTO);
    char vis[64];
    zui_bidi_visual(&bd, 0, bd.n, vis, sizeof vis);
    CHECK(bd.para_level == 1 && !strcmp(vis, "\xEF\xBB\xA1\xEF\xBB\xBC\xEF\xBA\xB3"),
          "shaped then reordered");
    end("test_arabic");
}

static uint32_t enc(const uint32_t *cp, uint32_t n, char *out)
{
    uint32_t w = 0;
    for (uint32_t i = 0; i < n; i++) w += zui_utf8_encode(cp[i], out + w, 8);
    out[w] = 0;
    return w;
}

static void test_grapheme_ucd(void)
{
    begin();
    for (uint32_t k = 0; k < N(UCD_GRAPHEME); k++) {
        const char *p = UCD_GRAPHEME[k];
        char s[128];
        uint32_t len = 0, want[32], nw = 0;
        while (*p) {
            char *e;
            uint32_t cp = (uint32_t) strtoul(p, &e, 16);
            len += zui_utf8_encode(cp, s + len, 8);
            p = e;
            if (*p == '/') want[nw++] = len;
            if (*p) p++;
        }
        want[nw++] = len;
        uint32_t got[32], ng = 0;
        for (uint32_t at = 0; at < len && ng < 32;) got[ng++] = at = zui_grapheme_next(s, len, at);
        bool ok = ng == nw;
        for (uint32_t i = 0; ok && i < nw; i++) ok = got[i] == want[i];
        CHECK(ok, "grapheme line %u: %s", (unsigned) k, UCD_GRAPHEME[k]);
        /* prev walks back over the same boundaries */
        bool okp = true;
        for (uint32_t i = nw; i-- > 0;) {
            uint32_t prev = i ? want[i - 1u] : 0;
            if (zui_grapheme_prev(s, len, want[i]) != prev) okp = false;
        }
        CHECK(okp && zui_grapheme_count(s, len) == nw, "grapheme prev/count line %u", (unsigned) k);
    }
    end("test_grapheme_ucd");
}

static void test_grapheme(void)
{
    begin();
    static const struct {
        const char *s;
        uint32_t clusters;
        const char *why;
    } T[] = {
        {"e\xCC\x81", 1, "e + acute"},
        {"\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7", 2, "two flags"},
        {"\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB", 2, "a flag and half a flag"},
        {"\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7", 1, "family"},
        {"\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD", 1, "thumbs up, skin tone"},
        {"\xF0\x9F\x8F\xB3\xEF\xB8\x8F\xE2\x80\x8D\xF0\x9F\x8C\x88", 1, "rainbow flag"},
        {"a\xE2\x80\x8D\xF0\x9F\x91\xA9", 2, "ZWJ after a letter does not join an emoji"},
        {"\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7\xE0\xA4\xBF", 1, "ksha + i (GB9c)"},
        {"\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80", 2, "hindi"},
        {"\xE0\xA4\xA8\xE0\xA4\xAE\xE0\xA4\xB8\xE0\xA5\x8D\xE0\xA4\xA4\xE0\xA5\x87", 3, "namaste"},
        {"\r\n", 1, "CR LF"},
        {"\n\r", 2, "LF CR"},
        {"\xED\x95\x9C", 1, "hangul syllable"},
        {"\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB", 1, "hangul jamo L V T"},
        {"\xEA\xB0\x80\xE1\x86\xA8", 1, "LV + T"},
        {"\xEA\xB0\x81\xE1\x85\xA1", 2, "LVT + V breaks"},
        {"\xD8\xA8\xD9\x8E", 1, "beh + fatha"},
        {"\xD7\x90\xD6\xB8", 1, "alef + qamats"},
        {"\xE0\xB8\x81\xE0\xB8\xB3", 1, "thai sara am is a spacing mark"},
        {"\xD8\x80"
         "1",
         1, "prepend"},
        {"abc", 3, "letters"},
        {"", 0, "empty"},
    };
    for (uint32_t k = 0; k < N(T); k++)
        CHECK(zui_grapheme_count(T[k].s, (uint32_t) strlen(T[k].s)) == T[k].clusters,
              "%s: %u clusters", T[k].why,
              (unsigned) zui_grapheme_count(T[k].s, (uint32_t) strlen(T[k].s)));
    const char *fam = "x\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9y"; /* 1 + 11 + 1 bytes */
    uint32_t len = (uint32_t) strlen(fam);
    CHECK(zui_grapheme_next(fam, len, 0) == 1, "next after x");
    CHECK(zui_grapheme_next(fam, len, 1) == 12, "next over the family");
    CHECK(zui_grapheme_next(fam, len, 5) == 12, "next from inside");
    CHECK(zui_grapheme_next(fam, len, 12) == 13, "next to the end");
    CHECK(zui_grapheme_next(fam, len, 13) == 13 && zui_grapheme_next(fam, len, 99) == 13, "at end");
    CHECK(zui_grapheme_prev(fam, len, 13) == 12, "prev from the end");
    CHECK(zui_grapheme_prev(fam, len, 12) == 1, "prev over the family");
    CHECK(zui_grapheme_prev(fam, len, 8) == 1, "prev from inside");
    CHECK(zui_grapheme_prev(fam, len, 1) == 0 && zui_grapheme_prev(fam, len, 0) == 0, "prev to 0");
    CHECK(zui_grapheme_prev(fam, len, 99) == 12, "prev clamps");
    CHECK(zui_grapheme_is_boundary(fam, len, 0) && zui_grapheme_is_boundary(fam, len, 1), "b0 b1");
    CHECK(!zui_grapheme_is_boundary(fam, len, 5) && !zui_grapheme_is_boundary(fam, len, 8),
          "inside");
    CHECK(zui_grapheme_is_boundary(fam, len, 12) && zui_grapheme_is_boundary(fam, len, 13), "end");
    CHECK(!zui_grapheme_is_boundary(fam, len, 14), "past the end");
    CHECK(!zui_grapheme_is_boundary(NULL, 0, 3) && zui_grapheme_count(NULL, 9) == 0, "NULL");
    /* the flags: a caret between the two letters of one flag is not a boundary */
    const char *flags = "\xF0\x9F\x87\xBA\xF0\x9F\x87\xB8\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7";
    CHECK(!zui_grapheme_is_boundary(flags, 16, 4) && zui_grapheme_is_boundary(flags, 16, 8) &&
              !zui_grapheme_is_boundary(flags, 16, 12),
          "flag pairs");
    CHECK(zui_grapheme_break(0x1F1E6) == ZUI_GCB_RI && zui_grapheme_break(0x200D) == ZUI_GCB_ZWJ,
          "RI, ZWJ");
    CHECK(zui_grapheme_break(0xAC00) == ZUI_GCB_LV && zui_grapheme_break(0xAC01) == ZUI_GCB_LVT &&
              zui_grapheme_break(0xAC1C) == ZUI_GCB_LV && zui_grapheme_break(0xD7A3) == ZUI_GCB_LVT,
          "hangul LV/LVT computed");
    CHECK(zui_grapheme_break(0x1100) == ZUI_GCB_L && zui_grapheme_break(0x1161) == ZUI_GCB_V &&
              zui_grapheme_break(0x11A8) == ZUI_GCB_T,
          "jamo");
    CHECK(zui_grapheme_break('\r') == ZUI_GCB_CR && zui_grapheme_break('\n') == ZUI_GCB_LF &&
              zui_grapheme_break(0) == ZUI_GCB_CONTROL && zui_grapheme_break('a') == ZUI_GCB_OTHER,
          "controls");
    CHECK(zui_grapheme_break(0x0903) == ZUI_GCB_SPACINGMARK &&
              zui_grapheme_break(0x0600) == ZUI_GCB_PREPEND &&
              zui_grapheme_break(0x0301) == ZUI_GCB_EXTEND,
          "marks");
    CHECK(zui_extended_pictographic(0x1F600) && zui_extended_pictographic(0x00A9) &&
              !zui_extended_pictographic('a') && !zui_extended_pictographic(0x1F1E6),
          "pictographic");
    CHECK(zui_indic_conjunct_break(0x0915) == ZUI_INCB_CONSONANT &&
              zui_indic_conjunct_break(0x094D) == ZUI_INCB_LINKER &&
              zui_indic_conjunct_break(0x093C) == ZUI_INCB_EXTEND &&
              zui_indic_conjunct_break(0x200D) == ZUI_INCB_EXTEND &&
              zui_indic_conjunct_break('a') == ZUI_INCB_NONE &&
              zui_indic_conjunct_break(0x0905) == ZUI_INCB_NONE,
          "InCB");
    CHECK(zui_grapheme_break(0x110000) == ZUI_GCB_OTHER, "out of range");
    end("test_grapheme");
}

static void test_indic(void)
{
    begin();
    zui_indic_syllable_t s;
    uint32_t o[16];
    uint16_t m[16];
    CHECK(zui_indic_category(0x0915) == ZUI_INDIC_C && zui_indic_category(0x0939) == ZUI_INDIC_C &&
              zui_indic_category(0x0958) == ZUI_INDIC_C &&
              zui_indic_category(0x097F) == ZUI_INDIC_C,
          "consonants");
    CHECK(zui_indic_category(0x0905) == ZUI_INDIC_V && zui_indic_category(0x0914) == ZUI_INDIC_V &&
              zui_indic_category(0x0960) == ZUI_INDIC_V &&
              zui_indic_category(0x0972) == ZUI_INDIC_V,
          "vowels");
    CHECK(zui_indic_category(0x093F) == ZUI_INDIC_MPRE &&
              zui_indic_category(0x094E) == ZUI_INDIC_MPRE &&
              zui_indic_category(0x0940) == ZUI_INDIC_M &&
              zui_indic_category(0x093E) == ZUI_INDIC_M &&
              zui_indic_category(0x0947) == ZUI_INDIC_M &&
              zui_indic_category(0x0962) == ZUI_INDIC_M,
          "matras");
    CHECK(zui_indic_category(0x094D) == ZUI_INDIC_H && zui_indic_category(0x093C) == ZUI_INDIC_N &&
              zui_indic_category(0x0902) == ZUI_INDIC_VM &&
              zui_indic_category(0x0903) == ZUI_INDIC_VM &&
              zui_indic_category(0x0951) == ZUI_INDIC_A,
          "signs");
    CHECK(zui_indic_category(0x0964) == ZUI_INDIC_X && zui_indic_category(0x0966) == ZUI_INDIC_X &&
              zui_indic_category(0x093D) == ZUI_INDIC_X && zui_indic_category('a') == ZUI_INDIC_X &&
              zui_indic_category(0x200D) == ZUI_INDIC_ZWJ &&
              zui_indic_category(0x200C) == ZUI_INDIC_ZWNJ,
          "others");

    static const uint32_t KI[] = {0x0915, 0x093F};
    CHECK(zui_indic_syllable(KI, 2, 0, &s) == 2 && s.kind == ZUI_SYL_CONSONANT && s.base == 0 &&
              s.pre_matra == 1 && !s.reph && !s.dead && s.consonants == 1,
          "ki");
    uint32_t n = zui_indic_visual(KI, 2, o, m, 16);
    CHECK(n == 2 && o[0] == 0x093F && o[1] == 0x0915 && m[0] == 1 && m[1] == 0, "ki visual");

    static const uint32_t KSSI[] = {0x0915, 0x094D, 0x0937, 0x093F};
    static const uint32_t KSSI_W[] = {0x093F, 0x0915, 0x094D, 0x0937};
    CHECK(zui_indic_syllable(KSSI, 4, 0, &s) == 4 && s.base == 2 && s.pre_matra == 3 &&
              s.consonants == 2,
          "kssi");
    n = zui_indic_visual(KSSI, 4, o, NULL, 16);
    CHECK(n == 4 && same(o, KSSI_W, 4), "kssi: i before the conjunct");

    static const uint32_t HINDI[] = {0x0939, 0x093F, 0x0928, 0x094D, 0x0926, 0x0940};
    static const uint32_t HINDI_W[] = {0x093F, 0x0939, 0x0928, 0x094D, 0x0926, 0x0940};
    CHECK(zui_indic_syllable(HINDI, 6, 0, &s) == 2 && s.pre_matra == 1, "hindi 1");
    CHECK(zui_indic_syllable(HINDI, 6, 2, &s) == 6 && s.base == 4 && s.pre_matra < 0 &&
              s.consonants == 2,
          "hindi 2");
    n = zui_indic_visual(HINDI, 6, o, m, 16);
    CHECK(n == 6 && same(o, HINDI_W, 6) && m[0] == 1 && m[1] == 0 && m[5] == 5, "hindi visual");

    static const uint32_t RKI[] = {0x0930, 0x094D, 0x0915, 0x093F};
    static const uint32_t RKI_W[] = {0x093F, 0x0930, 0x094D, 0x0915};
    CHECK(zui_indic_syllable(RKI, 4, 0, &s) == 4 && s.reph && s.base == 2 && s.pre_matra == 3,
          "reph");
    n = zui_indic_visual(RKI, 4, o, NULL, 16);
    CHECK(n == 4 && same(o, RKI_W, 4), "reph visual");
    static const uint32_t RA[] = {0x0930, 0x094D};
    CHECK(zui_indic_syllable(RA, 2, 0, &s) == 2 && !s.reph && s.dead && s.base == 0,
          "ra halant alone is no reph");
    static const uint32_t RZ[] = {0x0930, 0x094D, 0x200D, 0x092F};
    CHECK(zui_indic_syllable(RZ, 4, 0, &s) == 4 && !s.reph && s.consonants == 2,
          "eyelash ra (ZWJ) is no reph");
    static const uint32_t KRA[] = {0x0915, 0x094D, 0x0930};
    CHECK(zui_indic_syllable(KRA, 3, 0, &s) == 3 && !s.reph && s.base == 2, "ra after: no reph");

    static const uint32_t K_[] = {0x0915, 0x094D, ' '};
    CHECK(zui_indic_syllable(K_, 3, 0, &s) == 2 && s.dead && s.pre_matra < 0, "dead consonant");
    static const uint32_t KZ[] = {0x0915, 0x094D, 0x200D, 0x0937};
    CHECK(zui_indic_syllable(KZ, 4, 0, &s) == 4 && s.consonants == 2, "half form with ZWJ");
    static const uint32_t KN[] = {0x0915, 0x094D, 0x200C, 0x0937};
    CHECK(zui_indic_syllable(KN, 4, 0, &s) == 4 && s.consonants == 2, "explicit halant ZWNJ");
    static const uint32_t K_ZWNJ[] = {0x0915, 0x094D, 0x200C};
    CHECK(zui_indic_syllable(K_ZWNJ, 3, 0, &s) == 3 && s.dead, "dead with ZWNJ");

    static const uint32_t NUK[] = {0x0915, 0x093C, 0x093F};
    CHECK(zui_indic_syllable(NUK, 3, 0, &s) == 3 && s.pre_matra == 2, "nukta");
    n = zui_indic_visual(NUK, 3, o, NULL, 16);
    CHECK(n == 3 && o[0] == 0x093F && o[1] == 0x0915 && o[2] == 0x093C, "nukta visual");

    static const uint32_t NAMASTE[] = {0x0928, 0x092E, 0x0938, 0x094D, 0x0924, 0x0947};
    uint32_t syl = 0;
    for (uint32_t at = 0; at < 6; syl++) at = zui_indic_syllable(NAMASTE, 6, at, &s);
    CHECK(syl == 3 && s.base == 4 && s.start == 2 && s.end == 6, "namaste: 3 syllables");
    n = zui_indic_visual(NAMASTE, 6, o, NULL, 16);
    CHECK(n == 6 && same(o, NAMASTE, 6), "namaste unchanged");

    static const uint32_t KIM[] = {0x0915, 0x093F, 0x0902};
    CHECK(zui_indic_syllable(KIM, 3, 0, &s) == 3, "anusvara in the syllable");
    n = zui_indic_visual(KIM, 3, o, NULL, 16);
    CHECK(n == 3 && o[0] == 0x093F && o[1] == 0x0915 && o[2] == 0x0902, "anusvara stays after");
    static const uint32_t AM[] = {0x0905, 0x0902, 0x0915};
    CHECK(zui_indic_syllable(AM, 3, 0, &s) == 2 && s.kind == ZUI_SYL_VOWEL && s.base < 0,
          "vowel + anusvara");
    static const uint32_t BRK[] = {0x093F, 0x0915};
    CHECK(zui_indic_syllable(BRK, 2, 0, &s) == 1 && s.kind == ZUI_SYL_BROKEN, "lone matra");
    static const uint32_t OTH[] = {'a', 0x0915};
    CHECK(zui_indic_syllable(OTH, 2, 0, &s) == 1 && s.kind == ZUI_SYL_OTHER, "other");
    CHECK(zui_indic_syllable(OTH, 2, 2, &s) == 2 && s.start == 2 && s.end == 2, "at end");
    CHECK(zui_indic_syllable(KI, 2, 0, NULL) == 2, "no out");
    n = zui_indic_visual(HINDI, 6, o, NULL, 3);
    CHECK(n == 3 && o[0] == 0x093F && o[1] == 0x0939 && o[2] == 0x0928, "visual cap");
    static const uint32_t PE[] = {0x0915, 0x094E};
    n = zui_indic_visual(PE, 2, o, NULL, 16);
    CHECK(n == 2 && o[0] == 0x094E, "prishthamatra");
    /* syllables agree with grapheme clusters for these words */
    static const uint32_t *const W[] = {HINDI, NAMASTE, KSSI, RKI};
    static const uint32_t WN[] = {6, 6, 4, 4};
    for (uint32_t k = 0; k < 4; k++) {
        char u[64];
        uint32_t len = enc(W[k], WN[k], u), sy = 0;
        for (uint32_t at = 0; at < WN[k]; sy++) at = zui_indic_syllable(W[k], WN[k], at, NULL);
        CHECK(zui_grapheme_count(u, len) == sy, "word %u: syllables = clusters", (unsigned) k);
    }
    end("test_indic");
}

int main(void)
{
    test_joining();
    test_presentation();
    test_forms();
    test_arabic();
    test_grapheme_ucd();
    test_grapheme();
    test_indic();
    printf("%s test_zui_shape: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails,
           checks);
    return fails ? 1 : 0;
}
