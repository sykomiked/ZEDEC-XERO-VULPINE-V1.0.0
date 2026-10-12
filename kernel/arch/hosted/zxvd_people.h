/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_people.h — the people features inside zxvd: the social feed and
 * groups (kernel/src/social), call signalling (kernel/src/call), translation
 * (kernel/src/xlate), the owner's device mesh (kernel/src/devmesh) and the
 * retail "simple mode" setting, served on the control socket as social.*,
 * call.*, xlate.*, devmesh.* and ui.* (docs in api/zxvd.openrpc.json).
 *
 * This file is host glue (files, malloc, the OS random source). The
 * libraries it drives are the freestanding kernel ones, used unchanged.
 *
 *   H1  IDENTITY.  96 bytes drawn from the OS at the first start: 32 seed the
 *       ML-DSA-65 social key (the NodeID is SHA-256 of its public key,
 *       social_post.h P1), 64 seed the device-mesh identity (devmesh.h,
 *       PQM_LEVEL_HIGH: ML-DSA-87, ML-KEM-1024 + X25519). The call member id
 *       is the NodeID's first four bytes, big-endian (0 is read as 1).
 *   H2  THE PEOPLE FILE.  With a books file (zxvd_rpc.h F1) the people state
 *       lives beside it in "people.zxvp": append-only, mode 0600, the books
 *       file's record format and chain ("<64 hex> <JSON>\n", SHA-256 of the
 *       previous hash and the JSON, from 32 zero bytes). Records:
 *         {"op":"people","v":1}            first record
 *         {"op":"seed","hex":...}          H1 (the seed is in this file:
 *                                          protected by 0600 only, not by a
 *                                          hardware key store)
 *         {"op":"rec","hex":...}           a signed social record
 *                                          (social_post.h P3 encoding)
 *         {"op":"ui","simple":true|false}  simple mode
 *       A record is written and fsync'd before the call that made it
 *       answers and before the change applies. At start the file is
 *       replayed; a broken chain, a record that does not verify or an
 *       unknown op stops the people features (zxvd still runs; the methods
 *       answer -32601). A torn last record (no newline) is dropped. The
 *       file is locked (flock) while open. Without a books file everything
 *       is in memory and lost at exit.
 *   H3  THE MESH SNAPSHOT.  The device mesh (dm_mesh_t, about 0.5 MB with
 *       its keys) is saved whole to "people.zxvp.mesh" (0600, written to a
 *       temporary file, fsync'd and renamed) after every change to the
 *       roster; a header carries a magic, the struct size and its SHA-256.
 *       Sessions, pending invitations and pairings are not resumed after a
 *       restart. A snapshot from a build with another struct layout, or one
 *       whose identity is not this seed's, stops the people features.
 *   H4  SOCIAL.  The newest ZXVD_PEOPLE_RECORDS records are held (older ones
 *       stay in the file and replay, then fall out of the ring the same
 *       way). The feed ranks POST and REPLY records with sf_rank (ISF
 *       surplus, bounded freshness, no reputation term: no reputation source
 *       is wired) and never reads reaction or reply counts to rank. GROUP
 *       posts are shown only to members (sg_derive over the held records).
 *       Records from other people arrive only through social.import (a
 *       record that carries its key and verifies): there is no gossip
 *       transport in zxvd yet.
 *   H5  CALLS.  Signalling only, through call_session.h with no ICE agent
 *       (the transport is taken as connected). Capture, codecs and the RTP
 *       media path are the host's job and are not wired in this build: a
 *       CONNECTED call carries no sound. ZXVD_PEOPLE_LINES lines plus one
 *       loopback line: a call to "loopback" rings the loopback line inside
 *       this process, so a call can be placed, answered and hung up with no
 *       network. Messages for other nodes wait in an outbox that call.signal
 *       drains; call.signal also takes the other node's messages in. The
 *       messages are not encrypted here: carry them inside an encrypted
 *       session (call_session.h). Calls are not persisted.
 *   H6  TRANSLATION.  xlate.translate fills the Qwen2.5 ChatML template
 *       (xlate_prompts.c, control tokens in the text defused) and runs it
 *       through the host's generate hook: the same model slot and budget
 *       gate as swarm.ask. With no model that can generate, it answers
 *       ZXVD_E_NO_MODEL and never invents text. Every translation is marked
 *       as a machine translation.
 *   H7  DEVICE MESH.  devmesh.* drive devmesh.h: create the mesh on the
 *       first invitation, invite (QR payload in RFC 4648 base32, short code),
 *       join, confirm the six-digit SAS, revoke, list. Frames for other
 *       devices wait in an outbox for the carrier (zxvd_people_mesh_out);
 *       frames that arrive are fed in with zxvd_people_mesh_in. No carrier
 *       is wired to the socket in this build (frames are up to 96 KiB, past
 *       the socket's line limit): pairing across two machines needs the
 *       phone link or another carrier calling these two functions.
 *   H8  SIMPLE MODE.  ui.get / ui.set {"simple_mode"} only records what the
 *       interfaces should show (R3.1). It changes no check, key, limit or
 *       confirmation anywhere: every method answers the same either way.
 *   H9  LIMITS.  Every string parameter has a byte limit and every id is
 *       exact hex; a fraction, an exponent or an out-of-range number is
 *       -32602, never rounded.
 */
#ifndef ZXVD_PEOPLE_H
#define ZXVD_PEOPLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zxvd_rpc.h"

#define ZXVD_PEOPLE_RECORDS 256u /* H4 */
#define ZXVD_PEOPLE_LINES   4u   /* H5 */
#define ZXVD_PEOPLE_FEED    50u  /* most posts one social.feed returns */
#define ZXVD_PEOPLE_TEXT    4000u /* xlate text, bytes */

/* generate: 0 with the model's answer in out, ZXVD_GEN_NO_MODEL when no
 * model can generate, any other negative value with the reason in out */
#define ZXVD_GEN_NO_MODEL (-1)
typedef struct {
    int (*generate)(void *ctx, const char *prompt, char *out, size_t cap);
    void *ctx;
} zxvd_people_host_t;

typedef struct zxvd_people zxvd_people_t;

/* An instance. path NULL or "" = in memory (H2). NULL with the reason in
 * why when the file, the snapshot or the OS random source fails. */
zxvd_people_t *zxvd_people_new(const char *path, const zxvd_people_host_t *host, char *why,
                               size_t why_cap);
void zxvd_people_free(zxvd_people_t *p);
/* One method: 0 with the result in out, or a JSON-RPC error code with the
 * reason in msg (see zxvd_people_ops_t). */
int zxvd_people_call(zxvd_people_t *p, const char *method, const char *params, size_t len,
                     zxvd_buf_t *out, char *msg, size_t msg_cap);
/* Timers: call retransmits and timeouts, mesh pings. now_ms: monotonic. */
void zxvd_people_tick(zxvd_people_t *p, uint64_t now_ms);
/* Tests: freeze this instance's clock at now_ms (0 = the OS clock again). */
void zxvd_people_set_now(zxvd_people_t *p, uint64_t now_ms);

/* H7: the mesh carrier. out: the next waiting frame (its length, 0 when
 * none; to = the device id, all zero for a short-code pairing broadcast).
 * A frame larger than cap stays queued and its length is returned. */
uint32_t zxvd_people_mesh_out(zxvd_people_t *p, uint8_t to[16], uint8_t *buf, uint32_t cap);
int zxvd_people_mesh_in(zxvd_people_t *p, const uint8_t *frame, uint32_t len); /* dm_status_t */

/* The process-wide instance behind the socket. books_path: the books file
 * (people.zxvp goes beside it), NULL for memory. NULL with a reason on
 * stderr; the methods then answer -32601. */
const zxvd_people_ops_t *zxvd_people_open(const char *books_path, const zxvd_people_host_t *host);
void zxvd_people_close(void);
void zxvd_people_poll(uint64_t now_ms);

/* The window's bridge (POST /api/rpc): one JSON-RPC request line from the
 * local page, answered only for social.*, call.*, xlate.*, devmesh.*, ui.*
 * and the four methods one-tap Ask and Pay need (swarm.ask, ledger.accounts,
 * ledger.balance, ledger.post). Returns false (nothing in out) for any other
 * method or a line that does not parse. */
bool zxvd_people_http(const char *body, size_t len, zxvd_buf_t *out);

#endif
