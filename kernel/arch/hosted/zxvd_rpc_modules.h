/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_rpc_modules.h — the kernel modules zxvd serves beside its own methods:
 * freight.*, harmonic.*, stream.*, ehop.* and web4.* (zxvd_rpc_modules.c).
 *
 * They are one method group (zxvd_ext.h), feature "codecs", registered by
 * zxvd_groups.c; every method answers with the switch off. Everything here is a pure codec over the
 * request: no method keeps state between calls, opens the books, posts to a
 * ledger or touches the swarm (zxvd_rpc.h M2 is unchanged: the root cap is
 * never reached from here).
 *
 *   X1  Bytes travel as lowercase hex strings ("*_hex"; either case is read).
 *       64-bit values travel as decimal strings in results and may be sent as
 *       a JSON integer or a decimal string, as in zxvd_rpc.h M3.
 *   X2  A request that is well formed but that the module refuses (a freight
 *       with too few rows, a payload whose SHA-256 does not match, an ehop
 *       frame that fails its AEAD tag) is error ZXVD_E_MODULE; the message
 *       names the module's reason. A malformed parameter is -32602.
 *   X3  ehop.* is stateless: zxvd keeps no channel, no sequence counter and
 *       no replay window. The caller owns the nonce discipline of
 *       kernel/src/ehop/ehop.h (never seal two plaintexts under the same
 *       key, chan_id, epoch, configuration, sender and seq) and tracks the
 *       (sender, seq) pairs it has opened.
 *   X4  web4.agent_card takes the agent's 32-byte key seed; zxvd derives the
 *       ML-DSA-65 key pair, signs the self-bound card deterministically and
 *       wipes the secret key before it answers. The socket is owner-only
 *       (zxvd_rpc.h T1/T2), the same trust as every other method.
 */
#ifndef ZXVD_RPC_MODULES_H
#define ZXVD_RPC_MODULES_H

#include <stddef.h>
#include <stdint.h>

#include "zxvd_ext.h"

#define ZXVD_E_MODULE (-32017)

/* the group (zxvd_groups.h lists it too) */
const zxvd_group_t *zxvd_codecs_group(void);

#endif
