/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zl_kpy.h — the number view of the alphabet: Kaṭapayādi numbers for
 * Sanskrit names, and digital-root and residue labels (docs/spec/ALPHABET.md
 * section 5).
 *
 * Kaṭapayādi gives each consonant a digit:
 *   1 k ṭ p y   2 kh ṭh ph r   3 g ḍ b l   4 gh ḍh bh v   5 ṅ ṇ m ś
 *   6 c t ṣ     7 ch th s      8 j d h     9 jh dh        0 ñ n
 * A name is read one consonant group at a time; in a group of consonants
 * only the last one counts, and a vowel with no consonant before it counts
 * 0. Digits run right to left in place value (ankānām vāmato gatiḥ): the
 * first digit is the units. So √gam is 3 then 5, the number 53.
 * One departure from the verse rule: a root is named in its bare form, so a
 * final consonant with no vowel after it (the m of gam) also counts.
 *
 * These are labels. Many names share a number, so a number does not give a
 * name back; zl_kpy_spell writes one canonical spelling that reads back to
 * the same number. The digital root and residues of a CID are quick checks
 * for a hand-copied number, never a placement: MATRIX.tsv places blocks.
 *
 * Freestanding C11: integers only, no allocation. */
#ifndef ZXV_ZL_KPY_H
#define ZXV_ZL_KPY_H

#include <stdint.h>

#define ZL_KPY_MAXDIG 19u /* the most digits a uint64_t holds for every value */

/* The digits of a name in IAST (UTF-8), in reading order, into d[cap].
 * Returns how many, or -1 for a character that is not IAST Sanskrit or
 * more than cap digits. Anusvāra (ṃ) and visarga (ḥ) give no digit. */
int32_t zl_kpy_digits(const char *iast, uint8_t *d, uint32_t cap);
/* The name's number (first digit the units). Returns the digit count, or
 * -1 as zl_kpy_digits does, or for more than ZL_KPY_MAXDIG digits. */
int32_t zl_kpy_value(const char *iast, uint64_t *v);
/* One canonical spelling of an n-digit number (leading zeros kept, n from 1
 * to ZL_KPY_MAXDIG), ASCII: 1 ka, 2 kha, 3 ga, 4 gha, 5 ma, 6 ca, 7 cha,
 * 8 ja, 9 jha, 0 na. Returns its length, or -1 if v has more than n digits
 * or out[cap] is too small (it is NUL-terminated). */
int32_t zl_kpy_spell(uint64_t v, uint32_t n, char *out, uint32_t cap);

/* The digital root: 0 for 0, else 1 + (n - 1) mod 9. */
uint32_t zl_dr9(uint64_t n);
/* A big-endian byte string read as one number, mod m (m from 1 to 2^24). */
uint32_t zl_mod_be(const uint8_t *b, uint32_t len, uint32_t m);
/* The digital root of a big-endian byte string read as one number (a CID's
 * 32 hash bytes, say): 0 when every byte is 0. */
uint32_t zl_dr9_be(const uint8_t *b, uint32_t len);

#endif
