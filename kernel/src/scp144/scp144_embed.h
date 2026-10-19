/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* scp144_embed.h — the SCP-144 boot self-check: the 144-dimensional
 * Zeckendorf-Casimir lattice ROM (zxv_scp144_blob.h) and the improved
 * SCP-144 standard, verified before [BOOT_OK] on every architecture. */
#ifndef ZXV_SCP144_EMBED_H
#define ZXV_SCP144_EMBED_H

#include <stdbool.h>
#include <stdint.h>

/* Recompute the embedded lattice tables, the 144x144 fold matrix and the
 * digest, run the improved SCP-144 live checks (Cassini identities, the
 * bijective AC tunnel round trip, the lattice binding's determinism, the
 * cavity clock invariants) and the fractal seed chain. Returns true when
 * every check passes; `out` receives one "[SCP144_OK] ..." or
 * "[FAULT] ..." line. */
bool scp144_boot_selfcheck(void (*out)(const char *));

/* The holographic healing self-audit: re-project the canonical SCP-144
 * tables into `shadow` and return the number of bytes that differ from the
 * .rodata ROM (0 = intact; UINT32_MAX = buffer too small). See scp144_embed.c. */
uint32_t scp144_heal_audit(uint8_t *shadow, uint32_t cap);

#endif /* ZXV_SCP144_EMBED_H */