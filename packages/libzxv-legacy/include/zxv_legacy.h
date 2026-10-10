/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zxv_legacy.h - public header of libzxv-legacy.
 *
 * Exact data transcoding between mainframe-era representations and
 * modern ones, as one static library:
 *   cobol.h         COMP-3 / zoned / binary codecs, copybook parser,
 *                   fixed and RDW record readers (scaled 64-bit integers)
 *   fortran.h       unformatted sequential record framing, IBM HFP <->
 *                   IEEE 754 on bit patterns (no floating point types)
 *   ebcdic.h        EBCDIC CCSID 037/500/1047 <-> UTF-8
 *   lightningrod.h  representation adapters into exact rationals, with an
 *                   explicit lossless / lossy / no-bridge verdict
 *   rational.h      the 64-bit exact rationals those convert into
 * Integer only; no allocation. Each header's HONEST LIMITS section lists
 * what is not handled (no COMP-1/COMP-2, no edited pictures, single-byte
 * EBCDIC only, ...).
 *
 * Tested by the ZXV tree's test_legacy_*, test_lightningrod* and
 * test_rational* suites. NOT formally verified and NOT certified; check
 * results against your own mainframe data before relying on them.
 */
#ifndef ZXV_LEGACY_H
#define ZXV_LEGACY_H

/* system headers first: the kernel headers include them too */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "zxv/legacy/cobol.h"
#include "zxv/legacy/fortran.h"
#include "zxv/legacy/ebcdic.h"
#include "zxv/rational/rational.h"
#include "zxv/lightningrod/lightningrod.h"

#ifdef __cplusplus
}
#endif

#endif /* ZXV_LEGACY_H */
