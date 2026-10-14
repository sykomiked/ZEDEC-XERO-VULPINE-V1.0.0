/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_kron.h — register the KRON runtime (apl-kron pillar) with a manifest
 * host, so `logic kron ...` entries load and run (hosted builds only). */
#ifndef ZXAPP_KRON_H
#define ZXAPP_KRON_H

#include "zxapp.h"

int zxapp_kron_register(zxapp_host_t *h);

#endif /* ZXAPP_KRON_H */
