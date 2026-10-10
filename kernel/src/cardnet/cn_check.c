/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_check.c — Luhn, Damm and Verhoeff. See cn_check.h. */
#include "cn_check.h"
#include "cn_util.h"

/* ---- Luhn ---- */

/* Luhn sum over digits[0..len), doubling every second digit counting from
 * the right starting with the rightmost when `double_last` is set. */
static uint32_t luhn_sum(const char *d, uint32_t len, bool double_last)
{
    uint32_t sum = 0;
    bool dbl = double_last;
    for (uint32_t i = len; i-- > 0;) {
        uint32_t v = (uint32_t) (d[i] - '0');
        if (dbl) {
            v *= 2u;
            if (v > 9u) v -= 9u;
        }
        sum += v;
        dbl = !dbl;
    }
    return sum % 10u;
}

int cn_luhn_digit(const char *digits, uint32_t len)
{
    if (len == 0 || !cn_all_digits(digits, len)) return -1;
    uint32_t s = luhn_sum(digits, len, true);
    return (int) ((10u - s) % 10u);
}

bool cn_luhn_valid(const char *digits, uint32_t len)
{
    if (len < 2 || !cn_all_digits(digits, len)) return false;
    return luhn_sum(digits, len, false) == 0;
}

/* ---- Damm (H. M. Damm, 2004; the standard order-10 table) ---- */

static const uint8_t damm_tab[10][10] = {
    {0, 3, 1, 7, 5, 9, 8, 6, 4, 2}, {7, 0, 9, 2, 1, 5, 4, 8, 6, 3}, {4, 2, 0, 6, 8, 7, 1, 3, 5, 9},
    {1, 7, 5, 0, 9, 8, 3, 4, 2, 6}, {6, 1, 2, 3, 0, 4, 5, 9, 7, 8}, {3, 6, 7, 4, 2, 0, 9, 5, 8, 1},
    {5, 8, 6, 9, 7, 2, 0, 1, 3, 4}, {8, 9, 4, 5, 3, 6, 2, 0, 1, 7}, {9, 4, 3, 8, 6, 1, 7, 2, 0, 5},
    {2, 5, 8, 1, 4, 3, 6, 7, 9, 0}};

static uint32_t damm_run(const char *d, uint32_t len)
{
    uint32_t interim = 0;
    for (uint32_t i = 0; i < len; i++) interim = damm_tab[interim][(uint32_t) (d[i] - '0')];
    return interim;
}

int cn_damm_digit(const char *digits, uint32_t len)
{
    if (len == 0 || !cn_all_digits(digits, len)) return -1;
    /* The table has a zero diagonal, so the interim digit is its own check. */
    return (int) damm_run(digits, len);
}

bool cn_damm_valid(const char *digits, uint32_t len)
{
    if (len < 2 || !cn_all_digits(digits, len)) return false;
    return damm_run(digits, len) == 0;
}

/* ---- Verhoeff (J. Verhoeff, 1969) ---- */

static const uint8_t vh_d[10][10] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {1, 2, 3, 4, 0, 6, 7, 8, 9, 5}, {2, 3, 4, 0, 1, 7, 8, 9, 5, 6},
    {3, 4, 0, 1, 2, 8, 9, 5, 6, 7}, {4, 0, 1, 2, 3, 9, 5, 6, 7, 8}, {5, 9, 8, 7, 6, 0, 4, 3, 2, 1},
    {6, 5, 9, 8, 7, 1, 0, 4, 3, 2}, {7, 6, 5, 9, 8, 2, 1, 0, 4, 3}, {8, 7, 6, 5, 9, 3, 2, 1, 0, 4},
    {9, 8, 7, 6, 5, 4, 3, 2, 1, 0}};

static const uint8_t vh_p[8][10] = {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}, {1, 5, 7, 6, 2, 8, 3, 0, 9, 4},
                                    {5, 8, 0, 3, 7, 9, 6, 1, 4, 2}, {8, 9, 1, 6, 0, 4, 3, 5, 2, 7},
                                    {9, 4, 5, 3, 1, 2, 6, 8, 7, 0}, {4, 2, 8, 6, 5, 7, 3, 9, 0, 1},
                                    {2, 7, 9, 3, 8, 0, 6, 4, 1, 5}, {7, 0, 4, 6, 9, 1, 3, 2, 5, 8}};

static const uint8_t vh_inv[10] = {0, 4, 3, 2, 1, 5, 6, 7, 8, 9};

/* offset 1 when computing (the check digit will occupy position 0). */
static uint32_t verhoeff_run(const char *d, uint32_t len, uint32_t offset)
{
    uint32_t c = 0;
    uint32_t pos = offset;
    for (uint32_t i = len; i-- > 0; pos++) c = vh_d[c][vh_p[pos & 7u][(uint32_t) (d[i] - '0')]];
    return c;
}

int cn_verhoeff_digit(const char *digits, uint32_t len)
{
    if (len == 0 || !cn_all_digits(digits, len)) return -1;
    return (int) vh_inv[verhoeff_run(digits, len, 1)];
}

bool cn_verhoeff_valid(const char *digits, uint32_t len)
{
    if (len < 2 || !cn_all_digits(digits, len)) return false;
    return verhoeff_run(digits, len, 0) == 0;
}
