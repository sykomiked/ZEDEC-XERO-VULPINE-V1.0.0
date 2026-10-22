/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mca_x86.h — x86_64 machine-check glue (mca_x86.c) over src/mca. */
#ifndef ZXV_MCA_X86_H
#define ZXV_MCA_X86_H

#include <stdint.h>

/* Bring up machine checks: CPUID MCE/MCA, MCG_CAP, enable every bank, log
 * then clear records left from before the reset, arm the #MC gate (vector
 * 18, own IST stack) and set CR4.MCE. Must run after the IDT is loaded
 * (x86_ring3_init). Prints one line through log. Returns the bank count,
 * or 0 when the CPU has no MCA (nothing is enabled then). */
uint32_t x86_mca_init(void (*log)(const char *line));

/* Read every bank, log and clear what is valid. Corrected errors raise no
 * interrupt (CMCI is not used), so this is how they are seen. Returns the
 * number of records logged. Does nothing before x86_mca_init. */
uint32_t x86_mca_poll(void);

#endif
