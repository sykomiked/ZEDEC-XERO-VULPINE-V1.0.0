/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_session.h — one-to-one call state machine and signalling.
 *
 * States:
 *   IDLE -> OUTGOING (offer sent) -> RINGING_OUT (peer is ringing)
 *        -> CONNECTING (answer received; connectivity checks) -> CONNECTED
 *   IDLE -> INCOMING (offer received, local ringing) -> CONNECTING ...
 *   CONNECTED <-> HELD (local or remote hold)        any -> ENDED
 *
 * SIGNALLING travels inside an already-encrypted Carracho session through
 * the send callback; this module never sees keys. Message layout
 * (big-endian):
 *   0 2 magic 0x4353 ("CS")  2 1 version 1  3 1 type
 *   4 8 call id  12 4 from  16 4 to  20 2 message seq  22 2 body length
 *   OFFER / ANSWER body: 4 tie-breaker, 4 capability bits, 1 media flags,
 *     1 codec count, count x (1 payload type, 1 kind),
 *     8 ICE ufrag, 16 ICE password, 1 candidate count, count x 29 octets
 *   REJECT / BYE / CANCEL body: 1 reason.  Others: empty.
 *
 * CODECS are negotiated by payload type only. Defaults, in preference
 * order: Opus (111) for audio, VP8 (96) then VP9 (98) and AV1 (45) for
 * video, all royalty-free. The answerer picks, per kind, the first offered
 * payload type it also supports; the answer carries only the choices and
 * the offerer checks they were offered. Capability bits are ANDed; UBH-168
 * carriage is used only if both set CALL_CAP_UBH168 (call_rtp_negotiate).
 *
 * TIMERS (configurable, milliseconds): an offer is resent at 1 s, 2 s, 4 s
 * and the call ends NO_RESPONSE after 4 sends; ringing ends NO_ANSWER after
 * 30 s (caller sends CANCEL; the callee reports a missed call); an ANSWER is
 * resent until ACKed or media flows; CONNECTING ends ICE_FAILED after 10 s;
 * a connected call sends KEEPALIVE every 5 s and ends TIMEOUT after 15 s
 * with nothing from the peer.
 *
 * GLARE (both sides call each other at once): each OFFER carries a random
 * 32-bit tie-breaker; on receiving an OFFER from the peer it is itself
 * calling, the side with the larger (tie-breaker, member id) keeps its
 * call, the other drops its own offer and answers the winner's, so both end
 * in one call with the winner's call id. No missed-call notice is raised.
 *
 * NOTIFY callback (for the system notification bus): INCOMING when an offer
 * starts ringing here, MISSED when an incoming call ends unanswered (peer
 * cancelled, ring timeout, or arrived while busy), ENDED when any other
 * call ends, CONNECTED when media can flow. Every call that leaves IDLE
 * ends with exactly one MISSED or ENDED. A plain function pointer; for a
 * busy-missed call 'peer' names the caller and 's' is the ongoing call.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Times are 32-bit wrapping milliseconds.
 *
 * HONEST LIMITS. One call per session object; a phone with call waiting
 * runs several. No codecs and no camera or microphone capture live here:
 * payload types are just numbers, and encoding, decoding and capture come
 * from the host OS (AVFoundation on macOS) or from bundled libopus/libvpx
 * later. Identity is whatever the Carracho session authenticated; the
 * from/to fields are checked only against the configured peer. The
 * tie-breaker comes from a caller RNG callback; a weak RNG only makes glare
 * resolution fall back to comparing member ids.
 */
#ifndef CALL_SESSION_H
#define CALL_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include "call_ice.h"
#include "call_rtp.h"

#define CALL_SIG_MAGIC   0x4353
#define CALL_SIG_VERSION 1
#define CALL_SIG_HDR_LEN 24
#define CALL_MAX_CODECS  8
#define CALL_SIG_MAX                                                                               \
    (CALL_SIG_HDR_LEN + 10 + 2 * CALL_MAX_CODECS + CALL_ICE_UFRAG_LEN + CALL_ICE_PWD_LEN + 1 +     \
     CALL_ICE_MAX_CANDS * CALL_CAND_WIRE)

#define CALL_MEDIA_AUDIO 0x01
#define CALL_MEDIA_VIDEO 0x02
#define CALL_MEDIA_DATA  0x04

typedef enum {
    CALL_MSG_OFFER = 1,
    CALL_MSG_RINGING = 2,
    CALL_MSG_ANSWER = 3,
    CALL_MSG_REJECT = 4,
    CALL_MSG_BYE = 5,
    CALL_MSG_HOLD = 6,
    CALL_MSG_RESUME = 7,
    CALL_MSG_ACK = 8,
    CALL_MSG_KEEPALIVE = 9,
    CALL_MSG_CANCEL = 10,
} call_msg_type_t;

typedef enum {
    CALL_ST_IDLE = 0,
    CALL_ST_OUTGOING,
    CALL_ST_RINGING_OUT,
    CALL_ST_INCOMING,
    CALL_ST_CONNECTING,
    CALL_ST_CONNECTED,
    CALL_ST_HELD,
    CALL_ST_ENDED,
} call_state_t;

typedef enum {
    CALL_END_NONE = 0,
    CALL_END_LOCAL_HANGUP,
    CALL_END_REMOTE_HANGUP,
    CALL_END_REJECTED,
    CALL_END_NO_ANSWER,
    CALL_END_NO_RESPONSE,
    CALL_END_ICE_FAILED,
    CALL_END_TIMEOUT,
    CALL_END_BUSY,
    CALL_END_INCOMPATIBLE,
    CALL_END_CANCELLED,
} call_end_reason_t;

typedef enum {
    CALL_NOTIFY_INCOMING = 1,
    CALL_NOTIFY_MISSED = 2,
    CALL_NOTIFY_ENDED = 3,
    CALL_NOTIFY_CONNECTED = 4,
} call_notify_t;

typedef struct call_codec {
    uint8_t pt;
    uint8_t kind; /* CALL_MEDIA_AUDIO / VIDEO / DATA */
} call_codec_t;

typedef struct call_sig {
    uint8_t type;
    uint8_t call_id[8];
    uint32_t from, to;
    uint16_t seq;
    /* OFFER / ANSWER */
    uint32_t tie;
    uint32_t caps;
    uint8_t media;
    uint8_t ncodecs;
    call_codec_t codecs[CALL_MAX_CODECS];
    uint8_t ufrag[CALL_ICE_UFRAG_LEN];
    uint8_t pwd[CALL_ICE_PWD_LEN];
    uint8_t ncands;
    call_cand_t cands[CALL_ICE_MAX_CANDS];
    /* REJECT / BYE / CANCEL */
    uint8_t reason;
} call_sig_t;

int call_sig_write(const call_sig_t *m, uint8_t *out, uint32_t cap);
int call_sig_parse(const uint8_t *in, uint32_t len, call_sig_t *m);

struct call_session;
typedef int (*call_sig_send_fn)(void *ctx, const uint8_t *msg, uint32_t len);
typedef void (*call_notify_fn)(void *ctx, call_notify_t what, uint32_t peer,
                               call_end_reason_t reason, const struct call_session *s);
typedef uint32_t (*call_rand_fn)(void *ctx);

typedef struct call_session_cfg {
    uint32_t self_id;
    uint32_t caps;
    uint8_t ncodecs; /* 0 = defaults */
    call_codec_t codecs[CALL_MAX_CODECS];
    call_sig_send_fn send;
    call_notify_fn notify;
    call_rand_fn rand;
    void *ctx;
    call_ice_t *ice; /* optional: NULL = the transport is already connected */
    uint32_t offer_rtx_ms, offer_tries, ring_ms, connect_ms, keepalive_ms, idle_ms;
} call_session_cfg_t;

typedef struct call_session {
    call_session_cfg_t cfg;
    call_state_t state;
    call_end_reason_t end_reason;
    uint32_t peer;
    uint8_t call_id[8];
    uint8_t outgoing; /* we placed this call */
    uint8_t local_hold, remote_hold;
    uint8_t acked;
    uint8_t media_wanted;
    uint32_t tie;
    uint16_t tx_seq;
    uint32_t timer;    /* next retransmit or deadline */
    uint32_t deadline; /* state deadline */
    uint32_t last_rx;  /* last message or media from peer */
    uint32_t last_tx;
    uint32_t tries;
    /* negotiated */
    uint32_t common_caps;
    call_carriage_t carriage;
    uint8_t audio_pt, video_pt, data_pt; /* 0 = none */
    uint8_t media;
    /* remote ICE parameters from the offer/answer */
    uint8_t rufrag[CALL_ICE_UFRAG_LEN], rpwd[CALL_ICE_PWD_LEN];
    uint8_t nrcands;
    call_cand_t rcands[CALL_ICE_MAX_CANDS];
    call_sig_t pending_offer; /* incoming offer awaiting the user */
    uint32_t transitions;
    uint32_t sent, received, rejected_msgs;
} call_session_t;

void call_session_init(call_session_t *s, const call_session_cfg_t *cfg);
/* Back to IDLE after ENDED (keeps cfg). */
void call_session_reset(call_session_t *s);

int call_session_dial(call_session_t *s, uint32_t peer, uint8_t media, uint32_t now);
int call_session_accept(call_session_t *s, uint32_t now);
int call_session_reject(call_session_t *s, uint32_t now);
int call_session_hangup(call_session_t *s, uint32_t now);
int call_session_hold(call_session_t *s, bool on, uint32_t now);

/* A signalling message from the Carracho session. */
int call_session_on_message(call_session_t *s, const uint8_t *msg, uint32_t len, uint32_t now);
/* Media or any traffic from the peer was seen (resets the idle timer). */
void call_session_on_media(call_session_t *s, uint32_t now);
void call_session_tick(call_session_t *s, uint32_t now);

const char *call_state_name(call_state_t st);
const char *call_end_reason_name(call_end_reason_t r);

#endif /* CALL_SESSION_H */
