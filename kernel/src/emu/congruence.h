/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* congruence.h — the structural-congruence QC test: a state is valid only if it
 * is geometrically sound (set/Fibonacci chains, complex plane, M5-axis
 * perpendicularity, >3-valued logic). See congruence.c. */
#ifndef ZXV_CONGRUENCE_H
#define ZXV_CONGRUENCE_H

/* Returns a 4-bit mask; 15 = fully congruent:
 *   bit0 set/Fibonacci   bit1 complex-plane   bit2 M5 perpendicularity   bit3 >3-valued logic */
int m5_congruence_selfcheck(void);

#endif /* ZXV_CONGRUENCE_H */
