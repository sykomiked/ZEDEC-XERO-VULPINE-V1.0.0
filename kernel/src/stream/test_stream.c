/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_stream.c — host tests for swarm streaming: live/VOD segmentation,
 * signed manifests, freight compatibility, MDS and RLNC decoding, rebuild
 * from rows gathered from random peers, scheduler invariants (upload caps,
 * no duplicate requests, nothing asked after k, deadlines, rarest-first,
 * origin as last resort, weighted push, economy hooks), the scaling table
 * against a client-server baseline, the conditions under which the scaling
 * claim fails, churn, decoding of what simulated viewers actually hold, and
 * fuzzing of every wire parser.
 *
 * Build and run (from kernel/):
 *
 *   PATH=/tmp/claude-0/gcc11:$PATH gcc -std=c11 -Wall -Wextra -Werror -O1 -g \
 *     -fsanitize=address,undefined -fno-sanitize-recover=all \
 *     -Isrc/stream -Isrc/freight -Isrc/robin_debanks -Isrc/ipfs_node -Isrc/modbind -Iinclude \
 *     src/stream/test_stream.c src/stream/stream_seg.c src/stream/stream_swarm.c \
 *     src/stream/stream_sim.c src/freight/freight.c src/robin_debanks/sha256.c \
 *     src/ipfs_node/ipfsn_multiformats.c -o /tmp/test_stream && /tmp/test_stream
 *
 * (drop the sanitizer flags and use -O2 for a fast run; "quick" as the first
 * argument skips the large simulations)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stream_seg.h"
#include "stream_sim.h"
#include "stream_swarm.h"
#include "freight.h"
#include "sha256.h"

static int failures = 0, checks = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s (line %d)\n", m, __LINE__);                                          \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static int quiet_fail = 0;
#define QCHECK(c, m)                                                                               \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            if (quiet_fail++ < 10) printf("  [fail] %s (line %d)\n", m, __LINE__);                 \
        }                                                                                          \
    } while (0)

/* ---- deterministic PRNG (splitmix64) ---- */
static uint64_t rng_s = 0x5A58565354524D31ull;
static uint64_t rnd(void)
{
    uint64_t z = (rng_s += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static uint32_t rndn(uint32_t n)
{
    return (uint32_t) (rnd() % n);
}
static void rnd_fill(uint8_t *p, size_t n)
{
    for (size_t i = 0; i < n; i++) p[i] = (uint8_t) rnd();
}

static uint8_t dec_work[STREAM_DEC_WORK_BYTES];

/* ---- a stand-in "signature": keyed SHA-256. NOT a signature scheme; the
 * real one is ML-DSA through the same callbacks. ---- */
typedef struct {
    uint8_t key[32];
} toy_key_t;

static void toy_tag(const toy_key_t *k, const uint8_t *msg, uint32_t len, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, k->key, 32);
    sha256_update(&c, msg, len);
    sha256_final(&c, out);
}

static int toy_sign(void *ctx, const uint8_t *msg, uint32_t len, uint8_t *sig, uint32_t cap,
                    uint32_t *sig_len)
{
    if (cap < 32) return -1;
    toy_tag((const toy_key_t *) ctx, msg, len, sig);
    *sig_len = 32;
    return 0;
}

static bool toy_verify(void *ctx, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                       uint32_t sig_len)
{
    uint8_t t[32];
    if (sig_len != 32) return false;
    toy_tag((const toy_key_t *) ctx, msg, len, t);
    return memcmp(t, sig, 32) == 0;
}

/* ---- deterministic segment content ---- */
static void seg_data(uint32_t seq, uint8_t *out, uint32_t len)
{
    uint64_t s = 0xC0DEC0DE00000000ull ^ seq;
    for (uint32_t i = 0; i < len; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        out[i] = (uint8_t) (s >> 24);
    }
}

/* ===================================================================== */
/* 1. segmentation                                                        */
/* ===================================================================== */

#define NUNITS 300
static uint8_t rec[NUNITS * 700];
static stream_unit_t units[NUNITS];
static uint8_t cutbuf[65536];

static void test_segmentation(void)
{
    printf("\n== live and VOD segmentation ==\n");
    uint32_t off = 0;
    for (uint32_t i = 0; i < NUNITS; i++) { /* 30 fps, keyframe every 15 frames */
        uint32_t len = 200 + rndn(500);
        rnd_fill(rec + off, len);
        units[i].offset = off;
        units[i].len = len;
        units[i].pts_us = (uint64_t) i * 33333u;
        units[i].keyframe = (i % 15) == 0;
        off += len;
    }
    const uint32_t total = off;
    stream_cutter_t c;
    CHECK(stream_cut_init(&c, cutbuf, sizeof cutbuf, 1000000, 2000000) == STREAM_OK, "cutter init");
    stream_seg_out_t segs[32];
    uint32_t seg_off[32], ns = 0, consumed = 0;
    bool ok = true;
    for (uint32_t i = 0; i < NUNITS; i++) {
        stream_seg_out_t o;
        int rc = stream_cut_push(&c, rec + units[i].offset, units[i].len, units[i].pts_us,
                                 units[i].keyframe, &o);
        if (rc == STREAM_CUT_READY) {
            if (ns < 32) {
                segs[ns] = o;
                seg_off[ns] = consumed;
                ok &= memcmp(o.data, rec + consumed, o.len) == 0;
                ns++;
            }
            consumed += o.len;
            stream_cut_next(&c);
            rc = stream_cut_push(&c, rec + units[i].offset, units[i].len, units[i].pts_us,
                                 units[i].keyframe, &o);
        }
        ok &= rc == STREAM_CUT_NONE;
    }
    stream_seg_out_t o;
    CHECK(stream_cut_flush(&c, 33333, &o) == STREAM_CUT_READY, "flush closes the last segment");
    ok &= memcmp(o.data, rec + consumed, o.len) == 0;
    segs[ns] = o;
    seg_off[ns] = consumed;
    ns++;
    consumed += o.len;
    CHECK(ok && consumed == total, "live segments are contiguous and cover every byte");
    bool keys = true, durs = true;
    for (uint32_t i = 0; i < ns; i++) {
        keys &= segs[i].key_start;
        if (i + 1 < ns) durs &= segs[i].dur_us >= 1000000 && segs[i].dur_us < 1500000 + 33334;
    }
    printf("  %u live segments, first durations %u %u %u us\n", ns, (unsigned) segs[0].dur_us,
           (unsigned) segs[1].dur_us, (unsigned) segs[2].dur_us);
    CHECK(keys, "every live segment starts on a keyframe");
    CHECK(durs, "segments close at the first keyframe at or after 1 s (keyframes every 0.5 s)");

    stream_vod_seg_t vs[32];
    int nv = stream_vod_plan(units, NUNITS, 1000000, 2000000, sizeof cutbuf, 33333, vs, 32);
    bool same = nv == (int) ns;
    for (int i = 0; same && i < nv; i++)
        same = vs[i].offset == seg_off[i] && vs[i].len == segs[i].len &&
               vs[i].dur_us == segs[i].dur_us && vs[i].t_start_us == segs[i].t_start_us;
    CHECK(same, "VOD plan over the recording equals the live cut");
    bool cids = same;
    for (int i = 0; cids && i < nv; i++) {
        static stream_manifest_t a, b;
        stream_manifest_build(&a, 1, (uint32_t) i, vs[i].t_start_us, vs[i].dur_us, 0, 128, 0,
                              rec + vs[i].offset, vs[i].len);
        stream_manifest_build(&b, 1, (uint32_t) i, segs[i].t_start_us, segs[i].dur_us, 0, 128, 0,
                              segs[i].data == cutbuf ? rec + seg_off[i] : segs[i].data,
                              segs[i].len);
        cids = ipfsn_cid_equal(&a.cid, &b.cid);
    }
    CHECK(cids, "VOD and live segments get the same CIDs");

    /* forced cut without keyframes: max duration */
    for (uint32_t i = 0; i < NUNITS; i++) units[i].keyframe = i == 0;
    nv = stream_vod_plan(units, NUNITS, 1000000, 2000000, sizeof cutbuf, 33333, vs, 32);
    bool forced = nv >= 4;
    for (int i = 0; forced && i + 1 < nv; i++)
        forced = vs[i].dur_us >= 2000000 && vs[i].dur_us < 2040000;
    CHECK(forced, "without keyframes segments are cut at the maximum duration");
    stream_cutter_t c2;
    stream_cut_init(&c2, cutbuf, 1000, 1000000, 2000000);
    CHECK(stream_cut_push(&c2, rec, 1001, 0, true, &o) == STREAM_ERR_SPACE,
          "unit larger than the buffer refused");
    stream_cut_push(&c2, rec, 100, 1000, true, &o);
    CHECK(stream_cut_push(&c2, rec, 100, 999, false, &o) == STREAM_ERR_ARG,
          "time going backwards refused");
    stream_unit_t bad[2] = {{0, 10, 0, true}, {11, 10, 5, false}};
    CHECK(stream_vod_plan(bad, 2, 1000, 2000, 100, 0, vs, 32) == STREAM_ERR_FORMAT,
          "VOD gap refused");
}

/* ===================================================================== */
/* 2. manifests                                                           */
/* ===================================================================== */

static uint8_t mbuf[STREAM_MANIFEST_MAX];

static void test_manifest(void)
{
    printf("\n== signed manifests ==\n");
    toy_key_t key, other;
    rnd_fill(key.key, 32);
    rnd_fill(other.key, 32);
    static uint8_t d0[10240], d1[10240];
    seg_data(0, d0, sizeof d0);
    seg_data(1, d1, sizeof d1);
    static stream_manifest_t m0, m1, p;
    CHECK(stream_manifest_build(&m0, 77, 0, 0, 1000000, STREAM_MF_LIVE | STREAM_MF_KEYSTART, 128, 0,
                                d0, sizeof d0) == STREAM_OK,
          "manifest build");
    CHECK(m0.nfreights == 4 && stream_seg_verify(&m0, d0, sizeof d0) == STREAM_OK,
          "segment CID verifies (4 freights at k = 128)");
    uint8_t h0[32], h0b[32];
    int l0 = stream_manifest_serialize(&m0, mbuf, sizeof mbuf, toy_sign, &key, h0);
    CHECK(l0 > 0, "serialise with signature");
    CHECK(stream_manifest_parse(mbuf, (uint32_t) l0, &p, toy_verify, &key, h0b) == STREAM_OK &&
              memcmp(h0, h0b, 32) == 0,
          "parse + verify");
    CHECK(p.seq == 0 && p.k == 128 && p.nfreights == 4 && p.data_len == 10240 &&
              p.freight_id == m0.freight_id && ipfsn_cid_equal(&p.cid, &m0.cid) &&
              memcmp(p.freight_sha256, m0.freight_sha256, 4 * 32) == 0 &&
              p.flags == (STREAM_MF_LIVE | STREAM_MF_KEYSTART),
          "fields survive the round trip");
    CHECK(stream_manifest_parse(mbuf, (uint32_t) l0, &p, toy_verify, &other, 0) == STREAM_ERR_SIG,
          "wrong key: signature refused");
    CHECK(stream_manifest_parse(mbuf, (uint32_t) l0, &p, 0, 0, 0) == STREAM_ERR_ARG,
          "no verify callback: refused");
    int flips = 0;
    for (int i = 0; i < l0; i++)
        for (int b = 0; b < 8; b += 3) {
            mbuf[i] ^= (uint8_t) (1u << b);
            if (stream_manifest_parse(mbuf, (uint32_t) l0, &p, toy_verify, &key, 0) == STREAM_OK)
                flips++;
            mbuf[i] ^= (uint8_t) (1u << b);
        }
    CHECK(flips == 0, "every single-bit change of a signed manifest is rejected");
    stream_manifest_build(&m1, 77, 1, 1000000, 1000000, STREAM_MF_LIVE, 128, h0, d1, sizeof d1);
    CHECK(stream_manifest_chains(&m1, h0) && !stream_manifest_chains(&m0, h0),
          "hash chain links segment 1 to segment 0");
    d1[5] ^= 1;
    CHECK(stream_seg_verify(&m1, d1, sizeof d1) == STREAM_ERR_HASH,
          "changed segment fails its CID");
    d1[5] ^= 1;
}

/* ===================================================================== */
/* 3. freights, MDS ids beyond 168, RLNC                                  */
/* ===================================================================== */

static uint8_t segbuf[STREAM_SEG_MAX_BYTES];
static uint8_t outbuf[STREAM_SEG_MAX_BYTES];

static void test_freight_rows(void)
{
    printf("\n== freight compatibility and rateless rows ==\n");
    static const uint8_t ks[] = {1, 7, 64, 128, 168};
    bool same = true, fdec = true;
    for (uint32_t t = 0; t < sizeof ks; t++) {
        uint8_t k = ks[t];
        uint32_t len = FREIGHT_CAPACITY(k) * 3 - 17;
        rnd_fill(segbuf, len);
        static stream_manifest_t m;
        stream_manifest_build(&m, 5, t, 0, 1000000, 0, k, 0, segbuf, len);
        for (uint32_t f = 0; f < m.nfreights; f++) {
            freight_header_t h;
            static uint8_t pk[FREIGHT_BYTES];
            stream_freight_header(&m, f, &h);
            freight_encode(&h, segbuf + FREIGHT_CAPACITY(k) * f, pk);
            for (uint32_t r = 0; r < FREIGHT_ROWS; r++) {
                uint8_t row[20];
                stream_seg_row(&m, segbuf, len, f, r, row);
                same &= pk[r * 21] == r && memcmp(row, pk + r * 21 + 1, 20) == 0;
            }
            /* freight's own decoder accepts our parity rows */
            static uint8_t sel[FREIGHT_BYTES];
            uint32_t n = 0;
            for (uint32_t r = FREIGHT_ROWS - k; r < FREIGHT_ROWS; r++, n++)
                memcpy(sel + n * 21, pk + r * 21, 21);
            static uint8_t fo[FREIGHT_MAX_PAYLOAD];
            static uint8_t fw[FREIGHT_DECODE_WORK_BYTES];
            fdec &= freight_decode(&h, sel, n, fo, sizeof fo, fw, sizeof fw) == FREIGHT_OK &&
                    memcmp(fo, segbuf + FREIGHT_CAPACITY(k) * f, h.payload_len) == 0;
        }
    }
    CHECK(same, "ids 0..167 equal freight_encode byte for byte (k = 1, 7, 64, 128, 168)");
    CHECK(fdec, "freight_decode rebuilds from the last k rows (all parity)");

    /* any k distinct ids of 0..255 decode, including the 168..255 extension */
    int ok = 0, trials = 0, used_ext = 0;
    for (uint32_t t = 0; t < 60; t++) {
        uint8_t k = (uint8_t) (1 + rndn(88)); /* k <= 88 so 168..255 alone can carry it */
        if (t % 3 == 0) k = 128;
        uint32_t len = FREIGHT_CAPACITY(k) * 2;
        rnd_fill(segbuf, len);
        static stream_manifest_t m;
        stream_manifest_build(&m, 6, t, 0, 0, 0, k, 0, segbuf, len);
        for (uint32_t f = 0; f < m.nfreights; f++) {
            stream_dec_t d;
            stream_dec_init(&d, &m, f, dec_work, sizeof dec_work);
            uint8_t ids[256];
            for (uint32_t i = 0; i < 256; i++) ids[i] = (uint8_t) i;
            for (uint32_t i = 255; i > 0; i--) {
                uint32_t j = rndn(i + 1);
                uint8_t x = ids[i];
                ids[i] = ids[j];
                ids[j] = x;
            }
            if (t % 3 == 1) /* only extension ids */
                for (uint32_t i = 0; i < k; i++) ids[i] = (uint8_t) (168 + i);
            for (uint32_t i = 0; i < k; i++) {
                uint8_t row[20];
                stream_seg_row(&m, segbuf, len, f, ids[i], row);
                if (ids[i] >= 168) used_ext++;
                stream_dec_add(&d, ids[i], row);
            }
            trials++;
            ok += stream_dec_ready(&d) &&
                  stream_dec_finish(&d, outbuf, sizeof outbuf) == STREAM_OK &&
                  memcmp(outbuf, segbuf + FREIGHT_CAPACITY(k) * f, stream_freight_len(&m, f)) == 0;
        }
    }
    CHECK(ok == trials && used_ext > 1000,
          "any k distinct ids of 0..255 rebuild the freight (ids 168..255 are fresh MDS parity)");

    /* RLNC beyond 255: probabilistic */
    int need_extra = 0, rl_ok = 0, rl_trials = 0;
    for (uint32_t t = 0; t < 40; t++) {
        uint8_t k = 128;
        uint32_t len = FREIGHT_CAPACITY(k);
        rnd_fill(segbuf, len);
        static stream_manifest_t m;
        stream_manifest_build(&m, 7, t, 0, 0, 0, k, 0, segbuf, len);
        stream_dec_t d;
        stream_dec_init(&d, &m, 0, dec_work, sizeof dec_work);
        uint32_t sent = 0;
        for (uint32_t id = 256 + t * 1000; !stream_dec_ready(&d) && sent < (uint32_t) k + 8;
             id++, sent++) {
            uint8_t row[20];
            stream_seg_row(&m, segbuf, len, 0, id, row);
            stream_dec_add(&d, id, row);
        }
        rl_trials++;
        need_extra += (int) (sent - k);
        rl_ok += stream_dec_finish(&d, outbuf, sizeof outbuf) == STREAM_OK &&
                 memcmp(outbuf, segbuf, len) == 0;
    }
    printf("  RLNC ids >= 256: %d/%d decoded, %d extra rows over %d freights\n", rl_ok, rl_trials,
           need_extra, rl_trials);
    CHECK(rl_ok == rl_trials && need_extra <= 4, "RLNC rows (ids >= 256) decode with ~k rows");

    /* redundancy, corruption, too few */
    {
        uint8_t k = 32;
        uint32_t len = FREIGHT_CAPACITY(k);
        rnd_fill(segbuf, len);
        static stream_manifest_t m;
        stream_manifest_build(&m, 8, 0, 0, 0, 0, k, 0, segbuf, len);
        stream_dec_t d;
        stream_dec_init(&d, &m, 0, dec_work, sizeof dec_work);
        uint8_t row[20];
        stream_seg_row(&m, segbuf, len, 0, 3, row);
        int a = stream_dec_add(&d, 3, row), b = stream_dec_add(&d, 3, row);
        CHECK(a == 1 && b == 0, "a repeated id is reported redundant");
        CHECK(stream_dec_finish(&d, outbuf, sizeof outbuf) == STREAM_ERR_TOO_FEW, "too few rows");
        for (uint32_t id = 200; id < 200u + k - 1u; id++) {
            stream_seg_row(&m, segbuf, len, 0, id, row);
            if (id == 210) row[4] ^= 0x40; /* a lying peer */
            stream_dec_add(&d, id, row);
        }
        CHECK(stream_dec_ready(&d) &&
                  stream_dec_finish(&d, outbuf, sizeof outbuf) == STREAM_ERR_HASH,
              "one corrupted row is caught by the freight SHA-256");
    }
}

/* ===================================================================== */
/* 4. rebuild from rows gathered from random peers                        */
/* ===================================================================== */

static void test_random_peers(void)
{
    printf("\n== reconstruction from many uncoordinated peers ==\n");
    int ok = 0, total = 0;
    uint64_t dups = 0, rows = 0;
    for (uint32_t t = 0; t < 50; t++) {
        const uint8_t k = 128;
        const uint32_t len = FREIGHT_CAPACITY(k) * 4;
        seg_data(1000 + t, segbuf, len);
        static stream_manifest_t m;
        stream_manifest_build(&m, 9, t, 0, 0, 0, k, 0, segbuf, len);
        /* 12 peers: 3 complete (mint any id), 9 partial with 15..60 random ids */
        uint8_t hold[12][256];
        for (int p = 0; p < 12; p++)
            for (int i = 0; i < 256; i++) hold[p][i] = p < 3 ? 1 : rndn(100) < 6 + (uint32_t) p * 2;
        for (uint32_t f = 0; f < m.nfreights; f++) {
            stream_dec_t d;
            stream_dec_init(&d, &m, f, dec_work, sizeof dec_work);
            uint8_t got[256] = {0};
            while (!stream_dec_ready(&d)) {
                int p = (int) rndn(12);
                int id = (int) rndn(256);
                if (!hold[p][id]) continue;
                uint8_t row[20];
                stream_seg_row(&m, segbuf, len, f, (uint32_t) id, row);
                rows++;
                if (got[id]) dups++; /* uncoordinated peers do resend: the decoder skips */
                got[id] = 1;
                stream_dec_add(&d, (uint32_t) id, row);
            }
            total++;
            ok += stream_dec_finish(&d, outbuf + FREIGHT_CAPACITY(k) * f,
                                    stream_freight_len(&m, f)) == STREAM_OK;
        }
        ok += 0;
        QCHECK(stream_seg_verify(&m, outbuf, len) == STREAM_OK, "joined segment matches CID");
    }
    printf("  %d freights, %llu rows received, %llu duplicates skipped\n", total,
           (unsigned long long) rows, (unsigned long long) dups);
    CHECK(ok == total && quiet_fail == 0,
          "every freight rebuilds from rows of random peers; joined segments match their CIDs");
}

/* ===================================================================== */
/* 5. scheduler invariants on hand-built nodes                            */
/* ===================================================================== */

static ssw_node_t nodes[8];
static ssw_advert_t adv;
static uint8_t advbuf[SSW_ADV_MAX];

static void give_advert(ssw_node_t *to, const ssw_node_t *from)
{
    int len = ssw_advert_build(from, advbuf, sizeof advbuf);
    int pi = ssw_peer_find(to, from->self_id);
    if (len > 0 && pi >= 0 && ssw_advert_parse(advbuf, (uint32_t) len, &adv) == SSW_OK)
        ssw_advert_apply(to, pi, &adv);
}

static void mk_node(ssw_node_t *n, uint32_t id, uint32_t up, uint32_t dn, uint8_t mode,
                    uint32_t start, bool origin)
{
    ssw_config_t c;
    ssw_config_default(&c);
    c.up_rows_per_sec = up;
    c.dn_rows_per_sec = dn;
    c.mode = mode;
    c.start_seq = start;
    c.is_origin = origin;
    c.startup_segs = 1;
    ssw_init(n, &c, 42, id, 0);
}

/* a holder: complete for seqs [a, b) */
static void fill_complete(ssw_node_t *n, uint32_t a, uint32_t b)
{
    for (uint32_t q = a; q < b; q++) {
        ssw_seg_known(n, q, 128, 4);
        for (uint8_t f = 0; f < 4; f++)
            for (uint8_t pc = 0; pc < 16; pc++) {
                int pi = 0;
                while (!n->peer[pi].used) pi++;
                ssw_on_piece(n, pi, q, f, pc, SSW_TAG_PUSH);
            }
    }
}

static uint64_t tithe_rows = 0;
static uint32_t boosted = 0;
static void tithe(void *ctx, uint32_t from, uint32_t to, uint32_t rows)
{
    (void) ctx;
    (void) from;
    (void) to;
    tithe_rows += rows;
}
static uint32_t weight(void *ctx, uint32_t peer, uint32_t base)
{
    (void) ctx;
    return peer == boosted ? 8 : base;
}

static void test_scheduler(void)
{
    printf("\n== scheduler invariants ==\n");
    ssw_req_t rq[512];
    ssw_send_t sd[1024];

    /* (a) upload cap + DRR fairness + hooks + no monopoly */
    {
        ssw_node_t *u = &nodes[0];
        mk_node(u, 100, 800, 0, SSW_MODE_LIVE, 0, false);
        u->cfg.tithe_hook = tithe;
        u->cfg.weight_hook = weight;
        /* content arrives from a peer that then leaves, so no requester has
         * earned reciprocity credit and all base weights are equal */
        ssw_peer_add(u, 199, false);
        fill_complete(u, 0, 4);
        ssw_peer_remove(u, 199);
        for (uint32_t i = 0; i < 6; i++) ssw_peer_add(u, 200 + i, false);
        boosted = 205;
        uint64_t served[6] = {0};
        uint32_t window[100] = {0}, wsum = 0, worst = 0;
        uint64_t sent = 0;
        uint16_t tag = 0;
        for (uint32_t t = 1; t <= 3000; t++) {
            ssw_tick(u, t);
            for (int p = 0; p < 6; p++)
                for (int r = 0; r < 4; r++) /* everyone floods: every queue stays non-empty */
                    ssw_on_request(u, p, rndn(4), (uint8_t) rndn(4), (uint8_t) rndn(32),
                                   (uint16_t) (tag++ & 0x7FFF), false);
            uint32_t ns = ssw_upload(u, sd, 1024), rows = 0;
            for (uint32_t i = 0; i < ns; i++)
                if (sd[i].kind == SSW_SEND_DATA) {
                    served[sd[i].peer]++;
                    rows += SSW_PIECE_ROWS;
                }
            sent += rows;
            wsum = wsum - window[t % 100] + rows;
            window[t % 100] = rows;
            if (wsum > worst) worst = wsum;
        }
        printf("  upload cap 800 rows/s: worst 1-s window %u rows, mean %llu rows/s\n", worst,
               (unsigned long long) (sent / 30));
        CHECK(worst <= 800 + 2 * 8 + 8 && sent / 30 >= 780,
              "upload cap is never exceeded (and is used)");
        CHECK(tithe_rows == sent, "tithe hook told about every uploaded row");
        uint64_t mn = served[0], mx = served[0];
        for (int p = 0; p < 5; p++) {
            if (served[p] < mn) mn = served[p];
            if (served[p] > mx) mx = served[p];
        }
        printf("  served pieces: %llu %llu %llu %llu %llu | boosted %llu\n",
               (unsigned long long) served[0], (unsigned long long) served[1],
               (unsigned long long) served[2], (unsigned long long) served[3],
               (unsigned long long) served[4], (unsigned long long) served[5]);
        CHECK(mx - mn <= mx / 10, "equal weights get equal service (deficit round robin)");
        CHECK(served[5] > 4 * mx && served[5] < 10 * mn && mn > 0,
              "economy hook raises one peer's share but cannot starve the others");
    }

    /* (b) no duplicate requests, nothing after k, reciprocity order */
    {
        ssw_node_t *v = &nodes[1], *h1 = &nodes[2], *h2 = &nodes[3], *h3 = &nodes[4];
        mk_node(v, 1, 500, 100000, SSW_MODE_VOD, 0, false);
        ssw_node_t *hs[3] = {h1, h2, h3};
        for (int i = 0; i < 3; i++) {
            mk_node(hs[i], 10 + (uint32_t) i, 1000, 0, SSW_MODE_LIVE, 0, false);
            ssw_peer_add(hs[i], 99, false);
            fill_complete(hs[i], 0, 3);
            ssw_peer_add(hs[i], 1, false);
            ssw_peer_add(v, 10 + (uint32_t) i, false);
        }
        for (uint32_t q = 0; q < 3; q++) ssw_seg_known(v, q, 128, 4);
        for (int i = 0; i < 3; i++) give_advert(v, hs[i]);
        uint8_t asked[3][4][32] = {{{0}}};
        int twice = 0, after = 0;
        for (uint32_t t = 1; t < 400 && !(ssw_seg_complete(v, 0) && ssw_seg_complete(v, 1) &&
                                          ssw_seg_complete(v, 2));
             t++) {
            ssw_tick(v, t);
            uint32_t nr = ssw_schedule(v, rq, 512);
            for (uint32_t i = 0; i < nr; i++) {
                const ssw_req_t *r = &rq[i];
                if (asked[r->seq][r->fr][r->pc]) twice++;
                asked[r->seq][r->fr][r->pc] = 1;
                if (v->seg[r->seq].fr[r->fr].done) after++;
            }
            for (uint32_t i = 0; i < nr; i++) /* deliver the batch at once (lossless) */
                ssw_on_piece(v, rq[i].peer, rq[i].seq, rq[i].fr, rq[i].pc, rq[i].tag);
        }
        bool done = ssw_seg_complete(v, 0) && ssw_seg_complete(v, 1) && ssw_seg_complete(v, 2);
        uint32_t extra = 0;
        for (int q = 0; q < 3; q++)
            for (int f = 0; f < 4; f++) extra += v->seg[q].fr[f].nhave - 16u;
        CHECK(done && twice == 0, "every piece is requested at most once");
        CHECK(after == 0, "nothing is requested for a freight once k distinct rows are held");
        CHECK(extra <= 12, "at most urgent_extra spare pieces per freight are fetched");
        uint32_t nr = 0;
        for (int t = 0; t < 5; t++) nr += ssw_schedule(v, rq, 512);
        CHECK(nr == 0, "a complete window produces no requests");
    }

    /* (c) deadlines: urgent first, from the fastest peer; (d) rarest first */
    {
        ssw_node_t *v = &nodes[1], *fast = &nodes[2], *slow = &nodes[3], *rare = &nodes[4];
        mk_node(v, 1, 500, 100000, SSW_MODE_VOD, 10, false);
        mk_node(fast, 20, 3000, 0, SSW_MODE_LIVE, 0, false);
        mk_node(slow, 21, 3000, 0, SSW_MODE_LIVE, 0, false);
        mk_node(rare, 22, 3000, 0, SSW_MODE_LIVE, 0, false);
        ssw_peer_add(fast, 99, false);
        ssw_peer_add(slow, 99, false);
        ssw_peer_add(rare, 99, false);
        fill_complete(fast, 10, 20);
        fill_complete(slow, 10, 20);
        /* rare: holds 3 pieces of seg 21 fr 0 only (partial) */
        for (uint32_t q = 10; q < 22; q++) ssw_seg_known(rare, q, 128, 4);
        for (uint8_t pc = 0; pc < 3; pc++) ssw_on_piece(rare, 0, 21, 0, pc, SSW_TAG_PUSH);
        ssw_peer_add(fast, 1, false);
        ssw_peer_add(slow, 1, false);
        ssw_peer_add(rare, 1, false);
        int pf = ssw_peer_add(v, 20, false), ps = ssw_peer_add(v, 21, false);
        ssw_peer_add(v, 22, false);
        for (uint32_t q = 10; q < 22; q++) ssw_seg_known(v, q, 128, 4);
        give_advert(v, fast);
        give_advert(v, slow);
        give_advert(v, rare);
        v->peer[pf].rate = 4000; /* measured: fast */
        v->peer[ps].rate = 100;
        v->peer[pf].infl_cap = 64;
        v->peer[ps].infl_cap = 64;
        ssw_tick(v, 1);
        CHECK(ssw_is_urgent(v, 10) && !ssw_is_urgent(v, 15),
              "startup segment is urgent, later ones are not");
        v->dn_credit = 1 << 30;
        uint32_t nr = ssw_schedule(v, rq, 512);
        bool order = true, seen_normal = false, fastest = true, rare_first = false;
        uint32_t first_normal = nr, last_urg_seq = 0, nurg = 0;
        for (uint32_t i = 0; i < nr; i++) {
            if (!rq[i].urgent) {
                if (!seen_normal) first_normal = i;
                seen_normal = true;
                continue;
            }
            if (seen_normal || rq[i].seq < last_urg_seq) order = false;
            last_urg_seq = rq[i].seq;
            /* the fastest takes urgent work up to its in-flight cap (64);
             * only then does the next fastest get any */
            if ((nurg < 64) != (rq[i].peer == pf)) fastest = false;
            nurg++;
        }
        if (first_normal < nr) rare_first = rq[first_normal].seq == 21;
        printf("  %u requests: %u urgent (seq %u..%u), first normal: seq %u\n", nr, nurg, rq[0].seq,
               last_urg_seq, first_normal < nr ? rq[first_normal].seq : 0);
        CHECK(nr > 0 && order, "urgent requests come first, earliest deadline first");
        CHECK(fastest, "urgent requests fill the fastest neighbour first, then spill to the next");
        CHECK(rare_first, "first non-urgent request is the rarest freight (one partial holder)");
    }

    /* (e) origin only as last resort */
    {
        ssw_node_t *v = &nodes[1], *o = &nodes[2], *h = &nodes[3];
        mk_node(v, 1, 500, 100000, SSW_MODE_VOD, 5, false);
        mk_node(o, 0, 3000, 0, SSW_MODE_LIVE, 0, true);
        mk_node(h, 30, 3000, 0, SSW_MODE_LIVE, 0, false);
        ssw_peer_add(o, 1, false);
        ssw_peer_add(h, 99, false);
        fill_complete(h, 5, 12);
        ssw_peer_add(h, 1, false);
        uint32_t nno;
        ssw_notice_t no[64];
        for (uint32_t q = 5; q < 12; q++) ssw_origin_publish(o, q, 128, 4, no, 64, &nno);
        int po = ssw_peer_add(v, 0, true);
        int ph = ssw_peer_add(v, 30, false);
        for (uint32_t q = 5; q < 12; q++) ssw_seg_known(v, q, 128, 4);
        give_advert(v, o);
        give_advert(v, h);
        ssw_tick(v, 1);
        v->peer[po].rate = 1 << 20; /* the origin looks fastest of all */
        v->peer[ph].infl_cap = 512; /* and the holder has room for everything */
        v->dn_credit = 1 << 30;
        uint32_t nr = ssw_schedule(v, rq, 512), to_origin = 0;
        for (uint32_t i = 0; i < nr; i++) to_origin += rq[i].peer == po;
        CHECK(nr > 0 && to_origin == 0,
              "with another supplier the origin is not asked, even when fastest");
        ssw_peer_remove(v, 30);
        ssw_tick(v, 2);
        v->dn_credit = 1 << 30;
        nr = ssw_schedule(v, rq, 512);
        CHECK(nr == 0,
              "a viewer still filling its startup buffer waits for the swarm, not the origin");
        ssw_tick(v, 2 + 2 * 100); /* nobody near has had it for two segment durations */
        v->dn_credit = 1 << 30;
        nr = ssw_schedule(v, rq, 512);
        bool urgent_only = nr > 0;
        for (uint32_t i = 0; i < nr; i++) urgent_only &= rq[i].peer == po && rq[i].urgent;
        CHECK(urgent_only, "then, with no other supplier, only urgent requests go to the origin");
    }

    /* (f) weighted push with notices */
    {
        ssw_node_t *o = &nodes[0];
        mk_node(o, 0, 100000, 0, SSW_MODE_LIVE, 0, true);
        static const uint32_t ups[4] = {0, 256, 768, 1536};
        for (int i = 0; i < 4; i++) {
            ssw_node_t *v = &nodes[1 + i];
            mk_node(v, 50 + (uint32_t) i, ups[i], 4000, SSW_MODE_LIVE, 0, false);
            ssw_peer_add(o, 50 + (uint32_t) i, false);
            ssw_peer_add(v, 0, true);
            give_advert(o, v); /* carries the declared upload */
        }
        ssw_notice_t no[64];
        uint32_t nno = 0, cnt[4] = {0};
        CHECK(ssw_origin_publish(o, 0, 128, 4, no, 64, &nno) == SSW_OK && nno == 12,
              "publish: notices for the three targets that declared upload");
        bool zero_none = true;
        uint8_t owner[4][32];
        memset(owner, 0xFF, sizeof owner);
        bool unique = true;
        for (uint32_t i = 0; i < nno; i++) {
            int idx = o->peer[no[i].peer].id - 50;
            if (idx == 0) zero_none = false;
            ssw_on_notice(&nodes[1 + idx], 0, 0, no[i].fr, no[i].bm);
            for (uint32_t pc = 0; pc < 32; pc++)
                if ((no[i].bm[pc >> 3] >> (pc & 7)) & 1) {
                    cnt[idx]++;
                    if (owner[no[i].fr][pc] != 0xFF) unique = false;
                    owner[no[i].fr][pc] = (uint8_t) idx;
                }
        }
        printf("  pushed pieces by declared upload 0/256/768/1536: %u %u %u %u\n", cnt[0], cnt[1],
               cnt[2], cnt[3]);
        CHECK(zero_none && unique && cnt[1] + cnt[2] + cnt[3] == 80,
              "each pushed piece has exactly one target, none for zero upload");
        CHECK(cnt[3] > cnt[2] && cnt[2] > cnt[1] && cnt[1] > 0,
              "push share follows declared upload");
        /* push bytes */
        uint64_t pushed = 0;
        for (uint32_t t = 1; t < 50; t++) {
            ssw_tick(o, t);
            uint32_t ns = ssw_upload(o, sd, 1024);
            for (uint32_t i = 0; i < ns; i++) {
                if (sd[i].kind != SSW_SEND_PUSH) continue;
                pushed++;
                int idx = o->peer[sd[i].peer].id - 50;
                ssw_node_t *v = &nodes[1 + idx];
                ssw_seg_known(v, 0, 128, 4);
                ssw_on_piece(v, ssw_peer_find(v, 0), 0, sd[i].fr, sd[i].pc, SSW_TAG_PUSH);
            }
        }
        CHECK(pushed == 80, "origin pushes each of the 20 x 4 pieces once");
        /* a target never asks anyone for a piece announced to it */
        ssw_node_t *v = &nodes[4];
        bool clean = true;
        for (int f = 0; f < 4; f++)
            for (int pc = 0; pc < 32; pc++)
                if (owner[f][pc] == 3) clean &= (v->seg[0].fr[f].have[pc >> 3] >> (pc & 7)) & 1;
        CHECK(clean, "the target holds exactly the pieces its notice announced");
    }

    /* (g) unverified input never moves the window */
    {
        ssw_node_t *v = &nodes[1];
        mk_node(v, 1, 500, 4000, SSW_MODE_LIVE, 0, false);
        int p = ssw_peer_add(v, 9, false);
        ssw_seg_known(v, 3, 128, 4);
        unsigned rc = ssw_on_piece(v, p, 1000000, 0, 0, SSW_TAG_PUSH);
        CHECK((rc & SSW_PC_IGNORED) && v->base == 3, "a piece for a far-future segment is ignored");
    }
}

/* ===================================================================== */
/* 6. simulations                                                         */
/* ===================================================================== */

static void *arena = 0;
static size_t arena_len = 0;

static ssim_t *sim_new(const ssim_params_t *p)
{
    size_t need = ssim_arena_bytes(p);
    if (need > arena_len) {
        free(arena);
        arena_len = (need + 15u) & ~(size_t) 15u;
        arena = aligned_alloc(16, arena_len);
    }
    return ssim_create(p, arena, arena_len);
}

static int run_cs(const ssim_params_t *p, ssim_result_t *r)
{
    size_t need = ssim_cs_arena_bytes(p);
    void *a = aligned_alloc(16, (need + 15u) & ~(size_t) 15u);
    int rc = ssim_run_cs(p, a, need, r);
    free(a);
    return rc;
}

static bool invariants_ok(const ssim_result_t *r)
{
    return r->cap_violations == 0 && r->dn_violations == 0 && r->req_twice == 0 &&
           r->req_after_done == 0 && r->dup_rx == 0 && r->event_overflow == 0;
}

static double per_viewer(const ssim_result_t *r)
{
    return r->active_ticks ? (double) r->useful_rows * 100.0 / (double) r->active_ticks : 0;
}

static double joiner_rate(const ssim_params_t *p, const ssim_result_t *r)
{
    if (!r->probe_done || !r->probe_done_ticks) return 0;
    return (double) p->probe_segs * p->k * p->nfr * 100.0 / r->probe_done_ticks;
}

/* Decode, with real bytes, every complete freight a node holds. */
static int verify_node(const ssw_node_t *n, uint32_t lo, uint32_t hi, int *checked)
{
    int bad = 0;
    for (uint32_t q = lo; q < hi; q++) {
        if (!ssw_seg_complete(n, q)) continue;
        const uint32_t len = 4u * 128u * 20u;
        seg_data(q, segbuf, len);
        static stream_manifest_t m;
        stream_manifest_build(&m, 1, q, 0, 0, 0, 128, 0, segbuf, len);
        for (uint8_t f = 0; f < 4; f++) {
            uint8_t bm[SSW_BM];
            ssw_pieces_held(n, q, f, bm);
            stream_dec_t d;
            stream_dec_init(&d, &m, f, dec_work, sizeof dec_work);
            for (uint32_t pc = 0; pc < SSW_PIECES && !stream_dec_ready(&d); pc++) {
                if (!((bm[pc >> 3] >> (pc & 7)) & 1)) continue;
                for (uint32_t i = 0; i < SSW_PIECE_ROWS; i++) {
                    uint8_t row[20];
                    stream_seg_row(&m, segbuf, len, f, pc * SSW_PIECE_ROWS + i, row);
                    stream_dec_add(&d, pc * SSW_PIECE_ROWS + i, row);
                }
            }
            if (stream_dec_finish(&d, outbuf + f * 2560u, 2560) != STREAM_OK) bad++;
        }
        if (stream_seg_verify(&m, outbuf, len) != STREAM_OK) bad++;
        (*checked)++;
    }
    return bad;
}

#define NSCALE 8
static const uint32_t scale_n[NSCALE] = {1, 2, 4, 8, 16, 32, 64, 128};

static void test_scaling(void)
{
    printf("\n== scaling: more viewers, same origin ==\n");
    ssim_params_t base;
    ssim_defaults(&base);
    base.probe = 1;
    const double R = (double) base.k * base.nfr * 100.0 / base.seg_ticks; /* rows/s */
    printf("  stream R = %.0f rows/s (%.1f kbit/s payload, %.1f kbit/s as UBH-168 frames); origin "
           "uplink %u rows/s = %.1fR\n",
           R, R * 20 * 8 / 1000, R * 21 * 8 / 1000, base.origin_up, base.origin_up / R);
    printf("  viewers: upload 0.5R/1.5R/3R (25/50/25%%), download 4R-16R, latency 2-10 ticks per "
           "link, %u ppm loss; joiner: start-over at seg %u, download cap %.0fR\n\n",
           base.loss_ppm, base.probe_start, base.probe_dn / R);
    printf("  %4s | %5s | %6s %6s | %7s %9s %6s | %6s %6s | %11s | %5s | %7s %8s | %7s %7s\n", "N",
           "cap", "viewer", "aggreg", "startup", "stalled", "stall", "origin", "peers", "joiner",
           "ctl", "CS view", "CS stall", "CS join", "CS orig");
    printf("  %4s | %5s | %6s %6s | %7s %9s %6s | %6s %6s | %11s | %5s | %7s %8s | %7s %7s\n", "",
           "xNR", "xR", "xR", "ms", "sess", "%time", "xR", "xR", "xR (ms)", "%data", "xR", "sess",
           "xR", "xR");
    double pv[NSCALE], agg[NSCALE], org[NSCALE], peers[NSCALE], join[NSCALE], cspv[NSCALE],
        csjoin[NSCALE], startup[NSCALE];
    uint32_t stalled[NSCALE], sessions[NSCALE], csstalled[NSCALE];
    double stall_frac[NSCALE], capr[NSCALE];
    bool inv = true;
    for (int i = 0; i < NSCALE; i++) {
        ssim_params_t p = base;
        p.n_viewers = scale_n[i];
        ssim_t *s = sim_new(&p);
        ssim_result_t r, c;
        ssim_run(s, &r);
        run_cs(&p, &c);
        inv &= invariants_ok(&r);
        const double secs = p.ticks / 100.0;
        double sum_up = p.origin_up;
        for (uint32_t v = 1; v <= p.n_viewers; v++) sum_up += ssim_node(s, v)->cfg.up_rows_per_sec;
        capr[i] = sum_up / (p.n_viewers * R);
        pv[i] = per_viewer(&r) / R;
        agg[i] = pv[i] * scale_n[i];
        org[i] = r.origin_up_rows / secs / R;
        peers[i] = (r.peer_up_rows + r.probe_up_rows) / secs / R;
        join[i] = joiner_rate(&p, &r) / R;
        cspv[i] = per_viewer(&c) / R;
        csjoin[i] = joiner_rate(&p, &c) / R;
        startup[i] = r.started ? (double) r.startup_ticks_sum * 10.0 / r.started : 1e9;
        stalled[i] = r.stalled_sessions;
        sessions[i] = r.sessions;
        csstalled[i] = c.stalled_sessions + c.not_started;
        stall_frac[i] = r.active_ticks ? (double) r.stall_ticks / r.active_ticks : 0;
        const double data = (double) (r.rx_rows + r.probe_rx_rows) * 21.0;
        const double ctl = (double) (r.advert_bytes + r.request_bytes + r.notice_bytes);
        printf(
            "  %4u | %5.2f | %6.2f %6.1f | %7.0f %4u/%-4u %5.2f%% | %6.2f %6.1f | %4.2f (%4.0f) | "
            "%4.0f%% | %7.3f %3u/%-4u | %7.2f %7.2f\n",
            scale_n[i], capr[i], pv[i], agg[i], startup[i], r.stalled_sessions, r.sessions,
            100.0 * stall_frac[i], org[i], peers[i], join[i], r.probe_startup_ticks * 10.0,
            data > 0 ? 100.0 * ctl / data : 0, cspv[i], csstalled[i], c.sessions, csjoin[i],
            c.origin_up_rows / secs / R);
        fflush(stdout);
        if (scale_n[i] == 16) {
            int checked = 0, bad = 0;
            for (uint32_t v = 1; v <= p.n_viewers + 1; v++)
                bad += verify_node(ssim_node(s, v), 0, ssim_produced(s), &checked);
            CHECK(bad == 0 && checked > 300,
                  "every segment the N=16 viewers completed decodes from the pieces they hold");
            printf("  (decoded %d segments from real rows)\n", checked);
        }
    }
    printf("  columns: cap = (origin + sum of viewer uploads) / (N R), the most the swarm could "
           "carry; viewer = useful rate per live viewer; aggreg = sum over viewers; stalled = "
           "sessions that ever stalled; stall = share of watch time stalled; origin, peers = "
           "upload; joiner = start-over catch-up rate (startup ms); ctl = adverts + requests + "
           "notices over data bytes; CS = client-server baseline\n");
    CHECK(inv, "simulator audits: upload caps, download pacing, no id asked twice, nothing after "
               "k, no duplicate arrivals");
    bool live = true, stall_ok = true;
    for (int i = 0; i < NSCALE; i++) {
        live &= pv[i] >= 0.95;
        stall_ok &= stalled[i] * 10 <= sessions[i] * 4 && stall_frac[i] < 0.01;
    }
    CHECK(live, "swarm: every live viewer gets >= 0.95 R at every N (1..128)");
    CHECK(stall_ok,
          "swarm: stalls are < 1% of watch time at every N (but up to 40% of sessions see one)");
    double omin = org[0], omax = org[0];
    for (int i = 0; i < NSCALE; i++) {
        if (org[i] < omin) omin = org[i];
        if (org[i] > omax) omax = org[i];
    }
    CHECK(omax <= omin * 1.35 && omax * R <= base.origin_up,
          "origin upload stays flat (max/min <= 1.35) from N = 1 to 128");
    bool grows = true;
    for (int i = 2; i < NSCALE; i++)
        grows &= peers[i] >= 1.6 * peers[i - 1] && agg[i] >= 1.8 * agg[i - 1];
    CHECK(grows, "aggregate delivered rate and total peer upload rise with N (~2x per doubling)");
    /* Measured, not hoped for: the start-over joiner catches up faster with
     * a bigger audience, but reaches only about half of its 4R download cap
     * (its non-urgent pulls queue behind the neighbours' live traffic). */
    const double small = join[0] > join[1] ? join[0] : join[1];
    bool jr = small > 0;
    for (int i = 0; i < NSCALE; i++) jr &= join[i] <= 1.05 * base.probe_dn / R;
    for (int i = 6; i < NSCALE; i++) jr &= join[i] >= 1.5 && join[i] >= 1.25 * small;
    CHECK(jr, "joiner's catch-up rate rises with N: >= 1.5R and >= 1.25x the N <= 2 rate at "
              "N >= 64 (below its own 4R cap)");
    CHECK(startup[NSCALE - 1] < 1e8, "every live viewer started");
    bool cs = true;
    for (int i = 0; i < NSCALE; i++) {
        cs &= cspv[i] * R <= base.origin_up / (double) scale_n[i] * 1.05 + 1;
        if (scale_n[i] >= 4) cs &= csstalled[i] > 0;
    }
    CHECK(cs, "client-server: per-viewer rate <= origin/N, and from N = 4 viewers stall");
    CHECK(pv[NSCALE - 1] > 20 * cspv[NSCALE - 1],
          "at N = 128 the swarm gives each viewer >20x the client-server rate");
}

static void test_conditions(void)
{
    printf("\n== when it does NOT hold ==\n");
    ssim_params_t p;
    ssim_defaults(&p);
    p.n_viewers = 32;
    const double R = (double) p.k * p.nfr * 100.0 / p.seg_ticks;
    ssim_result_t r;
    /* zero upload */
    ssim_params_t z = p;
    z.free_rider_pct = 100;
    ssim_run(sim_new(&z), &r);
    double pv = per_viewer(&r);
    printf("  32 viewers, all zero upload:   %.2fR per viewer, %u stalls in %u sessions, %u never "
           "started (bound: origin/N = %.2fR)\n",
           pv / R, r.stalls, r.sessions, r.not_started, p.origin_up / 32.0 / R);
    CHECK(pv <= p.origin_up / 32.0 * 1.1 && (r.stalled_sessions + r.not_started) >= 16,
          "zero-upload viewers: the swarm is no better than client-server");
    /* asymmetric uplink-limited */
    ssim_params_t a = p;
    for (int i = 0; i < 4; i++) a.up[i] = 192; /* 0.375R up, 8R down */
    for (int i = 0; i < 4; i++) a.dn[i] = 4096;
    ssim_run(sim_new(&a), &r);
    pv = per_viewer(&r);
    const double bound = (p.origin_up + 32.0 * 192) / 32.0;
    printf("  32 viewers, upload 0.375R:     %.2fR per viewer, %u stalls in %u sessions, %u never "
           "started (bound: (origin + sum up)/N = %.2fR)\n",
           pv / R, r.stalls, r.sessions, r.not_started, bound / R);
    CHECK(
        pv <= bound * 1.05 && r.stalled_sessions + r.not_started >= 16,
        "uplink-limited asymmetric links: rate is capped by (origin + sum up)/N and viewers stall");
    /* a quarter free riders */
    for (int pass = 0; pass < 2; pass++) {
        ssim_params_t f = p;
        f.n_viewers = 64;
        f.free_rider_pct = 25;
        if (pass)
            for (int i = 0; i < 4; i++) f.up[i] = f.up[i] * 3 / 2; /* contributors give 1.5x */
        ssim_t *s = sim_new(&f);
        ssim_run(s, &r);
        double sum_up = f.origin_up;
        for (uint32_t v = 1; v <= f.n_viewers; v++) sum_up += ssim_node(s, v)->cfg.up_rows_per_sec;
        const double fb = sum_up / f.n_viewers;
        pv = per_viewer(&r);
        printf("  64 viewers, 25%% free riders%s: %.2fR per viewer, bound %.2fR, stalled sessions "
               "%u/%u (free riders %u/%u)\n",
               pass ? ", contributors 1.5x up" : "", pv / R, fb / R, r.stalled_sessions, r.sessions,
               r.zero_up_stalled, r.zero_up_sessions);
        if (!pass)
            CHECK(invariants_ok(&r) && pv <= fb * 1.05 && fb < 1.0 * R && pv < 0.9 * R,
                  "free riders that push total upload below demand (bound < 1R): the stream "
                  "fails, as it must");
        else
            CHECK(invariants_ok(&r) && fb >= 1.25 * R && pv >= 0.95 * R,
                  "with enough contributor upload (bound >= 1.25R) the contributors carry 25% "
                  "free riders");
    }
}

static void test_churn(void)
{
    printf("\n== churn ==\n");
    ssim_params_t p;
    ssim_defaults(&p);
    p.n_viewers = 64;
    p.churn_leaves = 32;
    p.churn_from = 1000;
    p.churn_to = 3200;
    p.churn_rejoin = 1;
    p.loss_ppm = 20000;
    const double R = (double) p.k * p.nfr * 100.0 / p.seg_ticks;
    ssim_t *s = sim_new(&p);
    ssim_result_t r;
    ssim_run(s, &r);
    const double pv = per_viewer(&r);
    printf("  64 viewers, 32 departures (each replaced 1 s later), 2%% loss: %u sessions, %.2fR "
           "per viewer, %u stalls, %u stalled sessions, %u never started, %llu timeouts\n",
           r.sessions, pv / R, r.stalls, r.stalled_sessions, r.not_started,
           (unsigned long long) r.timeouts);
    CHECK(invariants_ok(&r), "audits hold under churn");
    CHECK(pv >= 0.9 * R && r.stalled_sessions * 3 <= r.sessions && r.not_started <= 2,
          "churn: rate holds, at most a third of sessions ever stall");
    int checked = 0, bad = 0;
    for (uint32_t v = 1; v <= p.n_viewers; v++)
        if (ssim_active(s, v)) bad += verify_node(ssim_node(s, v), 0, ssim_produced(s), &checked);
    CHECK(bad == 0 && checked > 500, "survivors' and replacements' segments all decode");
}

/* ===================================================================== */
/* 7. fuzzing the parsers                                                 */
/* ===================================================================== */

static void mutate(uint8_t *b, uint32_t *len, uint32_t cap)
{
    uint32_t kind = rndn(6);
    if (kind == 0 && *len) b[rndn(*len)] ^= (uint8_t) (1u << rndn(8));
    if (kind == 1 && *len) b[rndn(*len)] = (uint8_t) rnd();
    if (kind == 2 && *len) *len = rndn(*len + 1);
    if (kind == 3 && *len < cap) b[(*len)++] = (uint8_t) rnd();
    if (kind == 4 && *len > 8) {
        uint32_t i = 4 + rndn(*len - 4);
        b[i] = (uint8_t) (b[i] + 1);
    }
    if (kind == 5)
        for (int i = 0; i < 4 && *len; i++) b[rndn(*len)] ^= (uint8_t) rnd();
}

static void test_fuzz(void)
{
    printf("\n== fuzzing parsers ==\n");
    static uint8_t buf[SSW_ADV_MAX + 64];
    /* adverts: seeds from a real node */
    ssw_node_t *n = &nodes[0];
    mk_node(n, 7, 768, 4000, SSW_MODE_LIVE, 0, false);
    int pi = ssw_peer_add(n, 9, false);
    for (uint32_t q = 0; q < 40; q++) {
        ssw_seg_known(n, q, 128, 4);
        for (uint8_t f = 0; f < 4; f++)
            for (uint32_t c = rndn(20); c; c--)
                ssw_on_piece(n, pi, q, f, (uint8_t) rndn(32), SSW_TAG_PUSH);
    }
    static uint8_t seed[SSW_ADV_MAX];
    int sl = ssw_advert_build(n, seed, sizeof seed);
    CHECK(sl > 0 && ssw_advert_parse(seed, (uint32_t) sl, &adv) == SSW_OK, "advert round trip");
    ssw_node_t *m = &nodes[1];
    mk_node(m, 9, 768, 4000, SSW_MODE_LIVE, 0, false);
    int mp = ssw_peer_add(m, 7, false);
    for (uint32_t q = 0; q < 40; q++) ssw_seg_known(m, q, 128, 4);
    /* delta adverts: a neighbour fed one full advert and then only deltas
     * holds exactly the view a fresh full advert gives */
    {
        ssw_node_t *m2 = &nodes[2];
        mk_node(m2, 9, 768, 4000, SSW_MODE_LIVE, 0, false);
        int m2p = ssw_peer_add(m2, 7, false);
        for (uint32_t q = 0; q < 40; q++) ssw_seg_known(m2, q, 128, 4);
        int l = ssw_advert_next(n, buf, sizeof buf, true);
        bool ok = l > 0 && ssw_advert_parse(buf, (uint32_t) l, &adv) == SSW_OK &&
                  ssw_advert_apply(m, mp, &adv) == SSW_OK;
        uint32_t ndelta = 0, dbytes = 0;
        for (int round = 0; round < 60; round++) {
            for (uint32_t c = rndn(12); c; c--)
                ssw_on_piece(n, pi, rndn(40), (uint8_t) rndn(4), (uint8_t) rndn(32), SSW_TAG_PUSH);
            l = ssw_advert_next(n, buf, sizeof buf, false);
            if (l == 0) continue;
            ok &= l > 0 && ssw_advert_parse(buf, (uint32_t) l, &adv) == SSW_OK &&
                  (adv.flags & SSW_ADV_DELTA) && ssw_advert_apply(m, mp, &adv) == SSW_OK;
            ndelta++;
            dbytes += (uint32_t) l;
        }
        l = ssw_advert_build(n, buf, sizeof buf);
        ok &= l > 0 && ssw_advert_parse(buf, (uint32_t) l, &adv) == SSW_OK &&
              ssw_advert_apply(m2, m2p, &adv) == SSW_OK;
        for (uint32_t q = 0; q < 40 && ok; q++)
            for (int f = 0; f < 4; f++) {
                const ssw_vfr_t *a1 = &m->peer[mp].v[q].fr[f], *a2 = &m2->peer[m2p].v[q].fr[f];
                ok &= a1->st == a2->st && memcmp(a1->bm, a2->bm, SSW_BM) == 0 &&
                      m->seg[q].fr[f].tot == m2->seg[q].fr[f].tot &&
                      m->seg[q].fr[f].ncomp == m2->seg[q].fr[f].ncomp;
            }
        printf("  %u deltas, %u bytes on average (full advert: %d bytes)\n", ndelta,
               ndelta ? dbytes / ndelta : 0, l);
        /* a delta before any full advert is refused */
        ssw_peer_add(m2, 8, false);
        int x = ssw_advert_next(n, buf, sizeof buf, false);
        ssw_on_piece(n, pi, 3, 1, 5, SSW_TAG_PUSH);
        x = ssw_advert_next(n, buf, sizeof buf, false);
        bool refused = x <= 0 || (ssw_advert_parse(buf, (uint32_t) x, &adv) == SSW_OK &&
                                  ssw_advert_apply(m2, ssw_peer_find(m2, 8), &adv) == SSW_ERR_ARG);
        CHECK(ok && ndelta > 20 && refused,
              "delta adverts keep a neighbour's view (and rarity counts) equal to a full advert");
    }
    static uint8_t dseed[SSW_ADV_MAX];
    for (uint32_t c = 6; c; c--)
        ssw_on_piece(n, pi, rndn(40), (uint8_t) rndn(4), (uint8_t) rndn(32), SSW_TAG_PUSH);
    int dl = ssw_advert_next(n, dseed, sizeof dseed, false);
    CHECK(dl > 0 && ssw_advert_parse(dseed, (uint32_t) dl, &adv) == SSW_OK, "delta round trip");
    uint32_t acc = 0, total = 0;
    for (uint32_t i = 0; i < 25000; i++, total++) {
        uint32_t len;
        if (i & 1) {
            len = (uint32_t) ((i & 2) ? dl : sl);
            memcpy(buf, (i & 2) ? dseed : seed, len);
            for (uint32_t k = 1 + rndn(3); k; k--) mutate(buf, &len, sizeof buf);
        } else {
            len = rndn(200);
            rnd_fill(buf, len);
            if (len >= 4) memcpy(buf, "ZXSA", 4);
            if (len > 4) buf[4] = 1;
            if (len > 5) buf[5] &= 3;
        }
        if (ssw_advert_parse(buf, len, &adv) == SSW_OK) {
            acc++;
            adv.sender = 7;
            adv.stream_id = 42;
            ssw_advert_apply(m, mp, &adv);
        }
    }
    /* counts stay consistent: recompute from views */
    bool consistent = true;
    for (uint32_t s2 = 0; s2 < SSW_WIN; s2++) {
        const ssw_oseg_t *o = &m->seg[s2];
        if (!o->used) continue;
        for (int f = 0; f < 4; f++) {
            uint32_t cnt = 0;
            const ssw_vseg_t *v = &m->peer[mp].v[s2];
            if (v->seq == o->seq && v->fr[f].st == SSW_ST_PARTIAL)
                for (int b = 0; b < SSW_BM * 8; b++) cnt += (v->fr[f].bm[b >> 3] >> (b & 7)) & 1;
            consistent &= cnt == o->fr[f].tot;
        }
    }
    printf("  adverts: %u inputs, %u accepted\n", total, acc);
    CHECK(total >= 20000 && consistent,
          "advert parser + apply: no crash, rarity counts stay exact");

    /* requests */
    ssw_req_t rq[SSW_REQ_MAX], rq2[SSW_REQ_MAX];
    for (int i = 0; i < 20; i++) {
        rq[i].seq = (uint32_t) rnd();
        rq[i].fr = (uint8_t) rndn(4);
        rq[i].pc = (uint8_t) rndn(32);
        rq[i].tag = (uint16_t) rndn(0xFFFF);
        rq[i].urgent = (uint8_t) rndn(2);
        rq[i].peer = 0;
    }
    int rl = ssw_req_encode(42, rq, 20, seed, sizeof seed);
    uint64_t sid;
    uint32_t cnt;
    bool rsame = rl > 0 && ssw_req_parse(seed, (uint32_t) rl, &sid, rq2, 64, &cnt) == SSW_OK &&
                 cnt == 20 && sid == 42;
    for (int i = 0; rsame && i < 20; i++) /* field by field: the struct has padding */
        rsame = rq[i].seq == rq2[i].seq && rq[i].fr == rq2[i].fr && rq[i].pc == rq2[i].pc &&
                rq[i].tag == rq2[i].tag && rq[i].urgent == rq2[i].urgent;
    CHECK(rsame, "request round trip");
    acc = 0;
    bool canon = true;
    for (uint32_t i = 0; i < 25000; i++) {
        uint32_t len = (uint32_t) rl;
        memcpy(buf, seed, len);
        if (i % 3 == 0) {
            len = rndn(64 * 9 + 20);
            rnd_fill(buf, len);
            if (len >= 6) memcpy(buf, "ZXSQ\001", 5);
            if (len >= 6) buf[5] = (uint8_t) ((len - 14) / 9);
        } else {
            for (uint32_t k = 1 + rndn(3); k; k--) mutate(buf, &len, sizeof buf);
        }
        if (ssw_req_parse(buf, len, &sid, rq2, 64, &cnt) == SSW_OK) {
            acc++;
            static uint8_t re[SSW_REQ_HDR + 64 * SSW_REQ_ENTRY];
            int l2 = ssw_req_encode(sid, rq2, cnt, re, sizeof re);
            canon &= l2 == (int) len && memcmp(re, buf, len) == 0;
        }
    }
    printf("  requests: 25000 inputs, %u accepted\n", acc);
    CHECK(canon, "request parser: accepted inputs re-encode to the same bytes");

    /* notices */
    uint8_t bm[SSW_BM] = {1, 2, 3, 4}, bm2[SSW_BM];
    int nl = ssw_notice_encode(42, 9, 2, bm, seed, sizeof seed);
    uint32_t sq;
    uint8_t fr;
    CHECK(nl == SSW_NOTICE_LEN &&
              ssw_notice_parse(seed, (uint32_t) nl, &sid, &sq, &fr, bm2) == SSW_OK && sq == 9 &&
              fr == 2 && !memcmp(bm, bm2, 4),
          "notice round trip");
    acc = 0;
    canon = true;
    for (uint32_t i = 0; i < 25000; i++) {
        uint32_t len = (uint32_t) nl;
        memcpy(buf, seed, len);
        for (uint32_t k = 1 + rndn(3); k; k--) mutate(buf, &len, sizeof buf);
        if (ssw_notice_parse(buf, len, &sid, &sq, &fr, bm2) == SSW_OK) {
            acc++;
            uint8_t re[SSW_NOTICE_LEN];
            canon &= ssw_notice_encode(sid, sq, fr, bm2, re, sizeof re) == (int) len &&
                     !memcmp(re, buf, len);
        }
    }
    printf("  notices: 25000 inputs, %u accepted\n", acc);
    CHECK(canon, "notice parser: accepted inputs re-encode to the same bytes");

    /* pieces */
    uint8_t frames[SSW_PIECE_ROWS * 21], fr2[SSW_PIECE_ROWS * 21];
    rnd_fill(frames, sizeof frames);
    for (int i = 0; i < 8; i++) frames[i * 21] = (uint8_t) (5 * 8 + i);
    int pl = ssw_piece_encode(42, 77, 3, 5, false, 1234, frames, seed, sizeof seed);
    uint8_t pc;
    bool pu;
    uint16_t tg;
    CHECK(pl == (int) SSW_PIECE_LEN &&
              ssw_piece_parse(seed, (uint32_t) pl, &sid, &sq, &fr, &pc, &pu, &tg, fr2) == SSW_OK &&
              sq == 77 && fr == 3 && pc == 5 && !pu && tg == 1234 &&
              !memcmp(frames, fr2, sizeof frames),
          "piece round trip (8 UBH-168 frames)");
    acc = 0;
    canon = true;
    for (uint32_t i = 0; i < 25000; i++) {
        uint32_t len = (uint32_t) pl;
        memcpy(buf, seed, len);
        for (uint32_t k = 1 + rndn(3); k; k--) mutate(buf, &len, sizeof buf);
        if (ssw_piece_parse(buf, len, &sid, &sq, &fr, &pc, &pu, &tg, fr2) == SSW_OK) {
            acc++;
            uint8_t re[SSW_PIECE_LEN];
            canon &= ssw_piece_encode(sid, sq, fr, pc, pu, tg, fr2, re, sizeof re) == (int) len &&
                     !memcmp(re, buf, len);
        }
    }
    printf("  pieces: 25000 inputs, %u accepted\n", acc);
    CHECK(canon, "piece parser: accepted inputs re-encode to the same bytes");

    /* manifests (accept-all verifier, to reach past the signature) */
    toy_key_t key;
    rnd_fill(key.key, 32);
    static uint8_t d0[10240];
    seg_data(3, d0, sizeof d0);
    static stream_manifest_t mf, mp2;
    stream_manifest_build(&mf, 77, 3, 0, 1000000, 0, 128, 0, d0, sizeof d0);
    static uint8_t ms[STREAM_MANIFEST_MAX], mb[STREAM_MANIFEST_MAX + 8];
    int ml = stream_manifest_serialize(&mf, ms, sizeof ms, toy_sign, &key, 0);
    acc = 0;
    uint32_t sig_ok = 0;
    for (uint32_t i = 0; i < 25000; i++) {
        uint32_t len = (uint32_t) ml;
        memcpy(mb, ms, len);
        for (uint32_t k = 1 + rndn(3); k; k--) mutate(mb, &len, sizeof mb);
        if (stream_manifest_parse(mb, len, &mp2, toy_verify, &key, 0) == STREAM_OK) sig_ok++;
        if ((i & 3) == 0) { /* structure only: refuse every signature */
            if (stream_manifest_parse(mb, len, &mp2, toy_verify, &key, 0) == STREAM_ERR_SIG) acc++;
        }
    }
    printf("  manifests: 25000 inputs, %u passed the signature (unchanged copies only)\n", sig_ok);
    CHECK(sig_ok < 25000 / 6 + 200, "manifest parser: mutated signed manifests are refused");
    /* unchanged-copy acceptances happen when a mutation is a no-op; check one */
    CHECK(stream_manifest_parse(ms, (uint32_t) ml, &mp2, toy_verify, &key, 0) == STREAM_OK,
          "the original manifest still parses");
}

int main(int argc, char **argv)
{
    const bool quick = argc > 1 && !strcmp(argv[1], "quick");
    test_segmentation();
    test_manifest();
    test_freight_rows();
    test_random_peers();
    test_scheduler();
    test_fuzz();
    if (!quick) {
        test_scaling();
        test_conditions();
        test_churn();
    }
    free(arena);
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
