/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* dimfold.c — Dimensional Fold: flatten/unflatten as compression + encryption +
 * a reversible nested transform. See dimfold.h. */
#include "dimfold.h"
#include "sha256.h"

/* ======================================================================== *
 * 1. TRANSFORM — reversible integer Haar / S-transform, applied at NESTED
 *    levels (the Russian-doll matrix). One level splits pairs (x0,x1) into a
 *    smooth coefficient s and a detail d, exactly reversibly:
 *        d = x1 - x0 ;  s = x0 + (d >> 1)         (>>1 = floor divide by 2)
 *      inverse:  x0 = s - (d >> 1) ;  x1 = x0 + d
 *    The smooth half is recursed — coarser and coarser — so a[0] ends up the
 *    single coarse IDENTITY and the rest are nested detail SHELLS (φ-scaled
 *    multiresolution). Smooth / self-similar data pushes almost all detail
 *    coefficients to zero, which is what makes compression work.
 * ======================================================================== */

void dimfold_fold(int32_t *a, int n){
    int32_t tmp[1024];                       /* n <= 1024 */
    for (int len = n; len >= 2; len >>= 1){
        int half = len >> 1;
        for (int i = 0; i < half; i++){
            int32_t x0 = a[2*i], x1 = a[2*i+1];
            int32_t d  = x1 - x0;
            int32_t s  = x0 + (d >> 1);
            tmp[i]        = s;               /* smooth -> low half  */
            tmp[half + i] = d;               /* detail -> high half */
        }
        for (int i = 0; i < len; i++) a[i] = tmp[i];
    }
}

void dimfold_unfold(int32_t *a, int n){
    int32_t tmp[1024];
    for (int len = 2; len <= n; len <<= 1){
        int half = len >> 1;
        for (int i = 0; i < half; i++){
            int32_t s = a[i], d = a[half + i];
            int32_t x0 = s - (d >> 1);
            int32_t x1 = x0 + d;
            tmp[2*i]   = x0;
            tmp[2*i+1] = x1;
        }
        for (int i = 0; i < len; i++) a[i] = tmp[i];
    }
}

/* ======================================================================== *
 * 2. COMPRESS / EXPAND — fold the bytes, then code the sparse coefficients
 *    with zig-zag varints and zero-run skips. Lossless.
 * ======================================================================== */

static int next_pow2(int n){ int p = 1; while (p < n) p <<= 1; return p; }

int dimfold_bound(int n){ return n * 5 + 16; }   /* worst case: no coeff is zero */

/* LEB128 unsigned varint */
static int put_varint(uint8_t *o, int oi, uint32_t v){
    while (v >= 0x80){ o[oi++] = (uint8_t)(v | 0x80); v >>= 7; }
    o[oi++] = (uint8_t)v; return oi;
}
static int get_varint(const uint8_t *in, int n, int *pi, uint32_t *out){
    uint32_t v = 0; int shift = 0, i = *pi;
    while (i < n){
        uint8_t b = in[i++];
        v |= (uint32_t)(b & 0x7F) << shift;
        if (!(b & 0x80)){ *pi = i; *out = v; return 1; }
        shift += 7; if (shift > 28) break;
    }
    return 0;                                 /* truncated */
}
static uint32_t zigzag(int32_t v){ return ((uint32_t)v << 1) ^ (uint32_t)(v >> 31); }
static int32_t  unzigzag(uint32_t z){ return (int32_t)(z >> 1) ^ -(int32_t)(z & 1); }

int dimfold_compress(const uint8_t *in, int n, uint8_t *out, int out_cap){
    if (n <= 0 || out_cap < dimfold_bound(n)) return 0;
    int N = next_pow2(n);
    if (N > 1024) return 0;                   /* block cap; caller chunks larger */
    int32_t a[1024];
    for (int i = 0; i < N; i++) a[i] = (i < n) ? (int32_t)in[i] : 0;
    dimfold_fold(a, N);

    int oi = 0;
    oi = put_varint(out, oi, (uint32_t)n);    /* original length */
    /* trim trailing zero coefficients (the decoder refills them) */
    int last = N - 1; while (last >= 0 && a[last] == 0) last--;
    int i = 0;
    while (i <= last){
        uint32_t run = 0;
        while (i <= last && a[i] == 0){ run++; i++; }
        oi = put_varint(out, oi, run);        /* zeros to skip */
        if (i <= last){ oi = put_varint(out, oi, zigzag(a[i])); i++; }
    }
    return oi;
}

int dimfold_expand(const uint8_t *in, int n, uint8_t *out, int out_cap){
    int pi = 0; uint32_t orig = 0;
    if (!get_varint(in, n, &pi, &orig)) return 0;
    if ((int)orig <= 0 || (int)orig > out_cap) return 0;
    int N = next_pow2((int)orig);
    if (N > 1024) return 0;
    int32_t a[1024];
    for (int i = 0; i < N; i++) a[i] = 0;
    int idx = 0;
    while (pi < n && idx < N){
        uint32_t run = 0;
        if (!get_varint(in, n, &pi, &run)) break;
        idx += (int)run;
        if (pi < n && idx < N){
            uint32_t z = 0;
            if (!get_varint(in, n, &pi, &z)) break;
            a[idx++] = unzigzag(z);
        }
    }
    dimfold_unfold(a, N);
    for (int i = 0; i < (int)orig; i++){
        int32_t v = a[i]; if (v < 0) v = 0; if (v > 255) v = 255;
        out[i] = (uint8_t)v;
    }
    return (int)orig;
}

/* ======================================================================== *
 * 5. DIMENSIONAL ELEVATOR — raise/lower a whole file of ANY type and size,
 *    reversibly, keeping every shell. The container is:
 *        'Z''X''V''E' | identity[32] | varint(total) | { varint(clen) chunk }*
 *    descend rebuilds the file and VERIFIES it against the identity fingerprint.
 * ======================================================================== */
#define DF_CHUNK 512

int dimfold_elevate_bound(int n){
    int chunks = n / DF_CHUNK + 1;
    return 4 + 32 + 5 + chunks * (5 + DF_CHUNK*5 + 16) + 16;
}

int dimfold_elevate(const uint8_t *in, int n, uint8_t *out, int out_cap){
    if (n < 0 || out_cap < 4 + 32 + 6) return 0;
    int oi = 0;
    out[oi++]='Z'; out[oi++]='X'; out[oi++]='V'; out[oi++]='E';
    uint8_t id[32]; dimfold_identity(in, n, id);
    for (int i = 0; i < 32; i++) out[oi++] = id[i];
    oi = put_varint(out, oi, (uint32_t)n);

    uint8_t tmp[DF_CHUNK*5 + 16];
    int pos = 0;
    while (pos < n){
        int clen = n - pos; if (clen > DF_CHUNK) clen = DF_CHUNK;
        int t = dimfold_compress(in + pos, clen, tmp, (int)sizeof tmp);
        /* per-chunk tag = (payload_len << 1) | mode; mode 1 = folded, 0 = stored.
         * keep whichever is smaller so the elevator NEVER bloats any filetype. */
        if (t > 0 && t < clen){                       /* folding helped: store folded */
            uint32_t tag = ((uint32_t)t << 1) | 1u;
            if (oi + 5 + t > out_cap) return 0;
            oi = put_varint(out, oi, tag);
            for (int i = 0; i < t; i++) out[oi++] = tmp[i];
        } else {                                       /* incompressible: store raw   */
            uint32_t tag = ((uint32_t)clen << 1) | 0u;
            if (oi + 5 + clen > out_cap) return 0;
            oi = put_varint(out, oi, tag);
            for (int i = 0; i < clen; i++) out[oi++] = in[pos + i];
        }
        pos += clen;
    }
    return oi;
}

int dimfold_descend(const uint8_t *in, int n, uint8_t *out, int out_cap){
    if (n < 4 + 32) return 0;
    if (!(in[0]=='Z' && in[1]=='X' && in[2]=='V' && in[3]=='E')) return 0;
    int pi = 4;
    uint8_t id[32]; for (int i = 0; i < 32; i++) id[i] = in[pi++];
    uint32_t total = 0; if (!get_varint(in, n, &pi, &total)) return 0;
    if ((int)total > out_cap) return 0;

    int outpos = 0;
    while (pi < n && outpos < (int)total){
        uint32_t tag = 0; if (!get_varint(in, n, &pi, &tag)) return 0;
        int mode = (int)(tag & 1u), plen = (int)(tag >> 1);
        if (pi + plen > n || outpos + 1 > out_cap) return 0;
        int got;
        if (mode){                                     /* folded chunk */
            got = dimfold_expand(in + pi, plen, out + outpos, out_cap - outpos);
            if (got <= 0) return 0;
        } else {                                       /* stored chunk */
            if (outpos + plen > out_cap) return 0;
            for (int i = 0; i < plen; i++) out[outpos + i] = in[pi + i];
            got = plen;
        }
        outpos += got; pi += plen;
    }
    if (outpos != (int)total) return 0;
    /* integrity: the reconstruction must match the identity fingerprint exactly */
    uint8_t chk[32]; dimfold_identity(out, outpos, chk);
    for (int i = 0; i < 32; i++) if (chk[i] != id[i]) return 0;
    return outpos;
}

/* ======================================================================== *
 * 6. BANDS + CHANNELS — the fold as frequency subbands, and a multiplexed
 *    multi-channel container with a manifest (the assembly instructions).
 * ======================================================================== */

int dimfold_band_count(int n){ int b = 0; while ((1 << b) < n) b++; return b + 1; }

void dimfold_band_range(int n, int band, int *lo, int *hi){
    if (band <= 0){ *lo = 0; *hi = (n >= 1) ? 1 : 0; return; }
    int l = 1 << (band - 1), h = 1 << band;
    if (l > n) l = n; if (h > n) h = n;
    *lo = l; *hi = h;
}

int dimfold_pack(const dimfold_channel_t *ch, int nch, uint8_t *out, int out_cap){
    if (nch < 0 || out_cap < 6) return 0;
    int oi = 0;
    out[oi++]='Z'; out[oi++]='X'; out[oi++]='V'; out[oi++]='M';
    oi = put_varint(out, oi, (uint32_t)nch);
    for (int c = 0; c < nch; c++){
        if (oi + 5 > out_cap) return 0;
        out[oi++] = ch[c].type;               /* the data-type of this channel     */
        int lenpos = oi; oi += 4;             /* reserve fixed 32-bit length slot   */
        int elen = dimfold_elevate(ch[c].data, ch[c].len, out + oi, out_cap - oi);
        if (elen <= 0 && ch[c].len != 0) return 0;
        out[lenpos]   = (uint8_t)(elen);
        out[lenpos+1] = (uint8_t)(elen >> 8);
        out[lenpos+2] = (uint8_t)(elen >> 16);
        out[lenpos+3] = (uint8_t)(elen >> 24);
        oi += elen;
    }
    return oi;
}

int dimfold_unpack(const uint8_t *in, int n, uint8_t *out, int out_cap,
                   dimfold_slot_t *slots, int max_slots){
    if (n < 5 || !(in[0]=='Z'&&in[1]=='X'&&in[2]=='V'&&in[3]=='M')) return 0;
    int pi = 4; uint32_t nch = 0;
    if (!get_varint(in, n, &pi, &nch)) return 0;
    int outpos = 0;
    for (uint32_t c = 0; c < nch; c++){
        if (pi + 5 > n) return 0;
        uint8_t type = in[pi++];
        int elen = (int)((uint32_t)in[pi] | ((uint32_t)in[pi+1]<<8)
                       | ((uint32_t)in[pi+2]<<16) | ((uint32_t)in[pi+3]<<24));
        pi += 4;
        if (elen < 0 || pi + elen > n) return 0;
        int got = dimfold_descend(in + pi, elen, out + outpos, out_cap - outpos);
        if (got < 0) return 0;
        if ((int)c < max_slots){ slots[c].type = type; slots[c].offset = outpos; slots[c].len = got; }
        outpos += got; pi += elen;
    }
    return (int)nch;
}

/* ======================================================================== *
 * 3. SEAL / OPEN — the PRIME lock. Keystream = SHA-256d(key || counter),
 *    XORed into the data. Re-applying the same key opens it.
 * ======================================================================== */

static void sha256d(const uint8_t *in, int n, uint8_t out[32]){
    uint8_t t[32]; sha256(in, (size_t)n, t); sha256(t, 32, out);
}
static void keystream_block(const uint8_t *key, int klen, uint32_t ctr, uint8_t out[32]){
    uint8_t buf[72]; int p = 0;
    if (klen > 64) klen = 64;
    for (int i = 0; i < klen; i++) buf[p++] = key[i];
    buf[p++] = (uint8_t)(ctr);       buf[p++] = (uint8_t)(ctr >> 8);
    buf[p++] = (uint8_t)(ctr >> 16); buf[p++] = (uint8_t)(ctr >> 24);
    sha256d(buf, p, out);
}

void dimfold_seal(uint8_t *data, int n, const uint8_t *key, int keylen){
    uint8_t ks[32]; uint32_t ctr = 0;
    for (int i = 0; i < n; i++){
        if ((i & 31) == 0){ keystream_block(key, keylen, ctr, ks); ctr++; }
        data[i] ^= ks[i & 31];
    }
}
void dimfold_open(uint8_t *data, int n, const uint8_t *key, int keylen){
    dimfold_seal(data, n, key, keylen);       /* XOR is involutive */
}

/* ======================================================================== *
 * 4. IDENTITY — SHA-256d over the bytes.
 * ======================================================================== */
void dimfold_identity(const uint8_t *data, int n, uint8_t out[32]){
    sha256d(data, n, out);
}

/* ======================================================================== *
 * Self-check: every face round-trips losslessly; Fibonacci recovery key -> φ.
 * ======================================================================== */
int dimfold_selfcheck(uint32_t *ratio_permille_out){
    /* a representative scanline: piecewise-constant regions (sky / figure / ground)
     * — exactly the self-similar, low-detail structure the fold makes sparse. */
    uint8_t src[256], work[256], back[256];
    for (int i = 0; i < 256; i++){
        uint8_t v = 0x14;                      /* sky run            */
        if (i >= 96 && i < 128) v = 0xE5;      /* figure band        */
        else if (i >= 176)      v = 0x2E;      /* ground run         */
        src[i] = v;
    }

    /* (a) TRANSFORM round-trips exactly */
    int32_t a[256];
    for (int i = 0; i < 256; i++) a[i] = src[i];
    dimfold_fold(a, 256);
    dimfold_unfold(a, 256);
    int transform_ok = 1;
    for (int i = 0; i < 256; i++) if (a[i] != (int32_t)src[i]) transform_ok = 0;

    /* (b) COMPRESS/EXPAND round-trips exactly, and actually shrinks smooth data */
    uint8_t comp[1300];
    int clen = dimfold_compress(src, 256, comp, (int)sizeof comp);
    int xlen = dimfold_expand(comp, clen, back, (int)sizeof back);
    int compress_ok = (clen > 0) && (xlen == 256);
    if (compress_ok) for (int i = 0; i < 256; i++) if (back[i] != src[i]) compress_ok = 0;
    int shrank = compress_ok && (clen < 256);
    uint32_t ratio = clen > 0 ? (uint32_t)((clen * 1000) / 256) : 1000;

    /* (c) SEAL/OPEN round-trips exactly, and the sealed bytes differ from plain */
    static const uint8_t key[16] = { 'Z','X','V','-','d','i','m','f','o','l','d','-','k','e','y','!' };
    for (int i = 0; i < 256; i++) work[i] = src[i];
    dimfold_seal(work, 256, key, 16);
    int changed = 0; for (int i = 0; i < 256; i++) if (work[i] != src[i]) changed = 1;
    dimfold_open(work, 256, key, 16);
    int crypto_ok = changed;
    for (int i = 0; i < 256; i++) if (work[i] != src[i]) crypto_ok = 0;

    /* (d) the FIBONACCI recovery key aligns to φ (the axiom of alignment) */
    long fa = 1, fb = 1; for (int k = 0; k < 40; k++){ long t = fa + fb; fa = fb; fb = t; }
    /* fb/fa ≈ φ; check |fb/fa - φ| small using integer cross-multiply:
     * |fb - φ·fa| < ε·fa  <=>  |1000·fb - 1618·fa| < fa   (φ ≈ 1.618) */
    long lhs = 1000L*fb - 1618L*fa; if (lhs < 0) lhs = -lhs;
    int aligned = (lhs < fa);

    /* (e) the ELEVATOR round-trips an arbitrary multi-chunk "file" of any type,
     * and its integrity fingerprint confirms the reconstruction bit-for-bit. */
    static uint8_t big[1500], dbuf[1500]; static uint8_t ebuf[9000];
    for (int i = 0; i < 1500; i++)                     /* mixed structured content */
        big[i] = (i % 128 < 64) ? 0x20 : (uint8_t)(0x40 + ((i >> 6) & 7));
    int el = dimfold_elevate(big, 1500, ebuf, (int)sizeof ebuf);
    int de = dimfold_descend(ebuf, el, dbuf, (int)sizeof dbuf);
    int elevator_ok = (el > 0) && (de == 1500);
    if (elevator_ok) for (int i = 0; i < 1500; i++) if (dbuf[i] != big[i]) elevator_ok = 0;

    /* (f) BANDS partition the fold, and multi-CHANNEL pack/unpack round-trips
     * heterogeneous data types with a manifest and per-channel integrity. */
    int bands_ok = (dimfold_band_count(256) == 9);
    { int lo, hi, expect = 1; dimfold_band_range(256, 0, &lo, &hi);
      if (!(lo == 0 && hi == 1)) bands_ok = 0;
      for (int b = 1; b < 9; b++){ dimfold_band_range(256, b, &lo, &hi);
          if (lo != expect) bands_ok = 0; expect = hi; }
      if (expect != 256) bands_ok = 0; }

    static uint8_t gfx[400], txt[300], bin[200];
    for (int i = 0; i < 400; i++) gfx[i] = (i < 200) ? 0x11 : 0x22;   /* folds well */
    for (int i = 0; i < 300; i++) txt[i] = (uint8_t)('A' + (i % 26)); /* text       */
    for (int i = 0; i < 200; i++) bin[i] = (uint8_t)(i*37 + 11);      /* dense      */
    /* three channels, one per SPACE POLARITY (positive / neutral / negative) */
    dimfold_channel_t chs[3] = { { gfx,400,DIMFOLD_POSITIVE },
                                 { txt,300,DIMFOLD_NEUTRAL  },
                                 { bin,200,DIMFOLD_NEGATIVE } };
    static uint8_t packed[4096], unp[1024]; dimfold_slot_t slots[3];
    int pk = dimfold_pack(chs, 3, packed, (int)sizeof packed);
    int nc = dimfold_unpack(packed, pk, unp, (int)sizeof unp, slots, 3);
    int channels_ok = (pk > 0) && (nc == 3)
        && slots[0].type==DIMFOLD_POSITIVE && slots[0].len==400
        && slots[1].type==DIMFOLD_NEUTRAL  && slots[1].len==300
        && slots[2].type==DIMFOLD_NEGATIVE && slots[2].len==200;
    if (channels_ok){
        for (int i=0;i<400;i++) if (unp[slots[0].offset+i]!=gfx[i]) channels_ok=0;
        for (int i=0;i<300;i++) if (unp[slots[1].offset+i]!=txt[i]) channels_ok=0;
        for (int i=0;i<200;i++) if (unp[slots[2].offset+i]!=bin[i]) channels_ok=0;
    }

    if (ratio_permille_out) *ratio_permille_out = ratio;
    return transform_ok && compress_ok && shrank && crypto_ok && aligned
        && elevator_ok && bands_ok && channels_ok;
}
