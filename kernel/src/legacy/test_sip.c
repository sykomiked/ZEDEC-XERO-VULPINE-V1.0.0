/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_sip.c — SIP (RFC 3261) and SDP (RFC 4566) parser/builder tests.
 * Vectors adapted from RFC 3261 section 24.1 (the canonical INVITE example)
 * and RFC 4566 examples. Host test only (uses stdio). */
#include <stdio.h>
#include <string.h>
#include "sip.h"
#include "sdp.h"

static int pass = 0, fail = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)
#define OK(c) CHECK(c, #c)

static bool seq(sip_slice s, const char *lit)
{
    return s.len == strlen(lit) && memcmp(s.p, lit, s.len) == 0;
}

/* RFC 3261 section 24.1 INVITE (trimmed to the headers we model). */
static const char *INVITE = "INVITE sip:bob@biloxi.com SIP/2.0\r\n"
                            "Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK776asdhds\r\n"
                            "Max-Forwards: 70\r\n"
                            "To: Bob <sip:bob@biloxi.com>\r\n"
                            "From: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n"
                            "Call-ID: a84b4c76e66710@pc33.atlanta.com\r\n"
                            "CSeq: 314159 INVITE\r\n"
                            "Contact: <sip:alice@pc33.atlanta.com>\r\n"
                            "Content-Type: application/sdp\r\n"
                            "Content-Length: 0\r\n"
                            "\r\n";

/* RFC 4566 example SDP (audio portion). */
static const char *SDP_EX = "v=0\r\n"
                            "o=jdoe 2890844526 2890842807 IN IP4 10.47.16.5\r\n"
                            "s=SDP Seminar\r\n"
                            "c=IN IP4 224.2.17.12\r\n"
                            "t=2873397496 2873404696\r\n"
                            "m=audio 49170 RTP/AVP 0 8 97\r\n"
                            "a=rtpmap:0 PCMU/8000\r\n"
                            "a=rtpmap:8 PCMA/8000\r\n"
                            "a=rtpmap:97 iLBC/8000\r\n"
                            "a=sendrecv\r\n";

static void test_parse_request(void)
{
    /* build a real body so Content-Length (150 above is illustrative) matches */
    char buf[2048];
    int n = snprintf(buf, sizeof buf,
                     "INVITE sip:bob@biloxi.com SIP/2.0\r\n"
                     "v: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK776asdhds\r\n"
                     "t: Bob <sip:bob@biloxi.com>\r\n"
                     "f: Alice <sip:alice@atlanta.com>;tag=1928301774\r\n"
                     "i: a84b4c76e66710@pc33.atlanta.com\r\n"
                     "CSeq: 314159 INVITE\r\n"
                     "m: <sip:alice@pc33.atlanta.com>\r\n"
                     "c: application/sdp\r\n"
                     "l: 4\r\n"
                     "\r\n"
                     "v=0\n");
    sip_msg m;
    OK(sip_parse((const uint8_t *) buf, (uint32_t) n, &m));
    OK(m.kind == SIP_MSG_REQUEST);
    CHECK(seq(m.method, "INVITE"), "method");
    CHECK(seq(m.ruri, "sip:bob@biloxi.com"), "ruri");
    /* compact forms resolved */
    CHECK(seq(m.via, "SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK776asdhds"), "compact v");
    CHECK(seq(m.to, "Bob <sip:bob@biloxi.com>"), "compact t");
    CHECK(seq(m.from, "Alice <sip:alice@atlanta.com>;tag=1928301774"), "compact f");
    CHECK(seq(m.call_id, "a84b4c76e66710@pc33.atlanta.com"), "compact i");
    CHECK(seq(m.contact, "<sip:alice@pc33.atlanta.com>"), "compact m");
    CHECK(seq(m.content_type, "application/sdp"), "compact c");
    OK(m.has_content_length && m.content_length == 4);
    OK(m.body_len == 4 && memcmp(m.body, "v=0\n", 4) == 0);

    /* full-name INVITE (Content-Length honored even when buffer is larger) */
    sip_msg m2;
    size_t L = strlen(INVITE);
    OK(sip_parse((const uint8_t *) INVITE, (uint32_t) L, &m2));
    CHECK(seq(m2.cseq, "314159 INVITE"), "cseq full");
}

static void test_parse_status(void)
{
    const char *resp = "SIP/2.0 180 Ringing\r\n"
                       "Via: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK776asdhds\r\n"
                       "To: Bob <sip:bob@biloxi.com>;tag=8321234356\r\n"
                       "Content-Length: 0\r\n"
                       "\r\n";
    sip_msg m;
    OK(sip_parse((const uint8_t *) resp, (uint32_t) strlen(resp), &m));
    OK(m.kind == SIP_MSG_STATUS);
    OK(m.status == 180);
    CHECK(seq(m.reason, "Ringing"), "reason");
    OK(m.body_len == 0);
}

static void test_build(void)
{
    uint8_t out[1024];
    sip_build_hdr hdrs[] = {
        {"Via", "SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK77"},
        {"From", "Alice <sip:alice@atlanta.com>;tag=1928301774"},
        {"To", "Bob <sip:bob@biloxi.com>"},
        {"Call-ID", "a84b4c76e66710@pc33.atlanta.com"},
        {"CSeq", "314159 INVITE"},
    };
    const char *body = "v=0\r\n";
    sip_build b = {0};
    b.is_request = true;
    b.method = "INVITE";
    b.ruri = "sip:bob@biloxi.com";
    b.headers = hdrs;
    b.nheaders = 5;
    b.body = (const uint8_t *) body;
    b.body_len = (uint32_t) strlen(body);
    b.content_type = "application/sdp";
    uint32_t n = sip_build_msg(&b, out, sizeof out);
    OK(n > 0);
    /* round-trip: parse what we built */
    sip_msg m;
    OK(sip_parse(out, n, &m));
    CHECK(seq(m.method, "INVITE"), "rt method");
    OK(m.has_content_length && m.content_length == 5);
    CHECK(seq(m.content_type, "application/sdp"), "rt ctype");

    /* overflow is reported, never overrun */
    uint8_t tiny[16];
    OK(sip_build_msg(&b, tiny, sizeof tiny) == 0);

    /* status build */
    sip_build s = {0};
    s.is_request = false;
    s.status = 200;
    s.reason = "OK";
    s.headers = hdrs;
    s.nheaders = 5;
    uint32_t n2 = sip_build_msg(&s, out, sizeof out);
    OK(n2 > 0);
    sip_msg ms;
    OK(sip_parse(out, n2, &ms));
    OK(ms.status == 200);
}

static void test_sdp(void)
{
    sdp_session s;
    OK(sdp_parse((const uint8_t *) SDP_EX, (uint32_t) strlen(SDP_EX), &s));
    OK(s.has_audio);
    OK(s.audio_port == 49170);
    CHECK(seq(s.conn_addr, "224.2.17.12"), "sdp conn");
    OK(s.npt == 3 && s.pt[0] == 0 && s.pt[1] == 8 && s.pt[2] == 97);
    sip_slice enc;
    uint32_t clk;
    OK(sdp_pt_rtpmap(&s, 0, &enc, &clk));
    CHECK(seq(enc, "PCMU") && clk == 8000, "rtpmap 0");
    OK(sdp_pt_rtpmap(&s, 97, &enc, &clk));
    CHECK(seq(enc, "iLBC") && clk == 8000, "rtpmap 97");
    OK(s.dir == SDP_DIR_SENDRECV);

    /* build + round-trip */
    uint8_t out[1024];
    sdp_build_pt pts[] = {{0, "PCMU", 8000}, {8, "PCMA", 8000}, {101, "telephone-event", 8000}};
    sdp_build b = {0};
    b.sess_id = 123;
    b.sess_ver = 1;
    b.unicast_addr = "192.0.2.1";
    b.audio_port = 5004;
    b.pts = pts;
    b.npts = 3;
    b.dir = SDP_DIR_SENDRECV;
    uint32_t n = sdp_build_audio(&b, out, sizeof out);
    OK(n > 0);
    sdp_session s2;
    OK(sdp_parse(out, n, &s2));
    OK(s2.audio_port == 5004 && s2.npt == 3 && s2.pt[2] == 101);
    OK(sdp_pt_rtpmap(&s2, 101, &enc, &clk) && clk == 8000);
}

/* Fuzz-style: truncations and garbage must never crash and must be rejected
 * or parsed within bounds. */
static void test_fuzz(void)
{
    size_t L = strlen(INVITE);
    for (size_t t = 0; t < L; t++) {
        sip_msg m;
        /* truncated message: parsing must terminate and not read past t */
        (void) sip_parse((const uint8_t *) INVITE, (uint32_t) t, &m);
    }
    /* garbage inputs */
    const char *g1 = "\x00\x01\x02 not sip";
    const char *g2 = "INVITE\r\n\r\n";            /* request line missing tokens */
    const char *g3 = "SIP/2.0 999 Bad\r\n\r\n";   /* out-of-range status */
    const char *g4 = "FOO sip:x SIP/9.9\r\n\r\n"; /* bad version */
    const char *g5 = "A: b\r\n\r\n";              /* no request/status line */
    sip_msg m;
    OK(!sip_parse((const uint8_t *) g1, 11, &m));
    OK(!sip_parse((const uint8_t *) g2, (uint32_t) strlen(g2), &m));
    OK(!sip_parse((const uint8_t *) g3, (uint32_t) strlen(g3), &m));
    OK(!sip_parse((const uint8_t *) g4, (uint32_t) strlen(g4), &m));
    OK(!sip_parse((const uint8_t *) g5, (uint32_t) strlen(g5), &m));
    /* a header with no colon */
    const char *g6 = "INVITE sip:x SIP/2.0\r\nBadHeaderNoColon\r\n\r\n";
    OK(!sip_parse((const uint8_t *) g6, (uint32_t) strlen(g6), &m));
    /* Content-Length larger than the body present */
    const char *g7 = "INVITE sip:x SIP/2.0\r\nContent-Length: 99\r\n\r\nshort";
    OK(!sip_parse((const uint8_t *) g7, (uint32_t) strlen(g7), &m));
    /* deterministic pseudo-random garbage, must not crash */
    uint32_t r = 0x12345678u;
    uint8_t rnd[256];
    for (int it = 0; it < 4000; it++) {
        for (int i = 0; i < 256; i++) {
            r = r * 1664525u + 1013904223u;
            rnd[i] = (uint8_t) (r >> 17);
        }
        (void) sip_parse(rnd, (uint32_t) (r % 256), &m);
        sdp_session sd;
        (void) sdp_parse(rnd, (uint32_t) (r % 256), &sd);
    }
    OK(1); /* reached here without crashing */
}

int main(void)
{
    test_parse_request();
    test_parse_status();
    test_build();
    test_sdp();
    test_fuzz();
    printf("SIP/SDP: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
