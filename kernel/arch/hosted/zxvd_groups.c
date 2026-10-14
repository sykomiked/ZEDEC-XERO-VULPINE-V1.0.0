/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_groups.c — the method groups zxvd serves (zxvd_ext.h), registered in
 * one place so rpc.discover lists them in a fixed order on every platform.
 * Each group's sources are listed by app_sources.sh groups. */
#include "zxvd_groups.h"

int zxvd_groups_register(void)
{
    static const zxvd_group_t *(*const ALL[])(void) = {
        zxvd_cards_group,       zxvd_passkeys_group, zxvd_vault_group,
        zxvd_swap_group,        zxvd_auctions_group, zxvd_zenny_group,
        zxvd_stats_group,       zxvd_bonds_group, /* zxvd_instruments.c */
        zxvd_derivatives_group,                   /* zxvd_instruments.c */
        zxvd_grants_group,                        /* zxvd_estate.c */
        zxvd_estate_group,                        /* zxvd_estate.c */
        zxvd_companion_group,                     /* zxvd_companion.c */
        zxvd_web4_group,                          /* zxvd_web4.c */
        zxvd_seats_group,                         /* zxvd_seats.c */
        zxvd_power_group,                         /* zxvd_power.c */
        zxvd_pspace_group,                        /* zxvd_pspace.c */
    };
    for (size_t i = 0; i < sizeof ALL / sizeof ALL[0]; i++)
        if (ALL[i] && zxvd_rpc_add_group(ALL[i]()) != 0) return -1;
    return 0;
}
