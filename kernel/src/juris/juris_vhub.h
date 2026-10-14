/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* juris_vhub.h — the jurisdiction host and the V-Bill hub (juris.h J6). */
#ifndef ZXV_JURIS_VHUB_H
#define ZXV_JURIS_VHUB_H
#include "juris.h"
#include "../vhub/vhub.h"

/* ---- J6 ---- */
/* vhub_add_rule(hub, c, juris_vhub_rule, juris_vhub_commit, host) */
lpx_t juris_vhub_rule(void *host, const vhub_swap_t *sw, uint8_t side);
void juris_vhub_commit(void *host, const vhub_swap_t *sw, uint8_t side);
/* Fill p as a transfer plug-in for `country` around vhub's limit rule. */
void juris_limit_plugin(juris_plugin_t *p, const char *id, const char *country, vhub_limit_t *l);

#endif /* ZXV_JURIS_VHUB_H */
