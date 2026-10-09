/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Minimal x86-64 PE/COFF (EFI application) header — the x64 twin of the arm64
 * efi_header.S. One .text section covering RVA 0x1000..end; headers in the first
 * 0x1000. Relocs stripped (code is position-independent: rip-relative + a GDT
 * built on the fly). Machine = 0x8664 (AMD64), Subsystem = 10 (EFI application). */
.section .head, "a"
.globl _pe_start
_pe_start:
    /* ---- DOS header ---- */
    .ascii "MZ"                     /* 0x00: e_magic */
    .space 0x3c - 2                 /* pad to e_lfanew */
    .long  pe_hdr - _pe_start       /* 0x3c: e_lfanew -> PE header */

pe_hdr:
    .long  0x00004550              /* "PE\0\0" */
    /* ---- COFF file header ---- */
    .short 0x8664                  /* Machine = AMD64 (x86-64) */
    .short 1                       /* NumberOfSections */
    .long  0                       /* TimeDateStamp */
    .long  0                       /* PointerToSymbolTable */
    .long  0                       /* NumberOfSymbols */
    .short opt_end - opt_hdr       /* SizeOfOptionalHeader */
    .short 0x0206                  /* Characteristics: EXECUTABLE|LINE_NUMS_STRIPPED|DEBUG_STRIPPED */

opt_hdr:
    /* ---- Optional header (PE32+) ---- */
    .short 0x020B                  /* Magic = PE32+ */
    .byte  0, 0                    /* Linker version */
    .long  __image_end - 0x1000    /* SizeOfCode */
    .long  0                       /* SizeOfInitializedData */
    .long  0                       /* SizeOfUninitializedData */
    .long  efi_main                /* AddressOfEntryPoint (RVA; ImageBase=0) */
    .long  0x1000                  /* BaseOfCode */
    .quad  0                       /* ImageBase */
    .long  0x1000                  /* SectionAlignment */
    .long  0x1000                  /* FileAlignment */
    .short 0, 0                    /* OS version */
    .short 0, 0                    /* Image version */
    .short 0, 0                    /* Subsystem version */
    .long  0                       /* Win32VersionValue */
    .long  __image_end             /* SizeOfImage (0x1000-aligned) */
    .long  0x1000                  /* SizeOfHeaders */
    .long  0                       /* CheckSum */
    .short 10                      /* Subsystem = EFI_APPLICATION */
    .short 0x0040                  /* DllCharacteristics: DYNAMIC_BASE */
    .quad  0x10000                 /* SizeOfStackReserve */
    .quad  0x10000                 /* SizeOfStackCommit */
    .quad  0x10000                 /* SizeOfHeapReserve */
    .quad  0x10000                 /* SizeOfHeapCommit */
    .long  0                       /* LoaderFlags */
    .long  6                       /* NumberOfRvaAndSizes */
    .quad  0, 0, 0, 0, 0, 0        /* 6 empty data directories */
opt_end:

    /* ---- Section table (1 entry) ---- */
    .ascii ".text\0\0\0"           /* Name[8] */
    .long  __image_end - 0x1000    /* VirtualSize */
    .long  0x1000                  /* VirtualAddress */
    .long  __image_end - 0x1000    /* SizeOfRawData */
    .long  0x1000                  /* PointerToRawData */
    .long  0, 0                    /* PointerToRelocations, LineNumbers */
    .short 0, 0                    /* counts */
    .long  0xE0000020              /* Characteristics: CODE|EXEC|READ|WRITE */
