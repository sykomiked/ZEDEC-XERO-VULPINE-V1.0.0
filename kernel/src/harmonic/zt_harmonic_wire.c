/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_harmonic_wire.c — the canonical UBH-168 frame. See zt_harmonic_wire.h. */
#include "zt_harmonic_wire.h"

static const uint16_t TRUNK_HZ[ZT_NUM_TRUNK_LINES] = {111, 222, 333, 444, 555,
                                                      666, 777, 888, 999, 1111};

/* ===== W3: tags ===== */

uint8_t zt_wire_make_tag(uint8_t trunk_id, uint8_t shell, bool sync)
{
    return (uint8_t) ((sync ? 100u : 0u) + 10u * (shell % 10u) + trunk_id % 10u);
}

uint8_t zt_wire_tag_trunk(uint8_t tag)
{
    return (uint8_t) (tag % 10u);
}

uint8_t zt_wire_tag_shell(uint8_t tag)
{
    return (uint8_t) ((tag / 10u) % 10u);
}

bool zt_wire_tag_sync(uint8_t tag)
{
    return tag >= 100u && tag < 200u;
}

bool zt_wire_is_axis_trunk(uint8_t trunk_id)
{
    return trunk_id < ZT_WIRE_EXPANSION;
}

uint32_t zt_wire_trunk_hz(uint8_t trunk_id)
{
    return trunk_id < ZT_NUM_TRUNK_LINES ? TRUNK_HZ[trunk_id] : 0u;
}

uint32_t zt_wire_digital_root(uint32_t n)
{
    return n == 0u ? 0u : 1u + (n - 1u) % 9u;
}

/* ===== W2: pack / unpack ===== */

/* One preprocessor decision, no runtime branch: on a little-endian host the
 * LE words are stored as they are and the BE words are swapped; on a
 * big-endian host the reverse. */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#    define ZT_TO_LE(x) (x)
#    define ZT_TO_BE(x) __builtin_bswap32(x)
#else
#    define ZT_TO_LE(x) __builtin_bswap32(x)
#    define ZT_TO_BE(x) (x)
#endif

void zt_wire_pack_tagged(uint8_t tag, const uint32_t s_plus[3], const uint32_t s_minus[2],
                         zt_ubh168_wire_frame_t *dst_frame)
{
    dst_frame->tag = tag;
    dst_frame->w0_le = ZT_TO_LE(s_plus[0]);
    dst_frame->w1_be = ZT_TO_BE(s_minus[0]);
    dst_frame->w2_le = ZT_TO_LE(s_plus[1]);
    dst_frame->w3_be = ZT_TO_BE(s_minus[1]);
    dst_frame->w4_le = ZT_TO_LE(s_plus[2]);
}

void zt_wire_pack_ubh168(uint8_t trunk_id, const uint32_t s_plus[3], const uint32_t s_minus[2],
                         zt_ubh168_wire_frame_t *dst_frame)
{
    zt_wire_pack_tagged((uint8_t) (trunk_id % 10u), s_plus, s_minus, dst_frame);
}

void zt_wire_unpack_ubh168(const zt_ubh168_wire_frame_t *src_frame, zt_unpacked_rails_t *dst_rails)
{
    uint8_t tag = src_frame->tag;
    dst_rails->s_plus_lane[0] = ZT_TO_LE(src_frame->w0_le); /* the swap is its own inverse */
    dst_rails->s_minus_lane[0] = ZT_TO_BE(src_frame->w1_be);
    dst_rails->s_plus_lane[1] = ZT_TO_LE(src_frame->w2_le);
    dst_rails->s_minus_lane[1] = ZT_TO_BE(src_frame->w3_be);
    dst_rails->s_plus_lane[2] = ZT_TO_LE(src_frame->w4_le);
    dst_rails->tag = tag;
    dst_rails->trunk_id = (uint8_t) (tag % 10u);
    dst_rails->shell = zt_wire_tag_shell(tag);
    dst_rails->sync = zt_wire_tag_sync(tag);
    dst_rails->is_axis_trunk = dst_rails->trunk_id < ZT_WIRE_EXPANSION;
}

void zt_wire_rails_init(zt_unpacked_rails_t *rails)
{
    for (uint32_t k = 0; k < 3u; k++) rails->s_plus_lane[k] = 0;
    for (uint32_t k = 0; k < 2u; k++) rails->s_minus_lane[k] = 0;
    rails->tag = 0;
    rails->trunk_id = 0;
    rails->shell = 0;
    rails->sync = false;
    rails->is_axis_trunk = true;
    rails->state = ZT_TRUTH_UNKNOWN;
    rails->conflict_run = 0;
}

void zt_wire_to_octets(const zt_ubh168_wire_frame_t *frame, uint8_t out[21])
{
    const uint8_t *p = (const uint8_t *) frame; /* char access: always allowed */
    for (uint32_t i = 0; i < ZT_UBH168_FRAME_BYTES; i++) out[i] = p[i];
}

void zt_wire_from_octets(const uint8_t in[21], zt_ubh168_wire_frame_t *frame)
{
    uint8_t *p = (uint8_t *) frame;
    for (uint32_t i = 0; i < ZT_UBH168_FRAME_BYTES; i++) p[i] = in[i];
}

/* ===== W4: idle pilot ===== */

void zt_wire_idle_frame(uint8_t trunk_id, zt_ubh168_wire_frame_t *dst_frame)
{
    static const uint32_t P3[3] = {ZT_WIRE_IDLE_PILOT, ZT_WIRE_IDLE_PILOT, ZT_WIRE_IDLE_PILOT};
    static const uint32_t P2[2] = {ZT_WIRE_IDLE_PILOT, ZT_WIRE_IDLE_PILOT};
    zt_wire_pack_tagged(zt_wire_make_tag(trunk_id, 0, true), P3, P2, dst_frame);
}

bool zt_wire_is_idle(const zt_unpacked_rails_t *r)
{
    return r->s_plus_lane[0] == ZT_WIRE_IDLE_PILOT && r->s_plus_lane[1] == ZT_WIRE_IDLE_PILOT &&
           r->s_plus_lane[2] == ZT_WIRE_IDLE_PILOT && r->s_minus_lane[0] == ZT_WIRE_IDLE_PILOT &&
           r->s_minus_lane[1] == ZT_WIRE_IDLE_PILOT;
}

/* ===== W5: correlation resolver ===== */

static int64_t q16_mul(uint32_t a, uint32_t b)
{
    /* signed Q16.16 product, |result| <= 2^46: no overflow anywhere. The
     * shift is arithmetic on GCC and Clang (floor rounding). */
    return ((int64_t) (int32_t) a * (int64_t) (int32_t) b) >> 16;
}

static int64_t rail_dot(const zt_unpacked_rails_t *r)
{
    return q16_mul(r->s_plus_lane[0], r->s_minus_lane[0]) +
           q16_mul(r->s_plus_lane[1], r->s_minus_lane[1]);
}

int32_t zt_wire_correlation_q16(const zt_unpacked_rails_t *rails)
{
    int64_t d = rail_dot(rails);
    if (d > INT32_MAX) return INT32_MAX;
    if (d < INT32_MIN) return INT32_MIN;
    return (int32_t) d;
}

zt_truth_state_t zt_wire_resolve_rails(const zt_unpacked_rails_t *r, int32_t threshold_q16)
{
    bool plus = (r->s_plus_lane[0] | r->s_plus_lane[1] | r->s_plus_lane[2]) != 0u;
    bool minus = (r->s_minus_lane[0] | r->s_minus_lane[1]) != 0u;
    int64_t t = threshold_q16 < 0 ? -(int64_t) threshold_q16 : (int64_t) threshold_q16;
    if (!plus && !minus) return ZT_TRUTH_UNKNOWN;
    if (zt_wire_is_idle(r)) return ZT_TRUTH_NEUTRAL;
    if (!plus || !minus) return ZT_TRUTH_NEUTRAL;
    int64_t dot = rail_dot(r);
    if (dot > t) return ZT_TRUTH_TRUE;
    if (dot < -t) {
        int64_t ep = q16_mul(r->s_plus_lane[0], r->s_plus_lane[0]) +
                     q16_mul(r->s_plus_lane[1], r->s_plus_lane[1]);
        int64_t em = q16_mul(r->s_minus_lane[0], r->s_minus_lane[0]) +
                     q16_mul(r->s_minus_lane[1], r->s_minus_lane[1]);
        int64_t lo = ep < em ? ep : em, hi = ep < em ? em : ep;
        return 4 * lo >= hi ? ZT_TRUTH_GLUT : ZT_TRUTH_FALSE;
    }
    return ZT_TRUTH_GLUT;
}

zt_truth_state_t zt_wire_persist(uint8_t *run, zt_truth_state_t instant)
{
    if (instant != ZT_TRUTH_GLUT && instant != ZT_TRUTH_PARADOX) {
        *run = 0;
        return instant;
    }
    if (*run < 255u) (*run)++;
    return *run >= ZT_WIRE_PARADOX_RUN ? ZT_TRUTH_PARADOX : ZT_TRUTH_GLUT;
}

zt_truth_state_t zt_wire_resolve_step(zt_unpacked_rails_t *rails, int32_t threshold_q16)
{
    zt_truth_state_t s =
        zt_wire_persist(&rails->conflict_run, zt_wire_resolve_rails(rails, threshold_q16));
    rails->state = s;
    return s;
}

/* ===== W6: evidence resolver ===== */

zt_truth_state_t zt_wire_resolve_evidence(int32_t for_q16, int32_t against_q16,
                                          int32_t threshold_q16)
{
    int64_t f = for_q16 < 0 ? -(int64_t) for_q16 : (int64_t) for_q16;
    int64_t a = against_q16 < 0 ? -(int64_t) against_q16 : (int64_t) against_q16;
    int64_t t = threshold_q16 < 0 ? -(int64_t) threshold_q16 : (int64_t) threshold_q16;
    bool pf = f > t, pa = a > t;
    if (pf && pa) return ZT_TRUTH_GLUT;
    if (pf) return ZT_TRUTH_TRUE;
    if (pa) return ZT_TRUTH_FALSE;
    return ZT_TRUTH_UNKNOWN;
}

bool zt_truth_is_held(zt_truth_state_t s)
{
    return s == ZT_TRUTH_GLUT || s == ZT_TRUTH_PARADOX;
}

const char *zt_truth_name(zt_truth_state_t s)
{
    switch (s) {
    case ZT_TRUTH_UNKNOWN:
        return "UNKNOWN";
    case ZT_TRUTH_TRUE:
        return "TRUE";
    case ZT_TRUTH_FALSE:
        return "FALSE";
    case ZT_TRUTH_NEUTRAL:
        return "NEUTRAL";
    case ZT_TRUTH_GLUT:
        return "GLUT";
    case ZT_TRUTH_PARADOX:
        return "PARADOX";
    }
    return "INVALID";
}
