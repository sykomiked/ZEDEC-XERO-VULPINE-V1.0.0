/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_lingua.h — register Lingua (kernel/src/lingua) with a manifest host,
 * so `logic fortran77 ...`, `logic basic ...`, `logic forth ...` and
 * precompiled `logic zlir ...` entries load and run. The entry form is
 * described in zxapp_lingua.c. */
#ifndef ZXAPP_LINGUA_H
#define ZXAPP_LINGUA_H

#include "zxapp.h"

/* Registers all four. fuel is the instruction budget per call (0: 10^7). */
int zxapp_lingua_register(zxapp_host_t *h, uint64_t fuel);

#endif /* ZXAPP_LINGUA_H */
