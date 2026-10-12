/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_groups.h — every method group zxvd serves (zxvd_ext.h X1). Each
 * zxvd_<group>.c exports one function returning its static group table. */
#ifndef ZXVD_GROUPS_H
#define ZXVD_GROUPS_H

#include "zxvd_ext.h"

/* Register every group, in a fixed order. 0, or -1 if one is refused (a
 * clashing method name: a build error, reported by zxvd at start). */
int zxvd_groups_register(void);

/* cards.* (zxvd_cards.c, kernel/src/cardnet): feature "cards", regulated */
const zxvd_group_t *zxvd_cards_group(void);
/* passkeys.* (zxvd_ident.c, kernel/src/ident): feature "passkeys" */
const zxvd_group_t *zxvd_passkeys_group(void);
/* Unit tests only: where the groups draw random bytes (NULL = the OS,
 * zxv_os_random). fn returns 0 on success. */
void zxvd_cards_set_random(int (*fn)(uint8_t *p, size_t n));
void zxvd_passkeys_set_random(int (*fn)(uint8_t *p, size_t n));

#endif
