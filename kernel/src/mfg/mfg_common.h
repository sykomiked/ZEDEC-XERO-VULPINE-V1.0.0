/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mfg_common.h — shared types for the manufacturing track (roadmap F).
 *
 * WHAT IT DOES
 *   Defines the status codes, the four-valued truth enum and the job
 *   verdicts used by mfg_gcode (F.2 toolpath checker), mfg_qc (F.4
 *   inspection) and mfg_budget (F.10 nature budgets + minimal F.6 queue).
 *
 * HONEST LIMITS
 *   Everything in the mfg_ modules is ADVISORY and read-only. Nothing here
 *   talks to a machine, sends a program, writes a setpoint or starts a job.
 *   The machine's own certified safety system (e-stops, interlocks, PLC
 *   safety) stays in charge, and a person approves every release. These
 *   modules are designed to support that review; they make no IEC 61508 or
 *   IEC 62443 compliance claim. They are not for weapons or munitions work.
 *
 * Freestanding: integer-only, no allocation, no libc.
 */
#ifndef ZXV_MFG_COMMON_H
#define ZXV_MFG_COMMON_H

#include <stdint.h>
#include <stddef.h>

/* Status codes returned by every fallible mfg_ function. A job verdict of
 * REJECT or HOLD is NOT an error: the call succeeded and reported it. */
typedef enum {
    MFG_OK = 0,
    MFG_ERR_NULL = -1,     /* a required pointer was NULL */
    MFG_ERR_RANGE = -2,    /* an argument is outside its allowed range */
    MFG_ERR_CAPACITY = -3, /* a fixed-capacity table is full */
    MFG_ERR_OVERFLOW = -4, /* exact integer arithmetic would overflow */
    MFG_ERR_STATE = -5,    /* call not valid in the current state */
    MFG_ERR_NOT_FOUND = -6 /* no such id */
} mfg_status_t;

/* Four-valued truth. The numeric values are IDENTICAL to lpres_state_t in
 * kernel/src/lpres/lpres.h (NEITHER=0, TRUE=1, FALSE=2, BOTH=3); we define
 * our own copy because lpres.h pulls in other headers. NEITHER = no
 * evidence (hold), BOTH = conflicting evidence (hold, re-measure, ask a
 * person). Neither is ever resolved by guessing. */
typedef enum { MFG_NEITHER = 0, MFG_TRUE = 1, MFG_FALSE = 2, MFG_BOTH = 3 } mfg_tv_t;

/* Release verdict for a checked program or an inspected part. */
typedef enum {
    MFG_RELEASE_OK = 0, /* no issue found; a person still approves */
    MFG_HOLD = 1,       /* possible problem or missing evidence: a person decides */
    MFG_REJECT = 2      /* definite problem: not releasable */
} mfg_verdict_t;

#endif /* ZXV_MFG_COMMON_H */
