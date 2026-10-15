/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_shell_font.c — the system face, linked into the kernel image.
 *
 * The desktop's text is ASCW Golden Latin II (assets/fonts/ascw-golden,
 * SIL Open Font License 1.1), the same face as every other ZXV screen. The
 * TrueType file is placed in the image as it is, with .incbin (the build
 * runs from the repository root), in its own section so --gc-sections drops
 * it from an image that never draws the desktop. */
#include "zxv_shell.h"

__asm__(".pushsection .rodata.zxv_shell_font,\"a\"\n"
        ".balign 4\n"
        ".global zxv_shell_font_start\n"
        "zxv_shell_font_start:\n"
        ".incbin \"assets/fonts/ascw-golden/ASCW-Golden-Latin2.ttf\"\n"
        ".global zxv_shell_font_end\n"
        "zxv_shell_font_end:\n"
        ".popsection\n");

extern const uint8_t zxv_shell_font_start[];
extern const uint8_t zxv_shell_font_end[];

const uint8_t *zxv_shell_font_data(void)
{
    return zxv_shell_font_start;
}

uint32_t zxv_shell_font_size(void)
{
    return (uint32_t) (zxv_shell_font_end - zxv_shell_font_start);
}
