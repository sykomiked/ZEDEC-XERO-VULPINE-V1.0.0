/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kindred_rules.h — Kindred's rules in C, the reference its Eve, KRON,
 * Trebago and MYRA blocks are checked against (blocks/kindred). Pure
 * integer functions, no state: the model (kindred_model.c) and the
 * language test (tests/kindred_zxapp.inc) both include this, so the
 * languages are tested against the very C the model runs.
 *
 *   KR1 FIELD LIMITS (contact_fields_ok). A contact's name is 1..128 bytes
 *       of UTF-8, its phone number 0..15 digits (E.164's most), its mail
 *       address 0..254 bytes (RFC 5321's path limit) and its note 0..1024
 *       bytes, each held to its own range with nothing relating them.
 *   KR2 NAME CODE. A name's first 9 letters, two decimal digits each,
 *       left-aligned in 18 digits (00 past the end): a..z and A..Z are
 *       01..26 (case folded), 0..9 are 27..36, any character past ASCII
 *       is 37 (one code per character), and every other byte (spaces,
 *       punctuation) is passed over. A name with nothing to code has code
 *       0, which matches nothing (KR4). A prefix is coded the same way,
 *       with len its count of codes (0..9).
 *   KR3 NAME PREFIX (name_prefix_hit). A name begins with a prefix of len
 *       codes when prefix <= name < prefix + 100^(9 - len): 1, else 0;
 *       len outside 0..9 or a code outside 0..KINDRED_NAME_CODE_MAX is
 *       refused. len 0 (nothing typed) matches every name.
 *   KR4 LIKELY DUPLICATES (contact_dupes). For each contact, how many
 *       other contacts share its card code (both 1 or more and equal) or
 *       its name code (both 1 or more and equal); a contact matched both
 *       ways counts once. A card code outside 0..KINDRED_CARD_CODE_MAX or
 *       a name code outside 0..KINDRED_NAME_CODE_MAX is refused. At most
 *       KINDRED_ROWS_MAX contacts at once. Advisory: a likely duplicate
 *       is shown, never merged without the owner.
 *   KR5 A CONTACT'S RECORD (contact_record_ok) fits ^ZXKINDRED: contact at
 *       most 6 digits, card and node codes 15 each, name code 18,
 *       verified 1 and stamp (ms since 1970) 13, sign aside.
 *   KR6 THE NAME INDEX (name_index_ok) fits ^ZXKINDREDNAME: name code at
 *       most 18 digits, contact 6 and stamp 13, sign aside.
 *   KR7 CARD AND NODE CODES. An agent_id (web4.card_verify) or a NodeID
 *       (net.peers) is 64 hex digits; its code is the value of its first
 *       12 plus 1 (1..2^48, so 0 stays "none"). */
#ifndef KINDRED_RULES_H
#define KINDRED_RULES_H

#include <stdbool.h>
#include <stdint.h>

#define KINDRED_REFUSED       (-1)
#define KINDRED_NAME_MIN      1
#define KINDRED_NAME_MAX      128 /* KR1: bytes */
#define KINDRED_PHONE_MAX     15  /* KR1: digits */
#define KINDRED_MAIL_MAX      254 /* KR1: bytes */
#define KINDRED_NOTE_MAX      1024
#define KINDRED_CODES         9u                          /* KR2: letters coded */
#define KINDRED_NAME_CODE_MAX INT64_C(999999999999999999) /* KR2: 18 digits */
#define KINDRED_CARD_CODE_MAX INT64_C(999999999999999)    /* KR4, KR5: 15 digits */
#define KINDRED_CONTACT_MAX   INT64_C(999999)             /* KR5: 6 digits */
#define KINDRED_STAMP_MAX     INT64_C(9999999999999)      /* KR5: 13 digits */
#define KINDRED_ROWS_MAX      64u                         /* KR4 */
#define KINDRED_HEX_ID        64u                         /* KR7 */

/* 100^k for k = 0..9 (KR3) */
static inline int64_t kindred_pow100(uint32_t k)
{
    int64_t p = 1;
    for (uint32_t i = 0; i < k && i < KINDRED_CODES; i++) p *= 100;
    return p;
}

/* KR1 */
static inline bool kindred_fields_ok(int64_t namelen, int64_t phone, int64_t mail, int64_t note)
{
    return namelen >= KINDRED_NAME_MIN && namelen <= KINDRED_NAME_MAX && phone >= 0 &&
           phone <= KINDRED_PHONE_MAX && mail >= 0 && mail <= KINDRED_MAIL_MAX && note >= 0 &&
           note <= KINDRED_NOTE_MAX;
}

/* KR2: the code of n bytes of s, and in *len the count of codes (0..9). */
static inline int64_t kindred_name_code(const char *s, uint32_t n, int64_t *len)
{
    int64_t code = 0;
    uint32_t k = 0;
    for (uint32_t i = 0; s && i < n && k < KINDRED_CODES; i++) {
        unsigned char c = (unsigned char) s[i];
        int64_t v = 0;
        if (c >= 'a' && c <= 'z')
            v = c - 'a' + 1;
        else if (c >= 'A' && c <= 'Z')
            v = c - 'A' + 1;
        else if (c >= '0' && c <= '9')
            v = c - '0' + 27;
        else if (c >= 0xC0u) /* the lead byte of a character past ASCII */
            v = 37;
        if (!v) continue;
        code += v * kindred_pow100(KINDRED_CODES - 1u - k);
        k++;
    }
    if (len) *len = k;
    return code;
}

/* KR3 */
static inline int64_t kindred_prefix_hit(int64_t name, int64_t prefix, int64_t len)
{
    if (len < 0 || len > (int64_t) KINDRED_CODES || name < 0 || name > KINDRED_NAME_CODE_MAX ||
        prefix < 0 || prefix > KINDRED_NAME_CODE_MAX)
        return KINDRED_REFUSED;
    return prefix <= name && name - prefix < kindred_pow100(KINDRED_CODES - (uint32_t) len) ? 1 : 0;
}

/* KR4: each contact's count into out; 0, or KINDRED_REFUSED. */
static inline int kindred_dupes(const int64_t *card, const int64_t *name, uint32_t n, int64_t *out)
{
    if (!card || !name || !out || n > KINDRED_ROWS_MAX) return KINDRED_REFUSED;
    for (uint32_t i = 0; i < n; i++)
        if (card[i] < 0 || card[i] > KINDRED_CARD_CODE_MAX || name[i] < 0 ||
            name[i] > KINDRED_NAME_CODE_MAX)
            return KINDRED_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        int64_t d = 0;
        for (uint32_t j = 0; j < n; j++)
            if (j != i &&
                ((card[i] > 0 && card[i] == card[j]) || (name[i] > 0 && name[i] == name[j])))
                d++;
        out[i] = d;
    }
    return 0;
}

/* at most `digits` decimal digits, sign aside (MYRA's iN) */
static inline bool kindred_digits(int64_t v, int64_t max)
{
    return v >= -max && v <= max;
}

/* KR5 */
static inline bool kindred_record_ok(int64_t contact, int64_t card, int64_t node, int64_t name,
                                     int64_t verified, int64_t stamp)
{
    return kindred_digits(contact, KINDRED_CONTACT_MAX) &&
           kindred_digits(card, KINDRED_CARD_CODE_MAX) &&
           kindred_digits(node, KINDRED_CARD_CODE_MAX) &&
           kindred_digits(name, KINDRED_NAME_CODE_MAX) && kindred_digits(verified, 9) &&
           kindred_digits(stamp, KINDRED_STAMP_MAX);
}

/* KR6 */
static inline bool kindred_index_ok(int64_t name, int64_t contact, int64_t stamp)
{
    return kindred_digits(name, KINDRED_NAME_CODE_MAX) &&
           kindred_digits(contact, KINDRED_CONTACT_MAX) && kindred_digits(stamp, KINDRED_STAMP_MAX);
}

/* KR7: the code of a 64-hex-digit id, or KINDRED_REFUSED. */
static inline int64_t kindred_id_code(const char *hex)
{
    int64_t v = 0;
    uint32_t i = 0;
    if (!hex) return KINDRED_REFUSED;
    for (; hex[i]; i++) {
        char c = hex[i];
        int64_t d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
        if (d < 0 || i >= KINDRED_HEX_ID) return KINDRED_REFUSED;
        if (i < 12u) v = v * 16 + d;
    }
    return i == KINDRED_HEX_ID ? v + 1 : KINDRED_REFUSED;
}

#endif
