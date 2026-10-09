/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* stream_swarm.h — the swarm scheduler: every viewer is also a server.
 *
 * WHAT IT DOES. A stream is a sequence of segments (stream_seg.h); each
 * segment is nfr freights; each freight is decodable from ANY k distinct row
 * ids out of 0..255. The swarm moves rows in PIECES: piece p of a freight is
 * the 8 rows with ids 8p..8p+7 (8 UBH-168 frames, 168 bytes), so a freight
 * has 32 pieces and ANY ceil(k/8) distinct pieces decode it. A node (one per
 * viewer, plus the origin) keeps, for a sliding window of SSW_WIN segments:
 *   - which pieces it holds of every freight, and which it has asked for;
 *   - each neighbour's advert: per freight "none", "complete" (decoded, so it
 *     can mint ANY piece on demand) or "partial" plus a 32-bit piece bitmap;
 *   - per piece, how many neighbours hold it (kept incrementally from advert
 *     diffs; used for rarest-first);
 *   - requests it has in flight (tagged, so a reply is O(1) to match).
 * It does no I/O. The caller feeds it adverts, requests, pieces and the
 * clock, and sends what ssw_schedule (requests) and ssw_upload (pieces)
 * return. Row bytes are the caller's: a node that serves (seq, fr, piece)
 * computes the rows with stream_seg_row or reads them from its store.
 *
 * WHY PIECES, NOT SINGLE ROWS. Telling neighbours what you hold costs bits
 * per item per neighbour, and that cost is paid on every link. Per 21-byte
 * row it would exceed the data itself; per 168-byte piece, with delta
 * adverts, it is still large: in stream_sim (about 20 links per viewer,
 * adverts every 100 ms) adverts + requests + notices are 8% of the data
 * bytes for 1 viewer, ~60% at 16 and ~75-85% at 32-128 viewers (see HONEST
 * LIMITS). Fewer neighbours or a slower advert clock trade that against
 * start-up time and robustness.
 *
 * PULL POLICY (ssw_schedule), deadline-aware:
 *  1. URGENT freights (deadline within urgent_ticks, or the startup buffer
 *     before playback) come first, earliest deadline first, from the
 *     best-scoring neighbours (measured delivery rate plus a prior from the
 *     upload they declare), with a small over-request margin (urgent_extra
 *     distinct extra pieces) against loss. The origin is a last resort, and
 *     only for a player that is actually playing: used when the deadline is
 *     within rescue_ticks or no other neighbour can supply the freight. A
 *     viewer still filling its startup buffer never asks the origin unless
 *     nobody near has had the segment for two segment durations (a flash
 *     crowd of joiners would otherwise drain the origin's uplink; in the
 *     simulator that collapsed N = 128).
 *  2. Then FUTURE freights, rarest first: freights with the fewest copies
 *     among the neighbours (complete holders count as many) go first; inside
 *     a freight, from partial holders, the pieces the fewest neighbours hold
 *     go first. Each piece is asked from the best-scoring holder, so weak
 *     uplinks are asked only for what nobody else has.
 *  Every piece is requested at most once per freight (a timed-out piece is
 *  abandoned, not re-asked: a fresh piece is asked instead, which is what the
 *  MDS / fountain property buys), nothing held or in flight is asked, and
 *  nothing is asked for a freight once ceil(k/8) distinct pieces are held.
 *  Requests are paced by the declared download cap; in-flight per neighbour
 *  is bounded by 2 x (rate x min RTT) + 3 pieces, which keeps uploader
 *  queues short.
 *
 * UPLOAD POLICY (ssw_upload). A token bucket enforces the declared upload
 * cap (rows/s) exactly: burst at most two pieces plus one tick. Queue
 * management: a request that would wait more than ~250 ms (500 ms if urgent)
 * behind the queue is refused at once as "busy", so the requester asks
 * elsewhere instead of timing out; once the queue is half full, a requester
 * already holding its weighted share of that budget (and at least 2) is
 * refused too, so whoever asks first cannot fill the queue. Pending requests
 * are served by deficit round robin (a quantum cut short by the token bucket
 * resumes on the next call), urgent requests before others. Each
 * requester's weight is
 * 1 + reciprocity (pieces it uploaded to us recently, three levels) and may
 * be adjusted by the optional economy hook (the tithe / pay layer: a plain
 * callback). Weights are clamped to 1..8: everyone is always served
 * (cooperative), nobody can monopolise an uplink, and contributors get up to
 * 8x the share of free riders under contention. A tithe callback is told
 * about every piece uploaded.
 *
 * SOURCE RELIEF. The origin pushes each new segment once: pieces
 * 0..n_push-1 of every freight, each to push_fanout (default 1) of its
 * neighbours, chosen by smooth weighted round robin on the upload each
 * neighbour declares in its advert (a neighbour that declares zero gets
 * nothing: pushed pieces are unique at first, so a weak holder would become
 * everyone's bottleneck). n_push > ceil(k/8) leaves slack, so a viewer can
 * skip the slowest holders and still collect enough. The origin first sends
 * each target a NOTICE (the bitmap of pieces coming to it), so a target never
 * asks anyone else for those pieces. The origin's load is about
 * 8 n_push / k times the stream rate however large the audience; everything
 * else moves viewer to viewer.
 *
 * WIRE (little-endian; all carried inside the transport):
 *   ADVERT  "ZXSA" ver flags nseg nfr | stream u64 | sender u32 | base u32 |
 *           newest u32 | declared upload rows/s u32 | 2-bit states (0 none,
 *           1 partial, 2 complete) for nseg x nfr entries, padded to a byte
 *           with zero bits | one 4-byte piece bitmap per partial entry, in
 *           entry order (never empty, never full). Exact length.
 *   DELTA   "ZXSA" ver flags|0x02 count nfr(=SSW_MAX_FR) | base u32 |
 *           count x (offset u8 < SSW_WIN, fr | state << 4), entries strictly
 *           increasing, each partial one followed by its 4-byte bitmap.
 *           Only the listed freights changed. A delta carries no stream or
 *           sender id: it is bound to the session that carried the last full
 *           advert (ssw_advert_apply refuses a delta before a full one), so
 *           it relies on the transport's in-order, lossless delivery.
 *           ssw_advert_next builds deltas; send a full advert to a new
 *           neighbour and periodically (the simulator: every 2 s).
 *   REQUEST "ZXSQ" ver count | stream u64 | count x (seq u32, fr u8,
 *           piece u8 < 32, tag u16 != 0xFFFF, flags u8 bit0 urgent);
 *           count 1..64.
 *   NOTICE  "ZXSN" ver fr 0 0 | stream u64 | seq u32 | 4-byte piece bitmap.
 *   PIECE   "ZXSR" ver fr piece flags | stream u64 | seq u32 | tag u16 |
 *           8 UBH-168 freight packets (21 bytes: id + 20 payload bytes) with
 *           ids 8 piece .. 8 piece + 7 in order. flags bit0 = pushed (tag
 *           must be 0xFFFF exactly when pushed).
 * Parsers are strict (exact length, reserved bits zero, ranges checked) and
 * never read outside the given buffer.
 *
 * SECURITY. This module assumes an authenticated, encrypted transport: the
 * PQ-secured Carracho sessions (kernel/src/carracho: hybrid X25519 +
 * ML-KEM-768 key exchange, ML-DSA-65 identities, ChaCha20-Poly1305 records)
 * carry every message, and a neighbour's id is the authenticated session
 * identity. Nothing here encrypts or authenticates. Content integrity comes
 * from the signed manifests and the hashes in stream_seg.h. The origin flag
 * is set by the caller from that authenticated identity, never from an
 * advert; declared upload is a claim, used only to place pushes.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. The node struct (about 100 KB with the defaults) is
 * caller memory.
 *
 * HONEST LIMITS. "Faster with more people" holds only while viewers
 * contribute upload: the swarm can deliver at most (origin upload + sum of
 * viewer uploads) / viewers per viewer, so zero-upload viewers, or uplinks
 * well below the stream rate (asymmetric DSL/cellular), break it, and the
 * test measures exactly that failure. It also needs headroom: in the
 * simulator the live rate held with that bound at 1.22-1.45x the demand,
 * and at 0.91x (25% free riders on the default links) it fell to 0.6R. Up
 * to 40% of sessions saw at least one short stall (under 0.5% of watch
 * time). Availability gossip is the price of small pieces: 60-85% of the
 * data bytes at this 86 kbit/s test rate with ~20 links per viewer. That is
 * a per-link cost (it grows with links and with freight state changes, not
 * with the audience), but it is real upload and download the caller must
 * budget for. The scheduler is tuned on one simulated topology; with a
 * 1 s urgent horizon (urgent_ticks 100) the same code fell into a
 * congestion collapse at N = 128 on some seeds, at 0.6 s it did not on the
 * five seeds tried. A start-over joiner's catch-up reached ~2R, about half
 * its 4R download cap: its non-urgent pulls wait behind live traffic. An individual viewer is never
 * faster than its own download link, nor faster than its neighbours' spare upload. A LIVE stream
 * cannot be watched faster than it is produced: for live viewers "faster" means the per-viewer rate
 * holds at the stream rate as the audience grows (client-server collapses to origin/N), and joiners
 * or seekers catching up on already-aired segments get more bandwidth the more holders there are.
 * All content still starts at the origin, so a brand-new segment cannot spread faster than the
 * origin can push one copy of it, and a flash crowd that joins together all waits on the same fresh
 * segments. Reciprocity is a cooperative incentive, not a proof against collusion or Sybil peers;
 * declared upload can be a lie (it only steers pushes; a liar gets pushed pieces it then fails to
 * serve, and the slack absorbs it). The window tracks at most SSW_WIN x SSW_MAX_FR freights (raise
 * SSW_MAX_FR and lower SSW_WIN for high bitrates: memory goes with the product). Rows are verified
 * only when a freight decodes (see stream_seg.h), so a lying peer costs a refetch, and this module
 * does not identify the liar.
 */
#ifndef STREAM_SWARM_H
#define STREAM_SWARM_H

#include <stdbool.h>
#include <stdint.h>

#ifndef SSW_MAX_PEERS
#    define SSW_MAX_PEERS 24 /* neighbours, the origin included */
#endif
#ifndef SSW_WIN
#    define SSW_WIN 64 /* segments in the window; power of two */
#endif
#ifndef SSW_MAX_FR
#    define SSW_MAX_FR 4 /* freights per segment tracked */
#endif
#define SSW_ROW_IDS    256 /* row ids 0..255: the MDS range */
#define SSW_PIECE_ROWS 8   /* rows per piece */
#define SSW_PIECES     32  /* pieces per freight */
#define SSW_BM         4   /* bytes in a 32-bit piece bitmap */
#define SSW_PQ         64  /* per-neighbour upload queue (each class) */
#define SSW_INFLIGHT   512
#define SSW_PUSH_SEGS  4 /* origin: segments being pushed at once */
#define SSW_TAG_PUSH   0xFFFFu
#define SSW_WEIGHT_MAX 8u
#define SSW_REQ_MAX    64u /* entries in one REQUEST message */
#define SSW_NONE       0xFFFFFFFFu
/* pieces needed to decode a freight of k data rows */
#define SSW_KP(k) (((uint32_t) (k) + 7u) >> 3)

#define SSW_ST_NONE     0
#define SSW_ST_PARTIAL  1
#define SSW_ST_COMPLETE 2

#define SSW_ADV_ORIGIN    0x01u
#define SSW_ADV_DELTA     0x02u /* only the listed freights changed; others as before */
#define SSW_ADV_DELTA_MAX 255u  /* entries in one delta */
#define SSW_ADV_HDR       32u
#define SSW_ADV_DELTA_HDR 12u
#define SSW_ADV_MAX       (SSW_ADV_HDR + (SSW_WIN * SSW_MAX_FR + 3) / 4 + SSW_WIN * SSW_MAX_FR * SSW_BM)
#define SSW_REQ_HDR       14u
#define SSW_REQ_ENTRY     9u
#define SSW_NOTICE_LEN    24u
#define SSW_PIECE_HDR     22u
#define SSW_PIECE_LEN     (SSW_PIECE_HDR + SSW_PIECE_ROWS * 21u) /* 190 */

#define SSW_MODE_LIVE 0 /* start near the live edge, fetch everything produced */
#define SSW_MODE_VOD  1 /* start at start_seq (VOD, start-over, seek) */

enum {
    SSW_OK = 0,
    SSW_ERR_ARG = -1,
    SSW_ERR_FULL = -2,
    SSW_ERR_FORMAT = -3,
    SSW_ERR_NOTFOUND = -4,
    SSW_ERR_SPACE = -5,
};

/* ssw_on_piece result bits */
#define SSW_PC_NEW      0x01u /* a new distinct piece */
#define SSW_PC_COMPLETE 0x02u /* this piece completed the freight */
#define SSW_PC_DUP      0x04u /* piece already held: should never happen */
#define SSW_PC_LATE     0x08u /* distinct, but the freight was already complete */
#define SSW_PC_IGNORED  0x10u /* outside the window / unknown */

/* economy hooks (optional) */
typedef uint32_t (*ssw_weight_fn)(void *ctx, uint32_t peer_id, uint32_t base_weight);
typedef void (*ssw_tithe_fn)(void *ctx, uint32_t from_id, uint32_t to_id, uint32_t rows);
/* weight_hook: returns the DRR weight for a requester (clamped 1..8).
 * tithe_hook: called for every piece uploaded, rows = SSW_PIECE_ROWS. */

typedef struct {
    uint32_t ticks_per_sec;   /* clock resolution, e.g. 100 */
    uint32_t up_rows_per_sec; /* declared upload cap */
    uint32_t dn_rows_per_sec; /* declared download cap (request pacing) */
    uint32_t seg_ticks;       /* segment duration */
    uint32_t urgent_ticks;    /* deadline horizon for the urgent pass */
    uint32_t rescue_ticks;    /* deadline horizon that allows asking the origin */
    uint32_t start_seq;       /* VOD: first segment to play */
    uint8_t mode;             /* SSW_MODE_* */
    uint8_t is_origin;
    uint8_t live_back;     /* LIVE: start this many segments behind the newest */
    uint8_t startup_segs;  /* segments buffered before playback starts */
    uint8_t prefetch_segs; /* VOD: how far ahead of the playhead to fetch */
    uint8_t urgent_extra;  /* extra distinct pieces asked for urgent freights */
    uint16_t n_push;       /* origin: pieces 0..n_push-1 are pushed (<= 32) */
    uint8_t push_fanout;   /* origin: copies of each pushed id, 1..2 */
    ssw_weight_fn weight_hook;
    ssw_tithe_fn tithe_hook;
    void *hook_ctx;
} ssw_config_t;

/* ---- playback clock (also used by the client-server baseline) ---- */
typedef bool (*ssw_complete_fn)(void *ctx, uint32_t seq);
typedef struct {
    uint32_t seg_ticks;
    uint8_t startup_segs;
    bool start_known, started, stalled;
    uint32_t start_seq, play_seq, play_until;
    uint32_t join_tick, start_tick;
    uint32_t stalls, stall_ticks, played;
} ssw_player_t;

void ssw_player_init(ssw_player_t *p, uint32_t seg_ticks, uint8_t startup_segs, uint32_t now);
void ssw_player_set_start(ssw_player_t *p, uint32_t seq);
/* Advance the clock: start once startup_segs segments from start_seq are
 * complete; afterwards a segment not complete when its turn comes is a
 * stall (counted once) until it completes. */
void ssw_player_step(ssw_player_t *p, uint32_t now, ssw_complete_fn done, void *ctx);
/* Tick by which `seq` must be complete (now for the startup buffer). */
uint32_t ssw_player_deadline(const ssw_player_t *p, uint32_t seq, uint32_t now);

/* ---- node state ---- */
typedef struct {
    uint8_t have[SSW_BM];
    uint8_t asked[SSW_BM];   /* requested (or announced by a push notice) */
    uint8_t cnt[SSW_PIECES]; /* partial neighbours holding each id */
    uint16_t nhave, ninfl;
    uint16_t push_exp; /* notice ids not yet arrived */
    uint16_t tot;      /* sum of cnt[]: copies held by partial neighbours */
    uint8_t ncomp;     /* complete neighbours */
    uint8_t done;
    uint8_t dirty;       /* changed since the last advert */
    uint32_t push_until; /* push_exp counts only before this tick */
} ssw_ofr_t;

typedef struct {
    uint32_t seq;
    uint8_t used, known, k, nfr, ndone;
    uint32_t learned;
    ssw_ofr_t fr[SSW_MAX_FR];
} ssw_oseg_t;

typedef struct {
    uint8_t st;
    uint8_t bm[SSW_BM];
} ssw_vfr_t;

typedef struct {
    uint32_t seq;
    ssw_vfr_t fr[SSW_MAX_FR];
} ssw_vseg_t;

typedef struct {
    uint8_t peer; /* neighbour index (ssw_schedule output) */
    uint8_t fr;
    uint8_t pc;
    uint8_t urgent;
    uint16_t tag;
    uint32_t seq;
} ssw_req_t;

typedef struct {
    uint32_t id;
    uint8_t used, origin;
    uint8_t qu_h, qu_n, qn_h, qn_n;
    uint16_t infl, infl_cap;
    uint32_t srtt8, minrtt, minrtt_prev;
    uint32_t rate;     /* pieces per tick x 256, EWMA */
    uint32_t got_tick; /* pieces received from it this tick */
    uint32_t recv_recent, sent_recent;
    uint32_t deficit[2];
    uint32_t reject_until;
    uint32_t declared_up; /* from its advert; SSW_NONE until one arrives */
    uint8_t full_adv;     /* a full advert has been applied */
    int32_t wrr;          /* origin: smooth weighted round robin state */
    uint64_t recv_total, sent_total;
    ssw_req_t qu[SSW_PQ], qn[SSW_PQ];
    ssw_vseg_t v[SSW_WIN];
} ssw_peer_t;

typedef struct {
    uint32_t seq, sent;
    uint8_t peer, fr, pc, used;
} ssw_infl_t;

typedef struct {
    uint32_t seq;
    uint8_t active, nfr;
    uint16_t pc; /* cursor: piece-major over (piece, fr, copy) */
    uint8_t fr, copy;
    uint8_t tgt[SSW_MAX_FR][SSW_PIECES][2]; /* neighbour index, 0xFF = none */
} ssw_push_t;

typedef struct {
    uint64_t rx_new, rx_dup, rx_late, rx_push, rx_ignored;
    uint64_t req_sent, timeouts, rejects_rx, rejects_tx;
    uint64_t up_rows, up_push, freights_done;
} ssw_stats_t;

typedef struct ssw_node {
    ssw_config_t cfg;
    uint64_t stream_id;
    uint32_t self_id;
    uint32_t now;
    uint32_t base;   /* window: [base, base + SSW_WIN) */
    uint32_t newest; /* newest known seq, SSW_NONE if none */
    bool any;
    int32_t up_credit, dn_credit;
    uint8_t rr[2], rr_keep[2];
    uint32_t sec_ctr, epoch_ctr;
    uint32_t queued;             /* requests waiting in all upload queues */
    uint32_t dirty_lo, dirty_hi; /* segments changed since the last advert (lo > hi: none) */
    uint16_t nfree;
    uint16_t freelist[SSW_INFLIGHT];
    ssw_infl_t infl[SSW_INFLIGHT];
    ssw_oseg_t seg[SSW_WIN];
    ssw_peer_t peer[SSW_MAX_PEERS];
    ssw_push_t push[SSW_PUSH_SEGS];
    ssw_player_t player;
    ssw_stats_t st;
} ssw_node_t;

typedef struct {
    uint8_t peer;
    uint8_t kind; /* SSW_SEND_* */
    uint8_t fr, pc;
    uint16_t tag;
    uint32_t seq;
} ssw_send_t;
#define SSW_SEND_DATA   1
#define SSW_SEND_PUSH   2
#define SSW_SEND_REJECT 3 /* do not hold it */

typedef struct {
    uint8_t peer;
    uint8_t fr;
    uint32_t seq;
    uint8_t bm[SSW_BM];
} ssw_notice_t;

typedef struct {
    uint64_t stream_id;
    uint32_t sender, base, newest, up_rows_per_sec;
    uint8_t flags, nseg, nfr;
    uint8_t state[SSW_WIN * SSW_MAX_FR];
    uint8_t present[SSW_WIN * SSW_MAX_FR]; /* delta: entry listed */
    uint8_t bm[SSW_WIN * SSW_MAX_FR][SSW_BM];
} ssw_advert_t;

void ssw_config_default(ssw_config_t *c);
int ssw_init(ssw_node_t *n, const ssw_config_t *cfg, uint64_t stream_id, uint32_t self_id,
             uint32_t now);

/* Neighbours. add returns the index (>= 0). is_origin comes from the
 * authenticated session identity. remove frees its requests and counts. */
int ssw_peer_add(ssw_node_t *n, uint32_t id, bool is_origin);
int ssw_peer_remove(ssw_node_t *n, uint32_t id);
int ssw_peer_find(const ssw_node_t *n, uint32_t id);
uint32_t ssw_peer_count(const ssw_node_t *n);

/* A verified manifest for seq (k, nfr) is known. */
int ssw_seg_known(ssw_node_t *n, uint32_t seq, uint8_t k, uint8_t nfr);
/* Origin: seq is published (held in full). Writes push notices for the
 * targets (at most SSW_MAX_PEERS * nfr) to out; *nout gets the count. */
int ssw_origin_publish(ssw_node_t *n, uint32_t seq, uint8_t k, uint8_t nfr, ssw_notice_t *out,
                       uint32_t cap, uint32_t *nout);

void ssw_on_notice(ssw_node_t *n, int peer, uint32_t seq, uint8_t fr, const uint8_t bm[SSW_BM]);
/* A neighbour asks for a piece. Returns SSW_OK (queued) or SSW_ERR_FULL /
 * SSW_ERR_NOTFOUND: the caller replies with a reject. */
int ssw_on_request(ssw_node_t *n, int peer, uint32_t seq, uint8_t fr, uint8_t pc, uint16_t tag,
                   bool urgent);
/* A piece arrived (pushed: tag == SSW_TAG_PUSH). Returns SSW_PC_* bits. */
unsigned ssw_on_piece(ssw_node_t *n, int peer, uint32_t seq, uint8_t fr, uint8_t pc, uint16_t tag);
/* busy: the neighbour's queue was full (back off briefly); otherwise it did
 * not hold the piece (stale advert) and it may be asked elsewhere. */
void ssw_on_reject(ssw_node_t *n, int peer, uint16_t tag, bool busy);

/* Clock: timeouts, rate estimates, token buckets, playback. Call once per tick. */
void ssw_tick(ssw_node_t *n, uint32_t now);
uint32_t ssw_schedule(ssw_node_t *n, ssw_req_t *out, uint32_t cap);
uint32_t ssw_upload(ssw_node_t *n, ssw_send_t *out, uint32_t cap);

bool ssw_seg_complete(const ssw_node_t *n, uint32_t seq);
/* Copy the held-piece bitmap of (seq, fr); false if unknown. */
bool ssw_pieces_held(const ssw_node_t *n, uint32_t seq, uint8_t fr, uint8_t bm[SSW_BM]);
/* Deadline-ordering helper exposed for tests: is (seq) urgent now? */
bool ssw_is_urgent(const ssw_node_t *n, uint32_t seq);

/* ---- wire ---- */
/* Full advert: every segment of the window the node holds anything of. */
int ssw_advert_build(const ssw_node_t *n, uint8_t *out, uint32_t cap);
/* What to send to every neighbour this round: a full advert when `full`,
 * otherwise a delta (SSW_ADV_DELTA) over just the segments whose state
 * changed since the last call. Returns the length, 0 when there is no
 * change to report, or < 0. A delta assumes the transport delivers adverts
 * in order and without loss (a Carracho stream does); send a full advert to
 * a new neighbour and periodically (the simulator: every 2 s). */
int ssw_advert_next(ssw_node_t *n, uint8_t *out, uint32_t cap, bool full);
int ssw_advert_parse(const uint8_t *in, uint32_t len, ssw_advert_t *a);
int ssw_advert_apply(ssw_node_t *n, int peer, const ssw_advert_t *a);

int ssw_req_encode(uint64_t stream_id, const ssw_req_t *r, uint32_t count, uint8_t *out,
                   uint32_t cap);
int ssw_req_parse(const uint8_t *in, uint32_t len, uint64_t *stream_id, ssw_req_t *r, uint32_t cap,
                  uint32_t *count);
int ssw_notice_encode(uint64_t stream_id, uint32_t seq, uint8_t fr, const uint8_t bm[SSW_BM],
                      uint8_t *out, uint32_t cap);
int ssw_notice_parse(const uint8_t *in, uint32_t len, uint64_t *stream_id, uint32_t *seq,
                     uint8_t *fr, uint8_t bm[SSW_BM]);
/* frames: the 8 UBH-168 freight packets of the piece (8 x 21 bytes, ids
 * 8 pc .. 8 pc + 7 in order). */
int ssw_piece_encode(uint64_t stream_id, uint32_t seq, uint8_t fr, uint8_t pc, bool pushed,
                     uint16_t tag, const uint8_t frames[SSW_PIECE_ROWS * 21], uint8_t *out,
                     uint32_t cap);
int ssw_piece_parse(const uint8_t *in, uint32_t len, uint64_t *stream_id, uint32_t *seq,
                    uint8_t *fr, uint8_t *pc, bool *pushed, uint16_t *tag,
                    uint8_t frames[SSW_PIECE_ROWS * 21]);

#endif /* STREAM_SWARM_H */
