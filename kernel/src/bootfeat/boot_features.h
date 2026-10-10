/* boot_features.h — bring the platform layer up, on top of the M5 core.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * The arch boot driver (kernel_main) brings up the M5 core and the classic
 * drivers. This one call brings up the PLATFORM layer built on top of it and
 * prints an evidence line per subsystem, so the boot log proves each is not
 * just present but reachable and self-checking:
 *
 *   deploy   — size the OS to THIS machine (embedded..supercomputer)
 *   theme    — the customisable dragon palette
 *   icon     — per-system vector icons
 *   font     — the multi-language font registry (script -> face)
 *   bridge   — one resolver across Web2/Web3/Web4
 *   update   — decentralised, opt-in, content-addressed updates
 *   mage     — the hat-colour security framework (self-test frictionless)
 *   reality  — sigil circuits evaluated against live variables per phase tick
 *
 * It is arch-neutral: it emits through a puts callback the arch driver supplies,
 * and returns the number of subsystems that initialised and self-checked OK.
 * It runs in phase-tick order — no wall-clock is read.
 */
#ifndef ZXV_BOOT_FEATURES_H
#define ZXV_BOOT_FEATURES_H

typedef void (*bf_puts_t)(const char *);

/* caps the OS discovered about this machine; the arch driver fills these from
 * what it actually probed (cores, RAM). Passing 0 uses a safe single-node
 * default so boot still demonstrates the path. */
unsigned boot_features_init(bf_puts_t puts, unsigned cpu_cores, unsigned mem_mb);

/* Bring up the economy foundation (The One Policy, the nine-form capital
 * substrate, the Crown and Ministry pillars, the content-addressed spine) on top
 * of the platform layer. Returns the count that self-checked OK (out of 5). */
unsigned boot_economy_init(bf_puts_t puts);

#endif /* ZXV_BOOT_FEATURES_H */
