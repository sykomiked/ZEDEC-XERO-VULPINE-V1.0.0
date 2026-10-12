/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* x86_extable.c — exception-fixup table lookup (pure C, host-testable). */
#include "x86_extable.h"

uint64_t x86_extable_search(const x86_extable_entry_t *start, const x86_extable_entry_t *end,
                            uint64_t rip)
{
    if (!start || !end || end < start) return 0;
    for (const x86_extable_entry_t *e = start; e < end; e++)
        if (e->insn == rip && e->fixup != 0) return e->fixup;
    return 0;
}
