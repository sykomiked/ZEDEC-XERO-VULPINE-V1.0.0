/* deploy.h — deployment profiles: one OS from a laptop to a supercomputer
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHAT THIS IS
 * -----------
 * "Works equally well on a home computer, a work computer, a server, and a
 * supercomputer" is only real if the OS ADAPTS to the machine instead of being
 * rebuilt for each. ZXV already has the mechanisms — the cellular multikernel
 * (fault-isolated cells), the constellation (multi-node coordination), and the
 * execution profiles — but nothing decides HOW MUCH of each to use on a given
 * box. This module is that decision.
 *
 * It reads the machine's capabilities (cores, memory, node count, power source)
 * classifies the deployment (embedded / workstation / server / cluster / HPC),
 * and resolves a PROFILE: how many cells to run, the target concurrency,
 * whether to join a constellation, the execution profile, the event budget, and
 * the power and network postures. The numbers SCALE WITH THE ACTUAL HARDWARE —
 * a 4-core laptop and a 128-core node run the same OS with different dials, and
 * neither wastes the other's capacity nor exceeds its own.
 *
 * This is pure policy: it computes a profile from capabilities. Applying the
 * profile (sizing the cell fabric, arming the constellation) is the caller's
 * job at boot; deploy_summarise() and the profile fields are what it applies.
 *
 * Freestanding: integer only, no float, no allocation.
 */
#ifndef ZXV_DEPLOY_H
#define ZXV_DEPLOY_H

#include <stdint.h>
#include <stdbool.h>

/* the deployment class, coarsest to largest */
typedef enum {
    DEPLOY_EMBEDDED = 0,   /* SBC / microcontroller-class                     */
    DEPLOY_WORKSTATION,    /* home or work desktop / laptop                   */
    DEPLOY_SERVER,         /* a single big serving box                        */
    DEPLOY_CLUSTER,        /* several nodes cooperating                       */
    DEPLOY_HPC             /* many-node, many-core supercomputer              */
} deploy_class_t;

/* execution profile (from m5_types: DC deterministic / AC adaptive / PC
 * parallel-coordinated). Mirrored here so deploy has no hard dependency. */
typedef enum { DEPLOY_EXEC_DC = 0, DEPLOY_EXEC_AC = 1, DEPLOY_EXEC_PC = 2 } deploy_exec_t;

typedef enum { POWER_BATTERY = 0, POWER_MAINS = 1 } deploy_power_t;
typedef enum { NETROLE_CLIENT = 0, NETROLE_PEER, NETROLE_SERVING } deploy_netrole_t;

/* what the OS can see about the machine it woke up on */
typedef struct {
    uint32_t cpu_cores;    /* logical cores on THIS node                      */
    uint32_t mem_mb;       /* usable RAM on THIS node, MB                      */
    uint32_t node_count;   /* nodes in the deployment (1 = single box)        */
    deploy_power_t power;   /* battery or mains                                */
    bool     has_network;
} deploy_caps_t;

/* the resolved dials the OS should run with */
typedef struct {
    deploy_class_t   klass;
    deploy_exec_t    exec;
    uint32_t         max_cells;        /* cellular-multikernel cells to run    */
    uint32_t         target_concurrency;
    bool             enable_constellation;  /* join a multi-node fabric        */
    uint32_t         event_budget;     /* per-cycle event budget               */
    uint32_t         kernel_reserve_mb; /* RAM held back for the kernel         */
    deploy_netrole_t net_role;
    deploy_power_t   power_posture;    /* conserve on battery, perform on mains */
} deploy_profile_t;

/* hard ceiling so a wildly-large machine cannot ask for an unbounded fabric */
#define DEPLOY_MAX_CELLS 1024u

/* Classify a machine into a deployment class. */
deploy_class_t deploy_classify(const deploy_caps_t *caps);

/* Resolve the full profile from capabilities. The class sets the shape; the
 * actual cores/memory/nodes scale the numbers within it. */
void deploy_resolve(const deploy_caps_t *caps, deploy_profile_t *out);

/* Human-readable names. */
const char *deploy_class_name(deploy_class_t k);
const char *deploy_exec_name(deploy_exec_t e);

/* Write a one-line summary of the resolved profile into `buf`. Returns length. */
uint32_t deploy_summarise(const deploy_profile_t *p, char *buf, uint32_t cap);

#endif /* ZXV_DEPLOY_H */
