/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_aipi.h — AIPI inside zxvd: content-addressed functions (kernel/src/aipi)
 * run in the Z-Wasm sandbox (sdk/kits/aipi), served on the control socket as
 * aipi.put / aipi.get / aipi.publish / aipi.execute / aipi.status.
 *
 *   Z1  One node, offline (AIPI rule A3): online is false and no mesh is
 *       bound, so a call runs here from the local blockstore or fails with
 *       AIPI_ERR_OFFLINE. Nothing is broadcast and no bounty is reserved.
 *   Z2  The blockstore is in memory (ZXVD_AIPI_STORE bytes) and is lost when
 *       zxvd stops. PRIVATE blocks are sealed with a key drawn from the OS
 *       at open and never written anywhere.
 *   Z3  Every run gets the function's own fuel_max, under one compute budget
 *       (ZXVD_AIPI_FUEL) for the life of the process: a run past it fails,
 *       it never hangs the swarm loop.
 *   Z4  aipi.get returns at most ZXVD_AIPI_GET_MAX bytes; larger blocks are
 *       refused, not cut.
 */
#ifndef ZXVD_AIPI_H
#define ZXVD_AIPI_H

#include "zxvd_rpc.h"

#define ZXVD_AIPI_STORE   (32u << 20)
#define ZXVD_AIPI_GET_MAX 65536u
#define ZXVD_AIPI_FUEL    (INT64_C(1) << 40)

/* Set up the blockstore, the AIPI context and the sandbox. The ops to hand
 * to zxvd_rpc_set_aipi_ops, or NULL with a reason on stderr (nothing is left
 * open). A second open returns the same ops. */
const zxvd_aipi_ops_t *zxvd_aipi_open(void);
void zxvd_aipi_close(void);

#endif
