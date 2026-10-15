/* fab_bench.c — measures stream fabrication: wire bytes per frame, bit
 * rate at the frame rate, build and apply time, fidelity, and (with --udp)
 * loss and latency of 60 fps frames over UDP on this host's loopback.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 *   fab_bench [--frames N] [--udp]
 *
 * Three synthetic 640x360 RGB scenes (691,200 bytes a frame), so the
 * numbers are reproducible and the content is known:
 *   static+shared  a background block and a sprite block both public; the
 *                  sprite moves and 200 scattered pixels change each frame
 *                  (a talking head over a still room, as a proxy)
 *   static+private the same, but the sprite is not public, so its pixels
 *                  arrive in the diff when it first appears
 *   noise          every frame fresh random bytes: nothing to share
 * Every frame is lossless: fidelity is the fraction of frames rebuilt bit
 * for bit (any mismatch is a bug, not a quality setting). Nothing here
 * models lossy or model-based reconstruction; those make a different
 * picture and would need their own fidelity score.
 *
 * --udp sends the static+shared stream at 60 fps from one process to
 * another over 127.0.0.1 in datagrams of at most 1400 bytes and reports
 * frames lost and one-way latency (first byte sent to frame applied). That
 * is loopback on one host, not a radio mesh: it measures this code's own
 * cost, not air time, contention or loss on a real link.
 */
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "../../kernel/src/fab/fab.h"

#define WD  640u
#define HT  360u
#define FB  (WD * HT * 3u)
#define SP  48u
#define SPB (SP * SP * 3u)
#define FPS 60u

static fab_work_t W;
static uint8_t bg[FB], sprite[SPB], fr[2][FB], out[FB], prevr[FB], diffb[FB], tdiff[FB];
static uint8_t opsb[FAB_OP_REC * 65536], plan[FAB_PLAN_HDR + 4 * FAB_BLOCK_REC + 65536 * 12];
static uint8_t planr[sizeof plan], frame[sizeof plan + FB + 64], rec[1u << 21];
static fab_ient_t table[1u << 18];
static uint8_t spk[FAB_SIG_PK], ssk[FAB_SIG_SK], ek[FAB_KEM_EK], dk[FAB_KEM_DK];

static uint64_t rs = 0x2545F4914F6CDD1Dull;
static uint32_t rnd32(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return (uint32_t) (rs >> 16);
}

static uint64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t) t.tv_sec * 1000000000ull + (uint64_t) t.tv_nsec;
}

static int bget(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    fab_src_t *s = (fab_src_t *) ctx;
    for (int i = 0; i < 2; i++)
        if (s[i].len && ipfsn_cid_equal(cid, &s[i].cid) && s[i].len <= cap) {
            memcpy(buf, s[i].data, s[i].len);
            *len = s[i].len;
            return 0;
        }
    return -1;
}

static int bview(void *ctx, const ipfsn_cid_t *cid, const uint8_t **data, uint32_t *len)
{
    fab_src_t *s = (fab_src_t *) ctx;
    for (int i = 0; i < 2; i++)
        if (s[i].len && !s[i].prev && ipfsn_cid_equal(cid, &s[i].cid)) {
            *data = s[i].data;
            *len = s[i].len;
            return 0;
        }
    return -1;
}

enum { SCENE_SHARED, SCENE_PRIVATE, SCENE_NOISE };

/* Frame k of a scene into f. */
static void render(int scene, uint32_t k, uint8_t *f)
{
    if (scene == SCENE_NOISE) {
        for (uint32_t i = 0; i < FB; i += 4) {
            uint32_t r = rnd32();
            memcpy(f + i, &r, FB - i < 4 ? FB - i : 4);
        }
        return;
    }
    memcpy(f, bg, FB);
    uint32_t x = (40u + k * 3u) % (WD - SP), y = (60u + k) % (HT - SP);
    for (uint32_t r = 0; r < SP; r++)
        memcpy(f + ((y + r) * WD + x) * 3u, sprite + r * SP * 3u, SP * 3u);
    for (uint32_t i = 0; i < 200; i++) {
        uint32_t p = (rnd32() % (WD * HT)) * 3u;
        f[p] = (uint8_t) rnd32();
    }
}

typedef struct {
    uint64_t wire, plan, diff, ops, build_ns, apply_ns, exact, frames;
    uint64_t max_wire;
} stats_t;

/* Open a stream for `scene`, then send and apply `n` frames. Each sealed
 * frame is handed to `sink` when not NULL (the UDP sender). */
static int run_scene(int scene, uint32_t n, stats_t *st, fab_stream_t *tx, fab_stream_t *rx,
                     void (*sink)(const uint8_t *, uint32_t, uint64_t, void *), void *sink_ctx)
{
    fab_src_t src[3];
    memset(src, 0, sizeof src);
    memset(st, 0, sizeof *st);
    uint32_t nsrc = 0;
    if (scene != SCENE_NOISE) {
        src[nsrc].data = bg;
        src[nsrc].len = FB;
        ipfsn_cid_sha256(IPFSN_MC_RAW, bg, FB, &src[nsrc].cid);
        nsrc++;
    }
    if (scene == SCENE_SHARED) {
        src[nsrc].data = sprite;
        src[nsrc].len = SPB;
        ipfsn_cid_sha256(IPFSN_MC_RAW, sprite, SPB, &src[nsrc].cid);
        nsrc++;
    }
    fab_io_t io = {bget, src, bview, src, 0, 0, diffb, FB, out, FB, &W};
    uint32_t no, dl, pl, len, olen;
    ipfsn_cid_t tc;

    /* frame 0: the open recipe */
    render(scene, 0, fr[0]);
    if (fab_build(src, nsrc, fr[0], FB, 16, table, 18, opsb, 65536, &no, tdiff, FB, &dl)) return 1;
    fab_target_cid(fr[0], FB, &W, &tc);
    if (fab_plan_write(&tc, FB, 1000000u / FPS, src, nsrc, opsb, no, plan, sizeof plan, &pl))
        return 1;
    uint8_t coins[32] = {1}, srnd[32] = {2};
    fab_seal_args_t a = {FAB_KIND_STREAM, FAB_MODE_SEALED, spk, ssk, ek, coins, srnd};
    if (fab_recipe_seal(&a, plan, pl, tdiff, dl, tx, rec, sizeof rec, &len)) return 1;
    fab_recipe_t r;
    fab_plan_t p;
    if (fab_recipe_parse(rec, len, &r) || fab_recipe_check(&r, 0, 0) ||
        fab_open_plan(&r, dk, planr, sizeof planr, &p) || fab_assemble(&r, &p, dk, &io, rx, &olen))
        return 1;
    memcpy(prevr, out, FB);

    for (uint32_t k = 1; k <= n; k++) {
        uint8_t *prev = fr[(k - 1) & 1], *cur = fr[k & 1];
        render(scene, k, cur);
        uint64_t t0 = now_ns();
        src[nsrc].data = prev;
        src[nsrc].len = FB;
        src[nsrc].prev = true;
        if (fab_build(src, nsrc + 1, cur, FB, 16, table, 18, opsb, 65536, &no, tdiff, FB, &dl))
            return 1;
        fab_target_cid(cur, FB, &W, &tc);
        if (fab_plan_write(&tc, FB, 0, src, nsrc + 1, opsb, no, plan, sizeof plan, &pl)) return 1;
        if (fab_frame_seal(tx, k - 1, k, plan, pl, tdiff, dl, frame, sizeof frame, &len)) return 1;
        uint64_t t1 = now_ns();
        if (sink) {
            sink(frame, len, k, sink_ctx);
        } else {
            if (fab_frame_apply(rx, frame, len, prevr, FB, planr, sizeof planr, &io, &olen))
                return 1;
            st->apply_ns += now_ns() - t1;
            st->exact += olen == FB && memcmp(out, cur, FB) == 0;
            memcpy(prevr, out, FB);
        }
        st->build_ns += t1 - t0;
        st->wire += len;
        st->plan += pl;
        st->diff += dl;
        st->ops += no;
        if (len > st->max_wire) st->max_wire = len;
        st->frames++;
    }
    return 0;
}

static void report(const char *name, const stats_t *s)
{
    double f = (double) s->frames;
    printf("%-15s %9.0f %9.0f %7.0f %9.0f %10.1f %9.0f %9.0f %7.3f\n", name, s->wire / f,
           (double) s->max_wire, s->ops / f, s->diff / f, s->wire / f * 8.0 * FPS / 1000.0,
           s->build_ns / f / 1000.0, s->apply_ns / f / 1000.0, s->exact / f);
}

/* ---- UDP loopback -------------------------------------------------------- */

#define MTU_PAY  1400u
#define FRAG_HDR 16u /* seq u32, idx u16, count u16, sent_ns u64 */

typedef struct {
    int fd;
    struct sockaddr_in to;
    uint64_t t0;
} udp_tx_t;

static void udp_sink(const uint8_t *f, uint32_t len, uint64_t k, void *ctx)
{
    udp_tx_t *u = (udp_tx_t *) ctx;
    uint64_t slot = u->t0 + k * (1000000000ull / FPS);
    while (now_ns() < slot) {
        struct timespec ts = {0, 100000};
        nanosleep(&ts, 0);
    }
    uint32_t cnt = (len + MTU_PAY - 1u) / MTU_PAY;
    uint64_t sent = now_ns();
    uint8_t pkt[FRAG_HDR + MTU_PAY];
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t o = i * MTU_PAY, n = len - o < MTU_PAY ? len - o : MTU_PAY;
        uint32_t s32 = (uint32_t) k;
        memcpy(pkt, &s32, 4);
        uint16_t i16 = (uint16_t) i, c16 = (uint16_t) cnt;
        memcpy(pkt + 4, &i16, 2);
        memcpy(pkt + 6, &c16, 2);
        memcpy(pkt + 8, &sent, 8);
        memcpy(pkt + FRAG_HDR, f + o, n);
        sendto(u->fd, pkt, FRAG_HDR + n, 0, (struct sockaddr *) &u->to, sizeof u->to);
    }
}

static int udp_test(uint32_t n)
{
    int rfd = socket(AF_INET, SOCK_DGRAM, 0), tfd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (rfd < 0 || tfd < 0 || bind(rfd, (struct sockaddr *) &a, sizeof a)) return 1;
    socklen_t al = sizeof a;
    getsockname(rfd, (struct sockaddr *) &a, &al);
    int big = 8 << 20;
    setsockopt(rfd, SOL_SOCKET, SO_RCVBUF, &big, sizeof big);

    static fab_stream_t tx, rx;
    stats_t st;
    /* Open the stream in this process first so both ends share frame 0 and
     * the stream key, as a real receiver would after the open recipe. */
    pid_t pid = fork();
    if (pid == 0) {
        close(tfd);
        /* the receiver rebuilds its state from the same deterministic open */
        rs = 0x2545F4914F6CDD1Dull;
        fab_src_t src[2];
        memset(src, 0, sizeof src);
        src[0].data = bg;
        src[0].len = FB;
        ipfsn_cid_sha256(IPFSN_MC_RAW, bg, FB, &src[0].cid);
        src[1].data = sprite;
        src[1].len = SPB;
        ipfsn_cid_sha256(IPFSN_MC_RAW, sprite, SPB, &src[1].cid);
        fab_io_t io = {bget, src, bview, src, 0, 0, diffb, FB, out, FB, &W};
        if (run_scene(SCENE_SHARED, 0, &st, &tx, &rx, 0, 0)) _exit(2);
        static uint8_t asm_buf[sizeof frame];
        uint32_t want = 0, got = 0, applied = 0, bad = 0, olen;
        uint32_t cur = 0;
        uint64_t lat_sum = 0, lat_max = 0;
        uint8_t pkt[FRAG_HDR + MTU_PAY];
        struct timeval tv = {3, 0}; /* the sender starts after a 1 s pause */
        setsockopt(rfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        uint64_t sent = 0;
        uint32_t flen = 0;
        for (;;) {
            ssize_t r = recv(rfd, pkt, sizeof pkt, 0);
            if (r < (ssize_t) FRAG_HDR) break;
            uint32_t s32;
            uint16_t i16, c16;
            memcpy(&s32, pkt, 4);
            memcpy(&i16, pkt + 4, 2);
            memcpy(&c16, pkt + 6, 2);
            if (s32 != cur) {
                cur = s32;
                want = c16;
                got = 0;
                flen = 0;
                memcpy(&sent, pkt + 8, 8);
            }
            memcpy(asm_buf + (uint32_t) i16 * MTU_PAY, pkt + FRAG_HDR, (size_t) r - FRAG_HDR);
            got++;
            if ((uint32_t) i16 == want - 1u)
                flen = (uint32_t) i16 * MTU_PAY + (uint32_t) (r - FRAG_HDR);
            if (got == want && flen) {
                if (fab_frame_apply(&rx, asm_buf, flen, prevr, FB, planr, sizeof planr, &io,
                                    &olen) == FAB_OK) {
                    uint64_t l = now_ns() - sent;
                    lat_sum += l;
                    if (l > lat_max) lat_max = l;
                    memcpy(prevr, out, FB);
                    applied++;
                } else {
                    bad++;
                }
            }
        }
        printf("udp loopback: %u/%u frames applied, %u failed, %u lost; latency mean %.0f us, "
               "max %.0f us (send of first datagram to frame rebuilt and checked)\n",
               applied, n, bad, n - applied - bad,
               applied ? lat_sum / (double) applied / 1000.0 : 0.0, lat_max / 1000.0);
        fflush(stdout);
        _exit(applied ? 0 : 3);
    }
    close(rfd);
    sleep(1);
    rs = 0x2545F4914F6CDD1Dull;
    udp_tx_t u = {tfd, a, now_ns()};
    if (run_scene(SCENE_SHARED, n, &st, &tx, &rx, udp_sink, &u)) return 1;
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 1;
}

int main(int argc, char **argv)
{
    uint32_t n = 120;
    int udp = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            n = (uint32_t) atoi(argv[++i]);
        else if (!strcmp(argv[i], "--udp"))
            udp = 1;
        else {
            fprintf(stderr, "usage: fab_bench [--frames N] [--udp]\n");
            return 1;
        }
    }
    uint8_t s[32] = {7}, d[32] = {8}, z[32] = {9};
    fab_sig_keypair(s, spk, ssk);
    fab_kem_keypair(d, z, ek, dk);
    /* a smooth background and a sprite */
    for (uint32_t y = 0; y < HT; y++)
        for (uint32_t x = 0; x < WD; x++) {
            uint8_t *p = bg + (y * WD + x) * 3u;
            p[0] = (uint8_t) (x / 3u);
            p[1] = (uint8_t) (y / 2u);
            p[2] = (uint8_t) ((x ^ y) & 0x3F);
        }
    for (uint32_t i = 0; i < SPB; i++) sprite[i] = (uint8_t) rnd32();

    if (udp) return udp_test(n);

    static fab_stream_t tx, rx;
    stats_t st;
    printf("%u frames of %ux%u RGB (%u bytes), %u fps, lossless\n", n, WD, HT, FB, FPS);
    printf("%-15s %9s %9s %7s %9s %10s %9s %9s %7s\n", "scene", "wire B/f", "max B", "ops/f",
           "diff B/f", "kbit/s", "build us", "apply us", "exact");
    const char *names[3] = {"static+shared", "static+private", "noise"};
    for (int sc = 0; sc < 3; sc++) {
        rs = 0x2545F4914F6CDD1Dull + (uint64_t) sc;
        if (run_scene(sc, n, &st, &tx, &rx, 0, 0)) {
            printf("%s: failed\n", names[sc]);
            return 1;
        }
        report(names[sc], &st);
    }
    printf("raw video at this size: %.0f kbit/s\n", FB * 8.0 * FPS / 1000.0);
    return 0;
}
