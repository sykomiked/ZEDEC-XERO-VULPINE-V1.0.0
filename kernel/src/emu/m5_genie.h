/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* m5_genie.h — the Game Genie mechanism as a kernel interception layer.
 *
 * The retro Game Genie sat between the cartridge and the CPU bus: an
 * Address / Compare / Replace tuple, applied to every read the CPU made.
 * Here it is the same thing for the whole M5 kernel — an in-memory,
 * live-patching and customisation toolchain that works at the execution
 * layer (the emulated cores' bus reads) and at the dimensional desktop
 * (the MegaROM "warp gate"), on every substrate, without self-modifying
 * code that would break W^X.
 *
 * The tuple is the Interception & Substitution Vector Table (ISVT):
 *   target_addr   the address whose reads are intercepted
 *   compare_val   the value the read must have for the patch to fire
 *                 (0 = unconditional)
 *   replace_val   the value returned instead
 *   width_bytes   the access width the patch covers (1, 2 or 4)
 *   code_str      the human code ("ZXV-LATT-144X"), for the CLI and UI
 *   hits          how many reads the patch intercepted
 */
#ifndef ZXV_M5_GENIE_H
#define ZXV_M5_GENIE_H

#include <stdint.h>

#define ZXV_GENIE_MAX         32u
#define ZXV_GENIE_CODE_LEN    16u
#define ZXV_GENIE_FLAG_ENABLED 0x0001u

typedef struct {
    uint64_t target_addr;
    uint64_t compare_val; /* 0 = unconditional */
    uint64_t replace_val;
    uint32_t width_bytes; /* 1, 2 or 4 */
    uint16_t flags;
    char     code_str[ZXV_GENIE_CODE_LEN];
    uint32_t hits;
} zxv_genie_patch_t;

/* Register a patch; returns its index, or -1 when the table is full or the
 * width is not 1/2/4. `code` is copied (truncated to the code field). */
int genie_apply_intercept(const char *code, uint64_t addr, uint64_t cmp,
                          uint64_t rep, uint32_t width);

/* Decode a symbolic code ("AAAA-BBBB-CCCC", three dash-separated words) into
 * the tuple deterministically: word 1 folds to the address, word 2 to the
 * compare value, word 3 to the replacement, each by FNV-1a over the word; a
 * trailing digit picks the width (default 1). The ref_forge/Gematria parser
 * is the future hook for the full Enochian/trit grammar; this canonical
 * spelling is what the CLI and tests use today. Returns 0 and fills *out on
 * a well-formed code, -1 otherwise. */
int genie_decode_code(const char *code, zxv_genie_patch_t *out);

/* The bus read hook: for a read of `width` bytes at `addr` returning `val`,
 * returns the replacement when an enabled patch covers it, else `val`
 * unchanged. Width must be 1, 2 or 4. */
uint32_t genie_eval_read(uint32_t addr, uint32_t val, uint32_t width);

/* True when at least one patch is enabled. */
int genie_active(void);

/* The number of patches currently registered. */
uint32_t genie_count(void);

/* The patch at index i (NULL when out of range). */
const zxv_genie_patch_t *genie_get(uint32_t i);

/* Disable and drop every patch. */
void genie_clear(void);

#endif /* ZXV_M5_GENIE_H */