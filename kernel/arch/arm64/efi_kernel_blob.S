/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* efi_kernel_blob.S — embed the flat kernel image inside BOOTAA64.EFI so the EFI
 * application is self-contained (no ESP filesystem code needed to load it). The
 * stub copies [_kernel_blob_start.._kernel_blob_end) to the kernel link address
 * 0x40080000, then branches. .incbin is relative to the assembler CWD (the
 * build runs from 05_KERNEL/, where kernel_arm64.bin is produced first). */
.section .kernel_blob, "a"
.balign 16
.globl _kernel_blob_start
_kernel_blob_start:
.incbin "kernel_arm64.bin"
.balign 16
.globl _kernel_blob_end
_kernel_blob_end:
