/* zxv_fab.h — what the zxv-fab tool's files share (zxv_fab.c: files and
 * keys; zxv_fab_app.c: apps through the program fabricator's gate).
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_FAB_TOOL_H
#define ZXV_FAB_TOOL_H

#include <stdint.h>

#include "../../kernel/src/fab/fab.h"

#define MAX_FILE (1u << 30)

extern fab_work_t W;

void die(const char *m);
void fail(const char *what, int rc);
uint8_t *read_file(const char *path, uint32_t *len, uint32_t max);
/* Write to PATH.tmp, then rename: a reader never sees half a file. */
void write_file(const char *path, const uint8_t *b, uint32_t len, int mode);
uint8_t *read_key(const char *path, uint32_t want);
/* A block from a store directory (ctx: its path), named by its CID. */
int store_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len);

int cmd_pack_app(int argc, char **argv);
int cmd_unpack_app(int argc, char **argv);

#endif /* ZXV_FAB_TOOL_H */
