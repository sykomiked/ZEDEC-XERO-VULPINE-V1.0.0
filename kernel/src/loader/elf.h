/* elf.h — minimal ELF64 static executable loader for ZXV EL0
 *
 * Parses and validates a statically-linked AArch64 ELF64 executable
 * and drives its PT_LOAD segments into a target address space through
 * a caller-supplied map+copy callback. The loader itself performs no
 * memory mapping, which keeps it (a) host-unit-testable and (b) reused
 * verbatim by the x86-64 port later — only the callback differs.
 *
 * Security posture: the loader validates every offset/size against the
 * file length before dereferencing, rejects non-AArch64 / non-static
 * images, and hands each segment's W/X intent to the callback so the
 * mapper can enforce W^X (a segment that is both writable and
 * executable is rejected outright).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ELF-loader slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef ZXV_ELF_H
#define ZXV_ELF_H

#include <stdint.h>
#include <stdbool.h>

/* A single loadable segment resolved from a PT_LOAD program header. */
typedef struct {
    uint64_t vaddr;       /* target virtual address */
    uint64_t file_size;   /* bytes to copy from the image (src) */
    uint64_t mem_size;    /* bytes to reserve (>= file_size; rest zeroed) */
    const uint8_t *data;  /* pointer into the ELF image for file_size bytes */
    bool readable;
    bool writable;
    bool executable;
} elf_segment_t;

/* Callback: place one segment. Return 0 on success, non-zero to abort
 * the load. `ctx` is passed through from elf_load. */
typedef int (*elf_map_fn)(void *ctx, const elf_segment_t *seg);

typedef enum {
    ELF_OK = 0,
    ELF_ERR_SHORT       = -1,  /* image smaller than a header claims */
    ELF_ERR_MAGIC       = -2,  /* not 0x7f 'E' 'L' 'F' */
    ELF_ERR_CLASS       = -3,  /* not ELFCLASS64 */
    ELF_ERR_ENDIAN      = -4,  /* not little-endian */
    ELF_ERR_TYPE        = -5,  /* not ET_EXEC (static) */
    ELF_ERR_MACHINE     = -6,  /* not EM_AARCH64 */
    ELF_ERR_PHNUM       = -7,  /* too many / zero program headers */
    ELF_ERR_WX          = -8,  /* a segment requested write+exec (W^X) */
    ELF_ERR_RANGE       = -9,  /* segment vaddr/size outside allowed window */
    ELF_ERR_MAP         = -10, /* callback rejected a segment */
    ELF_ERR_ENTRY       = -11, /* entry point not inside any loaded segment */
} elf_result_t;

/* Address window every segment (and the entry point) must fall inside.
 * Matches the EL0 user layout in el0_userspace.h (first 2MB block). */
#define ELF_VA_MIN  0x00001000ULL
#define ELF_VA_MAX  0x00200000ULL

/* Validate and load `image` (`len` bytes). Calls `map` once per PT_LOAD
 * segment. On success writes the entry VA to *entry_out and returns
 * ELF_OK; otherwise returns a negative elf_result_t and does not call
 * map again after the first failure. */
elf_result_t elf_load(const uint8_t *image, uint64_t len,
                      elf_map_fn map, void *ctx, uint64_t *entry_out);

const char *elf_strerror(elf_result_t r);

#endif /* ZXV_ELF_H */
