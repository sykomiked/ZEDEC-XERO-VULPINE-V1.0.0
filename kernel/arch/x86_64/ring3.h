/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ring3.h — x86-64 ring-3 parity: GDT/TSS/IDT + syscall gate + ring-3 entry. */
#ifndef X86_64_RING3_H
#define X86_64_RING3_H

/* Install the GDT (with ring-3 segments) + TSS + IDT (int 0x80 syscall gate). */
void x86_ring3_init(void);

/* Run a tiny user program in ring 3 that does SYS_WRITE + SYS_EXIT via int 0x80.
 * Returns 1 if it ran and returned cleanly (no CPU exception), 0 otherwise. */
int  x86_ring3_selftest(void);

#endif /* X86_64_RING3_H */
