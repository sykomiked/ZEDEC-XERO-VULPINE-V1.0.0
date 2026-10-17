/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vna_door.c — the seed's plain-http door: files, the l2 stream and
 * the POSTed delta, end to end on fake connections. */
#include <stdio.h>
#include <string.h>
#include "vna_door.h"

static int passes, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            passes++;                                                                              \
        else {                                                                                     \
            failures++;                                                                            \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                           \
        }                                                                                          \
    } while (0)

/* the host's tree: two files and a block */
static const char *FILE_A = "<html>the doorway</html>";
static const uint8_t BLOB[37] = {7};

static int32_t fsize(void *ctx, const char *path, uint32_t plen)
{
    (void) ctx;
    if (plen == 11 && !memcmp(path, "/index.html", 11)) return (int32_t) strlen(FILE_A);
    if (plen == 17 && !memcmp(path, "/ipfs/bafybeiblob", 17)) return (int32_t) sizeof BLOB;
    return -1;
}

static int32_t fbody(void *ctx, const char *path, uint32_t plen, uint64_t off, uint8_t *out,
                     uint32_t cap)
{
    (void) ctx;
    const uint8_t *src = 0;
    uint32_t len = 0;
    if (plen == 11 && !memcmp(path, "/index.html", 11)) {
        src = (const uint8_t *) FILE_A;
        len = (uint32_t) strlen(FILE_A);
    } else if (plen == 17 && !memcmp(path, "/ipfs/bafybeiblob", 17)) {
        src = BLOB;
        len = sizeof BLOB;
    } else
        return -1;
    if (off >= len) return 0;
    uint32_t take = len - (uint32_t) off;
    if (take > cap) take = cap;
    memcpy(out, src + off, take);
    return (int32_t) take;
}

static uint32_t gossiped;
static uint8_t gossip_buf[600];
static void gsp(void *ctx, const uint8_t *b, uint32_t n)
{
    (void) ctx;
    gossiped++;
    memcpy(gossip_buf, b, n < sizeof gossip_buf ? n : sizeof gossip_buf);
}

static int qrefuse;
static uint32_t qseen;
static int32_t fquery(void *ctx, const uint8_t *in, uint32_t in_len, uint8_t *out,
                      uint32_t cap)
{
    (void) ctx;
    (void) in;
    if (qrefuse) return -1;
    qseen = in_len;
    if (cap < 3) return -1;
    out[0] = 0xAA;
    out[1] = 0xBB;
    out[2] = 0xCC;
    return 3;
}

static int delta(uint16_t cell, uint8_t lattice, uint8_t *out)
{
    vna_delta_t d;
    memset(&d, 0, sizeof d);
    d.cell = cell;
    d.lattice = lattice;
    return vna_delta_encode(&d, out, VNA_DELTA_HDR);
}

/* pull until the conn stops giving; collect into `acc` */
static uint32_t drain(vna_door_t *d, uint32_t c, uint8_t *acc, uint32_t cap)
{
    uint8_t tmp[256];
    uint32_t n, at = 0;
    while ((n = vna_door_pull(d, c, tmp, sizeof tmp)) && at + n <= cap) {
        memcpy(acc + at, tmp, n);
        at += n;
    }
    return at;
}

int main(void)
{
    vna_door_t d;
    uint8_t req[2048], acc[4096], pk[64];
    vna_door_init(&d, 0, fsize, 0, fbody, gsp);

    /* ---- the doorway: GET / answers the index ---- */
    int c = vna_door_open(&d);
    CHECK(c >= 0);
    int n = snprintf((char *) req, sizeof req, "GET / HTTP/1.1\r\nHost: s\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    uint32_t got = drain(&d, (uint32_t) c, acc, sizeof acc);
    acc[got] = 0;
    CHECK(strstr((char *) acc, "HTTP/1.1 200") != 0);
    CHECK(strstr((char *) acc, "Content-Length: 24") != 0);
    CHECK(memcmp(acc + got - 24, FILE_A, 24) == 0);
    CHECK(d.conn[c].state == VNA_DOOR_DONE);
    vna_door_close(&d, (uint32_t) c);

    /* ---- a CID block: /ipfs/<cid> serves the raw bytes ---- */
    c = vna_door_open(&d);
    CHECK(c >= 0);
    n = snprintf((char *) req, sizeof req, "GET /ipfs/bafybeiblob HTTP/1.1\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(memcmp(acc + got - (int) sizeof BLOB, BLOB, sizeof BLOB) == 0);
    vna_door_close(&d, (uint32_t) c);

    /* ---- a file that is not there answers 404 ---- */
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req, "GET /nope HTTP/1.1\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "404") != 0);
    vna_door_close(&d, (uint32_t) c);

    /* ---- POST /l2: the delta joins the page and gossips ---- */
    int dn = delta(1, LPRES_STATE_TRUE, pk);
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req, "POST /l2 HTTP/1.1\r\nContent-Length: %d\r\n\r\n", dn);
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    CHECK(vna_door_input(&d, (uint32_t) c, pk, (uint32_t) dn) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(got > 0 && acc[got - 1] == '1'); /* the lattice answer: TRUE */
    CHECK(gossiped == 1 && memcmp(gossip_buf, pk, 40) == 0);
    vna_door_close(&d, (uint32_t) c);

    /* ---- the stream: GET /l2 gets the log and then lives ---- */
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req, "GET /l2 HTTP/1.1\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    /* head + one frame: [len u16][tick u64][40 bytes] */
    CHECK(strstr((char *) acc, "200 OK") != 0);
    const uint8_t *fr = acc + got - 50; /* 10 + 40 */
    CHECK((fr[0] | fr[1] << 8) == dn);
    CHECK(d.conn[c].state == VNA_DOOR_STREAM);

    /* a new delta from a peer reaches the open stream */
    dn = delta(1, LPRES_STATE_FALSE, pk);
    CHECK(vna_door_delta_in(&d, pk, (uint32_t) dn, 7) == LPRES_STATE_BOTH);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(got == 50); /* exactly one frame */
    CHECK((acc[0] | acc[1] << 8) == 40);
    uint64_t ft = 0;
    for (int i = 0; i < 8; i++) ft |= (uint64_t) acc[2 + i] << (8 * i);
    CHECK(ft == 7);
    CHECK(memcmp(acc + 10, pk, 40) == 0);
    CHECK(vna_door_page(&d)->cell[0].lattice == LPRES_STATE_BOTH);

    /* resume: after=<seen tick> gets only what is newer — the head, then no
     * frame at all (the log holds nothing past 7) */
    vna_door_close(&d, (uint32_t) c);
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req, "GET /l2?after=7 HTTP/1.1\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    uint32_t head_end = 0;
    for (uint32_t i = 0; i + 3 < got; i++)
        if (acc[i] == '\r' && acc[i + 2] == '\r') head_end = i + 4;
    CHECK(head_end > 0 && got == head_end); /* head only, nothing framed */
    vna_door_close(&d, (uint32_t) c);

    /* the stream ends on schedule; keepalive frames meanwhile */
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req, "GET /l2?after=7 HTTP/1.1\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    drain(&d, (uint32_t) c, acc, sizeof acc); /* the head out of the way */
    vna_door_tick(&d, 9000);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(got == 10 && (acc[0] | acc[1] << 8) == 0); /* keepalive: a zero frame */
    vna_door_tick(&d, 31000);
    CHECK(d.conn[c].state == VNA_DOOR_DONE);

    /* ---- the rest of the verbs are refused ---- */
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req, "DELETE /l2 HTTP/1.1\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "405") != 0);
    vna_door_close(&d, (uint32_t) c);

    /* a POST to a file is 404, a POSTed non-delta is 400 ---- */
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req,
                 "POST /index.html HTTP/1.1\r\nContent-Length: 3\r\n\r\nabc");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "404") != 0);
    vna_door_close(&d, (uint32_t) c);

    c = vna_door_open(&d);
    uint8_t garbage[40];
    memset(garbage, 0x5a, sizeof garbage);
    n = snprintf((char *) req, sizeof req, "POST /l2 HTTP/1.1\r\nContent-Length: 40\r\n\r\n");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    CHECK(vna_door_input(&d, (uint32_t) c, garbage, 40) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "400") != 0);
    vna_door_close(&d, (uint32_t) c);

    /* ---- POST /cisex: delegated routing through the query hook ---- */
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req,
                 "POST /cisex HTTP/1.1\r\nContent-Length: 2\r\n\r\n\x01\x03");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "404") != 0); /* no query hook yet */
    vna_door_close(&d, (uint32_t) c);

    d.query = fquery;
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req,
                 "POST /cisex HTTP/1.1\r\nContent-Length: 2\r\n\r\n\x01\x03");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "HTTP/1.1 200") != 0);
    CHECK(strstr((char *) acc, "application/octet-stream") != 0);
    CHECK(strstr((char *) acc, "ZEDEC-XERO-VULPINE") != 0);
    CHECK(qseen == 2);
    CHECK(got >= 3 && acc[got - 3] == 0xAA && acc[got - 2] == 0xBB && acc[got - 1] == 0xCC);
    vna_door_close(&d, (uint32_t) c);

    qrefuse = 1;
    c = vna_door_open(&d);
    n = snprintf((char *) req, sizeof req,
                 "POST /cisex HTTP/1.1\r\nContent-Length: 2\r\n\r\n\x07\x03");
    CHECK(vna_door_input(&d, (uint32_t) c, req, (uint32_t) n) == VNA_OK);
    got = drain(&d, (uint32_t) c, acc, sizeof acc);
    CHECK(strstr((char *) acc, "400") != 0);
    vna_door_close(&d, (uint32_t) c);
    qrefuse = 0;
    d.query = 0;

    printf("vna_door: %d pass, %d fail\n", passes, failures);
    return failures ? 1 : 0;
}
