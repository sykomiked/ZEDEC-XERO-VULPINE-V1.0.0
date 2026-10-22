/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* appfab.h — the program fabricator's acceptance gate: is a fabricated app
 * whole (fit) and does it answer as it promises (function)?
 *
 * fab (kernel/src/fab) rebuilds an app's manifest from a signed recipe and
 * checks it against its CID; the recipe's plan lists the app's closure
 * (every block it pins), and fab fetches and checks each of those too. This
 * gate takes over from there (docs/spec/FABRICATION.md, section 10).
 *
 *   F1 PARTS ARE CHECKED AGAIN. Every part must match its CID, and the CID
 *      must be a SHA-256 one: a zxapp content id ("sha256:" + hex, zxapp.h
 *      C3) and a raw CIDv1 of the same bytes carry the same digest, so a
 *      pin names a part with no second hash. The gate trusts nothing it is
 *      handed, not even bytes fab has already checked.
 *   F2 THE MANIFEST IS AN APP ("zxapp 1"), not a block, and it parses.
 *   F3 EVERY USE IS PINNED, TO A PART. A use with no content id, or with
 *      one that is not among the parts, does not fit: a fabricated app is
 *      built only from what its recipe names. A part that uses others (an
 *      app used as a block, zxapp.h C1) is resolved the same way first,
 *      at most APPFAB_DEPTH deep. Two parts with one id and version are
 *      refused by the registry, never picked between.
 *   F4 SOURCES COME FROM THE PARTS. A logic line with a path must name
 *      "cid:<CID>" of a part (at most the host's file_buf); any other path
 *      does not fit, because a fabricated app reads nothing from the
 *      device's disk. Inline sources are already inside a checked part.
 *   F5 EVERY ENTRY RUNS HERE. After composing, every logic entry must load
 *      on this host: its language needs a runtime registered here (Lingua's
 *      rule: kernel runtimes, or an SDK this host has). An entry with none,
 *      or a runtime that refuses its source, does not fit; the C model
 *      fallback of zxapp.h A2 is not part of what the recipe proves.
 *   F6 Fit answers TRUE or FALSE, and why[] says what failed; never BOTH.
 *      The one NEITHER is an empty scaffold: an app with no use, no logic
 *      and no provides is a glue node of the matrix, marked empty, never
 *      accepted and never filled with stand-in content.
 *   F7 ZECKENDORF LINEAGE (appfab_fit_layered). The anchors are 0 and the
 *      Fibonacci numbers (0 1 2 3 5 8 13 21 34 55 89 144 ...). A branch
 *      N = F + M, F the largest Fibonacci number <= N, has lin(N) = every
 *      anchor <= F, M and lin(M); an anchor A has every anchor below it;
 *      lin(0) is empty. Layer N may use parts from N or lin(N) only (the
 *      matrix's alx_mx_draws and pack_matrix lineage(), one rule with the
 *      golden table docs/spec/ZECKENDORF_ANCESTORS.tsv). Every use, at
 *      every depth, is checked against the layer of the part that makes
 *      it; a part with no layer is refused. appfab_fit is the same gate
 *      without layers (docs/spec/FABRICATION.md, section 10).
 *
 *   C1 FUNCTION IS WITNESSED. appfab_function runs each check (an entry,
 *      its arguments, the answer it must give) through
 *      zxapp_call_witnessed: every language that implements the entry
 *      runs. A check is TRUE when the answer matches and two languages or
 *      more agree on it; NEITHER when it matches but only one language ran
 *      (unwitnessed); BOTH when the languages contradict each other
 *      (zxapp.h A5); FALSE when the answer differs or every language
 *      refuses. A check that expects the contradiction (want_both, a
 *      "= both" check) is TRUE only when the answer is BOTH.
 *   C2 NO CHECKS, NO VERDICT. With no checks the answer is NEITHER
 *      (unwitnessed), and so is a call before a fit that answered TRUE.
 *      The checks are joined with lpres_conjoin.
 *   C3 THE VERDICT is lpres_conjoin(fit, function) (appfab_verdict). The
 *      kernel's chain order makes TRUE come only from TRUE and TRUE: a
 *      contradiction or a gap never accepts an app.
 *
 * No allocation: appfab_t is large (it holds a parsed manifest per part);
 * give it static storage. The parts and the manifest must outlive it, as
 * zxapp_parse requires of its text.
 */
#ifndef APPFAB_H
#define APPFAB_H

#include <stdbool.h>
#include <stdint.h>

#include "../apps/zxapp.h"
#include "../ipfs_node/ipfs_node.h"
#include "../lpres/lpres.h"

#define APPFAB_PARTS    ZXAPP_BLOCKS
#define APPFAB_DEPTH    8u
#define APPFAB_WHY      192u
#define APPFAB_NO_LAYER 0xFFFFFFFFu

/* One part of the closure: bytes and the CID they must match (F1). */
typedef struct {
    const uint8_t *data;
    uint32_t len;
    ipfsn_cid_t cid;
    uint32_t layer; /* its matrix layer, read only when layered (F7) */
    bool layered;
} appfab_part_t;

/* One check (C1): call `entry` with args; it must answer want. */
typedef struct {
    const char *entry;
    const zxapp_arg_t *args;
    uint32_t nargs;
    const zxapp_num_t *want;
    uint32_t nwant;
    bool want_both; /* the languages must disagree (want is not read) */
} appfab_check_t;

typedef struct {
    zxapp_t app;
    zxapp_t part[APPFAB_PARTS];
    uint8_t state[APPFAB_PARTS]; /* 0 not used, 1 resolving, 2 registered */
    char pin[APPFAB_PARTS][ZXAPP_CID_MAX];
    zxapp_registry_t reg;
    const appfab_part_t *parts;
    uint32_t nparts;
    uint32_t layer;    /* the app's matrix layer, or APPFAB_NO_LAYER */
    lpres_state_t fit; /* the last appfab_fit */
    char why[APPFAB_WHY];
} appfab_t;

/* F1-F6. On TRUE the app stays loaded on h for appfab_function; call
 * appfab_unload when done. h->read_file is used during the call and put
 * back after. */
lpres_state_t appfab_fit(appfab_t *w, const uint8_t *manifest, uint32_t len,
                         const appfab_part_t *parts, uint32_t nparts, zxapp_host_t *h);
/* F1-F7: the app sits at matrix layer `layer` */
lpres_state_t appfab_fit_layered(appfab_t *w, const uint8_t *manifest, uint32_t len, uint32_t layer,
                                 const appfab_part_t *parts, uint32_t nparts, zxapp_host_t *h);
/* F1-F4, F6, F7 without a host: the closure is complete and in order.
 * Nothing is loaded and w->fit stays FALSE, so no function follows; a
 * sender (zxv-fab pack-app) checks this before it signs. */
lpres_state_t appfab_closure(appfab_t *w, const uint8_t *manifest, uint32_t len, uint32_t layer,
                             const appfab_part_t *parts, uint32_t nparts);
/* F7: may layer n use a part from layer m? */
bool appfab_zeck_may_use(uint32_t n, uint32_t m);
/* C1, C2 */
lpres_state_t appfab_function(appfab_t *w, zxapp_host_t *h, const appfab_check_t *checks,
                              uint32_t n);
/* C3 */
lpres_state_t appfab_verdict(lpres_state_t fit, lpres_state_t function);
void appfab_unload(appfab_t *w, zxapp_host_t *h);

#endif /* APPFAB_H */
