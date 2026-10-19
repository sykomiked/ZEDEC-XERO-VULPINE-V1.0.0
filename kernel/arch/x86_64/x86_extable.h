/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* x86_extable.h — kernel exception-fixup table (x86_64).
 *
 * An entry pairs the address of ONE instruction that is allowed to fault with
 * the address execution resumes at if it does. The #GP path in ring3_asm.S
 * (x86_exc_common) looks the faulting RIP up here; on a hit it rewrites the
 * saved RIP to the fixup and IRETQs, so the faulting instruction continues at
 * its recovery path. On a miss the fault takes the existing route (ring-3
 * unwind, or report + halt), unchanged.
 *
 * Entries are emitted by assembly into section .rodata.zxv_extable (the
 * X86_EXTABLE_ENTRY macro in msr_safe_asm.S); linker64.ld brackets that
 * section with __zxv_extable_start / __zxv_extable_end and KEEP()s it.
 *
 * The lookup is pure C with no kernel dependencies so the host unit test
 * (kernel/arch/x86_64/test_x86_extable.c) runs it under ASan/UBSan. */
#ifndef X86_EXTABLE_H
#define X86_EXTABLE_H

#include <stdint.h>

typedef struct {
    uint64_t insn;  /* address of the instruction that may fault   */
    uint64_t fixup; /* where to resume when that instruction faults */
} x86_extable_entry_t;

/* Linear search of [start, end) for an entry whose insn == rip. Returns the
 * fixup address, or 0 when rip is not in the table (or the bounds are NULL or
 * reversed). An entry with fixup == 0 is treated as absent, so 0 is never a
 * valid recovery address. The first matching entry wins. */
uint64_t x86_extable_search(const x86_extable_entry_t *start, const x86_extable_entry_t *end,
                            uint64_t rip);

#endif
