/* elf.c — minimal ELF64 static executable loader for ZXV EL0
 *
 * See elf.h. Every field is bounds-checked against the image length
 * before use, so a malformed or hostile image is rejected rather than
 * dereferenced. The loader is transport- and mapper-agnostic: it only
 * reads the image and calls the caller's map callback.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV ELF-loader slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "elf.h"

/* ---- ELF64 on-disk structures (subset) ---- */
#define EI_NIDENT 16
typedef struct {
    uint8_t  e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} elf64_phdr_t;

#define PT_LOAD     1
#define ET_EXEC     2
#define EM_AARCH64  183
#define ELFCLASS64  2
#define ELFDATA2LSB 1

#define PF_X 0x1
#define PF_W 0x2
#define PF_R 0x4

#define MAX_PHNUM 16

/* Read a possibly-unaligned little-endian value from the image. */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

const char *elf_strerror(elf_result_t r) {
    switch (r) {
        case ELF_OK:          return "ok";
        case ELF_ERR_SHORT:   return "image truncated";
        case ELF_ERR_MAGIC:   return "not an ELF file";
        case ELF_ERR_CLASS:   return "not ELF64";
        case ELF_ERR_ENDIAN:  return "not little-endian";
        case ELF_ERR_TYPE:    return "not a static executable (ET_EXEC)";
        case ELF_ERR_MACHINE: return "not AArch64";
        case ELF_ERR_PHNUM:   return "bad program-header count";
        case ELF_ERR_WX:      return "segment is both writable and executable";
        case ELF_ERR_RANGE:   return "segment outside allowed address window";
        case ELF_ERR_MAP:     return "mapper rejected a segment";
        case ELF_ERR_ENTRY:   return "entry point not in a loaded segment";
        default:              return "unknown error";
    }
}

elf_result_t elf_load(const uint8_t *image, uint64_t len,
                      elf_map_fn map, void *ctx, uint64_t *entry_out) {
    if (!image || !map || len < sizeof(elf64_ehdr_t)) return ELF_ERR_SHORT;

    /* --- ELF identification --- */
    if (!(image[0] == 0x7f && image[1] == 'E' &&
          image[2] == 'L' && image[3] == 'F')) return ELF_ERR_MAGIC;
    if (image[4] != ELFCLASS64)  return ELF_ERR_CLASS;
    if (image[5] != ELFDATA2LSB) return ELF_ERR_ENDIAN;

    uint16_t e_type    = rd16(image + 16);
    uint16_t e_machine = rd16(image + 18);
    uint64_t e_entry   = rd64(image + 24);
    uint64_t e_phoff   = rd64(image + 32);
    uint16_t e_phentsize = rd16(image + 54);
    uint16_t e_phnum     = rd16(image + 56);

    if (e_type != ET_EXEC)      return ELF_ERR_TYPE;
    if (e_machine != EM_AARCH64) return ELF_ERR_MACHINE;
    if (e_phnum == 0 || e_phnum > MAX_PHNUM) return ELF_ERR_PHNUM;
    if (e_phentsize < sizeof(elf64_phdr_t)) return ELF_ERR_PHNUM;

    /* program-header table must lie fully inside the image */
    if (e_phoff > len) return ELF_ERR_SHORT;
    uint64_t ph_bytes = (uint64_t)e_phnum * e_phentsize;
    if (ph_bytes / e_phentsize != e_phnum) return ELF_ERR_SHORT; /* overflow */
    if (e_phoff + ph_bytes > len) return ELF_ERR_SHORT;

    bool entry_covered = false;

    for (uint16_t i = 0; i < e_phnum; i++) {
        const uint8_t *ph = image + e_phoff + (uint64_t)i * e_phentsize;
        uint32_t p_type   = rd32(ph + 0);
        uint32_t p_flags  = rd32(ph + 4);
        uint64_t p_offset = rd64(ph + 8);
        uint64_t p_vaddr  = rd64(ph + 16);
        uint64_t p_filesz = rd64(ph + 32);
        uint64_t p_memsz  = rd64(ph + 40);

        if (p_type != PT_LOAD) continue;
        if (p_memsz == 0) continue;
        if (p_filesz > p_memsz) return ELF_ERR_SHORT;

        /* file contents referenced by the segment must be in the image */
        if (p_offset > len) return ELF_ERR_SHORT;
        if (p_offset + p_filesz < p_offset) return ELF_ERR_SHORT; /* overflow */
        if (p_offset + p_filesz > len) return ELF_ERR_SHORT;

        /* W^X: refuse a segment that asks for both write and execute */
        bool w = (p_flags & PF_W) != 0;
        bool x = (p_flags & PF_X) != 0;
        if (w && x) return ELF_ERR_WX;

        /* address window check (overflow-safe) */
        if (p_vaddr < ELF_VA_MIN) return ELF_ERR_RANGE;
        if (p_vaddr + p_memsz < p_vaddr) return ELF_ERR_RANGE;
        if (p_vaddr + p_memsz > ELF_VA_MAX) return ELF_ERR_RANGE;

        elf_segment_t seg;
        seg.vaddr = p_vaddr;
        seg.file_size = p_filesz;
        seg.mem_size = p_memsz;
        seg.data = image + p_offset;
        seg.readable = (p_flags & PF_R) != 0;
        seg.writable = w;
        seg.executable = x;

        if (map(ctx, &seg) != 0) return ELF_ERR_MAP;

        if (e_entry >= p_vaddr && e_entry < p_vaddr + p_memsz)
            entry_covered = true;
    }

    if (!entry_covered) return ELF_ERR_ENTRY;
    if (entry_out) *entry_out = e_entry;
    return ELF_OK;
}
