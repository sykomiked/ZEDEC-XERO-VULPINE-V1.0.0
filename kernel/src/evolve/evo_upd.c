/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_upd.c — binds lineage trust to the update module's opt-in publisher
 * list, so a user keeps ONE list of whom they trust. Key ids are the
 * 32-byte SHA3-256 of a publisher's public key (evo_key_id). */
#include "evo.h"
#include "update.h"

bool evo_trust_upd(const uint8_t key_id[32], void *upd_catalog)
{
    return upd_catalog && upd_is_trusted((const upd_catalog_t *) upd_catalog, key_id);
}
