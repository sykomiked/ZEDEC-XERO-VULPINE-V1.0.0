/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_fec.h — XOR parity forward error correction (ULPFEC-like).
 *
 * The encoder protects consecutive media packets of one stream in groups of
 * k (2..48, configurable). After k packets (or on a sequence gap, or on
 * flush) it emits one parity packet: an ordinary call_rtp packet with payload
 * type CALL_PT_FEC on its own stream id and sequence space, whose payload is
 *
 *   off len field
 *    0   4  protected stream id
 *    4   2  base sequence number of the group
 *    6   1  group size n (1..48)
 *    7   1  version (1)
 *    8   2  XOR of the n packet lengths
 *   10   P  XOR of the n whole packets (header + payload), each zero-padded
 *           to P = the longest packet in the group
 *
 * Protected "packets" are complete call_rtp packets, so a recovered packet
 * carries its own header and CRC and is re-parsed before it is released.
 * The decoder keeps the last nmedia packets and nfec parity packets in
 * caller memory; whenever exactly one packet of a group is missing it XORs
 * the parity with the n-1 present packets and hands the rebuilt packet to a
 * callback.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. All memory is the caller's.
 *
 * HONEST LIMITS. Single parity repairs ONE loss per group; two losses in a
 * group repair nothing, so it is weak against burst loss (interleaving or
 * Reed-Solomon, as in kernel/src/freight, would do better). The overhead is
 * 1/k extra packets, each 30 octets larger than the largest protected one:
 * keep the media MTU 30 octets under the path MTU. Recovery only helps if
 * the parity arrives before the jitter buffer's playout deadline. There are
 * no codecs here; FEC is codec-agnostic bytes, and the media itself comes
 * from the host OS (AVFoundation on macOS) or bundled libopus/libvpx later.
 */
#ifndef CALL_FEC_H
#define CALL_FEC_H

#include <stdbool.h>
#include <stdint.h>
#include "call_rtp.h"

#define CALL_FEC_HDR_LEN 10
#define CALL_FEC_MAX_K   48
#define CALL_FEC_VERSION 1

typedef struct call_fec_enc {
    uint32_t protected_ssrc;
    uint8_t k;
    uint8_t n; /* packets in the open group */
    uint16_t base_seq;
    uint16_t len_xor;
    uint16_t maxlen;
    uint8_t *parity; /* >= CALL_FEC_HDR_LEN + longest protected packet */
    uint32_t parity_cap;
    call_packetizer_t pk; /* parity stream */
    uint32_t emitted;
} call_fec_enc_t;

bool call_fec_enc_init(call_fec_enc_t *e, uint32_t protected_ssrc, uint32_t fec_ssrc, uint8_t k,
                       uint8_t *parity, uint32_t parity_cap, uint16_t first_seq);

/* Add one protected packet (a whole call_rtp packet). May emit a parity
 * packet through emit, built in scratch (>= 20 + parity_cap bytes).
 * Returns the number of parity packets emitted (0..2) or a CALL_ERR_*. */
int call_fec_enc_add(call_fec_enc_t *e, const uint8_t *pkt, uint32_t len, uint8_t *scratch,
                     uint32_t scratch_cap, call_emit_fn emit, void *ctx);
int call_fec_enc_flush(call_fec_enc_t *e, uint8_t *scratch, uint32_t scratch_cap, call_emit_fn emit,
                       void *ctx);

typedef struct call_fec_mslot {
    uint16_t seq;
    uint16_t len;
    uint8_t used;
} call_fec_mslot_t;

typedef struct call_fec_fslot {
    uint16_t base_seq;
    uint16_t len_xor;
    uint16_t plen;
    uint8_t n;
    uint8_t used;
    uint32_t age;
} call_fec_fslot_t;

typedef struct call_fec_dec {
    uint32_t protected_ssrc;
    call_fec_mslot_t *m;
    uint8_t *mmem;
    uint32_t nmedia; /* power of two */
    uint32_t mcap;   /* bytes per media slot */
    call_fec_fslot_t *f;
    uint8_t *fmem;
    uint32_t nfec;
    uint32_t fcap; /* bytes per parity slot */
    uint32_t tick;
    uint8_t *work; /* mcap bytes */
    uint32_t recovered;
    uint32_t bad_recovery;
    uint32_t fec_in;
    uint32_t fec_useless; /* group complete or too old */
    uint32_t fec_overrun; /* evicted before it could help */
} call_fec_dec_t;

bool call_fec_dec_init(call_fec_dec_t *d, uint32_t protected_ssrc, call_fec_mslot_t *m,
                       uint8_t *mmem, uint32_t nmedia, uint32_t mcap, call_fec_fslot_t *f,
                       uint8_t *fmem, uint32_t nfec, uint32_t fcap, uint8_t *work);

/* Feed a received media packet (whole call_rtp packet of the protected
 * stream). Returns the number of packets recovered as a result (each passed
 * to emit), or a CALL_ERR_*. */
int call_fec_dec_media(call_fec_dec_t *d, const uint8_t *pkt, uint32_t len, call_emit_fn emit,
                       void *ctx);
/* Feed a received parity packet (whole call_rtp packet with pt CALL_PT_FEC). */
int call_fec_dec_parity(call_fec_dec_t *d, const uint8_t *pkt, uint32_t len, call_emit_fn emit,
                        void *ctx);

/* Parse the FEC payload header (after the RTP header). */
typedef struct call_fec_hdr {
    uint32_t protected_ssrc;
    uint16_t base_seq;
    uint16_t len_xor;
    uint8_t n;
    uint32_t plen; /* parity bytes after the header */
} call_fec_hdr_t;
int call_fec_hdr_parse(const uint8_t *payload, uint32_t len, call_fec_hdr_t *h);

#endif /* CALL_FEC_H */
