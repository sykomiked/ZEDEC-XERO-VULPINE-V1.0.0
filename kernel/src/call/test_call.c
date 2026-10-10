/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* test_call.c — host test for the real-time call engine (kernel/src/call).
 *
 * Build and run from the repository root (gcc 11 or newer):
 *
 *   gcc -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
 *       -fno-sanitize-recover=all -Ikernel/src/call \
 *       kernel/src/call/test_call.c kernel/src/call/call_rtp.c \
 *       kernel/src/call/call_jitter.c kernel/src/call/call_cc.c \
 *       kernel/src/call/call_fec.c kernel/src/call/call_mesh.c \
 *       kernel/src/call/call_ice.c kernel/src/call/call_session.c \
 *       -o /tmp/test_call && /tmp/test_call
 *
 * Everything is deterministic: one xorshift PRNG seeds the simulated
 * network (loss, delay jitter and so reordering, bandwidth caps with a
 * drop-tail queue) and all random inputs. The test is hosted code and may
 * use libc and doubles for printing; the library under test may not.
 *
 * Sections: packetisation + UBH carriage, jitter buffer, congestion control,
 * FEC, group mesh (2..32 members), session state machine (timeouts, glare,
 * busy, hold, lossy signalling), ICE/NAT traversal + rendezvous, fuzzing of
 * every wire format (>= 20000 inputs each).
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "call_cc.h"
#include "call_fec.h"
#include "call_ice.h"
#include "call_jitter.h"
#include "call_mesh.h"
#include "call_rtp.h"
#include "call_session.h"

static int g_fail, g_checks;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        g_checks++;                                                                                \
        if (!(c)) {                                                                                \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                  \
        }                                                                                          \
    } while (0)

/* ---------------- deterministic PRNG ---------------- */

static uint64_t g_rng = 0x2545F4914F6CDD1DULL;
static uint64_t rng64(void)
{
    uint64_t x = g_rng;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng = x;
    return x;
}
static uint32_t rng32(void)
{
    return (uint32_t) (rng64() >> 32);
}
static uint32_t rnd_below(uint32_t n)
{
    return n ? (uint32_t) (rng64() % n) : 0;
}
static int chance_ppm(uint32_t ppm)
{
    return rnd_below(1000000) < ppm;
}

/* ---------------- network simulator ---------------- */

#define NS_MAX_PKT 2600
#define NS_MAX_Q   4096

typedef struct ns_pkt {
    uint64_t at_us;
    uint32_t len;
    uint32_t tag;
    uint8_t data[NS_MAX_PKT];
} ns_pkt_t;

typedef struct netsim {
    ns_pkt_t *q;
    uint32_t n;
    uint64_t link_free_us;
    uint64_t rate_bps; /* 0 = unlimited */
    uint64_t base_us;
    uint64_t jitter_us;
    uint32_t loss_ppm;
    uint64_t queue_limit_us;
    uint32_t sent, lost_random, lost_queue, delivered;
} netsim_t;

static void ns_init(netsim_t *ns, uint64_t rate_bps, uint64_t base_us, uint64_t jitter_us,
                    uint32_t loss_ppm, uint64_t queue_limit_us)
{
    if (!ns->q) ns->q = calloc(NS_MAX_Q, sizeof(ns_pkt_t));
    ns->n = 0;
    ns->link_free_us = 0;
    ns->rate_bps = rate_bps;
    ns->base_us = base_us;
    ns->jitter_us = jitter_us;
    ns->loss_ppm = loss_ppm;
    ns->queue_limit_us = queue_limit_us;
    ns->sent = ns->lost_random = ns->lost_queue = ns->delivered = 0;
}

static void ns_free(netsim_t *ns)
{
    free(ns->q);
    ns->q = NULL;
}

/* returns 1 if queued */
static int ns_send(netsim_t *ns, uint64_t now_us, const uint8_t *d, uint32_t len, uint32_t tag)
{
    ns->sent++;
    if (len > NS_MAX_PKT || ns->n >= NS_MAX_Q) {
        ns->lost_queue++;
        return 0;
    }
    uint64_t depart = now_us;
    if (ns->rate_bps) {
        uint64_t start = ns->link_free_us > now_us ? ns->link_free_us : now_us;
        if (start - now_us > ns->queue_limit_us) {
            ns->lost_queue++;
            return 0;
        }
        depart = start + (uint64_t) len * 8u * 1000000u / ns->rate_bps;
        ns->link_free_us = depart;
    }
    if (chance_ppm(ns->loss_ppm)) { /* lost after the bottleneck */
        ns->lost_random++;
        return 0;
    }
    ns_pkt_t *p = &ns->q[ns->n++];
    p->at_us = depart + ns->base_us + (ns->jitter_us ? rng64() % (ns->jitter_us + 1) : 0);
    p->len = len;
    p->tag = tag;
    memcpy(p->data, d, len);
    return 1;
}

/* pop the earliest packet due at or before now; returns 0 if none */
static int ns_pop(netsim_t *ns, uint64_t now_us, ns_pkt_t *out)
{
    int best = -1;
    for (uint32_t i = 0; i < ns->n; i++)
        if (ns->q[i].at_us <= now_us && (best < 0 || ns->q[i].at_us < ns->q[best].at_us))
            best = (int) i;
    if (best < 0) return 0;
    *out = ns->q[best];
    ns->q[best] = ns->q[--ns->n];
    ns->delivered++;
    return 1;
}

/* ================================================================
 * 1. packetisation and UBH carriage
 * ================================================================ */

typedef struct pk_sink {
    uint8_t pkts[300][1600];
    uint32_t lens[300];
    uint32_t n;
} pk_sink_t;

static int sink_emit(void *ctx, const uint8_t *pkt, uint32_t len)
{
    pk_sink_t *s = ctx;
    if (s->n >= 300 || len > 1600) return CALL_ERR_SPACE;
    memcpy(s->pkts[s->n], pkt, len);
    s->lens[s->n++] = len;
    return 0;
}

static void test_rtp(void)
{
    printf("[rtp] packetisation and carriage\n");
    static pk_sink_t sink;
    static uint8_t frame[260000], scratch[1600], wire[3000], back[3000];
    static call_reasm_slot_t slots[4];
    static uint8_t mem[4 * 262144];
    call_reasm_t r;
    call_reasm_init(&r, slots, 4, mem, 262144);
    uint32_t mtus[] = {120, 576, 1200};
    uint64_t pay_bytes = 0, plain_bytes = 0, ubh_bytes = 0;
    uint32_t frames_ok = 0, frames_total = 0;
    for (uint32_t mi = 0; mi < 3; mi++) {
        for (uint32_t trial = 0; trial < 120; trial++) {
            uint32_t len = trial < 40 ? trial + 1 : 1 + rnd_below(mtus[mi] * 40);
            for (uint32_t i = 0; i < len; i++) frame[i] = (uint8_t) rng32();
            call_packetizer_t p;
            call_packetizer_init(&p, 0xA1B2C3D4u, CALL_PT_VP8, 2, (uint16_t) mtus[mi],
                                 (uint16_t) (65530 + trial));
            sink.n = 0;
            int np = call_packetize(&p, frame, len, 90000u * trial, trial % 7 == 0, scratch,
                                    sizeof(scratch), sink_emit, &sink);
            CHECK(np > 0);
            CHECK((uint32_t) np == sink.n);
            frames_total++;
            int done = 0;
            call_frame_t f;
            /* deliver in reverse to exercise reassembly order independence */
            for (int k = (int) sink.n - 1; k >= 0; k--) {
                CHECK(sink.lens[k] <= mtus[mi]);
                call_carriage_t c = (k & 1) ? CALL_CARRIAGE_UBH168 : CALL_CARRIAGE_PLAIN;
                int w = call_rtp_encap(c, sink.pkts[k], sink.lens[k], wire, sizeof(wire));
                CHECK(w > 0);
                CHECK((uint32_t) w == call_rtp_wire_len(c, sink.lens[k]));
                if (c == CALL_CARRIAGE_UBH168) {
                    CHECK(w % 21 == 0);
                    ubh_bytes += (uint32_t) w;
                    plain_bytes += sink.lens[k];
                }
                int b = call_rtp_decap(c, wire, (uint32_t) w, back, sizeof(back), 0);
                CHECK(b == (int) sink.lens[k]);
                CHECK(memcmp(back, sink.pkts[k], sink.lens[k]) == 0);
                call_rtp_hdr_t h;
                const uint8_t *pl;
                int n = call_rtp_parse(back, (uint32_t) b, &h, &pl);
                CHECK(n > 0);
                CHECK(h.ssrc == 0xA1B2C3D4u && h.pt == CALL_PT_VP8 && h.layer == 2);
                CHECK(h.seq == (uint16_t) (65530 + trial + k));
                pay_bytes += (uint32_t) n;
                int rr = call_reasm_push(&r, &h, pl, &f);
                CHECK(rr >= 0);
                if (rr == 1) done = 1;
            }
            if (done && f.len == len && memcmp(f.data, frame, len) == 0 &&
                f.keyframe == (trial % 7 == 0))
                frames_ok++;
        }
    }
    CHECK(frames_ok == frames_total);
    printf("  %u/%u frames reassembled byte-exact (MTU 120/576/1200, seq wrap, reverse order)\n",
           frames_ok, frames_total);
    printf("  UBH-168 carriage: %llu packet bytes -> %llu wire bytes (+%.2f%%)\n",
           (unsigned long long) plain_bytes, (unsigned long long) ubh_bytes,
           100.0 * ((double) ubh_bytes - (double) plain_bytes) / (double) plain_bytes);

    /* fragment limit: 255 fragments ok, 256 refused */
    {
        call_packetizer_t p;
        call_packetizer_init(&p, 1, CALL_PT_OPUS, 0, 120, 0);
        static pk_sink_t big;
        big.n = 0;
        int np = call_packetize(&p, frame, 255 * 100, 0, false, scratch, sizeof(scratch), sink_emit,
                                &big);
        CHECK(np == 255);
        big.n = 0;
        np = call_packetize(&p, frame, 255 * 100 + 1, 0, false, scratch, sizeof(scratch), sink_emit,
                            &big);
        CHECK(np == CALL_ERR_SPACE);
    }
    /* corruption is caught */
    {
        call_packetizer_t p;
        call_packetizer_init(&p, 7, CALL_PT_OPUS, 0, 200, 0);
        sink.n = 0;
        call_packetize(&p, frame, 150, 0, false, scratch, sizeof(scratch), sink_emit, &sink);
        uint32_t caught = 0;
        for (uint32_t bit = 0; bit < 20 * 8; bit++) {
            memcpy(back, sink.pkts[0], sink.lens[0]);
            back[bit / 8] ^= (uint8_t) (1u << (bit % 8));
            call_rtp_hdr_t h;
            if (call_rtp_parse(back, sink.lens[0], &h, NULL) < 0) caught++;
        }
        CHECK(caught == 160);
        printf("  header bit flips detected: %u/160\n", caught);
        /* UBH: bad tag, bad padding, truncated */
        int w =
            call_rtp_encap(CALL_CARRIAGE_UBH168, sink.pkts[0], sink.lens[0], wire, sizeof(wire));
        wire[0] ^= 1;
        CHECK(call_rtp_decap(CALL_CARRIAGE_UBH168, wire, (uint32_t) w, back, sizeof(back), 0) < 0);
        wire[0] ^= 1;
        wire[w - 1] = 0x55; /* padding byte */
        CHECK(call_rtp_decap(CALL_CARRIAGE_UBH168, wire, (uint32_t) w, back, sizeof(back), 0) < 0);
        CHECK(call_rtp_decap(CALL_CARRIAGE_UBH168, wire, (uint32_t) w - 21, back, sizeof(back), 0) <
              0);
    }
    /* negotiation via the capability bit */
    CHECK(call_rtp_negotiate(CALL_CAP_UBH168 | CALL_CAP_FEC, CALL_CAP_UBH168) ==
          CALL_CARRIAGE_UBH168);
    CHECK(call_rtp_negotiate(CALL_CAP_UBH168, CALL_CAP_FEC) == CALL_CARRIAGE_PLAIN);
    CHECK(call_rtp_negotiate(0, CALL_CAP_UBH168) == CALL_CARRIAGE_PLAIN);
    (void) pay_bytes;
}

/* ================================================================
 * 2. jitter buffer
 * ================================================================ */

typedef struct jb_result {
    uint32_t jitter_us, delay_ms, played, concealed, late, order_errors, lost_net, max_depth;
    double mean_latency_ms;
} jb_result_t;

static jb_result_t run_jitter(uint32_t jitter_ms, uint32_t loss_ppm, uint16_t seq0,
                              uint32_t seconds)
{
    static netsim_t ns;
    ns_init(&ns, 0, 30000, (uint64_t) jitter_ms * 1000u, loss_ppm, 0);
    static call_jb_slot_t slots[256];
    static uint8_t mem[256 * 200];
    call_jb_t jb;
    CHECK(call_jb_init(&jb, slots, 256, mem, 200, 48, 20, 400));
    jb_result_t res;
    memset(&res, 0, sizeof(res));
    uint32_t npk = seconds * 50;
    uint32_t sent = 0;
    int32_t last_seq = -1;
    uint16_t expect = seq0;
    int have_expect = 0;
    double lat_sum = 0;
    uint32_t lat_n = 0;
    uint8_t pl[160];
    for (uint64_t t_ms = 0; t_ms < (uint64_t) seconds * 1000u + 1000u; t_ms++) {
        uint64_t now_us = t_ms * 1000u;
        if (t_ms % 20 == 0 && sent < npk) {
            uint16_t seq = (uint16_t) (seq0 + sent);
            uint32_t ts = 12345u + sent * 960u;
            memset(pl, (uint8_t) seq, sizeof(pl));
            memcpy(pl, &seq, 2);
            memcpy(pl + 2, &ts, 4);
            uint32_t ms = (uint32_t) t_ms;
            memcpy(pl + 6, &ms, 4);
            ns_send(&ns, now_us, pl, sizeof(pl), seq);
            sent++;
        }
        ns_pkt_t p;
        while (ns_pop(&ns, now_us, &p)) {
            uint16_t seq;
            uint32_t ts;
            memcpy(&seq, p.data, 2);
            memcpy(&ts, p.data + 2, 4);
            call_jb_push(&jb, seq, ts, true, p.data, p.len, (uint32_t) t_ms);
        }
        call_jb_out_t o;
        call_jb_result_t r;
        while ((r = call_jb_pop(&jb, (uint32_t) t_ms, &o)) != CALL_JB_EMPTY) {
            if (have_expect && o.seq != expect) res.order_errors++;
            have_expect = 1;
            expect = (uint16_t) (o.seq + 1);
            if (last_seq >= 0 && (uint16_t) (o.seq - (uint16_t) last_seq) != 1) res.order_errors++;
            last_seq = o.seq;
            if (r == CALL_JB_PACKET) {
                uint16_t s2;
                uint32_t sent_ms;
                memcpy(&s2, o.data, 2);
                memcpy(&sent_ms, o.data + 6, 4);
                if (s2 != o.seq) res.order_errors++;
                lat_sum += (double) ((uint32_t) t_ms - sent_ms);
                lat_n++;
            }
        }
    }
    res.jitter_us = call_jb_jitter_us(&jb);
    res.delay_ms = jb.delay_ms;
    res.played = jb.st.played;
    res.concealed = jb.st.concealed;
    res.late = jb.st.late;
    res.lost_net = ns.lost_random;
    res.max_depth = jb.st.max_depth;
    res.mean_latency_ms = lat_n ? lat_sum / lat_n : 0;
    return res;
}

static void test_jitter(void)
{
    printf("[jitter] adaptive jitter buffer, 20 ms Opus-sized packets, 48 kHz clock\n");
    printf("  %-10s %-6s %-11s %-9s %-8s %-9s %-6s %-7s %-10s\n", "jitter", "loss", "J est(us)",
           "delay ms", "played", "conceal", "late", "net lost", "latency ms");
    uint32_t jit[] = {0, 10, 30, 60, 100};
    uint32_t prev_delay = 0;
    for (uint32_t i = 0; i < 5; i++) {
        jb_result_t r = run_jitter(jit[i], 20000, 65000, 30);
        printf("  0..%-3u ms  2%%     %-11u %-9u %-8u %-9u %-6u %-8u %.1f\n", jit[i], r.jitter_us,
               r.delay_ms, r.played, r.concealed, r.late, r.lost_net, r.mean_latency_ms);
        CHECK(r.order_errors == 0);
        CHECK(r.played + r.concealed + r.late >= 1500 - 5);
        CHECK(r.delay_ms >= prev_delay); /* more jitter, more delay */
        /* RFC 3550 J for uniform [0,a] delay is about a/3; allow a wide band */
        if (jit[i]) {
            CHECK(r.jitter_us > jit[i] * 1000u / 6u && r.jitter_us < jit[i] * 1000u);
            /* late packets stay under 3% once adapted */
            CHECK(r.late * 100u <= r.played * 3u);
        } else {
            CHECK(r.jitter_us < 1000 && r.delay_ms <= 21 && r.late == 0);
        }
        prev_delay = r.delay_ms;
    }
    /* bounded memory: a packet far ahead slides the window instead of growing */
    {
        static call_jb_slot_t slots[16];
        static uint8_t mem[16 * 32];
        call_jb_t jb;
        CHECK(!call_jb_init(&jb, slots, 12, mem, 32, 48, 20, 100)); /* not a power of two */
        CHECK(call_jb_init(&jb, slots, 16, mem, 32, 48, 20, 100));
        uint8_t d[32] = {0};
        for (uint16_t s = 0; s < 10; s++) call_jb_push(&jb, s, s * 960u, true, d, 32, s * 20u);
        CHECK(call_jb_push(&jb, 1000, 1000 * 960u, true, d, 32, 300) == 1);
        CHECK(jb.depth <= 16 && jb.st.overflow == 10);
        CHECK(call_jb_push(&jb, 5, 5 * 960u, true, d, 32, 300) == 0);       /* now late */
        CHECK(call_jb_push(&jb, 1000, 1000 * 960u, true, d, 32, 300) == 0); /* duplicate */
        CHECK(call_jb_push(&jb, 1001, 1001 * 960u, true, d, 33, 300) == 0); /* too big */
        CHECK(jb.st.duplicate == 1 && jb.st.late == 1);
    }
}

/* ================================================================
 * 3. congestion control
 * ================================================================ */

typedef struct cc_phase {
    uint32_t until_s;
    uint64_t cap_bps;
    uint32_t loss_ppm;
} cc_phase_t;

typedef struct fb_msg {
    uint64_t at_us;
    uint8_t b[CALL_CC_FB_LEN];
} fb_msg_t;

static void test_cc(void)
{
    printf("[cc] delay-gradient rate control over a simulated bottleneck\n");
    static netsim_t ns;
    ns_init(&ns, 2000000, 20000, 1000, 0, 400000);
    const cc_phase_t ph[] = {
        {60, 2000000, 0},       /* converge to 2 Mbit/s */
        {120, 1000000, 0},      /* capacity halves */
        {150, 1000000, 150000}, /* 15% random (non-congestive) loss */
        {210, 1000000, 0},      /* loss stops */
        {270, 3000000, 0},      /* capacity grows */
    };
    call_cc_tx_t tx;
    call_cc_rx_t rx;
    call_cc_tx_init(&tx, 300000, 50000, 20000000);
    call_cc_rx_init(&rx, 0xBEEF, 100, 0);
    static fb_msg_t fbq[64];
    uint32_t nfb = 0;
    uint16_t seq = 0;
    uint64_t next_frame_us = 0;
    double sum_target[5] = {0}, sum_recv[5] = {0}, sum_qdelay[5] = {0};
    uint32_t n_s[5] = {0}, n_q[5] = {0};
    uint32_t min_in_loss = 0xffffffffu;
    uint32_t pre_loss_target = 0;
    uint64_t bytes_rx_sec = 0;
    uint32_t phase = 0;
    printf("  t(s)  cap(kb/s) loss  target(kb/s) recv(kb/s) qdelay(ms) state\n");
    /* pacer: packets of a frame spread across the frame interval */
    typedef struct {
        uint64_t at;
        uint32_t len;
    } tx_slot_t;
    static tx_slot_t pace[512];
    uint32_t npace = 0;
    for (uint64_t now = 0; now < 270ull * 1000000u; now += 1000) {
        uint32_t t_s = (uint32_t) (now / 1000000u);
        while (phase < 4 && t_s >= ph[phase].until_s) phase++;
        ns.rate_bps = ph[phase].cap_bps;
        ns.loss_ppm = ph[phase].loss_ppm;
        if (now >= next_frame_us) {
            uint32_t bytes = call_cc_target(&tx) / 8u / 30u;
            uint32_t n = (bytes + 1199) / 1200;
            for (uint32_t i = 0; i < n && npace < 512; i++) {
                pace[npace].at = now + (33000u * i) / n;
                pace[npace].len = i + 1 == n ? bytes - 1200 * (n - 1) : 1200;
                if (pace[npace].len < 50) pace[npace].len = 50;
                npace++;
            }
            next_frame_us += 33333;
        }
        for (uint32_t i = 0; i < npace;) {
            if (pace[i].at <= now) {
                uint8_t pk[16];
                uint32_t s32 = (uint32_t) now;
                memcpy(pk, &seq, 2);
                memcpy(pk + 4, &s32, 4);
                memcpy(pk + 8, &pace[i].len, 4);
                static uint8_t buf[1200];
                memcpy(buf, pk, 16);
                ns_send(&ns, now, buf, pace[i].len, seq);
                seq++;
                pace[i] = pace[--npace];
            } else {
                i++;
            }
        }
        ns_pkt_t p;
        while (ns_pop(&ns, now, &p)) {
            uint16_t s;
            uint32_t send_us;
            memcpy(&s, p.data, 2);
            memcpy(&send_us, p.data + 4, 4);
            call_cc_rx_on_packet(&rx, s, send_us, (uint32_t) now, p.len);
            bytes_rx_sec += p.len;
            double q = (double) (now - send_us) / 1000.0 - 20.0;
            sum_qdelay[phase] += q;
            n_q[phase]++;
        }
        call_cc_feedback_t fb;
        if (call_cc_rx_report(&rx, (uint32_t) now, &fb) && nfb < 64) {
            fbq[nfb].at_us = now + 20000;
            CHECK(call_cc_fb_write(&fb, fbq[nfb].b, CALL_CC_FB_LEN) == CALL_CC_FB_LEN);
            nfb++;
        }
        for (uint32_t i = 0; i < nfb;) {
            if (fbq[i].at_us <= now) {
                call_cc_feedback_t in;
                CHECK(call_cc_fb_parse(fbq[i].b, CALL_CC_FB_LEN, &in) == CALL_CC_FB_LEN);
                call_cc_tx_on_feedback(&tx, &in, (uint32_t) (now / 1000u), (uint32_t) now);
                fbq[i] = fbq[--nfb];
            } else {
                i++;
            }
        }
        if (now % 1000000u == 0 && now) {
            uint32_t tg = call_cc_target(&tx);
            uint32_t start = phase ? ph[phase - 1].until_s : 0;
            /* measure the second half of each phase */
            if (t_s > start + (ph[phase].until_s - start) / 2) {
                sum_target[phase] += tg;
                sum_recv[phase] += (double) bytes_rx_sec * 8.0;
                n_s[phase]++;
            }
            if (phase == 2) {
                if (tg < min_in_loss) min_in_loss = tg;
            }
            if (phase == 1 && t_s == ph[1].until_s - 1) pre_loss_target = tg;
            if (t_s % 10 == 0)
                printf("  %4u  %8llu  %3u%%  %11u  %10.0f  %9.1f  %s\n", t_s,
                       (unsigned long long) (ph[phase].cap_bps / 1000), ph[phase].loss_ppm / 10000,
                       tg / 1000, (double) bytes_rx_sec * 8.0 / 1000.0,
                       n_q[phase] ? sum_qdelay[phase] / n_q[phase] : 0.0,
                       tx.bw == CALL_BW_OVERUSE ? "over"
                                                : (tx.bw == CALL_BW_UNDERUSE ? "under" : "normal"));
            bytes_rx_sec = 0;
        }
    }
    printf("  phase summary (second half of each phase):\n");
    const char *names[] = {"2 Mbit/s", "1 Mbit/s", "1 Mbit/s + 15% loss", "1 Mbit/s, loss over",
                           "3 Mbit/s"};
    double ratio[5];
    for (uint32_t i = 0; i < 5; i++) {
        double tg = n_s[i] ? sum_target[i] / n_s[i] : 0;
        double rv = n_s[i] ? sum_recv[i] / n_s[i] : 0;
        ratio[i] = tg / (double) ph[i].cap_bps;
        printf("    %-22s target %7.0f kb/s = %5.1f%% of capacity, goodput %7.0f kb/s, mean "
               "queue %.1f ms\n",
               names[i], tg / 1000, 100 * ratio[i], rv / 1000,
               n_q[i] ? sum_qdelay[i] / n_q[i] : 0.0);
    }
    printf("  feedback %u, overuse %u, underuse %u, delay decreases %u, loss decreases %u, rtt "
           "%u ms\n",
           tx.n_feedback, tx.n_overuse, tx.n_underuse, tx.n_decrease, tx.n_loss_dec, tx.rtt_ms);
    printf("  loss phase: target %u kb/s before, minimum %u kb/s during\n", pre_loss_target / 1000,
           min_in_loss / 1000);
    /* stated band: 70%..110% of the bottleneck once converged */
    CHECK(ratio[0] >= 0.70 && ratio[0] <= 1.10);
    CHECK(ratio[1] >= 0.70 && ratio[1] <= 1.10);
    CHECK(ratio[3] >= 0.70 && ratio[3] <= 1.10);
    CHECK(ratio[4] >= 0.70 && ratio[4] <= 1.10);
    CHECK(min_in_loss < pre_loss_target / 2); /* backs off on loss */
    CHECK(tx.n_decrease > 0 && tx.n_loss_dec > 0);
    CHECK(tx.rtt_ms >= 35 && tx.rtt_ms <= 300);
    ns_free(&ns);
}

/* ================================================================
 * 4. FEC
 * ================================================================ */

typedef struct fec_ctx {
    netsim_t *ns;
    uint64_t now;
    uint32_t parity_sent;
} fec_ctx_t;

static int fec_emit_net(void *ctx, const uint8_t *pkt, uint32_t len)
{
    fec_ctx_t *c = ctx;
    c->parity_sent++;
    ns_send(c->ns, c->now, pkt, len, 1);
    return 0;
}

typedef struct rec_ctx {
    uint8_t *got; /* per original index */
    uint32_t recovered_ok, recovered_bad;
    const uint8_t (*orig)[1300];
    const uint32_t *olen;
    uint16_t seq0;
    uint32_t n;
} rec_ctx_t;

static int rec_emit(void *ctx, const uint8_t *pkt, uint32_t len)
{
    rec_ctx_t *r = ctx;
    call_rtp_hdr_t h;
    if (call_rtp_parse(pkt, len, &h, NULL) < 0) {
        r->recovered_bad++;
        return 0;
    }
    uint32_t idx = (uint16_t) (h.seq - r->seq0);
    if (idx < r->n && r->olen[idx] == len && memcmp(r->orig[idx], pkt, len) == 0) {
        r->recovered_ok++;
        r->got[idx] = 1;
    } else {
        r->recovered_bad++;
    }
    return 0;
}

static void test_fec(void)
{
    printf("[fec] XOR parity, iid loss, 20000 media packets per cell\n");
    printf("  %-4s %-6s %-12s %-12s %-12s %-10s\n", "k", "loss", "raw loss", "residual", "theory",
           "overhead");
    enum { N = 20000 };
    static uint8_t orig[N][1300];
    static uint32_t olen[N];
    static uint8_t got[N];
    static netsim_t ns;
    uint32_t ks[] = {2, 4, 8};
    uint32_t loss[] = {10000, 20000, 50000, 100000, 200000};
    for (uint32_t ki = 0; ki < 3; ki++)
        for (uint32_t li = 0; li < 5; li++) {
            ns_init(&ns, 0, 1000, 0, loss[li], 0);
            call_fec_enc_t enc;
            static uint8_t parity[1400], scratch[1500];
            CHECK(call_fec_enc_init(&enc, 0x5555, 0x5556, (uint8_t) ks[ki], parity, sizeof(parity),
                                    0));
            static call_fec_mslot_t ms[256];
            static uint8_t mmem[256 * 1400];
            static call_fec_fslot_t fs[16];
            static uint8_t fmem[16 * 1400], work[1400];
            call_fec_dec_t dec;
            CHECK(call_fec_dec_init(&dec, 0x5555, ms, mmem, 256, 1400, fs, fmem, 16, 1400, work));
            call_packetizer_t pk;
            uint16_t seq0 = 65000;
            call_packetizer_init(&pk, 0x5555, CALL_PT_VP8, 0, 1300, seq0);
            memset(got, 0, sizeof(got));
            rec_ctx_t rc = {got, 0, 0, (const uint8_t(*)[1300]) orig, olen, seq0, N};
            fec_ctx_t fc = {&ns, 0, 0};
            uint32_t raw_lost = 0;
            static pk_sink_t one;
            for (uint32_t i = 0; i < N; i++) {
                uint8_t fr[1280];
                uint32_t fl = 180 + rnd_below(1100);
                for (uint32_t b = 0; b < fl; b++) fr[b] = (uint8_t) rng32();
                one.n = 0;
                call_packetize(&pk, fr, fl, i * 3000u, false, scratch, sizeof(scratch), sink_emit,
                               &one);
                memcpy(orig[i], one.pkts[0], one.lens[0]);
                olen[i] = one.lens[0];
                fc.now = (uint64_t) i * 1000u;
                ns_send(&ns, fc.now, orig[i], olen[i], 0);
                call_fec_enc_add(&enc, orig[i], olen[i], scratch, sizeof(scratch), fec_emit_net,
                                 &fc);
                ns_pkt_t p;
                while (ns_pop(&ns, fc.now, &p)) {
                    if (p.tag == 0) {
                        call_rtp_hdr_t h;
                        call_rtp_parse(p.data, p.len, &h, NULL);
                        got[(uint16_t) (h.seq - seq0)] = 1;
                        call_fec_dec_media(&dec, p.data, p.len, rec_emit, &rc);
                    } else {
                        call_fec_dec_parity(&dec, p.data, p.len, rec_emit, &rc);
                    }
                }
            }
            ns_pkt_t p;
            while (ns_pop(&ns, ~0ull, &p)) {
                if (p.tag == 0) {
                    call_rtp_hdr_t h;
                    call_rtp_parse(p.data, p.len, &h, NULL);
                    got[(uint16_t) (h.seq - seq0)] = 1;
                    call_fec_dec_media(&dec, p.data, p.len, rec_emit, &rc);
                } else {
                    call_fec_dec_parity(&dec, p.data, p.len, rec_emit, &rc);
                }
            }
            uint32_t residual = 0;
            for (uint32_t i = 0; i < N; i++) residual += !got[i];
            raw_lost = residual + rc.recovered_ok;
            double pl = loss[li] / 1e6;
            double theory = 1.0;
            for (uint32_t q = 0; q < ks[ki]; q++) theory *= (1 - pl);
            theory = pl * (1 - theory); /* lost and another of the k others (k-1 media + parity) */
            double meas = (double) residual / N;
            printf("  %-4u %4.0f%%  %10.2f%%  %10.3f%%  %10.3f%%  %8.1f%%\n", ks[ki], pl * 100,
                   100.0 * raw_lost / N, 100 * meas, 100 * theory, 100.0 * fc.parity_sent / N);
            CHECK(rc.recovered_bad == 0 && dec.bad_recovery == 0);
            CHECK(meas <= theory * 1.35 + 0.0015 && meas >= theory * 0.65 - 0.0015);
            CHECK(meas < pl);
        }
    /* burst loss defeats single parity: drop two in a row */
    {
        call_fec_enc_t enc;
        static uint8_t parity[400], scratch[500];
        call_fec_enc_init(&enc, 9, 10, 4, parity, sizeof(parity), 0);
        static pk_sink_t fecs, med;
        fecs.n = med.n = 0;
        call_packetizer_t pk;
        call_packetizer_init(&pk, 9, CALL_PT_OPUS, 0, 200, 100);
        uint8_t fr[150];
        for (int i = 0; i < 4; i++) {
            memset(fr, i + 1, sizeof(fr));
            call_packetize(&pk, fr, 100 + i * 10, 0, false, scratch, sizeof(scratch), sink_emit,
                           &med);
            call_fec_enc_add(&enc, med.pkts[i], med.lens[i], scratch, sizeof(scratch), sink_emit,
                             &fecs);
        }
        CHECK(fecs.n == 1);
        static call_fec_mslot_t ms[64];
        static uint8_t mmem[64 * 400];
        static call_fec_fslot_t fs[4];
        static uint8_t fmem[4 * 400], work[400];
        call_fec_dec_t dec;
        call_fec_dec_init(&dec, 9, ms, mmem, 64, 400, fs, fmem, 4, 400, work);
        call_fec_dec_media(&dec, med.pkts[0], med.lens[0], NULL, NULL);
        call_fec_dec_media(&dec, med.pkts[3], med.lens[3], NULL, NULL);
        CHECK(call_fec_dec_parity(&dec, fecs.pkts[0], fecs.lens[0], NULL, NULL) == 0);
        CHECK(dec.recovered == 0);
        /* the second arrives late: now one is missing and the parity repairs it */
        CHECK(call_fec_dec_media(&dec, med.pkts[1], med.lens[1], NULL, NULL) == 1);
        CHECK(dec.recovered == 1);
        uint8_t *b = mmem + (uint32_t) ((102) & 63) * 400;
        CHECK(memcmp(b, med.pkts[2], med.lens[2]) == 0);
        printf("  two losses in one group: 0 repaired until one of them arrived late, then 1\n");
    }
    ns_free(&ns);
}

/* ================================================================
 * 5. group mesh
 * ================================================================ */

/* toy signature: 64 bytes of a keyed FNV stream (TEST ONLY, not crypto) */
static void toy_sig(const uint8_t *key, const uint8_t *msg, uint32_t len, uint8_t sig[64])
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < 32; i++) h = (h ^ key[i]) * 16777619u;
    for (uint32_t i = 0; i < len; i++) h = (h ^ msg[i]) * 16777619u;
    for (int i = 0; i < 64; i++) {
        h = (h ^ (uint32_t) i) * 16777619u;
        h ^= h >> 13;
        sig[i] = (uint8_t) h;
    }
}

static bool toy_verify(void *ctx, const uint8_t pubkey[32], const uint8_t *msg, uint32_t len,
                       const uint8_t sig[64])
{
    (void) ctx;
    uint8_t want[64];
    toy_sig(pubkey, msg, len, want);
    return memcmp(want, sig, 64) == 0;
}

static bool toy_sign(void *ctx, const uint8_t *msg, uint32_t len, uint8_t sig[64])
{
    toy_sig((const uint8_t *) ctx, msg, len, sig);
    return true;
}

typedef struct mesh_world {
    call_mesh_member_t m[40];
    uint8_t msg[40][CALL_MESH_MAX_MSG];
    uint32_t len[40];
    uint32_t n;
} mesh_world_t;

static void make_world(mesh_world_t *w, uint32_t n, bool tight)
{
    memset(w, 0, sizeof(*w));
    w->n = n;
    for (uint32_t i = 0; i < n; i++) {
        call_mesh_member_t *m = &w->m[i];
        m->id = 1000 + i * 7919u % 100000u + rnd_below(5) * 100000u;
        for (int k = 0; k < 32; k++) m->pubkey[k] = (uint8_t) rng32();
        m->version = 1;
        m->uplink_kbps = tight ? 800 + rnd_below(2400) : 4000 + rnd_below(20000);
        m->fanout = (uint8_t) (4 + rnd_below(9));
        m->nstreams = 2;
        m->streams[0] = (call_mesh_stream_t){m->id * 16 + 1, CALL_STREAM_AUDIO, 1, {40, 0, 0, 0}};
        m->streams[1] =
            (call_mesh_stream_t){m->id * 16 + 2, CALL_STREAM_VIDEO, 3, {150, 500, 1500, 0}};
    }
    /* subscriptions: all audio; speakers 0,1 at layer 2; up to 8 thumbnails at layer 0 */
    for (uint32_t i = 0; i < n; i++) {
        call_mesh_member_t *m = &w->m[i];
        for (uint32_t j = 0; j < n; j++) {
            if (j == i) continue;
            m->subs[m->nsubs++] = (call_mesh_sub_t){w->m[j].id * 16 + 1, 0};
        }
        uint32_t thumbs = 0;
        for (uint32_t j = 0; j < n; j++) {
            if (j == i) continue;
            if (j < 2)
                m->subs[m->nsubs++] = (call_mesh_sub_t){w->m[j].id * 16 + 2, 2};
            else if (thumbs < 8 && (rnd_below(3) || n < 12)) {
                m->subs[m->nsubs++] = (call_mesh_sub_t){w->m[j].id * 16 + 2, 0};
                thumbs++;
            }
        }
    }
    for (uint32_t i = 0; i < n; i++) {
        int l = call_mesh_encode_member(&w->m[i], 77, w->msg[i], CALL_MESH_MAX_MSG, toy_sign,
                                        w->m[i].pubkey);
        CHECK(l > 0);
        w->len[i] = (uint32_t) l;
    }
}

typedef struct mesh_check {
    uint32_t over_uplink, bad_tree, fanout_violations, missing, dup;
    uint32_t max_util_pct;
} mesh_check_t;

/* Forward one packet of every (stream, layer) along the plan's edges and
 * check delivery, uplink and fanout. */
static mesh_check_t verify_plan(const call_mesh_t *mesh, const call_mesh_plan_t *p)
{
    mesh_check_t c;
    memset(&c, 0, sizeof(c));
    static uint32_t used[64];
    memset(used, 0, sizeof(used));
    for (uint32_t e = 0; e < p->nedges; e++) {
        const call_mesh_edge_t *ed = &p->edges[e];
        int fi = -1;
        for (uint32_t i = 0; i < mesh->n; i++)
            if (mesh->members[i].id == ed->from) fi = (int) i;
        if (fi < 0 || !mesh->members[fi].alive) {
            c.bad_tree++;
            continue;
        }
        used[fi] += ed->kbps;
        if (call_mesh_children(p, ed->from, ed->stream_id, ed->layer, NULL, 0) >
            mesh->members[fi].fanout)
            c.fanout_violations++;
        /* the sender must be the publisher or have received this (stream, layer) */
        bool pub = false;
        for (uint32_t s = 0; s < mesh->members[fi].nstreams; s++)
            if (mesh->members[fi].streams[s].id == ed->stream_id) pub = true;
        if (!pub) {
            uint32_t par = call_mesh_parent(p, ed->from, ed->stream_id, ed->layer);
            if (!par) c.bad_tree++;
        }
    }
    for (uint32_t i = 0; i < mesh->n; i++) {
        if (used[i] > mesh->members[i].uplink_kbps) c.over_uplink++;
        if (used[i] != p->up_used_kbps[i]) c.bad_tree++;
        if (mesh->members[i].uplink_kbps) {
            uint32_t u = used[i] * 100u / mesh->members[i].uplink_kbps;
            if (u > c.max_util_pct) c.max_util_pct = u;
        }
    }
    /* flood: each delivered subscription is reached exactly once from its publisher */
    for (uint32_t d = 0; d < p->ndl; d++) {
        const call_mesh_delivery_t *dl = &p->dl[d];
        if (dl->got == CALL_MESH_NO_LAYER) continue;
        uint32_t hops = 0, cur = dl->member, copies = 0;
        for (uint32_t e = 0; e < p->nedges; e++)
            if (p->edges[e].to == dl->member && p->edges[e].stream_id == dl->stream_id &&
                p->edges[e].layer == dl->got)
                copies++;
        if (copies != 1) c.dup++;
        while (cur != dl->publisher && hops < 64) {
            cur = call_mesh_parent(p, cur, dl->stream_id, dl->got);
            if (!cur) break;
            hops++;
        }
        if (cur != dl->publisher) c.missing++;
    }
    return c;
}

static void test_mesh(void)
{
    printf("[mesh] decentralized group calls, relay forest from signed membership\n");
    static mesh_world_t w;
    static call_mesh_member_t table_a[40], table_b[40];
    static call_mesh_edge_t edges_a[8192], edges_b[8192];
    static call_mesh_delivery_t dl_a[4096], dl_b[4096];
    static uint32_t up_a[40], up_b[40];
    static uint32_t scratch[20000];
    uint32_t total_unserved_ok = 0;
    printf("  %-3s %-6s %-7s %-8s %-8s %-9s %-7s %-8s %s\n", "n", "edges", "served", "degraded",
           "unserved", "max depth", "max up%", "over-up", "determinism");
    for (uint32_t n = 2; n <= 32; n++) {
        make_world(&w, n, false);
        call_mesh_t A, B;
        call_mesh_init(&A, 77, table_a, 40, toy_verify, NULL);
        call_mesh_init(&B, 77, table_b, 40, toy_verify, NULL);
        for (uint32_t i = 0; i < n; i++) CHECK(call_mesh_on_message(&A, w.msg[i], w.len[i]) == 1);
        /* B sees them shuffled, with replays */
        uint32_t order[40];
        for (uint32_t i = 0; i < n; i++) order[i] = i;
        for (uint32_t i = n - 1; i > 0; i--) {
            uint32_t j = rnd_below(i + 1), t = order[i];
            order[i] = order[j];
            order[j] = t;
        }
        for (uint32_t i = 0; i < n; i++) {
            call_mesh_on_message(&B, w.msg[order[i]], w.len[order[i]]);
            if (i % 3 == 0) CHECK(call_mesh_on_message(&B, w.msg[order[0]], w.len[order[0]]) == 0);
        }
        call_mesh_plan_t pa = {edges_a, 8192, 0, dl_a, 4096, 0, up_a, 40, 0, 0, 0, 0, 0};
        call_mesh_plan_t pb = {edges_b, 8192, 0, dl_b, 4096, 0, up_b, 40, 0, 0, 0, 0, 0};
        CHECK(call_mesh_scratch_words(&A) <= 20000);
        CHECK(call_mesh_build(&A, &pa, scratch, 20000) == CALL_OK);
        CHECK(call_mesh_build(&B, &pb, scratch, 20000) == CALL_OK);
        bool same = pa.hash == pb.hash && pa.nedges == pb.nedges &&
                    memcmp(edges_a, edges_b, pa.nedges * sizeof(call_mesh_edge_t)) == 0;
        CHECK(same);
        mesh_check_t c = verify_plan(&A, &pa);
        CHECK(c.over_uplink == 0 && c.bad_tree == 0 && c.fanout_violations == 0);
        CHECK(c.missing == 0 && c.dup == 0);
        CHECK(pa.unserved == 0); /* enough capacity: everyone gets every stream */
        total_unserved_ok += pa.unserved;
        if (n <= 4 || n % 4 == 0 || n == 32)
            printf("  %-3u %-6u %-7u %-8u %-8u %-9u %-7u %-8u %s\n", n, pa.nedges, pa.served,
                   pa.degraded, pa.unserved, pa.max_depth_seen, c.max_util_pct, c.over_uplink,
                   same ? "same plan" : "DIFFERENT");

        /* leave + failure + rejoin, both views agree after each */
        if (n >= 4) {
            call_mesh_member_t leaver = w.m[1];
            leaver.version = 2;
            uint8_t lm[256];
            int ll = call_mesh_encode_leave(&leaver, 77, lm, sizeof(lm), toy_sign, leaver.pubkey);
            CHECK(ll > 0);
            CHECK(call_mesh_on_message(&A, lm, (uint32_t) ll) == 1);
            CHECK(call_mesh_on_message(&B, lm, (uint32_t) ll) == 1);
            CHECK(call_mesh_on_message(&A, w.msg[1], w.len[1]) == 0); /* old join replayed */
            CHECK(call_mesh_mark_failed(&A, w.m[2].id) == 1);
            CHECK(call_mesh_mark_failed(&B, w.m[2].id) == 1);
            CHECK(call_mesh_build(&A, &pa, scratch, 20000) == CALL_OK);
            CHECK(call_mesh_build(&B, &pb, scratch, 20000) == CALL_OK);
            CHECK(pa.hash == pb.hash);
            c = verify_plan(&A, &pa);
            CHECK(c.over_uplink == 0 && c.bad_tree == 0 && c.missing == 0 && c.dup == 0);
            bool absent = true;
            for (uint32_t e = 0; e < pa.nedges; e++)
                if (edges_a[e].from == w.m[1].id || edges_a[e].to == w.m[1].id ||
                    edges_a[e].from == w.m[2].id || edges_a[e].to == w.m[2].id)
                    absent = false;
            CHECK(absent);
            CHECK(call_mesh_alive(&A) == n - 2);
            /* the failed member comes back with a newer signed version */
            w.m[2].version = 5;
            uint8_t jm[CALL_MESH_MAX_MSG];
            int jl = call_mesh_encode_member(&w.m[2], 77, jm, sizeof(jm), toy_sign, w.m[2].pubkey);
            CHECK(call_mesh_on_message(&A, jm, (uint32_t) jl) == 1);
            CHECK(call_mesh_alive(&A) == n - 1);
        }
    }
    (void) total_unserved_ok;
    /* tight capacity: fallbacks happen, uplinks are never exceeded */
    uint32_t tot_deg = 0, tot_uns = 0, tot = 0, max_util = 0, over = 0, audio_uns = 0;
    for (uint32_t trial = 0; trial < 20; trial++) {
        uint32_t n = 8 + rnd_below(25);
        make_world(&w, n, true);
        call_mesh_t A;
        call_mesh_init(&A, 77, table_a, 40, toy_verify, NULL);
        for (uint32_t i = 0; i < n; i++) call_mesh_on_message(&A, w.msg[i], w.len[i]);
        call_mesh_plan_t pa = {edges_a, 8192, 0, dl_a, 4096, 0, up_a, 40, 0, 0, 0, 0, 0};
        CHECK(call_mesh_build(&A, &pa, scratch, 20000) == CALL_OK);
        mesh_check_t c = verify_plan(&A, &pa);
        CHECK(c.over_uplink == 0 && c.bad_tree == 0 && c.missing == 0 && c.dup == 0 &&
              c.fanout_violations == 0);
        for (uint32_t d = 0; d < pa.ndl; d++)
            if (dl_a[d].stream_id % 16 == 1 && dl_a[d].got == CALL_MESH_NO_LAYER) audio_uns++;
        tot_deg += pa.degraded;
        tot_uns += pa.unserved;
        tot += pa.ndl;
        over += c.over_uplink;
        if (c.max_util_pct > max_util) max_util = c.max_util_pct;
    }
    printf("  tight uplinks (800..3200 kb/s), 20 calls of 8..32: %u subscriptions, %u degraded "
           "to a lower layer, %u unserved (%u of them audio), uplink exceeded %u times, max "
           "utilisation %u%%\n",
           tot, tot_deg, tot_uns, audio_uns, over, max_util);
    CHECK(tot_deg > 0);
    CHECK(audio_uns == 0);
    /* authentication */
    make_world(&w, 3, false);
    call_mesh_t A;
    call_mesh_init(&A, 77, table_a, 40, toy_verify, NULL);
    w.msg[0][20] ^= 1; /* inside the signed bytes */
    CHECK(call_mesh_on_message(&A, w.msg[0], w.len[0]) == CALL_ERR_AUTH);
    w.msg[0][20] ^= 1;
    CHECK(call_mesh_on_message(&A, w.msg[0], w.len[0]) == 1);
    call_mesh_member_t imp = w.m[0];
    imp.version = 9;
    imp.pubkey[0] ^= 0xff; /* same id, someone else's key */
    uint8_t im[CALL_MESH_MAX_MSG];
    int il = call_mesh_encode_member(&imp, 77, im, sizeof(im), toy_sign, imp.pubkey);
    CHECK(call_mesh_on_message(&A, im, (uint32_t) il) == CALL_ERR_AUTH);
    int ol = call_mesh_encode_member(&w.m[0], 78, im, sizeof(im), toy_sign, w.m[0].pubkey);
    CHECK(call_mesh_on_message(&A, im, (uint32_t) ol) == CALL_ERR_ARG); /* other group */
}

/* ================================================================
 * 6. session state machine
 * ================================================================ */

typedef struct sig_link {
    uint8_t q[64][CALL_SIG_MAX];
    uint32_t len[64];
    uint32_t at[64];
    uint32_t n;
    uint32_t loss_ppm;
    uint32_t delay_ms;
    int down;
    uint32_t sent;
} sig_link_t;

typedef struct phone {
    call_session_t s;
    sig_link_t *out;
    uint32_t now;
    uint32_t notes[5];
    uint32_t last_note_peer;
    uint32_t rng;
} phone_t;

static int phone_send(void *ctx, const uint8_t *msg, uint32_t len)
{
    phone_t *p = ctx;
    sig_link_t *l = p->out;
    l->sent++;
    if (l->down || l->n >= 64 || chance_ppm(l->loss_ppm)) return 0;
    memcpy(l->q[l->n], msg, len);
    l->len[l->n] = len;
    l->at[l->n] = p->now + l->delay_ms;
    l->n++;
    return 0;
}

static void phone_notify(void *ctx, call_notify_t what, uint32_t peer, call_end_reason_t why,
                         const call_session_t *s)
{
    phone_t *p = ctx;
    (void) why;
    (void) s;
    if (what < 5) p->notes[what]++;
    p->last_note_peer = peer;
}

static uint32_t phone_rand(void *ctx)
{
    phone_t *p = ctx;
    if (p->rng) {
        uint32_t r = p->rng;
        return r; /* fixed value: forces tie-breaker collisions in glare tests */
    }
    return rng32();
}

static void phone_init(phone_t *p, uint32_t id, sig_link_t *out, uint32_t caps,
                       const call_codec_t *codecs, uint8_t ncodecs)
{
    memset(p, 0, sizeof(*p));
    call_session_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.self_id = id;
    cfg.caps = caps;
    cfg.ncodecs = ncodecs;
    for (uint32_t i = 0; i < ncodecs; i++) cfg.codecs[i] = codecs[i];
    cfg.send = phone_send;
    cfg.notify = phone_notify;
    cfg.rand = phone_rand;
    cfg.ctx = p;
    call_session_init(&p->s, &cfg);
    p->out = out;
}

/* deliver due messages from link to phone */
static void link_pump(sig_link_t *l, phone_t *to, uint32_t now)
{
    for (uint32_t i = 0; i < l->n;) {
        if ((int32_t) (now - l->at[i]) >= 0) {
            uint8_t m[CALL_SIG_MAX];
            uint32_t len = l->len[i];
            memcpy(m, l->q[i], len);
            for (uint32_t k = i + 1; k < l->n; k++) {
                memcpy(l->q[k - 1], l->q[k], l->len[k]);
                l->len[k - 1] = l->len[k];
                l->at[k - 1] = l->at[k];
            }
            l->n--;
            call_session_on_message(&to->s, m, len, now);
        } else {
            i++;
        }
    }
}

static void run_pair(phone_t *a, phone_t *b, sig_link_t *ab, sig_link_t *ba, uint32_t from,
                     uint32_t to, bool media)
{
    for (uint32_t t = from; t < to; t += 10) {
        a->now = b->now = t;
        link_pump(ab, b, t);
        link_pump(ba, a, t);
        call_session_tick(&a->s, t);
        call_session_tick(&b->s, t);
        if (media && ab->down == 0 && a->s.state >= CALL_ST_CONNECTING &&
            a->s.state <= CALL_ST_HELD && b->s.state >= CALL_ST_CONNECTING &&
            b->s.state <= CALL_ST_HELD && t % 20 == 0) {
            call_session_on_media(&b->s, t);
            call_session_on_media(&a->s, t);
        }
    }
}

static void test_session(void)
{
    printf("[session] call state machine\n");
    static sig_link_t ab, ba, cb, bc;
    static phone_t A, B, C;
    uint32_t allcaps = CALL_CAP_UBH168 | CALL_CAP_FEC | CALL_CAP_CC_FEEDBACK;

    /* a) basic call, UBH carriage, Opus + VP8, hold, hangup */
    memset(&ab, 0, sizeof(ab));
    memset(&ba, 0, sizeof(ba));
    ab.delay_ms = ba.delay_ms = 40;
    phone_init(&A, 11, &ab, allcaps, NULL, 0);
    phone_init(&B, 22, &ba, allcaps, NULL, 0);
    CHECK(call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO | CALL_MEDIA_VIDEO, 0) == 0);
    run_pair(&A, &B, &ab, &ba, 0, 1000, false);
    CHECK(A.s.state == CALL_ST_RINGING_OUT && B.s.state == CALL_ST_INCOMING);
    CHECK(B.notes[CALL_NOTIFY_INCOMING] == 1 && B.last_note_peer == 11);
    CHECK(call_session_accept(&B.s, 1000) == 0);
    run_pair(&A, &B, &ab, &ba, 1000, 2000, true);
    CHECK(A.s.state == CALL_ST_CONNECTED && B.s.state == CALL_ST_CONNECTED);
    CHECK(A.s.audio_pt == CALL_PT_OPUS && A.s.video_pt == CALL_PT_VP8);
    CHECK(B.s.audio_pt == CALL_PT_OPUS && B.s.video_pt == CALL_PT_VP8);
    CHECK(A.s.carriage == CALL_CARRIAGE_UBH168 && B.s.carriage == CALL_CARRIAGE_UBH168);
    CHECK(!memcmp(A.s.call_id, B.s.call_id, 8));
    call_session_hold(&A.s, true, 2000);
    run_pair(&A, &B, &ab, &ba, 2000, 2200, true);
    CHECK(A.s.state == CALL_ST_HELD && B.s.state == CALL_ST_HELD && B.s.remote_hold);
    call_session_hold(&A.s, false, 2200);
    run_pair(&A, &B, &ab, &ba, 2200, 2400, true);
    CHECK(A.s.state == CALL_ST_CONNECTED && B.s.state == CALL_ST_CONNECTED);
    run_pair(&A, &B, &ab, &ba, 2400, 40000, true); /* keepalives keep it up */
    CHECK(A.s.state == CALL_ST_CONNECTED && B.s.state == CALL_ST_CONNECTED);
    call_session_hangup(&A.s, 40000);
    run_pair(&A, &B, &ab, &ba, 40000, 40500, false);
    CHECK(A.s.state == CALL_ST_ENDED && B.s.state == CALL_ST_ENDED);
    CHECK(B.s.end_reason == CALL_END_REMOTE_HANGUP && A.s.end_reason == CALL_END_LOCAL_HANGUP);
    CHECK(A.notes[CALL_NOTIFY_ENDED] == 1 && B.notes[CALL_NOTIFY_ENDED] == 1);
    CHECK(A.notes[CALL_NOTIFY_MISSED] == 0 && B.notes[CALL_NOTIFY_MISSED] == 0);
    printf("  basic: offer/ringing/answer/ack -> CONNECTED (Opus %u, VP8 %u, UBH carriage), "
           "hold/resume, 38 s keepalive, hangup -> ENDED on both sides\n",
           A.s.audio_pt, A.s.video_pt);

    /* b) codec negotiation: callee has only VP9; then a callee without UBH */
    {
        call_codec_t onlyvp9[] = {{CALL_PT_OPUS, CALL_MEDIA_AUDIO},
                                  {CALL_PT_VP9, CALL_MEDIA_VIDEO}};
        memset(&ab, 0, sizeof(ab));
        memset(&ba, 0, sizeof(ba));
        phone_init(&A, 11, &ab, allcaps, NULL, 0);
        phone_init(&B, 22, &ba, CALL_CAP_FEC, onlyvp9, 2);
        call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO | CALL_MEDIA_VIDEO, 0);
        run_pair(&A, &B, &ab, &ba, 0, 100, false);
        call_session_accept(&B.s, 100);
        run_pair(&A, &B, &ab, &ba, 100, 500, true);
        CHECK(A.s.state == CALL_ST_CONNECTED && A.s.video_pt == CALL_PT_VP9);
        CHECK(A.s.carriage == CALL_CARRIAGE_PLAIN && B.s.carriage == CALL_CARRIAGE_PLAIN);
        CHECK(A.s.common_caps == CALL_CAP_FEC);
        /* audio-only callee for an A+V offer */
        call_codec_t audio[] = {{CALL_PT_OPUS, CALL_MEDIA_AUDIO}};
        memset(&ab, 0, sizeof(ab));
        memset(&ba, 0, sizeof(ba));
        phone_init(&A, 11, &ab, allcaps, NULL, 0);
        phone_init(&B, 22, &ba, allcaps, audio, 1);
        call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO | CALL_MEDIA_VIDEO, 0);
        run_pair(&A, &B, &ab, &ba, 0, 100, false);
        call_session_accept(&B.s, 100);
        run_pair(&A, &B, &ab, &ba, 100, 500, true);
        CHECK(A.s.state == CALL_ST_CONNECTED && A.s.media == CALL_MEDIA_AUDIO && A.s.video_pt == 0);
        /* nothing in common */
        call_codec_t h264[] = {{102, CALL_MEDIA_VIDEO}};
        memset(&ab, 0, sizeof(ab));
        memset(&ba, 0, sizeof(ba));
        phone_init(&A, 11, &ab, allcaps, NULL, 0);
        phone_init(&B, 22, &ba, allcaps, h264, 1);
        call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO | CALL_MEDIA_VIDEO, 0);
        run_pair(&A, &B, &ab, &ba, 0, 500, false);
        CHECK(A.s.state == CALL_ST_ENDED && A.s.end_reason == CALL_END_INCOMPATIBLE);
        CHECK(B.s.state == CALL_ST_IDLE && B.notes[CALL_NOTIFY_INCOMING] == 0);
        printf("  codecs: VP9-only callee -> VP9; audio-only callee -> audio call; no common "
               "codec -> INCOMPATIBLE\n");
    }

    /* c) no response: offer resent at 1, 2, 4 s, then NO_RESPONSE */
    memset(&ab, 0, sizeof(ab));
    memset(&ba, 0, sizeof(ba));
    ab.down = 1;
    phone_init(&A, 11, &ab, allcaps, NULL, 0);
    phone_init(&B, 22, &ba, allcaps, NULL, 0);
    call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO, 0);
    uint32_t t_end = 0;
    for (uint32_t t = 0; t < 20000 && !t_end; t += 10) {
        A.now = t;
        call_session_tick(&A.s, t);
        if (A.s.state == CALL_ST_ENDED) t_end = t;
    }
    CHECK(A.s.end_reason == CALL_END_NO_RESPONSE && ab.sent == 4);
    CHECK(t_end >= 7000 && t_end <= 15100);
    CHECK(A.notes[CALL_NOTIFY_ENDED] == 1);
    printf("  no response: %u offers, ended NO_RESPONSE at %u ms\n", ab.sent, t_end);

    /* d) ring timeout: caller NO_ANSWER, callee MISSED */
    memset(&ab, 0, sizeof(ab));
    memset(&ba, 0, sizeof(ba));
    phone_init(&A, 11, &ab, allcaps, NULL, 0);
    phone_init(&B, 22, &ba, allcaps, NULL, 0);
    call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO, 0);
    run_pair(&A, &B, &ab, &ba, 0, 31000, false);
    CHECK(A.s.state == CALL_ST_ENDED && A.s.end_reason == CALL_END_NO_ANSWER);
    CHECK(B.s.state == CALL_ST_ENDED && B.notes[CALL_NOTIFY_MISSED] == 1);
    CHECK(B.notes[CALL_NOTIFY_ENDED] == 0 && A.notes[CALL_NOTIFY_ENDED] == 1);
    printf("  ring timeout: caller %s, callee notified MISSED\n",
           call_end_reason_name(A.s.end_reason));

    /* e) caller cancels while ringing -> missed; f) reject */
    memset(&ab, 0, sizeof(ab));
    memset(&ba, 0, sizeof(ba));
    phone_init(&A, 11, &ab, allcaps, NULL, 0);
    phone_init(&B, 22, &ba, allcaps, NULL, 0);
    call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO, 0);
    run_pair(&A, &B, &ab, &ba, 0, 3000, false);
    call_session_hangup(&A.s, 3000);
    run_pair(&A, &B, &ab, &ba, 3000, 3200, false);
    CHECK(B.s.state == CALL_ST_ENDED && B.s.end_reason == CALL_END_CANCELLED &&
          B.notes[CALL_NOTIFY_MISSED] == 1);
    call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO, 4000); /* new call after ENDED */
    run_pair(&A, &B, &ab, &ba, 4000, 4500, false);
    CHECK(B.s.state == CALL_ST_INCOMING);
    call_session_reject(&B.s, 4500);
    run_pair(&A, &B, &ab, &ba, 4500, 5000, false);
    CHECK(A.s.state == CALL_ST_ENDED && A.s.end_reason == CALL_END_REJECTED);
    printf("  cancel while ringing -> callee MISSED; reject -> caller REJECTED\n");

    /* g) busy: B talks to C; A calls B */
    memset(&ab, 0, sizeof(ab));
    memset(&ba, 0, sizeof(ba));
    memset(&cb, 0, sizeof(cb));
    memset(&bc, 0, sizeof(bc));
    phone_init(&A, 11, &ab, allcaps, NULL, 0);
    phone_init(&B, 22, &ba, allcaps, NULL, 0);
    phone_init(&C, 33, &cb, allcaps, NULL, 0);
    /* B's outgoing link is shared: route by destination below */
    call_session_dial(&C.s, 22, CALL_MEDIA_AUDIO, 0);
    for (uint32_t t = 0; t < 600; t += 10) {
        C.now = B.now = t;
        link_pump(&cb, &B, t);
        link_pump(&ba, &C, t);
        call_session_tick(&B.s, t);
        call_session_tick(&C.s, t);
        if (B.s.state == CALL_ST_INCOMING) call_session_accept(&B.s, t);
    }
    CHECK(B.s.state == CALL_ST_CONNECTED && C.s.state == CALL_ST_CONNECTED);
    call_session_dial(&A.s, 22, CALL_MEDIA_AUDIO, 600);
    A.now = 600;
    link_pump(&ab, &B, 700); /* B answers REJECT(BUSY) on its link to... the shared queue */
    /* move B's reply addressed to A from ba into a link towards A */
    {
        uint32_t moved = 0;
        for (uint32_t i = 0; i < ba.n; i++) {
            call_sig_t m;
            if (call_sig_parse(ba.q[i], ba.len[i], &m) > 0 && m.to == 11) {
                call_session_on_message(&A.s, ba.q[i], ba.len[i], 700);
                moved++;
            }
        }
        CHECK(moved == 1);
    }
    CHECK(A.s.state == CALL_ST_ENDED && A.s.end_reason == CALL_END_BUSY);
    CHECK(B.s.state == CALL_ST_CONNECTED && B.notes[CALL_NOTIFY_MISSED] == 1 &&
          B.last_note_peer == 11);
    printf("  busy: caller ends BUSY, callee keeps its call and gets MISSED for the caller\n");

    /* h) glare, many trials incl. equal tie-breakers */
    uint32_t glare_ok = 0, glare_trials = 200;
    for (uint32_t trial = 0; trial < glare_trials; trial++) {
        memset(&ab, 0, sizeof(ab));
        memset(&ba, 0, sizeof(ba));
        ab.delay_ms = 10 + rnd_below(80);
        ba.delay_ms = 10 + rnd_below(80);
        phone_init(&A, 11, &ab, allcaps, NULL, 0);
        phone_init(&B, 22, &ba, allcaps, NULL, 0);
        if (trial % 4 == 0) A.rng = B.rng = 0x12345678u; /* same tie-breaker */
        uint32_t ta = rnd_below(30), tb = rnd_below(30);
        for (uint32_t t = 0; t < 3000; t += 10) {
            A.now = B.now = t;
            if (t == ta * 10 && A.s.state == CALL_ST_IDLE) call_session_dial(&A.s, 22, 3, t);
            if (t == tb * 10 && B.s.state == CALL_ST_IDLE) call_session_dial(&B.s, 11, 3, t);
            link_pump(&ab, &B, t);
            link_pump(&ba, &A, t);
            call_session_tick(&A.s, t);
            call_session_tick(&B.s, t);
            if (A.s.state == CALL_ST_INCOMING) call_session_accept(&A.s, t);
            if (B.s.state == CALL_ST_INCOMING) call_session_accept(&B.s, t);
            if (t % 20 == 0 && A.s.state >= CALL_ST_CONNECTING && B.s.state >= CALL_ST_CONNECTING) {
                call_session_on_media(&A.s, t);
                call_session_on_media(&B.s, t);
            }
        }
        if (A.s.state == CALL_ST_CONNECTED && B.s.state == CALL_ST_CONNECTED &&
            !memcmp(A.s.call_id, B.s.call_id, 8) && A.s.outgoing != B.s.outgoing &&
            A.notes[CALL_NOTIFY_MISSED] == 0 && B.notes[CALL_NOTIFY_MISSED] == 0)
            glare_ok++;
    }
    CHECK(glare_ok == glare_trials);
    printf("  glare: %u/%u simultaneous calls merged into one call with one call id (50 with "
           "equal tie-breakers)\n",
           glare_ok, glare_trials);

    /* i) lossy signalling */
    uint32_t lossy_ok = 0, lossy_trials = 300;
    for (uint32_t trial = 0; trial < lossy_trials; trial++) {
        memset(&ab, 0, sizeof(ab));
        memset(&ba, 0, sizeof(ba));
        ab.loss_ppm = ba.loss_ppm = 250000;
        ab.delay_ms = ba.delay_ms = 50;
        phone_init(&A, 11, &ab, allcaps, NULL, 0);
        phone_init(&B, 22, &ba, allcaps, NULL, 0);
        call_session_dial(&A.s, 22, 1, 0);
        for (uint32_t t = 0; t < 20000; t += 10) {
            A.now = B.now = t;
            link_pump(&ab, &B, t);
            link_pump(&ba, &A, t);
            call_session_tick(&A.s, t);
            call_session_tick(&B.s, t);
            if (B.s.state == CALL_ST_INCOMING && t > 2000) call_session_accept(&B.s, t);
        }
        if (A.s.state == CALL_ST_CONNECTED && B.s.state == CALL_ST_CONNECTED) lossy_ok++;
        /* every call that started ends in a consistent place */
        CHECK(A.s.state == CALL_ST_CONNECTED || A.s.state == CALL_ST_ENDED);
    }
    printf("  25%% signalling loss each way: %u/%u calls connected (offer x4, answer resent "
           "until ACK)\n",
           lossy_ok, lossy_trials);
    CHECK(lossy_ok * 100 >= lossy_trials * 85);

    /* j) idle timeout after the path dies */
    memset(&ab, 0, sizeof(ab));
    memset(&ba, 0, sizeof(ba));
    phone_init(&A, 11, &ab, allcaps, NULL, 0);
    phone_init(&B, 22, &ba, allcaps, NULL, 0);
    call_session_dial(&A.s, 22, 1, 0);
    run_pair(&A, &B, &ab, &ba, 0, 100, false);
    call_session_accept(&B.s, 100);
    run_pair(&A, &B, &ab, &ba, 100, 1000, true);
    CHECK(A.s.state == CALL_ST_CONNECTED);
    ab.down = ba.down = 1;
    uint32_t ta_end = 0;
    for (uint32_t t = 1000; t < 30000; t += 10) {
        call_session_tick(&A.s, t);
        call_session_tick(&B.s, t);
        if (!ta_end && A.s.state == CALL_ST_ENDED) ta_end = t;
    }
    CHECK(A.s.end_reason == CALL_END_TIMEOUT && B.s.end_reason == CALL_END_TIMEOUT);
    CHECK(ta_end >= 15000 && ta_end <= 17000);
    printf("  path loss while connected: both sides end TIMEOUT after %u ms of silence\n",
           ta_end - 1000);

    /* k) messages for other calls or peers are ignored */
    {
        call_sig_t m;
        memset(&m, 0, sizeof(m));
        m.type = CALL_MSG_BYE;
        m.from = 99;
        m.to = 11;
        uint8_t b[64];
        int l = call_sig_write(&m, b, sizeof(b));
        memset(&ab, 0, sizeof(ab));
        memset(&ba, 0, sizeof(ba));
        phone_init(&A, 11, &ab, allcaps, NULL, 0);
        phone_init(&B, 22, &ba, allcaps, NULL, 0);
        call_session_dial(&A.s, 22, 1, 0);
        run_pair(&A, &B, &ab, &ba, 0, 100, false);
        call_session_accept(&B.s, 100);
        run_pair(&A, &B, &ab, &ba, 100, 500, true);
        call_session_on_message(&A.s, b, (uint32_t) l, 500);
        CHECK(A.s.state == CALL_ST_CONNECTED);
    }
}

/* ================================================================
 * 7. ICE, NAT traversal, rendezvous
 * ================================================================ */

static bool toy_mac(void *ctx, const uint8_t *key, uint32_t klen, const uint8_t *msg, uint32_t len,
                    uint8_t out[16])
{
    (void) ctx;
    uint8_t k[32] = {0}, s[64];
    memcpy(k, key, klen > 32 ? 32 : klen);
    toy_sig(k, msg, len, s);
    memcpy(out, s, 16);
    return true;
}

/* A sits behind a NAT (10.0.0.1:5000 <-> 203.0.113.5:40000, address-restricted
 * filtering). B is public at 198.51.100.7:6000. A DHT reflector at
 * 192.0.2.1:3478 echoes the address it sees. */
typedef struct nat_world {
    call_ice_t A, B;
    call_addr_t a_host, a_pub, b_host, refl;
    call_addr_t a_opened[8];
    uint32_t nopen;
    struct nat_msg {
        int to_b; /* 1 to B, 0 to A, 2/4 to the reflector (from the agent/gatherer) */
        call_addr_t from;
        uint8_t m[CALL_STUN_MAX];
        uint32_t len;
    } q[256];
    uint32_t n;
    call_gather_t *g;
    uint32_t dropped;
} nat_world_t;

static nat_world_t g_nat;
static int g_block; /* drop everything: an unreachable peer */

static call_addr_t v4(uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port)
{
    call_addr_t x;
    memset(&x, 0, sizeof(x));
    x.family = 4;
    x.ip[0] = a;
    x.ip[1] = b;
    x.ip[2] = c;
    x.ip[3] = d;
    x.port = port;
    return x;
}

static void nat_enqueue(int to_b, const call_addr_t *from, const uint8_t *m, uint32_t len)
{
    if (g_nat.n >= 256) return;
    g_nat.q[g_nat.n].to_b = to_b;
    g_nat.q[g_nat.n].from = *from;
    memcpy(g_nat.q[g_nat.n].m, m, len);
    g_nat.q[g_nat.n].len = len;
    g_nat.n++;
}

/* A sends: through the NAT */
static int send_from_a(void *ctx, uint32_t local_idx, const call_addr_t *to, const uint8_t *m,
                       uint32_t len)
{
    int gather = ctx != NULL;
    (void) local_idx;
    if (g_block) return 0;
    if (g_nat.nopen < 8) g_nat.a_opened[g_nat.nopen++] = *to;
    if (call_addr_eq(to, &g_nat.b_host))
        nat_enqueue(1, &g_nat.a_pub, m, len);
    else if (call_addr_eq(to, &g_nat.refl))
        nat_enqueue(gather ? 4 : 2, &g_nat.a_pub, m, len);
    else
        g_nat.dropped++;
    return 0;
}

static int send_from_b(void *ctx, uint32_t local_idx, const call_addr_t *to, const uint8_t *m,
                       uint32_t len)
{
    (void) ctx;
    (void) local_idx;
    if (g_block) return 0;
    if (call_addr_eq(to, &g_nat.a_pub)) {
        bool open = false;
        for (uint32_t i = 0; i < g_nat.nopen; i++)
            if (call_addr_eq(&g_nat.a_opened[i], &g_nat.b_host)) open = true;
        if (open)
            nat_enqueue(0, &g_nat.b_host, m, len);
        else
            g_nat.dropped++; /* the NAT filters unsolicited packets */
    } else {
        g_nat.dropped++; /* 10.0.0.1 is private: unreachable */
    }
    return 0;
}

static void nat_pump(uint32_t now)
{
    while (g_nat.n) {
        struct nat_msg e = g_nat.q[0];
        memmove(&g_nat.q[0], &g_nat.q[1], (g_nat.n - 1) * sizeof(g_nat.q[0]));
        g_nat.n--;
        if (e.to_b == 1) call_ice_on_packet(&g_nat.B, 0, &e.from, e.m, e.len, now);
        if (e.to_b == 0) {
            call_ice_on_packet(&g_nat.A, 0, &e.from, e.m, e.len, now);
        }
        if (e.to_b == 2 || e.to_b == 4) { /* reflector answers with the mapped address */
            call_stun_t req, rsp;
            if (call_stun_parse(e.m, e.len, &req) > 0 && req.type == CALL_STUN_REQ) {
                memset(&rsp, 0, sizeof(rsp));
                rsp.type = CALL_STUN_RESP;
                memcpy(rsp.txid, req.txid, 12);
                rsp.has_mapped = 1;
                rsp.mapped = e.from;
                uint8_t b[CALL_STUN_MAX];
                int w = call_stun_write(&rsp, NULL, 0, NULL, NULL, b, sizeof(b));
                if (e.to_b == 4 && g_nat.g)
                    call_gather_on_packet(g_nat.g, 0, &g_nat.refl, b, (uint32_t) w);
            }
        }
    }
}

typedef struct dht_node {
    uint8_t id[32];
    call_addr_t addr;
    uint32_t known[6];
    uint32_t nknown;
    int alive;
    int has_record;
} dht_node_t;

static bool rv_verify(void *ctx, const uint8_t id[32], const uint8_t *msg, uint32_t len,
                      const uint8_t sig[64])
{
    return toy_verify(ctx, id, msg, len, sig);
}

static void test_ice(void)
{
    printf("[ice] candidates, binding checks, NAT traversal, DHT rendezvous\n");
    memset(&g_nat, 0, sizeof(g_nat));
    g_nat.a_host = v4(10, 0, 0, 1, 5000);
    g_nat.a_pub = v4(203, 0, 113, 5, 40000);
    g_nat.b_host = v4(198, 51, 100, 7, 6000);
    g_nat.refl = v4(192, 0, 2, 1, 3478);

    /* gathering through a DHT reflector */
    static call_cand_t gc[8];
    static call_gather_t g;
    call_gather_init(&g, gc, 8, 42, send_from_a, &g);
    g_nat.g = &g;
    CHECK(call_gather_add_host(&g, &g_nat.a_host, 65535) == 0);
    call_gather_start(&g, &g_nat.refl, 1, 0);
    nat_pump(0);
    CHECK(g.n == 2 && g.done);
    CHECK(gc[1].type == CALL_CAND_SRFLX && call_addr_eq(&gc[1].addr, &g_nat.a_pub));
    CHECK(gc[0].priority > gc[1].priority);
    printf("  gathered: host 10.0.0.1:5000 (prio %u), srflx 203.0.113.5:40000 (prio %u)\n",
           gc[0].priority, gc[1].priority);
    g_nat.g = NULL;
    g_nat.nopen = 0;

    uint8_t ua[8] = "AAAAaaaa", pa[16] = "passwordA-------", ub[8] = "BBBBbbbb",
            pb[16] = "passwordB-------";
    for (int conflict = 0; conflict < 2; conflict++) {
        g_nat.nopen = 0;
        g_nat.n = 0;
        g_nat.dropped = 0;
        call_ice_init(&g_nat.A, true, 0x1111222233334444ull, ua, pa, 7, send_from_a, NULL, toy_mac,
                      NULL);
        /* conflict: both believe they are controlling */
        call_ice_init(&g_nat.B, conflict ? true : false, 0x9999000011112222ull, ub, pb, 9,
                      send_from_b, NULL, toy_mac, NULL);
        call_ice_add_local(&g_nat.A, &gc[0]);
        call_ice_add_local(&g_nat.A, &gc[1]);
        call_cand_t bh;
        call_cand_make(&bh, CALL_CAND_HOST, &g_nat.b_host, NULL, 65535, 0);
        call_ice_add_local(&g_nat.B, &bh);
        call_ice_set_remote(&g_nat.A, ub, pb, &bh, 1);
        call_ice_set_remote(&g_nat.B, ua, pa, gc, 2);
        call_ice_start(&g_nat.A, 0);
        call_ice_start(&g_nat.B, 0);
        uint32_t done_at = 0;
        for (uint32_t t = 0; t < 10000; t += 5) {
            call_ice_tick(&g_nat.A, t);
            call_ice_tick(&g_nat.B, t);
            nat_pump(t);
            if (!done_at && g_nat.A.state == CALL_ICE_COMPLETED &&
                g_nat.B.state == CALL_ICE_COMPLETED)
                done_at = t;
        }
        const call_pair_t *sa = call_ice_selected(&g_nat.A), *sb = call_ice_selected(&g_nat.B);
        CHECK(sa && sb);
        CHECK(g_nat.A.controlling != g_nat.B.controlling);
        if (sa && sb) {
            CHECK(call_addr_eq(&g_nat.A.remote[sa->r].addr, &g_nat.b_host));
            CHECK(call_addr_eq(&g_nat.B.remote[sb->r].addr, &g_nat.a_pub));
        }
        printf("  %s: both COMPLETED at %u ms, %u+%u checks, %u packets dropped by NAT/private "
               "addresses, role switches %u/%u\n",
               conflict ? "role conflict (both controlling)" : "NAT punch-through", done_at,
               g_nat.A.checks_sent, g_nat.B.checks_sent, g_nat.dropped, g_nat.A.role_switches,
               g_nat.B.role_switches);
        CHECK(done_at > 0 && done_at < 2000);
        if (conflict) CHECK(g_nat.A.role_switches + g_nat.B.role_switches >= 1);
    }
    /* wrong password: integrity fails, nothing completes */
    {
        g_nat.n = 0;
        g_nat.nopen = 0;
        uint8_t bad[16] = "wrongpassword---";
        call_ice_init(&g_nat.A, true, 1, ua, pa, 7, send_from_a, NULL, toy_mac, NULL);
        call_ice_init(&g_nat.B, false, 2, ub, pb, 9, send_from_b, NULL, toy_mac, NULL);
        call_ice_add_local(&g_nat.A, &gc[0]);
        call_cand_t bh;
        call_cand_make(&bh, CALL_CAND_HOST, &g_nat.b_host, NULL, 65535, 0);
        call_ice_add_local(&g_nat.B, &bh);
        call_ice_set_remote(&g_nat.A, ub, bad, &bh, 1);
        call_ice_set_remote(&g_nat.B, ua, pa, gc, 1);
        call_ice_start(&g_nat.A, 0);
        call_ice_start(&g_nat.B, 0);
        for (uint32_t t = 0; t < 10000; t += 5) {
            call_ice_tick(&g_nat.A, t);
            call_ice_tick(&g_nat.B, t);
            nat_pump(t);
        }
        CHECK(g_nat.A.state == CALL_ICE_FAILED && g_nat.B.bad_integrity > 0);
        printf("  wrong ICE password: %u checks rejected by MESSAGE-INTEGRITY, agent FAILED\n",
               g_nat.B.bad_integrity);
    }

    /* the session drives ICE: offer/answer carry ufrag, password and candidates */
    for (int blocked = 0; blocked < 2; blocked++) {
        static sig_link_t l_ab, l_ba;
        static phone_t PA, PB;
        memset(&l_ab, 0, sizeof(l_ab));
        memset(&l_ba, 0, sizeof(l_ba));
        l_ab.delay_ms = l_ba.delay_ms = 30;
        phone_init(&PA, 11, &l_ab, CALL_CAP_UBH168, NULL, 0);
        phone_init(&PB, 22, &l_ba, CALL_CAP_UBH168, NULL, 0);
        g_nat.n = 0;
        g_nat.nopen = 0;
        g_block = blocked;
        call_ice_init(&g_nat.A, true, 0x5555, ua, pa, 7, send_from_a, NULL, toy_mac, NULL);
        call_ice_init(&g_nat.B, false, 0x6666, ub, pb, 9, send_from_b, NULL, toy_mac, NULL);
        call_ice_add_local(&g_nat.A, &gc[0]);
        call_ice_add_local(&g_nat.A, &gc[1]);
        call_cand_t bh;
        call_cand_make(&bh, CALL_CAND_HOST, &g_nat.b_host, NULL, 65535, 0);
        call_ice_add_local(&g_nat.B, &bh);
        PA.s.cfg.ice = &g_nat.A;
        PB.s.cfg.ice = &g_nat.B;
        call_session_dial(&PA.s, 22, CALL_MEDIA_AUDIO | CALL_MEDIA_VIDEO, 0);
        uint32_t conn_at = 0, end_at = 0;
        for (uint32_t t = 0; t < 15000; t += 5) {
            PA.now = PB.now = t;
            link_pump(&l_ab, &PB, t);
            link_pump(&l_ba, &PA, t);
            call_session_tick(&PA.s, t);
            call_session_tick(&PB.s, t);
            nat_pump(t);
            if (PB.s.state == CALL_ST_INCOMING && t >= 500) call_session_accept(&PB.s, t);
            if (!conn_at && PA.s.state == CALL_ST_CONNECTED && PB.s.state == CALL_ST_CONNECTED)
                conn_at = t;
            if (!end_at && PA.s.state == CALL_ST_ENDED) end_at = t;
            if (conn_at) break;
        }
        if (!blocked) {
            CHECK(conn_at > 0);
            CHECK(g_nat.A.state == CALL_ICE_COMPLETED && g_nat.B.state == CALL_ICE_COMPLETED);
            CHECK(PB.s.nrcands == 2);
            printf("  session + ICE: answered at 500 ms, both CONNECTED at %u ms over the "
                   "punched path\n",
                   conn_at);
        } else {
            CHECK(PA.s.end_reason == CALL_END_ICE_FAILED);
            CHECK(PA.notes[CALL_NOTIFY_ENDED] == 1);
            printf("  session + ICE, unreachable peer: ended %s at %u ms\n",
                   call_end_reason_name(PA.s.end_reason), end_at);
        }
    }
    g_block = 0;

    /* DHT rendezvous: 64 nodes, partial routing tables, some dead */
    {
        enum { NN = 64 };
        static dht_node_t nodes[NN];
        uint8_t target[32];
        for (int k = 0; k < 32; k++) target[k] = (uint8_t) rng32();
        for (uint32_t i = 0; i < NN; i++) {
            for (int k = 0; k < 32; k++) nodes[i].id[k] = (uint8_t) rng32();
            nodes[i].addr = v4(100, 64, (uint8_t) (i / 250), (uint8_t) (i % 250 + 1), 7000);
            nodes[i].alive = rnd_below(10) != 0;
        }
        /* the callee registers with the 3 nodes closest to its id */
        for (uint32_t r = 0; r < 3; r++) {
            int best = -1;
            for (uint32_t i = 0; i < NN; i++)
                if (!nodes[i].has_record &&
                    (best < 0 || call_rv_closer(target, nodes[i].id, nodes[best].id) < 0))
                    best = (int) i;
            nodes[best].has_record = 1;
            nodes[best].alive = 1;
        }
        /* routing tables: 3 random + the 3 closest-to-self by XOR among a sample */
        for (uint32_t i = 0; i < NN; i++) {
            nodes[i].nknown = 0;
            for (uint32_t k = 0; k < 6; k++) nodes[i].known[nodes[i].nknown++] = rnd_below(NN);
        }
        call_rv_record_t rec;
        memset(&rec, 0, sizeof(rec));
        memcpy(rec.id, target, 32);
        rec.expires_s = 3600;
        rec.ncands = 2;
        rec.cands[0] = gc[0];
        rec.cands[1] = gc[1];
        uint8_t sb[CALL_RV_MAX_MSG];
        int sl = call_rv_record_signed_bytes(&rec, sb, sizeof(sb));
        toy_sig(target, sb, (uint32_t) sl, rec.sig);
        uint32_t found = 0, trials = 50, qsum = 0, forged_rejected = 0;
        for (uint32_t trial = 0; trial < trials; trial++) {
            call_rv_peer_t seeds[3];
            for (int s = 0; s < 3; s++) {
                uint32_t i = rnd_below(NN);
                memcpy(seeds[s].id, nodes[i].id, 32);
                seeds[s].addr = nodes[i].addr;
            }
            call_lookup_t lk;
            call_lookup_init(&lk, target, seeds, 3, 300, trial * 1000, rv_verify, NULL);
            bool forge = trial % 10 == 9;
            for (uint32_t t = 0; t < 30000 && lk.state == CALL_LOOKUP_RUNNING; t += 10) {
                call_rv_peer_t to;
                call_rv_msg_t q;
                if (!call_lookup_next(&lk, t, &to, &q)) continue;
                uint8_t wire[CALL_RV_MAX_MSG];
                int wl = call_rv_write(&q, wire, sizeof(wire));
                int ni = -1;
                for (uint32_t i = 0; i < NN; i++)
                    if (!memcmp(nodes[i].id, to.id, 32)) ni = (int) i;
                if (ni < 0 || !nodes[ni].alive) continue; /* times out */
                call_rv_msg_t in, rep;
                CHECK(call_rv_parse(wire, (uint32_t) wl, &in) == wl);
                memset(&rep, 0, sizeof(rep));
                rep.txid = in.txid;
                memcpy(rep.target, in.target, 32);
                if (nodes[ni].has_record) {
                    rep.type = CALL_RV_FOUND;
                    rep.rec = rec;
                    if (forge) rep.rec.cands[0].addr.port ^= 1; /* tampered record */
                } else {
                    rep.type = CALL_RV_NOT_FOUND;
                    /* closest known peers, plus the true closest with low odds */
                    for (uint32_t k = 0; k < nodes[ni].nknown && rep.npeers < 8; k++) {
                        uint32_t j = nodes[ni].known[k];
                        memcpy(rep.peers[rep.npeers].id, nodes[j].id, 32);
                        rep.peers[rep.npeers++].addr = nodes[j].addr;
                    }
                    for (uint32_t j = 0; j < NN && rep.npeers < 8; j++)
                        if (nodes[j].has_record &&
                            call_rv_closer(target, nodes[j].id, nodes[ni].id) < 0 &&
                            rnd_below(3) == 0) {
                            memcpy(rep.peers[rep.npeers].id, nodes[j].id, 32);
                            rep.peers[rep.npeers++].addr = nodes[j].addr;
                        }
                }
                int rl = call_rv_write(&rep, wire, sizeof(wire));
                CHECK(rl > 0);
                CHECK(call_rv_parse(wire, (uint32_t) rl, &in) == rl);
                call_lookup_on_reply(&lk, &in);
            }
            if (forge) {
                if (lk.state != CALL_LOOKUP_FOUND && lk.bad_sig > 0) forged_rejected++;
            } else if (lk.state == CALL_LOOKUP_FOUND) {
                found++;
                qsum += lk.queries;
                CHECK(lk.found.ncands == 2 && call_addr_eq(&lk.found.cands[1].addr, &g_nat.a_pub));
            }
        }
        printf("  rendezvous over 64 DHT nodes (10%% dead): %u/45 lookups found the record, mean "
               "%.1f queries; %u/5 tampered records rejected\n",
               found, found ? (double) qsum / found : 0.0, forged_rejected);
        CHECK(found >= 40);
        CHECK(forged_rejected == 5);
    }
}

/* ================================================================
 * 8. fuzzing every wire format
 * ================================================================ */

static void mutate(uint8_t *b, uint32_t *len, uint32_t cap)
{
    uint32_t ops = 1 + rnd_below(4);
    for (uint32_t o = 0; o < ops; o++) {
        uint32_t k = rnd_below(6);
        if (*len == 0) k = 3;
        switch (k) {
        case 0:
            b[rnd_below(*len)] ^= (uint8_t) (1u << rnd_below(8));
            break;
        case 1:
            b[rnd_below(*len)] = (uint8_t) rng32();
            break;
        case 2:
            *len = rnd_below(*len + 1);
            break;
        case 3:
            if (*len < cap) b[(*len)++] = (uint8_t) rng32();
            break;
        case 4: {
            uint8_t specials[] = {0, 1, 0x7f, 0x80, 0xff, 0xfe};
            b[rnd_below(*len)] = specials[rnd_below(6)];
            break;
        }
        default:
            if (*len >= 2) {
                uint32_t i = rnd_below(*len - 1);
                uint8_t t = b[i];
                b[i] = b[i + 1];
                b[i + 1] = t;
            }
            break;
        }
    }
}

#define FUZZ_N 25000

static uint32_t fuzz_input(uint8_t *buf, uint32_t cap, const uint8_t seeds[][3000],
                           const uint32_t *seedlen, uint32_t nseeds, uint32_t i)
{
    uint32_t len;
    if (i % 5 == 0) {
        len = rnd_below(cap < 400 ? cap : 400);
        for (uint32_t k = 0; k < len; k++) buf[k] = (uint8_t) rng32();
    } else {
        uint32_t s = rnd_below(nseeds);
        len = seedlen[s];
        memcpy(buf, seeds[s], len);
        mutate(buf, &len, cap);
    }
    return len;
}

static void test_fuzz(void)
{
    printf("[fuzz] %u inputs per parser (mutated valid messages + random bytes)\n", FUZZ_N);
    static uint8_t seeds[16][3000];
    static uint32_t slen[16];
    static uint8_t buf[3000], out[3000];
    uint32_t ns, acc;

    /* RTP packets, plain */
    {
        static pk_sink_t s;
        s.n = 0;
        call_packetizer_t p;
        uint8_t fr[2000], scratch[1600];
        for (int i = 0; i < 2000; i++) fr[i] = (uint8_t) i;
        call_packetizer_init(&p, 5, CALL_PT_OPUS, 1, 600, 1);
        call_packetize(&p, fr, 1500, 77, true, scratch, sizeof(scratch), sink_emit, &s);
        call_packetize(&p, fr, 30, 78, false, scratch, sizeof(scratch), sink_emit, &s);
        ns = s.n;
        for (uint32_t i = 0; i < ns; i++) {
            memcpy(seeds[i], s.pkts[i], s.lens[i]);
            slen[i] = s.lens[i];
        }
        acc = 0;
        static call_reasm_slot_t rs[2];
        static uint8_t rmem[2 * 4096];
        call_reasm_t r;
        call_reasm_init(&r, rs, 2, rmem, 4096);
        uint32_t reenc_ok = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 2000, (const uint8_t(*)[3000]) seeds, slen, ns, i);
            call_rtp_hdr_t h;
            const uint8_t *pl;
            int n = call_rtp_parse(buf, len, &h, &pl);
            if (n > 0) {
                acc++;
                call_frame_t f;
                call_reasm_push(&r, &h, pl, &f);
                uint8_t hb[20];
                if (call_rtp_hdr_write(&h, hb, 20) == 20 && !memcmp(hb, buf, 20)) reenc_ok++;
            }
        }
        CHECK(reenc_ok == acc);
        printf("  rtp packet        : %u accepted, all re-encode byte-exact\n", acc);

        /* UBH carriage */
        uint32_t nseed = ns, full = 0;
        for (uint32_t i = 0; i < nseed; i++) {
            int w = call_rtp_encap(CALL_CARRIAGE_UBH168, s.pkts[i], s.lens[i], seeds[i], 3000);
            slen[i] = (uint32_t) w;
        }
        acc = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 3000, (const uint8_t(*)[3000]) seeds, slen, nseed, i);
            if (i % 2) len -= len % 21; /* keep many inputs frame-aligned */
            int n = call_rtp_decap(CALL_CARRIAGE_UBH168, buf, len, out, sizeof(out), 0);
            if (n > 0) {
                acc++;
                call_rtp_hdr_t h;
                if (call_rtp_parse(out, (uint32_t) n, &h, NULL) > 0) full++;
            }
            call_ubh_unwrap(buf, len, out, sizeof(out));
        }
        printf("  ubh-168 carriage  : %u accepted, %u of them full valid packets\n", acc, full);
    }

    /* congestion feedback */
    {
        call_cc_feedback_t fb = {0x1234, 7, 999, 100, 1500000, -3000, 0xdead, 50, 13, 16};
        slen[0] = (uint32_t) call_cc_fb_write(&fb, seeds[0], 3000);
        acc = 0;
        uint32_t same = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 64, (const uint8_t(*)[3000]) seeds, slen, 1, i);
            call_cc_feedback_t in;
            if (call_cc_fb_parse(buf, len, &in) > 0) {
                acc++;
                uint8_t re[CALL_CC_FB_LEN];
                call_cc_fb_write(&in, re, sizeof(re));
                if (!memcmp(re, buf, CALL_CC_FB_LEN)) same++;
                call_cc_tx_t tx;
                call_cc_tx_init(&tx, 500000, 30000, 5000000);
                call_cc_tx_on_feedback(&tx, &in, 100, 100000);
                CHECK(tx.target_bps >= 30000 && tx.target_bps <= 5000000);
            }
        }
        CHECK(same == acc);
        printf("  cc feedback       : %u accepted, all re-encode byte-exact, targets in range\n",
               acc);
    }

    /* FEC parity packets into a live decoder */
    {
        static pk_sink_t med, fec;
        med.n = fec.n = 0;
        call_fec_enc_t enc;
        static uint8_t parity[700], scratch[800];
        call_fec_enc_init(&enc, 3, 4, 3, parity, sizeof(parity), 0);
        call_packetizer_t p;
        call_packetizer_init(&p, 3, CALL_PT_OPUS, 0, 600, 10);
        uint8_t fr[500];
        for (int i = 0; i < 6; i++) {
            memset(fr, i, sizeof(fr));
            call_packetize(&p, fr, 100 + 50 * i, 0, false, scratch, sizeof(scratch), sink_emit,
                           &med);
            call_fec_enc_add(&enc, med.pkts[i], med.lens[i], scratch, sizeof(scratch), sink_emit,
                             &fec);
        }
        ns = fec.n;
        for (uint32_t i = 0; i < ns; i++) {
            memcpy(seeds[i], fec.pkts[i], fec.lens[i]);
            slen[i] = fec.lens[i];
        }
        static call_fec_mslot_t ms[64];
        static uint8_t mmem[64 * 700];
        static call_fec_fslot_t fs[4];
        static uint8_t fmem[4 * 700], work[700];
        call_fec_dec_t dec;
        call_fec_dec_init(&dec, 3, ms, mmem, 64, 700, fs, fmem, 4, 700, work);
        call_fec_dec_media(&dec, med.pkts[0], med.lens[0], NULL, NULL);
        call_fec_dec_media(&dec, med.pkts[2], med.lens[2], NULL, NULL);
        acc = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 800, (const uint8_t(*)[3000]) seeds, slen, ns, i);
            if (call_fec_dec_parity(&dec, buf, len, NULL, NULL) >= 0) acc++;
            call_fec_hdr_t fh;
            call_fec_hdr_parse(buf, len, &fh);
            if (i % 1000 == 0) { /* refresh the media so recovery paths keep running */
                call_fec_dec_init(&dec, 3, ms, mmem, 64, 700, fs, fmem, 4, 700, work);
                call_fec_dec_media(&dec, med.pkts[0], med.lens[0], NULL, NULL);
                call_fec_dec_media(&dec, med.pkts[2], med.lens[2], NULL, NULL);
            }
        }
        printf("  fec parity        : %u accepted, %u recoveries, %u rejected after re-parse\n",
               acc, dec.recovered, dec.bad_recovery);
    }

    /* mesh membership */
    {
        static mesh_world_t w;
        make_world(&w, 4, false);
        for (uint32_t i = 0; i < 4; i++) {
            memcpy(seeds[i], w.msg[i], w.len[i]);
            slen[i] = w.len[i];
        }
        call_mesh_member_t lv = w.m[0];
        lv.version = 3;
        slen[4] = (uint32_t) call_mesh_encode_leave(&lv, 77, seeds[4], 3000, toy_sign, lv.pubkey);
        static call_mesh_member_t table[40];
        call_mesh_t M;
        call_mesh_init(&M, 77, table, 40, toy_verify, NULL);
        static call_mesh_member_t m;
        acc = 0;
        uint32_t applied = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 1000, (const uint8_t(*)[3000]) seeds, slen, 5, i);
            uint32_t gid;
            uint8_t type;
            if (call_mesh_parse(buf, len, &gid, &type, &m) == CALL_OK) acc++;
            int r = call_mesh_on_message(&M, buf, len);
            if (r == 1) applied++;
        }
        static call_mesh_edge_t e[4096];
        static call_mesh_delivery_t d[4096];
        static uint32_t up[40], scr[20000];
        call_mesh_plan_t pl = {e, 4096, 0, d, 4096, 0, up, 40, 0, 0, 0, 0, 0};
        CHECK(call_mesh_build(&M, &pl, scr, 20000) == CALL_OK);
        CHECK(verify_plan(&M, &pl).over_uplink == 0);
        printf("  mesh member/leave : %u parsed, %u applied (signature still required)\n", acc,
               applied);
    }

    /* STUN-like binding messages, into a live agent */
    {
        call_stun_t m;
        memset(&m, 0, sizeof(m));
        m.type = CALL_STUN_REQ;
        memset(m.txid, 7, 12);
        m.has_username = 1;
        memcpy(m.username, "AAAAaaaaBBBBbbbb", 16);
        m.has_priority = 1;
        m.priority = 0x6e00ffff;
        m.use_candidate = 1;
        m.has_controlling = 1;
        m.tie = 0x0102030405060708ull;
        slen[0] = (uint32_t) call_stun_write(&m, (const uint8_t *) "passwordA-------", 16, toy_mac,
                                             NULL, seeds[0], 3000);
        memset(&m, 0, sizeof(m));
        m.type = CALL_STUN_RESP;
        m.has_mapped = 1;
        m.mapped = v4(1, 2, 3, 4, 5);
        slen[1] = (uint32_t) call_stun_write(&m, NULL, 0, NULL, NULL, seeds[1], 3000);
        m.mapped.family = 6;
        memset(m.mapped.ip, 0xab, 16);
        slen[2] = (uint32_t) call_stun_write(&m, NULL, 0, NULL, NULL, seeds[2], 3000);
        memset(&m, 0, sizeof(m));
        m.type = CALL_STUN_ERR;
        m.has_error = 1;
        m.error = 487;
        slen[3] = (uint32_t) call_stun_write(&m, NULL, 0, NULL, NULL, seeds[3], 3000);
        acc = 0;
        call_ice_t ag;
        uint8_t ua[8] = "AAAAaaaa", pa[16] = "passwordA-------", ub[8] = "BBBBbbbb";
        call_ice_init(&ag, false, 5, ua, pa, 3, send_from_b, NULL, NULL, NULL);
        call_cand_t c;
        call_cand_make(&c, CALL_CAND_HOST, &g_nat.b_host, NULL, 1, 0);
        call_ice_add_local(&ag, &c);
        call_cand_t rc;
        call_cand_make(&rc, CALL_CAND_SRFLX, &g_nat.a_pub, NULL, 1, 0);
        call_ice_set_remote(&ag, ub, pa, &rc, 1);
        call_ice_start(&ag, 0);
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 200, (const uint8_t(*)[3000]) seeds, slen, 4, i);
            call_stun_t in;
            if (call_stun_parse(buf, len, &in) > 0) {
                acc++;
                call_stun_verify(&in, buf, len, pa, 16, toy_mac, NULL);
            }
            call_addr_t from = v4(9, 9, 9, (uint8_t) i, (uint16_t) i);
            call_ice_on_packet(&ag, 0, &from, buf, len, i);
            call_ice_tick(&ag, i);
            g_nat.n = 0;
        }
        printf("  stun binding      : %u accepted; agent now knows %u remote candidates\n", acc,
               ag.nremote);
    }

    /* rendezvous messages and candidates */
    {
        call_rv_msg_t m;
        memset(&m, 0, sizeof(m));
        m.type = CALL_RV_REGISTER;
        m.txid = 5;
        memset(m.rec.id, 3, 32);
        m.rec.ncands = 2;
        call_cand_make(&m.rec.cands[0], CALL_CAND_HOST, &g_nat.a_host, NULL, 1, 0);
        call_cand_make(&m.rec.cands[1], CALL_CAND_SRFLX, &g_nat.a_pub, &g_nat.a_host, 1, 0);
        slen[0] = (uint32_t) call_rv_write(&m, seeds[0], 3000);
        m.type = CALL_RV_INTRODUCE;
        slen[1] = (uint32_t) call_rv_write(&m, seeds[1], 3000);
        m.type = CALL_RV_NOT_FOUND;
        m.npeers = 3;
        m.peers[1].addr = g_nat.b_host;
        m.peers[0].addr = g_nat.b_host;
        m.peers[2].addr = g_nat.a_pub;
        slen[2] = (uint32_t) call_rv_write(&m, seeds[2], 3000);
        m.type = CALL_RV_LOOKUP;
        slen[3] = (uint32_t) call_rv_write(&m, seeds[3], 3000);
        m.type = CALL_RV_REGISTERED;
        slen[4] = (uint32_t) call_rv_write(&m, seeds[4], 3000);
        acc = 0;
        uint32_t same = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 600, (const uint8_t(*)[3000]) seeds, slen, 5, i);
            call_rv_msg_t in;
            int r = call_rv_parse(buf, len, &in);
            if (r > 0) {
                acc++;
                int w = call_rv_write(&in, out, sizeof(out));
                if (w == (int) len && !memcmp(out, buf, len)) same++;
            }
            call_cand_t c;
            if (call_cand_parse(buf, len, &c) > 0) {
                uint8_t cb[CALL_CAND_WIRE];
                call_cand_write(&c, cb, sizeof(cb));
            }
        }
        printf("  rendezvous        : %u accepted, %u re-encode byte-exact\n", acc, same);
        CHECK(acc > 0);
    }

    /* signalling into live sessions */
    {
        call_sig_t m;
        memset(&m, 0, sizeof(m));
        m.type = CALL_MSG_OFFER;
        memset(m.call_id, 9, 8);
        m.from = 11;
        m.to = 22;
        m.tie = 99;
        m.caps = 1;
        m.media = 3;
        m.ncodecs = 2;
        m.codecs[0] = (call_codec_t){CALL_PT_OPUS, CALL_MEDIA_AUDIO};
        m.codecs[1] = (call_codec_t){CALL_PT_VP8, CALL_MEDIA_VIDEO};
        m.ncands = 1;
        m.cands[0] = (call_cand_t){0};
        call_cand_make(&m.cands[0], CALL_CAND_HOST, &g_nat.a_host, NULL, 1, 0);
        slen[0] = (uint32_t) call_sig_write(&m, seeds[0], 3000);
        m.type = CALL_MSG_ANSWER;
        slen[1] = (uint32_t) call_sig_write(&m, seeds[1], 3000);
        uint8_t types[] = {CALL_MSG_RINGING, CALL_MSG_BYE,    CALL_MSG_HOLD,
                           CALL_MSG_ACK,     CALL_MSG_CANCEL, CALL_MSG_REJECT};
        for (int i = 0; i < 6; i++) {
            call_sig_t s2;
            memset(&s2, 0, sizeof(s2));
            s2.type = types[i];
            memset(s2.call_id, 9, 8);
            s2.from = 11;
            s2.to = 22;
            s2.reason = 2;
            slen[2 + i] = (uint32_t) call_sig_write(&s2, seeds[2 + i], 3000);
        }
        static sig_link_t l1, l2;
        static phone_t P, Q;
        memset(&l1, 0, sizeof(l1));
        memset(&l2, 0, sizeof(l2));
        phone_init(&P, 22, &l1, 1, NULL, 0);
        phone_init(&Q, 11, &l2, 1, NULL, 0);
        acc = 0;
        uint32_t same = 0;
        for (uint32_t i = 0; i < FUZZ_N; i++) {
            uint32_t len = fuzz_input(buf, 400, (const uint8_t(*)[3000]) seeds, slen, 8, i);
            call_sig_t in;
            if (call_sig_parse(buf, len, &in) > 0) {
                acc++;
                int w = call_sig_write(&in, out, sizeof(out));
                if (w == (int) len && !memcmp(out, buf, len)) same++;
            }
            P.now = i;
            call_session_on_message(&P.s, buf, len, i);
            call_session_tick(&P.s, i);
            if (P.s.state == CALL_ST_INCOMING && i % 3 == 0) call_session_accept(&P.s, i);
            if (P.s.state == CALL_ST_ENDED) call_session_reset(&P.s);
            l1.n = 0;
        }
        CHECK(same == acc);
        printf("  call signalling   : %u accepted, all re-encode byte-exact; session survived\n",
               acc);
    }
}

int main(void)
{
    test_rtp();
    test_jitter();
    test_cc();
    test_fec();
    test_mesh();
    test_session();
    test_ice();
    test_fuzz();
    printf("\n%d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
