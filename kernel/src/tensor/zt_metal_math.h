/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* zt_metal_math.h — the integer arithmetic of the Metal kernels (zt_metal.m),
 * written once in the subset that is both C11 and Metal Shading Language.
 *
 * Every function here is a port of zt_kern.h's C reference, step for step:
 * the same exact 32-bit block sums, the same term52 of the same 64-bit
 * values, so a sum of them in any order is what zt_dot_*_c sums. One source
 * serves two builds:
 *   - zt_metal.m stringifies it (ZT_METAL_SRC(...) -> "...") into the shader
 *     it compiles at run time, after a prelude that defines the types below
 *     and ZM_DEV as Metal's `device` address space;
 *   - test_zt_metal_math.c compiles it as C (ZT_METAL_SRC(...) -> ...), and
 *     checks it against zt_kern.h on any host, with or without a GPU.
 * Rules for the body: no preprocessor lines and no `#` (it is stringified),
 * pointers only as ZM_DEV, types zl (int64) zw (uint64) zi (int32) zu
 * (uint32) zs (int16) zb (uint8) zc (int8). Not a header to include anywhere
 * else.
 *
 * The second half holds the whole-layer kernels (zt_gpu.h): RMS norm, the
 * activation quantiser, bias, RoPE, the KV store, attention, the residual
 * and SiLU, each zt_model.c's own integer steps, one output (or one row) per
 * thread. They share one signature, ZK_A, which each prelude defines:
 *     zu i, zg_p p, ZM_DEV zb *b0 ... ZM_DEV zb *b5, ZM_DEV zm_ctr *ctr
 * (thread i of p.u[0], the op's parameters, six buffers, and the saturation
 * counter that zm_count adds to: an atomic on the GPU, a plain add in C). */

ZT_METAL_SRC(

static inline zl zm_term52(zl v, zi e)
{
    const zl lim = ((zl) 1) << 52;
    if (v == 0) return 0;
    if (e <= 0) {
        if (e < -62) return v < 0 ? -1 : 0;
        v >>= -e;
    } else {
        if (e > 52 || v > (lim >> e) || v < -(lim >> e)) return v < 0 ? -lim : lim;
        v *= ((zl) 1) << e;
    }
    return v > lim ? lim : v < -lim ? -lim : v;
}

static inline zi zm_round(zl acc)
{
    zl v = (acc + 32768) >> 16;
    return (zi) (v > 2147483647 ? 2147483647 : v < -2147483647 - 1 ? -2147483647 - 1 : v);
}

static inline zi zm_f16_m(zu d)
{
    zu ex = (d >> 10) & 31u;
    zi m = (zi) (d & 0x3FFu);
    if (ex == 31u) return 0;
    if (ex) m |= 0x400;
    return (d & 0x8000u) ? -m : m;
}

static inline zi zm_f16_e(zu d)
{
    zu ex = (d >> 10) & 31u;
    if (ex == 31u) return 0;
    return ex ? (zi) ex - 25 : -24;
}

static inline zu zm_u16(ZM_DEV const zb *p)
{
    return (zu) p[0] | ((zu) p[1] << 8);
}

static inline zi zm_dot32_q4(ZM_DEV const zb *q, zu hi, ZM_DEV const zs *x)
{
    zi s = 0;
    for (zu i = 0; i < 32u; i++) s += (zi) ((q[i] >> hi) & 15u) * (zi) x[i];
    return s;
}

static inline zl zm_q4k_sub(ZM_DEV const zb *blk, zu j, ZM_DEV const zs *x, zi sh, zi asum)
{
    zu d = zm_u16(blk), dmin = zm_u16(blk + 2);
    ZM_DEV const zb *p = blk + 4;
    zu sc, mn;
    if (j < 4u) {
        sc = p[j] & 63u;
        mn = p[j + 4u] & 63u;
    } else {
        sc = (p[j + 4u] & 15u) | ((p[j - 4u] >> 6) << 4);
        mn = (p[j + 4u] >> 4) | ((p[j] >> 6) << 4);
    }
    zi s = zm_dot32_q4(blk + 16u + (j / 2u) * 32u, (j & 1u) * 4u, x);
    return zm_term52((zl) s * (zl) sc * (zl) zm_f16_m(d), zm_f16_e(d) + 16 + sh) -
           zm_term52((zl) asum * (zl) mn * (zl) zm_f16_m(dmin), zm_f16_e(dmin) + 16 + sh);
}

static inline zl zm_q6k_sub(ZM_DEV const zb *blk, zu j, ZM_DEV const zs *x, zi sh)
{
    zu h = j / 4u, quarter = j % 4u;
    ZM_DEV const zb *ql = blk + h * 64u + (quarter & 1u) * 32u;
    ZM_DEV const zb *qh = blk + 128u + h * 32u;
    zu s4 = (quarter & 2u) ? 4u : 0u, s2 = 2u * quarter;
    zi s0 = 0, s1 = 0;
    for (zu l = 0; l < 16u; l++) {
        s0 += ((zi) (((ql[l] >> s4) & 15u) | (((qh[l] >> s2) & 3u) << 4)) - 32) * (zi) x[l];
        s1 += ((zi) (((ql[l + 16u] >> s4) & 15u) | (((qh[l + 16u] >> s2) & 3u) << 4)) - 32) *
              (zi) x[l + 16u];
    }
    zu d = zm_u16(blk + 208);
    zl v = (zl) (zc) blk[192u + 2u * j] * (zl) s0 + (zl) (zc) blk[193u + 2u * j] * (zl) s1;
    return zm_term52(v * (zl) zm_f16_m(d), zm_f16_e(d) + 16 + sh);
}

static inline zl zm_q80_sub(ZM_DEV const zb *blk, ZM_DEV const zs *x, zi sh)
{
    zi s = 0;
    for (zu i = 0; i < 32u; i++) s += (zi) (zc) blk[2u + i] * (zi) x[i];
    zu d = zm_u16(blk);
    return zm_term52((zl) s * (zl) zm_f16_m(d), zm_f16_e(d) + 16 + sh);
}

/* Activation sub-block b (32 values) of row r's weights, for the weight type
 * qt (12 Q4_K, 14 Q6_K, 8 Q8_0): the term it adds to the row's Q32 sum. */
static inline zl zm_sub(zu qt, ZM_DEV const zb *row, zu b, ZM_DEV const zs *x, zi sh, zi asum)
{
    if (qt == 12u) return zm_q4k_sub(row + (b / 8u) * 144u, b % 8u, x, sh, asum);
    if (qt == 14u) return zm_q6k_sub(row + (b / 8u) * 210u, b % 8u, x, sh);
    return zm_q80_sub(row + b * 34u, x, sh);
}

static inline zu zm_row_bytes(zu qt, zu nb)
{
    return qt == 12u ? (nb / 8u) * 144u : qt == 14u ? (nb / 8u) * 210u : nb * 34u;
}

typedef struct zg_p {
    zu u[16];
} zg_p;

static inline zi zm_sat32(zl v)
{
    return (zi) (v > 2147483647 ? 2147483647 : v < -2147483647 - 1 ? -2147483647 - 1 : v);
}

static inline zu zm_uabs(zi v)
{
    return v < 0 ? (zu) 0 - (zu) v : (zu) v;
}

static inline zu zm_bitlen(zw v)
{
    zu n = 0;
    while (v) {
        v >>= 1;
        n++;
    }
    return n;
}

static inline zw zm_udiv(zw n, zw d)
{
    zw q = 0, r = 0;
    if (d == 0) return 0;
    for (zi i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> (zu) i) & 1u);
        if (r >= d) {
            r -= d;
            q |= ((zw) 1) << (zu) i;
        }
    }
    return q;
}

static inline zw zm_isqrt(zw v)
{
    zw r = 0, bit = ((zw) 1) << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (zw) (zu) r;
}

static inline zw zm_w64(zg_p p, zu k)
{
    return (zw) p.u[k] | ((zw) p.u[k + 1u] << 32);
}

static inline zi zm_exp(zi x, ZM_DEV const zu *E)
{
    if (x > 0) x = 0;
    zl t = ((zl) x * 94548) >> 16;
    zl ip = t >> 16;
    zu f = (zu) (t - ip * 65536);
    zu hi = f >> 8, lo = f & 255u;
    zu m = E[hi] + (((E[hi + 1u] - E[hi]) * lo + 128u) >> 8);
    zl sh = -ip;
    if (sh >= 32) return 0;
    if (sh <= 0) return (zi) m;
    return (zi) ((m + (1u << (zu) (sh - 1))) >> (zu) sh);
}

static inline zl zm_mul_shr(zl a, zu b, zu sh)
{
    const zw lim = ((zw) 1) << 58;
    bool neg = a < 0;
    zw u = neg ? (zw) 0 - (zw) a : (zw) a;
    zw hi = (u >> 32) * b, lo = (zw) (zu) u * b, r;
    if (sh >= 32u) {
        zw t = hi + (lo >> 32);
        r = sh - 32u >= 64u ? 0 : t >> (sh - 32u);
    } else if ((hi >> (26u + sh)) != 0 || (lo >> sh) > lim) {
        r = lim;
    } else {
        r = (hi << (32u - sh)) + (lo >> sh);
    }
    if (r > lim) r = lim;
    return neg ? -(zl) r : (zl) r;
}

static inline zl zm_round_div(zl n, zw d)
{
    bool neg = n < 0;
    zw u = neg ? (zw) 0 - (zw) n : (zw) n;
    zw q = zm_udiv(u + (d >> 1), d);
    return neg ? -(zl) q : (zl) q;
}

/* RMS norm, first half: row i (p.u[1] values, rows p.u[2] apart) of b0 to
 * its 1/rms in Q32 (b1, zw), as zt_model.c rmsnorm forms it; 0 when the mean
 * square and eps are both 0 (every output then is 0). p.u[3..4] = eps_q48. */
static inline void zk_rms_inv(ZK_A)
{
    ZM_DEV const zi *x = (ZM_DEV const zi *) b0 + (zw) i * p.u[2];
    zu n = p.u[1];
    zw eps = zm_w64(p, 3u);
    zu amax = 0;
    for (zu j = 0; j < n; j++)
        if (zm_uabs(x[j]) > amax) amax = zm_uabs(x[j]);
    zu bb = 2u * zm_bitlen(amax) + zm_bitlen(n), sh = bb > 60u ? bb - 60u : 0u;
    zw ss = 0;
    for (zu j = 0; j < n; j++) ss += (zw) ((zl) x[j] * (zl) x[j]) >> sh;
    zw M = zm_udiv(ss, n);
    zi F = 32 - (zi) sh;
    M += eps >> (zu) (48 - F);
    ZM_DEV zw *out = (ZM_DEV zw *) b1;
    if (M == 0) {
        out[i] = 0;
        return;
    }
    zi k = 62 - (zi) zm_bitlen(M);
    if ((k + F) & 1) k--;
    zw r = zm_isqrt(M << (zu) k);
    zi N = 32 + (k + F) / 2;
    zw q0 = zm_udiv(((zw) 1) << 63, r), inv;
    if (N >= 63) {
        zu up = (zu) (N - 63);
        inv = (up >= 63u || (q0 >> (62u - up)) != 0) ? ((zw) 1) << 62 : q0 << up;
    } else {
        inv = q0 >> (zu) (63 - N);
    }
    out[i] = inv;
    (void) b2, (void) b3, (void) b4, (void) b5, (void) ctr;
}

/* RMS norm, second half: value i of rows of p.u[1] (p.u[2] apart) times
 * 1/rms (b1) and gain b2 (Q p.u[3]) into b3 (rows p.u[4] apart; b3 may be b0). */
static inline void zk_rms_apply(ZK_A)
{
    zu n = p.u[1], row = i / n, col = i % n, frac = p.u[3];
    zi xv = ((ZM_DEV const zi *) b0)[(zw) row * p.u[2] + col];
    zl inv = (zl) ((ZM_DEV const zw *) b1)[row];
    zl t = ((zl) xv * inv + (((zl) 1) << 31)) >> 32;
    if (t > (((zl) 1) << 32)) t = ((zl) 1) << 32;
    if (t < -(((zl) 1) << 32)) t = -(((zl) 1) << 32);
    zl hw = frac ? ((zl) 1) << (frac - 1u) : 0;
    zl g = ((ZM_DEV const zi *) b2)[col];
    ((ZM_DEV zi *) b3)[(zw) row * p.u[4] + col] = zm_sat32((t * g + hw) >> frac);
    (void) b4, (void) b5, (void) ctr;
}

/* act_quant, block i of 32: rows of p.u[1] values p.u[2] apart (b0) to
 * 16-bit values (b1, rows p.u[1] apart), exponents (b2) and sums (b3). */
static inline void zk_act(ZK_A)
{
    zu n = p.u[1], nb = n / 32u, row = i / nb, blk = i % nb;
    ZM_DEV const zi *v = (ZM_DEV const zi *) b0 + (zw) row * p.u[2] + blk * 32u;
    ZM_DEV zs *q = (ZM_DEV zs *) b1 + (zw) i * 32u;
    zu amax = 0;
    for (zu j = 0; j < 32u; j++)
        if (zm_uabs(v[j]) > amax) amax = zm_uabs(v[j]);
    zu s = 0;
    while (s < 17u && (((zw) amax + ((((zw) 1) << s) >> 1)) >> s) > 32767u) s++;
    zu hw = (1u << s) >> 1;
    zi t = 0;
    for (zu j = 0; j < 32u; j++) {
        zu r = (zu) (((zw) zm_uabs(v[j]) + hw) >> s);
        q[j] = (zs) (v[j] < 0 ? -(zi) r : (zi) r);
        t += q[j];
    }
    b2[i] = (zb) s;
    ((ZM_DEV zi *) b3)[i] = t;
    (void) b4, (void) b5, (void) ctr;
}

/* One output of a matrix product (the C stand-in for zt_metal.m's simd
 * kernels, which give the same exact sums): token t = i / rows, row r.
 * p.u: 1 rows, 2 nb, 3 astride, 4 qtype, 5 rowbytes. b0 weights, b1 a16,
 * b2 ash, b3 asum, b4 y (rows apart). */
static inline void zk_mm(ZK_A)
{
    zu rows = p.u[1], nb = p.u[2], as = p.u[3], qt = p.u[4];
    zu t = i / rows, r = i % rows, sb = as / 32u;
    ZM_DEV const zb *row = b0 + (zw) r * p.u[5];
    zl acc = 0;
    for (zu b = 0; b < nb; b++)
        acc += zm_sub(qt, row, b, (ZM_DEV const zs *) b1 + (zw) t * as + b * 32u,
                      (zi) b2[(zw) t * sb + b], ((ZM_DEV const zi *) b3)[(zw) t * sb + b]);
    ((ZM_DEV zi *) b4)[(zw) t * rows + r] = zm_round(acc);
    (void) b5, (void) ctr;
}

/* After a product: y (b0) plus the bias (b1 when p.u[2], per row of p.u[1]),
 * clamped, counting values at or past the 32-bit range (zt_model.c mm_rows). */
static inline void zk_bias(ZK_A)
{
    ZM_DEV zi *y = (ZM_DEV zi *) b0;
    zl v = (zl) y[i] + (p.u[2] ? (zl) ((ZM_DEV const zi *) b1)[i % p.u[1]] : 0);
    zm_count(ctr, (v >= 2147483647 || v <= -2147483647 - 1) ? 1u : 0u);
    y[i] = zm_sat32(v);
    (void) b2, (void) b3, (void) b4, (void) b5;
}

/* RoPE, pair i: tokens of p.u[1] heads of p.u[2] values (b0), p.u[3] pairs
 * a head, NEOX pairing when p.u[4]; b1 holds each token's sin and cos (Q30)
 * per pair, as zt_rope_sincos gives them. */
static inline void zk_rope(ZK_A)
{
    zu nh = p.u[1], hd = p.u[2], hw = p.u[3];
    zu b = i / (nh * hw), h = (i / hw) % nh, k = i % hw;
    ZM_DEV zi *x = (ZM_DEV zi *) b0 + ((zw) b * nh + h) * hd;
    ZM_DEV const zi *sc = (ZM_DEV const zi *) b1 + ((zw) b * hw + k) * 2u;
    zu ia = p.u[4] ? k : 2u * k, ib = p.u[4] ? k + hw : 2u * k + 1u;
    zl x0 = x[ia], x1 = x[ib], s = sc[0], c = sc[1];
    x[ia] = zm_sat32((x0 * c - x1 * s + (((zl) 1) << 29)) >> 30);
    x[ib] = zm_sat32((x0 * s + x1 * c + (((zl) 1) << 29)) >> 30);
    (void) b2, (void) b3, (void) b4, (void) b5, (void) ctr;
}

/* KV store, value i of B tokens' k (b0) and v (b1), kv_dim p.u[1], into the
 * Q16 cache b2 / b3 at cell ((p.u[2] + token) * kv_dim), p.u[2] being
 * layer * n_ctx + the first token's position. */
static inline void zk_kv(ZK_A)
{
    zu kvd = p.u[1], b = i / kvd, j = i % kvd;
    zw cell = ((zw) p.u[2] + b) * kvd + j;
    ((ZM_DEV zi *) b2)[cell] = ((ZM_DEV const zi *) b0)[i];
    ((ZM_DEV zi *) b3)[cell] = ((ZM_DEV const zi *) b1)[i];
    (void) b4, (void) b5, (void) ctr;
}

/* Attention, item i = token * n_head + head: the query (b0, heads of p.u[2]
 * values, p.u[1] heads a token) scaled below 2^22 into b1, its shift in b2. */
static inline void zk_att_q(ZK_A)
{
    zu hd = p.u[2];
    ZM_DEV const zi *qh = (ZM_DEV const zi *) b0 + (zw) i * hd;
    ZM_DEV zi *qs = (ZM_DEV zi *) b1 + (zw) i * hd;
    zu qmax = 0, sq = 0;
    for (zu j = 0; j < hd; j++)
        if (zm_uabs(qh[j]) > qmax) qmax = zm_uabs(qh[j]);
    while ((qmax >> sq) >= (1u << 22)) sq++;
    for (zu j = 0; j < hd; j++)
        qs[j] = sq ? (zi) (((zl) qh[j] + (((zl) 1) << (sq - 1u))) >> sq) : qh[j];
    ((ZM_DEV zu *) b2)[i] = sq;
    (void) b3, (void) b4, (void) b5, (void) ctr;
}

/* p.u for the attention kernels below: 1 n_head, 2 head_dim, 3 heads per KV
 * head, 4 kv_dim, 5 P (score slots per item: first position + tokens),
 * 6 the first token's position, 7 layer * n_ctx, 8 inv_sqrt_q30. */

/* Score i = item * P + position: q.k over the Q16 cache b3, scaled
 * (b0 scaled queries, b1 their shifts, b2 scores). Positions past the item's
 * token are not written. */
static inline void zk_att_score(ZK_A)
{
    zu nh = p.u[1], hd = p.u[2], P = p.u[5];
    zu item = i / P, pos = i % P, b = item / nh, h = item % nh;
    if (pos > p.u[6] + b) return;
    ZM_DEV const zi *qs = (ZM_DEV const zi *) b0 + (zw) item * hd;
    ZM_DEV const zi *kr =
        (ZM_DEV const zi *) b3 + ((zw) p.u[7] + pos) * p.u[4] + (zw) (h / p.u[3]) * hd;
    zl S = 0;
    for (zu j = 0; j < hd; j++) S += (zl) qs[j] * (zl) kr[j];
    zu sq = ((ZM_DEV const zu *) b1)[item];
    ((ZM_DEV zi *) b2)[i] = zm_sat32(zm_mul_shr(S, p.u[8], 46u - sq));
    (void) b4, (void) b5, (void) ctr;
}

/* Item i: the largest of its scores (b0) into b1. */
static inline void zk_att_max(ZK_A)
{
    zu nh = p.u[1], P = p.u[5], n = p.u[6] + i / nh + 1u;
    ZM_DEV const zi *sc = (ZM_DEV const zi *) b0 + (zw) i * P;
    zi mx = -2147483647 - 1;
    for (zu j = 0; j < n; j++)
        if (sc[j] > mx) mx = sc[j];
    ((ZM_DEV zi *) b1)[i] = mx;
    (void) b2, (void) b3, (void) b4, (void) b5, (void) ctr;
}

/* Score i to e^(score - max) in Q16 (b0 in place, b1 maxima, b2 the exp2 table). */
static inline void zk_att_exp(ZK_A)
{
    zu nh = p.u[1], P = p.u[5], item = i / P, pos = i % P;
    if (pos > p.u[6] + item / nh) return;
    ZM_DEV zi *sc = (ZM_DEV zi *) b0;
    zl d = (zl) sc[i] - ((ZM_DEV const zi *) b1)[item];
    sc[i] = zm_exp(d < -2147483647 - 1 ? -2147483647 - 1 : (zi) d, (ZM_DEV const zu *) b2);
    (void) b3, (void) b4, (void) b5, (void) ctr;
}

/* Item i: the sum of its weights (b0) into b1 (zw). */
static inline void zk_att_sum(ZK_A)
{
    zu nh = p.u[1], P = p.u[5], n = p.u[6] + i / nh + 1u;
    ZM_DEV const zi *w = (ZM_DEV const zi *) b0 + (zw) i * P;
    zw sum = 0;
    for (zu j = 0; j < n; j++) sum += (zu) w[j];
    ((ZM_DEV zw *) b1)[i] = sum;
    (void) b2, (void) b3, (void) b4, (void) b5, (void) ctr;
}

/* Output i = item * head_dim + j: sum of weight (b0) times value (cache b3)
 * over the item's positions, over the weights' sum (b1), into b2 (tokens of
 * n_head * head_dim values, the item's head in place). */
static inline void zk_att_wv(ZK_A)
{
    zu nh = p.u[1], hd = p.u[2], P = p.u[5];
    zu item = i / hd, j = i % hd, b = item / nh, h = item % nh, n = p.u[6] + b + 1u;
    ZM_DEV const zi *w = (ZM_DEV const zi *) b0 + (zw) item * P;
    ZM_DEV const zi *v = (ZM_DEV const zi *) b3 + (zw) p.u[7] * p.u[4] + (zw) (h / p.u[3]) * hd + j;
    zl acc = 0;
    for (zu k = 0; k < n; k++)
        if (w[k]) acc += (zl) w[k] * (zl) v[(zw) k * p.u[4]];
    ((ZM_DEV zi *) b2)[i] = zm_sat32(zm_round_div(acc, ((ZM_DEV const zw *) b1)[item]));
    (void) b4, (void) b5, (void) ctr;
}

/* The residual: x (b0) += o (b1), clamped, counting overflows. */
static inline void zk_resid(ZK_A)
{
    ZM_DEV zi *x = (ZM_DEV zi *) b0;
    zl v = (zl) x[i] + ((ZM_DEV const zi *) b1)[i];
    zm_count(ctr, (v > 2147483647 || v < -2147483647 - 1) ? 1u : 0u);
    x[i] = zm_sat32(v);
    (void) p, (void) b2, (void) b3, (void) b4, (void) b5;
}

/* SiLU(gate) * up: gate b0 in place, up b1, the exp2 table b2. */
static inline void zk_silu(ZK_A)
{
    ZM_DEV zi *gp = (ZM_DEV zi *) b0;
    zi g = gp[i], u = ((ZM_DEV const zi *) b1)[i];
    (void) p;
    if (g == 0) {
        gp[i] = 0;
        return;
    }
    zu e = (zu) zm_exp(g < 0 ? g : -g, (ZM_DEV const zu *) b2);
    zu sig = 0xFFFFFFFFu / (65536u + e);
    if (g < 0) sig = 65536u - sig;
    zl s = ((zl) g * (zl) sig + 32768) >> 16;
    gp[i] = zm_sat32((s * (zl) u + 32768) >> 16);
    (void) b3, (void) b4, (void) b5, (void) ctr;
}

)
