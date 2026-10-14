/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_sutra.h — register the Sutra runtime (cobol-sutra pillar) with a
 * manifest host, so `logic sutra ...` entries load and run (hosted builds
 * only). The entry form is described in zxapp_sutra.c. */
#ifndef ZXAPP_SUTRA_H
#define ZXAPP_SUTRA_H

#include "zxapp.h"

int zxapp_sutra_register(zxapp_host_t *h);

#endif /* ZXAPP_SUTRA_H */
