/* scp144.h — Sicilian Crown Protection, 144-dimensional. The lattice
 * binding ties a Vinea session to the Fibonacci ladder (dimensions up to
 * F12 = 144): both peers derive the same binding from their public NodeIDs
 * alone, and the hybrid PQ key schedule mixes it in, so the session key
 * depends on the post-quantum secrets AND the lattice point (the SCP-144
 * series guarantee). Exact integers only.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZXV_SCP144_H
#define ZXV_SCP144_H

#include <stdint.h>

/* Derive the SCP-144 lattice binding of a two-party handshake from the
 * public identifiers. `init_id` and `resp_id` are 32-byte NodeIDs; `out`
 * receives 32 bytes. The computation is a deterministic function of the
 * public identifiers, so both peers obtain the same binding without an
 * extra round trip. */
void scp144_lattice_bind(const uint8_t init_id[32],
                         const uint8_t resp_id[32],
                         uint8_t out[32]);

#endif /* ZXV_SCP144_H */