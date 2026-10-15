/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* alx_int.h — helpers shared by the Alexandria files; not an interface. */
#ifndef ZXV_ALX_INT_H
#define ZXV_ALX_INT_H

#include "alx.h"

uint32_t alx_be32(const uint8_t *p);
uint16_t alx_be16(const uint8_t *p);
bool alx_zero(const uint8_t *p, uint32_t n);
bool alx_same(const uint8_t *a, const uint8_t *b, uint32_t n);
bool alx_language_ok(uint32_t l);
const uint8_t *alx_slot_rec(const alx_t *t, uint32_t slot);

#endif
