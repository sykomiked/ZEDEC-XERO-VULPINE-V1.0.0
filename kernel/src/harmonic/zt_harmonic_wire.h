/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_harmonic_wire.h — the canonical UBH-168 harmonic wire frame.
 *
 * This header is the ONE contract of kernel/src/harmonic. Every other file in
 * the module (endian mux, adapter house, trunk bank, residual carrier, router,
 * signal-data facade) uses this frame type, this truth enum and this line
 * status enum. The other specs' names are aliases declared here or in their
 * thin facades, never second definitions.
 *
 *   W1  FRAME.  21 octets, no padding:
 *         octet 0       tag (trunk, shell depth, sync; see W3)
 *         octets 1-4    W0  little-endian   I rail, S+ (forward evidence)
 *         octets 5-8    W1  big-endian      Q rail, S- (witness / residual)
 *         octets 9-12   W2  little-endian   I rail, S+
 *         octets 13-16  W3  big-endian      Q rail, S-
 *         octets 17-20  W4  little-endian   I rail, S+
 *       The byte order of each word is fixed by its position (L-B-L-B-L), so
 *       a frame's octets are the same on every host. This is line coding: the
 *       pattern is public and carries no secret (contrast kernel/src/ehop,
 *       whose keyed schedule changes per frame inside an AEAD).
 *   W2  PACK / UNPACK.  Branchless: the host byte order is resolved by the
 *       preprocessor (__BYTE_ORDER__), and the swaps are __builtin_bswap32.
 *   W3  TAG.  tag = 100 * sync + 10 * shell + trunk, with trunk 0..9,
 *       shell 0..9, sync 0..1, so trunk = tag mod 10 for every tag value.
 *       Tags 200..255 are reserved; they still decode a trunk (tag mod 10).
 *       Trunks 0..8 (111 .. 999 Hz) are axis trunks, trunk 9 (1111 Hz) is
 *       the expansion trunk (routed to the Fleet queue, not kernel memory).
 *   W4  IDLE PILOT.  An all-zero frame is byte-identical in either byte
 *       order, so it shows no alternation. An idle frame therefore carries the
 *       pilot word ZT_WIRE_IDLE_PILOT in all five words; its bytes differ
 *       between the LE and BE positions, so the L-B-L-B-L pattern is present
 *       on an idle line. The pilot resolves to NEUTRAL (line open, nothing
 *       asserted). The pilot pattern is a reserved code point: a data frame
 *       whose five words all equal the pilot reads as idle.
 *   W5  CORRELATION RESOLVER (one frame, I rail against Q rail).  The S- rail
 *       is read as the witness of the S+ rail. Words are signed Q16.16.
 *         dot = sum_{k=0,1} (S+_k * S-_k) >> 16    (int64, Q16.16)
 *         E+  = sum_{k=0,1} (S+_k)^2 >> 16,  E- likewise
 *       in this order:
 *         all five words zero                         -> UNKNOWN
 *         idle pilot (W4)                             -> NEUTRAL
 *         S+ all zero or S- all zero (one rail silent) -> NEUTRAL
 *         dot >  T                                    -> TRUE  (constructive)
 *         dot < -T and 4*min(E+,E-) >= max(E+,E-)     -> GLUT  (both strong,
 *                                                        directly opposed)
 *         dot < -T                                    -> FALSE (destructive,
 *                                                        one rail dominant)
 *         |dot| <= T, both rails active               -> GLUT  (orthogonal,
 *                                                        held in S0)
 *       It never faults, divides, allocates or uses floating point.
 *   W6  EVIDENCE RESOLVER (two claims, A against not-A).  The rule of
 *       kernel/src/lpres (positive and negative evidence counts) on Q16.16
 *       magnitudes: for > T and against > T -> GLUT, only for -> TRUE, only
 *       against -> FALSE, neither -> UNKNOWN.
 *   W7  PARADOX = PERSISTENT GLUT.  A stateless resolver never returns
 *       PARADOX. zt_wire_persist() turns a GLUT into PARADOX when it is the
 *       ZT_WIRE_PARADOX_RUN-th consecutive GLUT on the same counter (one
 *       counter per rail or per trunk; one evaluation = one shell traversal);
 *       any other state resets the counter.
 *
 * Freestanding integer C11: no libc, no float, no allocation, no 64-bit
 * division.
 */
#ifndef ZT_HARMONIC_WIRE_H
#define ZT_HARMONIC_WIRE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ZT_UBH168_FRAME_BYTES 21u
#define ZT_NUM_TRUNK_LINES    10u
#define ZT_WIRE_SHELLS        10u
#define ZT_WIRE_EXPANSION     9u          /* trunk 9: 1111 Hz, Fleet */
#define ZT_WIRE_IDLE_PILOT    0x5A585631u /* "ZXV1" big-endian, "1VXZ" little */
#define ZT_WIRE_PARADOX_RUN   3u          /* consecutive GLUTs that make PARADOX */
#define ZT_Q16_ONE            0x00010000

#if !defined(__BYTE_ORDER__) || !defined(__ORDER_LITTLE_ENDIAN__) || !defined(__ORDER_BIG_ENDIAN__)
#    error "zt_harmonic_wire needs __BYTE_ORDER__ (GCC or Clang)"
#endif
#if __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__ && __BYTE_ORDER__ != __ORDER_BIG_ENDIAN__
#    error "zt_harmonic_wire supports little- and big-endian hosts only"
#endif

/* The truth enum (values fixed by the user's specs). */
typedef enum {
    ZT_TRUTH_UNKNOWN = 0,
    ZT_TRUTH_TRUE = 1,
    ZT_TRUTH_FALSE = 2,
    ZT_TRUTH_NEUTRAL = 3,
    ZT_TRUTH_GLUT = 4,   /* contradiction held, no explosion */
    ZT_TRUTH_PARADOX = 5 /* GLUT that persisted (W7) */
} zt_truth_state_t;

/* Line status of a trunk (one enum; the specs' three spellings alias it). */
typedef enum {
    ZT_LINE_DEAD = 0,    /* no carrier above the noise floor */
    ZT_LINE_ON_HOOK = 1, /* steady carrier: idle, open line */
    ZT_LINE_OFF_HOOK = 2 /* carrier with phase transitions: data */
} zt_line_status_t;
typedef zt_line_status_t zt_trunk_line_state_t;
#define ZT_TRUNK_LINE_DEAD     ZT_LINE_DEAD
#define ZT_TRUNK_LINE_ON_HOOK  ZT_LINE_ON_HOOK
#define ZT_TRUNK_LINE_OFF_HOOK ZT_LINE_OFF_HOOK

/* W1: the canonical frame. */
#pragma pack(push, 1)
typedef struct {
    uint8_t tag;    /* octet 0 */
    uint32_t w0_le; /* octets 1-4   I rail, S+ */
    uint32_t w1_be; /* octets 5-8   Q rail, S- */
    uint32_t w2_le; /* octets 9-12  I rail, S+ */
    uint32_t w3_be; /* octets 13-16 Q rail, S- */
    uint32_t w4_le; /* octets 17-20 I rail, S+ */
} zt_ubh168_wire_frame_t;
#pragma pack(pop)

_Static_assert(sizeof(zt_ubh168_wire_frame_t) == ZT_UBH168_FRAME_BYTES,
               "UBH-168 frame must be exactly 21 octets");
_Static_assert(sizeof(zt_ubh168_wire_frame_t[2]) == 2u * ZT_UBH168_FRAME_BYTES,
               "UBH-168 frame array stride must be 21 octets");
_Static_assert(_Alignof(zt_ubh168_wire_frame_t) == 1, "UBH-168 frame must be byte aligned");

/* The adapter-house and trunk-bank specs call it zt_ubh168_frame_t, the
 * signal-data spec zt_frame_t: the same type. */
typedef zt_ubh168_wire_frame_t zt_ubh168_frame_t;

/* Unpacked dual rails. The anonymous unions give the endian-mux names
 * (i_channel, q_channel) and the signal-data names (s_plus, s_minus) to the
 * same storage, so there is one rail type. */
typedef struct {
    union {
        uint32_t s_plus_lane[3]; /* W0, W2, W4 in host order */
        uint32_t s_plus[3];
        uint32_t i_channel[3];
    };
    union {
        uint32_t s_minus_lane[2]; /* W1, W3 in host order */
        uint32_t s_minus[2];
        uint32_t q_channel[2];
    };
    union {
        uint8_t tag; /* the raw tag octet */
        uint8_t carrier_tag;
    };
    uint8_t trunk_id;       /* tag mod 10 */
    uint8_t shell;          /* (tag / 10) mod 10 */
    bool sync;              /* tag >= 100 and < 200 */
    bool is_axis_trunk;     /* trunk_id < 9 */
    zt_truth_state_t state; /* last zt_wire_resolve_step() result */
    uint8_t conflict_run;   /* W7 counter for zt_wire_resolve_step() */
} zt_unpacked_rails_t;

/* W3: tags. */
uint8_t zt_wire_make_tag(uint8_t trunk_id, uint8_t shell, bool sync);
uint8_t zt_wire_tag_trunk(uint8_t tag);
uint8_t zt_wire_tag_shell(uint8_t tag);
bool zt_wire_tag_sync(uint8_t tag);
bool zt_wire_is_axis_trunk(uint8_t trunk_id);
uint32_t zt_wire_trunk_hz(uint8_t trunk_id); /* 111 .. 999, 1111; 0 if > 9 */
uint32_t zt_wire_digital_root(uint32_t n);   /* 1..9, 0 for n = 0 */

/* W2: pack with tag = trunk_id mod 10 (the spec's contract). */
void zt_wire_pack_ubh168(uint8_t trunk_id, const uint32_t s_plus[3], const uint32_t s_minus[2],
                         zt_ubh168_wire_frame_t *dst_frame);
/* W2: pack with a full tag octet (zt_wire_make_tag). */
void zt_wire_pack_tagged(uint8_t tag, const uint32_t s_plus[3], const uint32_t s_minus[2],
                         zt_ubh168_wire_frame_t *dst_frame);
/* W2: unpack. Fills lanes and tag fields; leaves state and conflict_run as
 * they were, so a rail reused across traversals keeps its W7 history. */
void zt_wire_unpack_ubh168(const zt_ubh168_wire_frame_t *src_frame, zt_unpacked_rails_t *dst_rails);
/* A zeroed rail (state UNKNOWN, no history). */
void zt_wire_rails_init(zt_unpacked_rails_t *rails);

/* Raw octet access (the frame is byte data; these never alias-cast). */
void zt_wire_to_octets(const zt_ubh168_wire_frame_t *frame, uint8_t out[21]);
void zt_wire_from_octets(const uint8_t in[21], zt_ubh168_wire_frame_t *frame);

/* W4: idle pilot frame for a trunk, and its test. */
void zt_wire_idle_frame(uint8_t trunk_id, zt_ubh168_wire_frame_t *dst_frame);
bool zt_wire_is_idle(const zt_unpacked_rails_t *rails);

/* W5: stateless correlation resolver (never PARADOX). */
zt_truth_state_t zt_wire_resolve_rails(const zt_unpacked_rails_t *rails,
                                       int32_t correlation_threshold_q16);
/* W5 + W7: resolves, applies the rail's persistence counter, stores state. */
zt_truth_state_t zt_wire_resolve_step(zt_unpacked_rails_t *rails,
                                      int32_t correlation_threshold_q16);
/* The Q16.16 dot product of W5, saturated to int32, for diagnostics. */
int32_t zt_wire_correlation_q16(const zt_unpacked_rails_t *rails);

/* W6: stateless evidence resolver; magnitudes are |Q16.16|. */
zt_truth_state_t zt_wire_resolve_evidence(int32_t for_q16, int32_t against_q16,
                                          int32_t threshold_q16);

/* W7: persistence. *run counts consecutive GLUTs. */
zt_truth_state_t zt_wire_persist(uint8_t *run, zt_truth_state_t instant);

/* True for GLUT and PARADOX: the result is held in the S0 sump. */
bool zt_truth_is_held(zt_truth_state_t s);
const char *zt_truth_name(zt_truth_state_t s);

#endif /* ZT_HARMONIC_WIRE_H */
