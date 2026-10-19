/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* x86_userbin.S: the scheduler self-test's user program (a static x86-64
 * ELF64 built from kernel/userapp/x86_selftest.c) embedded as bytes, for
 * proc_create_from_elf to load into ring 3. The Makefile passes the path. */

.section .rodata.x86_selftest_elf, "a"
.balign 16
.global x86_selftest_elf
x86_selftest_elf:
    .incbin X86_USER_ELF
.global x86_selftest_elf_end
x86_selftest_elf_end:

.section .note.GNU-stack, "", @progbits
