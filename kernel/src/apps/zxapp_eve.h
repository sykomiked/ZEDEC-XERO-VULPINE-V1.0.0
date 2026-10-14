/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_eve.h — register the Eve runtime (ada-eve pillar) with a manifest
 * host, so `logic eve ...` entries load and run (hosted builds only). */
#ifndef ZXAPP_EVE_H
#define ZXAPP_EVE_H

#include "zxapp.h"

int zxapp_eve_register(zxapp_host_t *h);

#endif /* ZXAPP_EVE_H */
