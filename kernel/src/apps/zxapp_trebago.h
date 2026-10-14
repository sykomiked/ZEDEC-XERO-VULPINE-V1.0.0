/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_trebago.h — register the Trebago runtime (fortran-trebago pillar) with a
 * manifest host, so `logic trebago ...` entries load and run (hosted builds
 * only). The entry form is described in zxapp_trebago.c. */
#ifndef ZXAPP_TREBAGO_H
#define ZXAPP_TREBAGO_H

#include "zxapp.h"

int zxapp_trebago_register(zxapp_host_t *h);

#endif /* ZXAPP_TREBAGO_H */
