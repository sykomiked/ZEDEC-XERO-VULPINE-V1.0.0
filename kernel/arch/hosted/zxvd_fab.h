/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_fab.h — fab.*: file fabrication over zxvd's socket, the one block
 * method apps use to send, receive and stream (docs/spec/FABRICATION.md).
 * The engine is kernel/src/fab (fab.h); this group holds zxvd's keys, its
 * block store and up to ZXVD_FAB_STREAMS streams.
 *
 *   B1  The group has the feature "fab" and is not regulated: it moves
 *       files, not money. fab.status and fab.keys are `always` (zxvd_ext.h
 *       X4); every other method answers only with the switch on.
 *   B2  zxvd_fab_open(dir) keeps everything under dir (zxvd puts it beside
 *       the socket, "<socket folder>/fab"): blocks/ holds public blocks
 *       named by their CID string, the layout `zxv-fab put` writes;
 *       sign.seed (32 bytes) and kem.seed (64 bytes), owner-only, are the
 *       seeds of zxvd's ML-DSA-87 signing key and ML-KEM-1024 key, made
 *       from the OS random source the first time. The seeds never leave
 *       the folder and no method returns them.
 *   B3  Paths in parameters are absolute paths on this machine, at most
 *       ZXVD_FAB_PATH - 1 bytes; files are read whole into memory, at most
 *       ZXVD_FAB_MAX_FILE bytes. A file is written to PATH.tmp, flushed,
 *       then renamed over PATH, owner-only, and only after the rebuilt
 *       bytes match the target CID (fab.h): a failed call leaves no output.
 *   B4  The air-gap seal: zxvd_fab_set_seal registers the host's check
 *       (the Bare-metal seal after hydration). A call with airgap: true
 *       fetches and checks every block first, then assembles only when the
 *       check answers TRUE (lpres_state_t; FALSE, BOTH and NEITHER refuse).
 *       With no check registered, airgap: true is refused before anything
 *       is read. airgap: false (the default) asks no check.
 *   B5  fab.status: {open, sign_id, kem_id, seal: true when a check is
 *       registered, streams: [{stream, role: "send"|"receive", seq,
 *       checked}]} (ids null while closed). fab.keys: {sign_id, sign_pk,
 *       kem_id, kem_ek} as hex (ids are SHA-256 of the public key; give
 *       kem_ek to a sender, sign_id to a receiver's trust list).
 *   B6  fab.put {path}: copy one file (1..FAB_BLOCK_MAX bytes) into the
 *       store as a raw block; answers {cid, len}.
 *   B7  fab.send {target, out, blocks: [{cid}] = [], to: kem_ek hex = none,
 *       window = 32}: the recipe for target over those store blocks,
 *       signed with zxvd's key, PUBLIC without "to", SEALED to that key with
 *       it. Answers {cid, len, mode, ops, diff, used, decoys, recipe}.
 *   B8  fab.receive {recipe, out, trust: [{id}] = any, airgap = false}:
 *       check the recipe (signature; sender in trust when given), open it
 *       with zxvd's key when sealed, take the blocks from the store, write
 *       out. Answers {cid, len, mode, sender, blocks}. A stream recipe is
 *       refused here (fab.stream.open).
 *   B9  Streams, the sending end: fab.stream.start {target, out, to,
 *       slot_us > 0, blocks = [], window = 32} writes the stream recipe
 *       with target as frame 0 and answers {stream, cid, len, recipe};
 *       fab.stream.send {stream, target, out} writes the next frame, built
 *       on the frame before it and the blocks, and answers {seq, cid, len,
 *       frame, diff}; fab.stream.sign {stream, from, to} answers
 *       {checkpoint: hex} over frames from..to (fab.h).
 *   B10 Streams, the receiving end: fab.stream.open {recipe, out, trust,
 *       airgap} writes frame 0 and answers {stream, cid, len, slot_us};
 *       fab.stream.frame {stream, frame, out} applies the next frame file
 *       and answers {seq, cid, len}; a frame that needs a base this end
 *       does not hold is RESYNC, and the stream continues with a frame
 *       that does not use it. fab.stream.checkpoint {stream, checkpoint:
 *       hex} answers {from, to}. The last frame stays in memory as the
 *       base for the next.
 *   B11 fab.stream.close {stream} forgets one stream and wipes its keys;
 *       the group's close forgets every stream. Stream handles count up
 *       from 1 and are never reused in a run.
 *   B12 Errors: ZXVD_E_FAB (-32085), message "<FAB status>: why" (FORM,
 *       TRUST, SIGN, KEY, AUTH, PLAN, SPACE, FETCH, BLOCK, TARGET, SEAL,
 *       REPLAY, RESYNC, STREAM) or "FILE: why" for a file that cannot be
 *       read or written; bad parameters are ZXVD_E_PARAMS.
 */
#ifndef ZXVD_FAB_H
#define ZXVD_FAB_H

#include <stddef.h>
#include <stdint.h>

#include "lpres.h"

#define ZXVD_E_FAB        (-32085) /* B12; in -32099..-32001 (zxvd_rpc.h X4) */
#define ZXVD_FAB_STREAMS  4u
#define ZXVD_FAB_PATH     1024u
#define ZXVD_FAB_MAX_FILE (256u << 20)

/* B2: 0, or -1 when the folder or its keys cannot be made or read */
int zxvd_fab_open(const char *dir);
void zxvd_fab_close(void);
/* B4: NULL removes the check */
void zxvd_fab_set_seal(lpres_state_t (*fn)(void *ctx), void *ctx);
/* Unit tests only: where the group draws random bytes (NULL = the OS,
 * zxv_os_random). fn returns 0 on success. */
void zxvd_fab_set_random(int (*fn)(uint8_t *p, size_t n));

#endif
