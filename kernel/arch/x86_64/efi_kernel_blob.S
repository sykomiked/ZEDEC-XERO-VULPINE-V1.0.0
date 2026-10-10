/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* efi_kernel_blob.S — embed the flat x86-64 kernel image inside BOOTX64.EFI so
 * the EFI app is self-contained. Copied to Multiboot1 link addr 0x100000. */
.section .kernel_blob, "a"
.balign 16
.globl _kernel_blob_start
.hidden _kernel_blob_start
_kernel_blob_start:
.incbin "kernel_x86_64.bin"
.balign 16
.globl _kernel_blob_end
.hidden _kernel_blob_end
_kernel_blob_end:
