/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zs_picture.c — PICTURE strings, shared by the copybook and Sutra
 * parsers. Supported: X(n) and [S]9(i)[V9(d)] with at most 18 digits,
 * written with repeats ("9(5)") or spelled out ("99999"), any letter
 * case. Everything else is refused with a reason. Integer code only. */
#include <stdio.h>
#include <string.h>

#include "zs_internal.h"

static char up(char c) { return (c >= 'a' && c <= 'z') ? (char) (c - 32) : c; }

static int32_t fail(int32_t st, size_t at, size_t *out_at, char *msg, size_t cap,
                    const char *text) {
    *out_at = at;
    snprintf(msg, cap, "%s", text);
    return st;
}

int32_t zs_picture_parse(const char *s, size_t n, int32_t *out_kind, uint32_t *out_digits,
                         uint32_t *out_scale, int32_t *out_signed, size_t *out_at, char *msg,
                         size_t msg_cap) {
    uint64_t ints = 0, fracs = 0, xs = 0;
    int seen_s = 0, seen_v = 0, seen_9 = 0;
    size_t i = 0;
    if (n == 0) return fail(ZS_E_SYNTAX, 0, out_at, msg, msg_cap, "empty picture string");
    while (i < n) {
        size_t at = i;
        char c = up(s[i++]);
        uint64_t count = 1;
        if (i < n && s[i] == '(') {
            size_t j = i + 1u;
            count = 0;
            while (j < n && s[j] >= '0' && s[j] <= '9') {
                count = count * 10u + (uint64_t) (s[j] - '0');
                if (count > ZS_MAX_RECORD)
                    return fail(ZS_E_SYNTAX, j, out_at, msg, msg_cap,
                                "repeat count is larger than the largest record (1048576)");
                j++;
            }
            if (j == i + 1u || j >= n || s[j] != ')')
                return fail(ZS_E_SYNTAX, i, out_at, msg, msg_cap,
                            "expected a repeat count like (5) after the picture symbol");
            if (count == 0)
                return fail(ZS_E_SYNTAX, i, out_at, msg, msg_cap, "a repeat count must be at least 1");
            i = j + 1u;
        }
        switch (c) {
        case 'S':
            if (at != 0 || count != 1)
                return fail(ZS_E_SYNTAX, at, out_at, msg, msg_cap,
                            "S may appear once, at the start of the picture");
            seen_s = 1;
            break;
        case 'V':
            if (seen_v || count != 1)
                return fail(ZS_E_SYNTAX, at, out_at, msg, msg_cap, "V may appear only once");
            seen_v = 1;
            break;
        case '9':
            seen_9 = 1;
            if (seen_v) fracs += count;
            else ints += count;
            if (ints + fracs > ZS_MAX_DIGITS)
                return fail(ZS_E_UNSUPPORTED, at, out_at, msg, msg_cap,
                            "more than 18 digits (the most an int64 holds exactly)");
            break;
        case 'X':
            xs += count;
            if (xs > ZS_MAX_RECORD)
                return fail(ZS_E_SYNTAX, at, out_at, msg, msg_cap,
                            "picture is larger than the largest record (1048576 bytes)");
            break;
        case 'P':
            return fail(ZS_E_UNSUPPORTED, at, out_at, msg, msg_cap,
                        "P (decimal scaling positions) is not supported");
        case 'A':
            return fail(ZS_E_UNSUPPORTED, at, out_at, msg, msg_cap,
                        "A (alphabetic) is not supported; use X");
        case 'N':
        case 'G':
            return fail(ZS_E_UNSUPPORTED, at, out_at, msg, msg_cap,
                        "national and DBCS pictures (N, G) are not supported");
        case 'Z': case 'B': case '0': case '/': case ',': case '.': case '+': case '-':
        case '*': case '$': case 'C': case 'D': case 'R': case 'E':
            return fail(ZS_E_UNSUPPORTED, at, out_at, msg, msg_cap,
                        "edited pictures (Z . , + - $ * B 0 / CR DB) are display formats, "
                        "not record data; not supported");
        default:
            return fail(ZS_E_SYNTAX, at, out_at, msg, msg_cap, "not a picture symbol");
        }
    }
    if (xs) {
        if (seen_s || seen_v || seen_9)
            return fail(ZS_E_UNSUPPORTED, 0, out_at, msg, msg_cap,
                        "a picture mixing X with 9, S or V (alphanumeric edited) is not supported");
        *out_kind = ZS_K_TEXT;
        *out_digits = (uint32_t) xs;
        *out_scale = 0;
        *out_signed = 0;
        return ZXV_OK;
    }
    if (!seen_9) return fail(ZS_E_SYNTAX, 0, out_at, msg, msg_cap, "a numeric picture needs a 9");
    *out_kind = ZS_K_NUM;
    *out_digits = (uint32_t) (ints + fracs);
    *out_scale = (uint32_t) fracs;
    *out_signed = seen_s;
    return ZXV_OK;
}
