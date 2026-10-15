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
/* the economy modules (zxvd_econ.c): one in-memory economy shared by five
 * groups, each behind its own switch */
const zxvd_group_t *zxvd_vault_group(void);    /* vault.*   feature "vault"    */
const zxvd_group_t *zxvd_swap_group(void);     /* swap.*    feature "swap"     */
const zxvd_group_t *zxvd_auctions_group(void); /* auction.* feature "auctions" */
const zxvd_group_t *zxvd_zenny_group(void);    /* zenny.*   feature "zenny"    */
const zxvd_group_t *zxvd_stats_group(void);    /* stats.*   feature "stats"    */
/* zxvd_instruments.c: V-Bonds and V-Calls / V-Perps (regulated) */
const zxvd_group_t *zxvd_bonds_group(void);
const zxvd_group_t *zxvd_derivatives_group(void);
/* zxvd_estate.c: barrel grants (regulated); estate, recovery, faraid */
const zxvd_group_t *zxvd_grants_group(void);
const zxvd_group_t *zxvd_estate_group(void);
/* companion.* (zxvd_companion.c, zxvd_companion.h): feature "companion",
 * not regulated, every method answers with the switch off */
const zxvd_group_t *zxvd_companion_group(void);
/* web4.* (zxvd_web4.c, kernel/src/web4): the owner's Web4 envelope feed,
 * feature "web4" */
const zxvd_group_t *zxvd_web4_group(void);
void zxvd_web4_set_random(int (*fn)(uint8_t *p, size_t n)); /* unit tests only */
/* seats.* (zxvd_seats.c, kernel/src/seats): feature "seats", every method
 * always on (the login after boot). zxvd_seats_open loads the roster from
 * path ("" or NULL: in memory only); 0, or -1 with the reason on stderr
 * (a damaged file). The group's close forgets it. */
const zxvd_group_t *zxvd_seats_group(void);
int zxvd_seats_open(const char *path);
/* Unit tests only: random bytes (NULL = the OS), the wall clock in ms (NULL
 * = time()), and smaller Argon2id parameters (0 = the module default). */
void zxvd_seats_set_random(int (*fn)(uint8_t *p, size_t n));
void zxvd_seats_set_clock(uint64_t (*fn)(void));
void zxvd_seats_set_kdf(uint32_t m_kib, uint32_t t);
/* power.* (zxvd_power.c, kernel/src/power): feature "power", power.status
 * and power.top always on, power.budget behind the switch (zxvd_power.h) */
const zxvd_group_t *zxvd_power_group(void);
/* irc.* (zxvd_irc.c, zxvd_irc.h): feature "irc", the IRC gateway on
 * 127.0.0.1 over TLS; irc.status and irc.record always on */
const zxvd_group_t *zxvd_irc_group(void);

/* pspace.* and hall.* (zxvd_pspace.c, kernel/src/vinea/vna_space): feature
 * "pspace", bound to the net host by zxvd_pspace_bind (zxvd_pspace.h) */
const zxvd_group_t *zxvd_pspace_group(void);

#endif
