/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_myra.h — register the MYRA runtime (mumps-myra pillar) with a
 * manifest host, so `logic myra ...` entries load and run (hosted builds
 * only). The entry form is described in zxapp_myra.c. */
#ifndef ZXAPP_MYRA_H
#define ZXAPP_MYRA_H

#include "zxapp.h"

int zxapp_myra_register(zxapp_host_t *h);

#endif /* ZXAPP_MYRA_H */
