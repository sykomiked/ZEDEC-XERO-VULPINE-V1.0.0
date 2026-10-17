/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* vna_door.h — the seed's plain-http door: the doorway page, the tree's
 * files and CID blocks, and the L2 state bus down the same TCP connection.
 *
 * Why plain http and no WebSocket: a browser cannot open a UDP socket, and
 * an https page cannot fetch or ws-dial a plain http endpoint — but a page
 * served over http CAN fetch its own origin freely. So the seed serves the
 * doorway itself (the same boot.html the sealed tree carries), the pack and
 * block fetches the loader already knows, and the bus on two routes:
 *
 *   GET  /l2?after=<tick>   a held-open stream of frames, each
 *                           [u16le len][u64le tick][delta bytes], sent as
 *                           the bus produces them; a zero-length frame is
 *                           a keep-alive. The door closes the stream after
 *                           VNA_DOOR_STREAM_MS — the client reconnects with
 *                           after=<last tick it applied>, losing nothing.
 *   POST /l2                one canonical vna_delta (vna_delta.h) as the
 *                           body: parsed, joined into the door's page, and
 *                           handed to the gossip hook (the seeds carry it
 *                           onward as a VNA_SPACE_EV_DELTA event). The
 *                           answer is the cell's lattice state after the
 *                           join, one byte: 0, 1, 2 or 3.
 *   POST /cisex             one CISEX delegated-routing datagram as the
 *                           body (cisex_rt.h): keyword/digest queries,
 *                           index sync, want records, bounties. The host
 *                           registers d->query; the answer is the binary
 *                           reply datagram as application/octet-stream.
 *
 * Everything else is a file: the host's size/body callbacks serve the
 * doorway, the pack, /ipfs/<cid> blocks — whatever its tree holds. A body
 * is pulled in spans, never buffered whole.
 *
 * Like every Vinea file: a pure state machine — no sockets, no clock, no
 * allocation. The host binds listeners, feeds received bytes, sends what
 * vna_door_pull returns, calls vna_door_tick and vna_door_delta_in.
 */
#ifndef VNA_DOOR_H
#define VNA_DOOR_H

#include "vna_delta.h"

#define VNA_DOOR_CONNS        4u    /* connections held at once (one per listener) */
#define VNA_DOOR_REQ_MAX      1536u /* the request head, bounded */
#define VNA_DOOR_BODY_MAX     1024u /* one POSTed delta fits far inside */
#define VNA_DOOR_PATH_MAX     96u
#define VNA_DOOR_DELTAS       48u    /* the stream backlog ring */
#define VNA_DOOR_STREAM_MS    30000u /* a stream ends here; the client resumes */
#define VNA_DOOR_KEEPALIVE_MS 8000u  /* a zero frame goes out at least this often */

/* The host's file access. size: the whole length or <0 unknown/missing.
 * type: the Content-Type or NULL (application/octet-stream) — scripts need
 * a real type or the browser refuses them. body: the span starting at
 * `off`, 0 ends it, <0 fails it. `ctx` is the host's. */
typedef int32_t (*vna_door_size_fn)(void *ctx, const char *path, uint32_t plen);
typedef const char *(*vna_door_type_fn)(void *ctx, const char *path, uint32_t plen);
typedef int32_t (*vna_door_body_fn)(void *ctx, const char *path, uint32_t plen, uint64_t off,
                                    uint8_t *out, uint32_t cap);
/* The bus hook: a delta the door accepted, for the host to gossip on. */
typedef void (*vna_door_gossip_fn)(void *ctx, const uint8_t *delta, uint32_t len);
/* The delegated-routing hook: one bounded request datagram in, a bounded
 * reply out (bytes written to `out`, <0 to refuse with 400). Set d->query
 * and d->qctx to serve POST /cisex; NULL answers 404. */
typedef int32_t (*vna_door_query_fn)(void *ctx, const uint8_t *in, uint32_t in_len,
                                     uint8_t *out, uint32_t cap);

typedef enum {
    VNA_DOOR_REQ = 0, /* reading the request head */
    VNA_DOOR_BODY,    /* reading a POST body */
    VNA_DOOR_SEND,    /* a fixed response is going out */
    VNA_DOOR_FILE,    /* a file body is being pulled span by span */
    VNA_DOOR_STREAM,  /* subscribed to the bus */
    VNA_DOOR_DONE     /* answered; the host closes it */
} vna_door_state_t;

typedef struct {
    uint8_t state;
    uint8_t used;
    uint8_t rx[VNA_DOOR_REQ_MAX];
    uint32_t rx_len;
    uint8_t body[VNA_DOOR_BODY_MAX];
    uint32_t body_need, body_len;
    char path[VNA_DOOR_PATH_MAX];
    uint32_t path_len;
    /* the response: a fixed head then either nothing, a pulled file body or
     * the stream */
    uint8_t head[VNA_DOOR_REQ_MAX];
    uint32_t head_len, head_sent;
    uint64_t body_off;         /* next span offset for a file body */
    int64_t body_left;         /* bytes of the file still to pull; -1 chunked-free stream */
    uint64_t after;            /* the stream's resume cursor (a Lamport tick) */
    uint64_t tick;             /* highest tick already framed to this stream */
    uint64_t opened, last_out; /* stream lifetime and keepalive */
    uint8_t tx[VNA_DELTA_HDR + VNA_DELTA_PAYLOAD_MAX + 16]; /* one staged frame */
    uint32_t tx_len, tx_sent;
} vna_door_conn_t;

/* One delta the stream remembers: the frame's tick and bytes. */
typedef struct {
    uint64_t tick;
    uint16_t len;
    uint8_t bytes[VNA_DELTA_HDR + VNA_DELTA_PAYLOAD_MAX];
} vna_door_ent_t;

typedef struct {
    vna_door_conn_t conn[VNA_DOOR_CONNS];
    vna_door_ent_t log[VNA_DOOR_DELTAS];
    uint32_t log_head, log_n;
    vna_delta_page_t page;
    void *ctx;
    vna_door_size_fn size;
    vna_door_type_fn type;
    vna_door_body_fn body;
    vna_door_gossip_fn gossip;
    vna_door_query_fn query;
    void *qctx;
    uint64_t now;
} vna_door_t;

void vna_door_init(vna_door_t *d, void *ctx, vna_door_size_fn size, vna_door_type_fn type,
                   vna_door_body_fn body, vna_door_gossip_fn gossip);

/* A connection the host accepted: 0..VNA_DOOR_CONNS-1 or -1 full. The host
 * closes it (or it ended) → vna_door_close frees the slot. */
int vna_door_open(vna_door_t *d);
void vna_door_close(vna_door_t *d, uint32_t c);

/* Feed received bytes. 0 on progress, VNA_ERR_SPACE when the head outgrows
 * its buffer (the conn moves to DONE and the host should close it). */
int vna_door_input(vna_door_t *d, uint32_t c, const uint8_t *data, uint32_t n);

/* Bytes to send the conn, or 0 (nothing right now — a held stream is quiet
 * until the bus speaks or the keepalive is due). */
uint32_t vna_door_pull(vna_door_t *d, uint32_t c, uint8_t *out, uint32_t cap);

/* Stream lifetimes and keepalives; pass the time in ms. */
void vna_door_tick(vna_door_t *d, uint64_t now);

/* A delta the door itself accepted: a POST body. Parses, joins the page,
 * logs the frame for every stream and hands the bytes to the gossip hook
 * (POSTs gossip — the seeds relay for browser-only peers). `tick` names its
 * order. Returns the cell's lattice state or a negative status. */
int vna_door_delta_in(vna_door_t *d, const uint8_t *bytes, uint32_t n, uint64_t tick);

/* A delta the door heard: a VNA_SPACE_EV_DELTA already on the bus. Applies
 * and logs like _in but does NOT gossip — re-posting a bus event as a fresh
 * one would flood the space it came from. */
int vna_door_delta_hear(vna_door_t *d, const uint8_t *bytes, uint32_t n, uint64_t tick);

/* The door's page, for the host to read or seed. */
const vna_delta_page_t *vna_door_page(const vna_door_t *d);

#endif /* VNA_DOOR_H */
