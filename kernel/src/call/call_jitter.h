/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* call_jitter.h — adaptive jitter buffer for real-time media.
 *
 * Packets are pushed as they arrive (any order) and popped in sequence
 * order at their playout time. The buffer:
 *   - reorders by 16-bit sequence number, wrap-safe (distance as int16);
 *   - estimates interarrival jitter as in RFC 3550 section 6.4.1,
 *       D = (Rj - Ri) - (Sj - Si),  J += (|D| - J) / 16,
 *     kept as J*16 in media clock units, integer only;
 *   - sets the playout delay to min_delay + 3*J (milliseconds), raising it
 *     at once when jitter grows and lowering it by 1/16 of the excess per
 *     packet when jitter falls, clamped to [min_delay, max_delay];
 *   - maps media time to local time from the fastest transit seen, so
 *     playout(p) = base + (ts(p) - ts0) / clock_khz + delay;
 *   - reports CALL_JB_CONCEAL for a missing sequence number once a later
 *     packet is already due (the decoder runs packet loss concealment);
 *   - drops packets that arrive after their sequence number was played or
 *     concealed (late), duplicates, and anything that would not fit.
 * Memory is nslots slots of slot_cap bytes, both supplied by the caller;
 * nslots must be a power of two.
 *
 * RULES. Freestanding C11: no libc, no malloc, no floating point, no 64-bit
 * division, no __int128. Times are 32-bit wrapping milliseconds.
 *
 * HONEST LIMITS. There are no codecs and no camera or microphone capture
 * here: CONCEAL is only a signal; the actual concealment (Opus PLC, VP8
 * frame repeat) happens in codec code from the host OS or bundled
 * libopus/libvpx. The buffer does not time-stretch audio, so delay changes
 * show up as a gap or a skip at playout; a real audio path would stretch
 * speech over the change. Clock drift between sender and receiver is not
 * estimated: the transit base only moves down (to faster transits) unless
 * call_jb_reset is called, so a sender clock that runs slow slowly adds
 * delay.
 */
#ifndef CALL_JITTER_H
#define CALL_JITTER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct call_jb_slot {
    uint32_t ts;
    uint32_t arrival;
    uint16_t seq;
    uint16_t len;
    uint8_t used;
    uint8_t marker;
} call_jb_slot_t;

typedef struct call_jb_stats {
    uint32_t pushed;
    uint32_t played;
    uint32_t concealed;
    uint32_t late;
    uint32_t duplicate;
    uint32_t overflow; /* too far ahead or too big */
    uint32_t max_depth;
} call_jb_stats_t;

typedef struct call_jb {
    call_jb_slot_t *slots;
    uint8_t *mem;
    uint32_t nslots; /* power of two */
    uint32_t slot_cap;
    uint32_t clock_khz; /* media clock ticks per millisecond: 48, 90, ... */
    uint32_t min_delay_ms;
    uint32_t max_delay_ms;
    uint32_t delay_ms;  /* current playout delay */
    uint32_t jitter_q4; /* J * 16, media clock units */
    uint32_t ts0;       /* media timestamp of the reference packet */
    uint32_t base_ms;   /* local time of ts0 at the fastest transit seen */
    int32_t last_transit;
    uint16_t next_seq; /* next sequence number to play */
    uint8_t started;
    uint8_t have_transit;
    uint32_t depth;
    call_jb_stats_t st;
} call_jb_t;

typedef enum {
    CALL_JB_EMPTY = 0,   /* nothing due yet */
    CALL_JB_PACKET = 1,  /* *out holds the next packet */
    CALL_JB_CONCEAL = 2, /* sequence out->seq is lost: conceal it */
} call_jb_result_t;

typedef struct call_jb_out {
    const uint8_t *data; /* valid until the next push */
    uint32_t len;
    uint32_t ts;
    uint16_t seq;
    uint8_t marker;
} call_jb_out_t;

/* Returns false if nslots is not a power of two or a pointer is NULL. */
bool call_jb_init(call_jb_t *jb, call_jb_slot_t *slots, uint32_t nslots, uint8_t *mem,
                  uint32_t slot_cap, uint32_t clock_khz, uint32_t min_delay_ms,
                  uint32_t max_delay_ms);
void call_jb_reset(call_jb_t *jb);

/* 1 stored, 0 dropped (late, duplicate or overflow; counted in stats). */
int call_jb_push(call_jb_t *jb, uint16_t seq, uint32_t ts, bool marker, const uint8_t *data,
                 uint32_t len, uint32_t now_ms);

call_jb_result_t call_jb_pop(call_jb_t *jb, uint32_t now_ms, call_jb_out_t *out);

/* Current interarrival jitter estimate in microseconds. */
uint32_t call_jb_jitter_us(const call_jb_t *jb);

#endif /* CALL_JITTER_H */
