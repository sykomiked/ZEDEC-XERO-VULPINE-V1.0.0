/* mrschema.h — MegaROM: observed interaction dynamics -> Sutra schemas
 *
 * WHAT THIS MINES, AND WHAT IT CANNOT
 * -----------------------------------
 * Read this before quoting what the MegaROM corpus produces, because the
 * distinction decides whether the output is evidence or decoration.
 *
 * CAN be mined, because it is OBSERVED: how a title drives the machine and how
 * it responds to a player. Instruction tempo, render pressure, frame-sync
 * discipline, interrupt cadence, and — the one that matters most — AGENCY,
 * measured by running the same title under DIFFERENT input storms and
 * comparing the traces. A title whose behaviour diverges under different input
 * is responding to the player; one whose traces are identical is playing itself.
 * That is a real, falsifiable measurement of interactivity.
 *
 * CANNOT be mined here: storyline, dialogue, characters, plot, or "social
 * dynamics" in the narrative sense. Those live in a title's EXPRESSION, not in
 * its PPU writes, and no amount of trace statistics recovers them. Claiming a
 * schema derived from NMI counts describes a story would be a fabrication, and
 * the numbers would not support it. Narrative annotation is a separate,
 * human-or-licensed pipeline; it is deliberately not attempted from bytes.
 *
 * WHY THE OUTPUT IS ABSTRACT BY CONSTRUCTION
 * ------------------------------------------
 * Emitted schemas contain measured dynamics only. No ROM bytes, no strings
 * lifted from a title, no asset data — nothing of a title's expression travels
 * into the schema. Titles are identified by a corpus id and a content digest
 * (dev references, per the project's naming rule), never by carrying their
 * content forward. Besides being the defensible position on someone else's
 * copyrighted work, it is the more useful one: an abstract dynamic composes
 * across the corpus, whereas a copied script only describes the title it came
 * from.
 *
 * THE CROSS-CORPUS MATRIX
 * -----------------------
 * The value concentrates in relations, not rows. Two titles with the same
 * dynamics are redundant; two with COMPLEMENTARY dynamics (one high-agency and
 * slow, one low-agency and fast) describe an axis of design space. The matrix
 * scores every pair on affinity (how alike) and complementarity (how usefully
 * unalike), so the corpus becomes a map of that space rather than a pile of
 * profiles.
 *
 * Units are permille (0..1000) integers throughout: freestanding-safe, exact,
 * and identical on host and target — unlike a fixed/float type whose behaviour
 * differs between the two.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV MegaROM slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#ifndef ZXV_MRSCHEMA_H
#define ZXV_MRSCHEMA_H

#include <stdint.h>
#include <stdbool.h>

#define MRS_DIM        6      /* observed dynamics dimensions            */
#define MRS_ID_LEN     32     /* corpus id (a DEV REFERENCE, not a name) */
#define MRS_MAX_TITLES 64     /* titles per matrix pass                  */

/* One observed run of a title under one input storm. These are the counters the
 * emulator already produces (see game_runner / game_universe); nothing here is
 * inferred, all of it is counted. */
typedef struct {
    uint32_t insn;          /* instructions retired                */
    uint32_t ppu_writes;    /* display writes                      */
    uint32_t vblank_polls;  /* frame-sync polls                    */
    uint32_t nmis;          /* interrupts taken                    */
    uint32_t bytes;         /* ROM size                            */
    uint32_t input_seed;    /* the storm this run was driven with  */
    bool     running;       /* still alive at the end of the run   */
} mrs_run_t;

/* The measured dynamics of a title. Every field is permille of an observed
 * quantity — no field is a guess, and none carries title content. */
typedef struct {
    uint8_t  id[MRS_ID_LEN];    /* corpus id / content digest prefix   */
    uint16_t d[MRS_DIM];        /* the dynamics vector, permille       */
    uint16_t agency;            /* divergence under differing input    */
    uint8_t  runs;              /* how many runs backed this profile   */
    bool     agency_measured;   /* false => only ONE storm was run     */
} mrs_profile_t;

/* Dimension indices — named so a schema reader knows what was measured. */
enum { MRS_TEMPO = 0, MRS_RENDER, MRS_SYNC, MRS_INTERRUPT,
       MRS_FOOTPRINT, MRS_LIVENESS };

/* Build a profile from one or more runs of the SAME title.
 * Agency is only meaningful when the runs used DIFFERENT input seeds: with a
 * single run (or identical seeds) `agency_measured` is false and `agency` is 0,
 * because there is nothing to compare and a fabricated number would be worse
 * than an absent one. Returns 0 on success. */
int mrs_observe(const uint8_t id[MRS_ID_LEN], const mrs_run_t *runs,
                uint32_t nruns, mrs_profile_t *out);

/* Emit the profile as a SUTRA-PROGRAM schema into `buf`. Contains measured
 * dynamics only. Returns bytes written (excluding NUL), or <0. */
int mrs_emit_sutra(const mrs_profile_t *p, char *buf, uint32_t max);

/* Pairwise relation between two profiles, permille.
 *   affinity        1000 = identical dynamics (redundant in a corpus)
 *   complementarity 1000 = maximally unalike (spans design space) */
typedef struct { uint16_t affinity; uint16_t complementarity; } mrs_relation_t;
mrs_relation_t mrs_relate(const mrs_profile_t *a, const mrs_profile_t *b);

/* Cross-corpus matrix. Fills `rel` (n*n, row-major) and reports how many
 * titles are DISTINCT at the given affinity threshold — the dedup number: a
 * corpus of 140k titles containing 200 distinct dynamics is a finding. */
int mrs_matrix(const mrs_profile_t *p, uint32_t n, uint16_t affinity_threshold,
               mrs_relation_t *rel, uint32_t *distinct_out);

/* The most complementary pair — the widest axis the corpus spans. */
int mrs_widest_axis(const mrs_profile_t *p, uint32_t n,
                    uint32_t *a_out, uint32_t *b_out);

#endif /* ZXV_MRSCHEMA_H */
