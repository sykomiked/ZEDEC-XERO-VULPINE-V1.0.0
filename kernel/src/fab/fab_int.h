/* fab_int.h — helpers shared by fab.c and fab_send.c; not an API.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_FAB_INT_H
#define ZXV_FAB_INT_H

#include "fab.h"

void fab__cpy(void *dst, const void *src, uint32_t n);
void fab__wipe(void *dst, uint32_t n);
bool fab__eq(const void *a, const void *b, uint32_t n);
bool fab__zero(const uint8_t *p, uint32_t n);
uint32_t fab__get32(const uint8_t *p);
uint64_t fab__get64(const uint8_t *p);
void fab__put32(uint8_t *p, uint32_t v);
void fab__put64(uint8_t *p, uint64_t v);

void fab__prk(const uint8_t *hdr, const uint8_t *ct, const uint8_t ss[32], uint8_t prk[32]);
void fab__expand(const uint8_t prk[32], const char *label, uint8_t out[32]);
void fab__frame_nonce(uint8_t which, uint64_t seq, uint8_t nonce[12]);
void fab__stream_init(fab_stream_t *s, const uint8_t prk[32], const uint8_t *sender_pk,
                      uint32_t slot_us, const ipfsn_cid_t *frame0);
void fab__ring_push(fab_stream_t *s, uint64_t seq, const ipfsn_cid_t *cid);
bool fab__window(const fab_stream_t *s, uint64_t from, uint64_t to, uint8_t out[32]);
int fab__frame_parse(const uint8_t *f, uint32_t len, uint32_t *plan_len, uint32_t *diff_len);
void fab__cp_body(const fab_stream_t *s, uint64_t from, uint64_t to, const uint8_t digest[32],
                  uint8_t out[FAB_CP_BODY]);

#endif /* ZXV_FAB_INT_H */
