/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_check.h — check-digit algorithms for the card networks.
 *
 * Three decimal check-digit schemes, one per network:
 *   Dragon      -> Luhn (mod 10, ISO/IEC 7812-1 Annex B)
 *   Phoenix     -> Damm (totally anti-symmetric quasigroup of order 10)
 *   Thunderbird -> Verhoeff (dihedral group D5)
 *
 * `*_digit(digits, len)` returns the check digit (0..9) to append to the
 * `len` payload digits, or -1 if the input is empty, NULL or not all
 * decimal digits. `*_valid(digits, len)` checks a string whose LAST digit is
 * the check digit (len >= 2).
 *
 * Detection properties (exercised exhaustively by test_cardnet.c):
 *   Luhn     catches every single-digit error and every adjacent transposition
 *            except 09 <-> 90.
 *   Damm     catches every single-digit error and every adjacent transposition.
 *   Verhoeff catches every single-digit error and every adjacent transposition.
 *
 * HONEST LIMITS. A check digit is an integrity check, not authority: it says
 * a number is self-consistent, nothing about whether it was issued, by whom,
 * or whether it is a payment credential anywhere outside this deployment.
 * One decimal check digit also cannot by itself separate three schemes: any
 * given number passes a foreign scheme about one time in ten by chance. The
 * cross-network guarantee (VSS invariant C5) is therefore enforced one level
 * up, in cardnet.h, by prefix plus an exclusivity rule at minting time.
 */
#ifndef ZXV_CN_CHECK_H
#define ZXV_CN_CHECK_H

#include <stdint.h>
#include <stdbool.h>

int cn_luhn_digit(const char *digits, uint32_t len);
bool cn_luhn_valid(const char *digits, uint32_t len);

int cn_damm_digit(const char *digits, uint32_t len);
bool cn_damm_valid(const char *digits, uint32_t len);

int cn_verhoeff_digit(const char *digits, uint32_t len);
bool cn_verhoeff_valid(const char *digits, uint32_t len);

#endif /* ZXV_CN_CHECK_H */
