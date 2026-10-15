/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxapp_pce.h — the paraconsistent decision engines (kernel/src/pce) as the
 * `c` runtime of the app model, so blocks can name them.
 *
 *   P1 SOURCE IS AN ENTRY NAME. A `logic c <entry> <<` source holds one
 *      engine entry from pce_entry.h ("prob.join", "strat.zopa", ...),
 *      surrounding blank space ignored. Any other source is refused at load.
 *   P2 CONTRACTS MATCH. The engine checks its own input names, counts and
 *      shapes on every call, after the block's contract (zxapp.h B3).
 *   P3 ERRORS MAP. A result past cap is ZXAPP_E_BUFFER (with the count
 *      needed); a bad argument or an engine refusal (overflow, evidence that
 *      rules every case out) is refused with its reason, which zxapp_call
 *      reports as ZXAPP_E_RUNTIME with the reason in the app's err.
 *   P4 FREESTANDING. No libc, no allocation: the compiled handle is the
 *      engine's own contract record. */
#ifndef ZXAPP_PCE_H
#define ZXAPP_PCE_H

#include "zxapp.h"

int zxapp_pce_register(zxapp_host_t *h);

#endif /* ZXAPP_PCE_H */
