/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_groups.c — the method groups zxvd serves (zxvd_ext.h), registered in
 * one place so rpc.discover lists them in a fixed order on every platform.
 * Each group's sources are listed by app_sources.sh groups. */
#include "zxvd_groups.h"

int zxvd_groups_register(void)
{
    static const zxvd_group_t *(*const ALL[])(void) = {
        NULL, /* keeps the table non-empty while no group is built */
    };
    for (size_t i = 0; i < sizeof ALL / sizeof ALL[0]; i++)
        if (ALL[i] && zxvd_rpc_add_group(ALL[i]()) != 0) return -1;
    return 0;
}
