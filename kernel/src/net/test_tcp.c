/* test_tcp.c — the TCP endpoint against byte-exact segments.
 *
 * Two connections are driven against EACH OTHER through this file's `wire`,
 * so a handshake only completes if both sides agree on sequence numbers, byte
 * order and flags. Nothing is asserted by reading a variable the code just
 * set; every claim is checked on the bytes that would go on the wire.
 *
 * The four defects the previous implementation had each get a test that fails
 * if they come back: nothing transmitted, an unswapped acknowledgement number,
 * options read as payload, and a constant initial sequence number.
 */
#include <stdio.h>
#include <string.h>
#include "tcp.h"

static int failures = 0;
#define CHECK(c, m)                                                                                \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            printf("[FAIL] %s\n", m);                                                              \
            failures++;                                                                            \
        } else                                                                                     \
            printf("[PASS] %s\n", m);                                                              \
    } while (0)

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t) ((p[0] << 8) | p[1]);
}
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static uint8_t A_IP[4] = {10, 0, 2, 15};
static uint8_t B_IP[4] = {10, 0, 2, 99};

/* Hand a segment from one endpoint to the other; returns the reply length. */
static uint32_t wire(tcp_conn_t *dst, const uint8_t *seg, uint32_t len, uint8_t *out, uint32_t cap)
{
    return tcp_input(dst, seg, len, out, cap);
}

int main(void)
{
    printf("=== TCP endpoint (previously: a state variable, not a protocol) ===\n");
    uint8_t a[2048], b[2048];
    uint32_t an, bn;

    /* ---------------- three-way handshake ---------------- */
    tcp_conn_t A, B;
    tcp_init(&A, A_IP, 40000, B_IP, 80, 0x11223344u);
    tcp_init(&B, B_IP, 80, A_IP, 40000, 0xAABBCCDDu);
    tcp_listen(&B);

    an = tcp_connect(&A, a, sizeof a);
    CHECK(an > 0, "connect() actually EMITS a SYN (it used to emit nothing)");
    CHECK(an == 24, "the SYN is 20 bytes of header plus a 4-byte MSS option");
    CHECK(be16(a + 0) == 40000 && be16(a + 2) == 80, "ports are in network order");
    CHECK(be32(a + 4) == 0x11223344u, "the SYN carries our initial sequence number");
    CHECK((a[13] & TCPF_SYN) && !(a[13] & TCPF_ACK), "flags: SYN, not ACK");
    CHECK((a[12] >> 4) == 6, "the data offset says 6 words — options are present");
    CHECK(a[20] == 2 && a[21] == 4 && be16(a + 22) == TCP_MAX_SEG, "the option is MSS = 1460");
    CHECK(tcp_checksum(A_IP, B_IP, a, an) == 0,
          "the SYN's checksum VERIFIES against the pseudo-header");
    CHECK(A.state == TCPS_SYN_SENT, "state -> SYN_SENT");

    bn = wire(&B, a, an, b, sizeof b);
    CHECK(bn > 0, "the listener ANSWERS with a segment");
    CHECK((b[13] & TCPF_SYN) && (b[13] & TCPF_ACK), "it is a SYN-ACK");
    CHECK(be32(b + 8) == 0x11223345u,
          "the acknowledgement number is our ISS+1, byte-swapped correctly "
          "(the old code never swapped it)");
    CHECK(B.mss == TCP_MAX_SEG, "the listener parsed our MSS option");
    CHECK(tcp_checksum(B_IP, A_IP, b, bn) == 0, "the SYN-ACK's checksum verifies");
    CHECK(B.state == TCPS_SYN_RCVD, "listener state -> SYN_RCVD");

    an = wire(&A, b, bn, a, sizeof a);
    CHECK(an > 0 && (a[13] & TCPF_ACK) && !(a[13] & TCPF_SYN),
          "the initiator completes with a bare ACK");
    CHECK(be32(a + 8) == 0xAABBCCDEu, "it acknowledges the listener's ISS+1");
    CHECK(A.state == TCPS_ESTABLISHED, "initiator is ESTABLISHED");
    CHECK(A.mss == TCP_MAX_SEG, "the initiator parsed the listener's MSS");

    bn = wire(&B, a, an, b, sizeof b);
    CHECK(B.state == TCPS_ESTABLISHED, "listener is ESTABLISHED");
    CHECK(bn == 0, "a bare ACK that completes the handshake needs no reply");

    /* ---------------- data transfer ---------------- */
    {
        const char *msg = "GET / HTTP/1.0\r\n\r\n";
        uint32_t mlen = (uint32_t) strlen(msg);
        uint32_t used = 0;
        an = tcp_send(&A, (const uint8_t *) msg, mlen, a, sizeof a, &used);
        CHECK(an == 20 + mlen && used == mlen, "a data segment is built");
        CHECK((a[13] & TCPF_PSH) && (a[13] & TCPF_ACK), "it carries PSH|ACK");
        CHECK(memcmp(a + 20, msg, mlen) == 0, "the payload is on the wire verbatim");
        CHECK(tcp_checksum(A_IP, B_IP, a, an) == 0, "the data segment's checksum verifies");

        bn = wire(&B, a, an, b, sizeof b);
        CHECK(bn > 0 && (b[13] & TCPF_ACK), "the receiver ACKs the data");
        uint8_t got[64];
        uint32_t g = tcp_read(&B, got, sizeof got);
        CHECK(g == mlen && memcmp(got, msg, mlen) == 0,
              "the receiver delivers exactly the bytes that were sent");
        CHECK(be32(b + 8) == 0x11223345u + mlen, "the ACK number advances by the payload length");

        wire(&A, b, bn, a, sizeof a);
        CHECK(A.retx_len == 0, "the acknowledged data leaves the retransmit buffer");
    }

    /* ---- a peer's SYN options must NOT be delivered as data ----
     * This is the specific bug the old code had: it assumed a 20-byte header. */
    {
        tcp_conn_t C, D;
        tcp_init(&C, A_IP, 40001, B_IP, 80, 1000);
        tcp_init(&D, B_IP, 80, A_IP, 40001, 2000);
        tcp_listen(&D);
        uint32_t cn = tcp_connect(&C, a, sizeof a);
        uint32_t dn = wire(&D, a, cn, b, sizeof b);
        wire(&C, b, dn, a, sizeof a);
        uint8_t junk[64];
        CHECK(tcp_read(&C, junk, sizeof junk) == 0,
              "the SYN-ACK's MSS option is NOT delivered to the application "
              "as received data");

        /* a segment with a big option block and a real payload after it */
        uint8_t seg[80];
        memset(seg, 0, sizeof seg);
        seg[0] = 0;
        seg[1] = 80;
        seg[2] = (uint8_t) (40001 >> 8);
        seg[3] = (uint8_t) 40001;
        seg[4] = (uint8_t) (D.snd_nxt >> 24);
        seg[5] = (uint8_t) (D.snd_nxt >> 16);
        seg[6] = (uint8_t) (D.snd_nxt >> 8);
        seg[7] = (uint8_t) D.snd_nxt;
        seg[8] = (uint8_t) (C.rcv_nxt >> 24); /* ack, value unimportant here */
        seg[12] = (uint8_t) (10 << 4);        /* 40-byte header: 20 bytes of options */
        seg[13] = TCPF_ACK | TCPF_PSH;
        seg[14] = 0x10;
        seg[15] = 0x00;
        seg[20] = 1;
        seg[21] = 1; /* NOPs */
        seg[22] = 8;
        seg[23] = 10; /* timestamps, 10 bytes */
        seg[32] = 4;
        seg[33] = 2; /* SACK permitted */
        seg[34] = 3;
        seg[35] = 3;
        seg[36] = 7; /* window scale */
        seg[37] = 0; /* EOL */
        memcpy(seg + 40, "PAYLOAD", 7);
        (void) tcp_input(&C, seg, 47, a, sizeof a);
        uint8_t got[32];
        uint32_t g = tcp_read(&C, got, sizeof got);
        CHECK(g == 7 && memcmp(got, "PAYLOAD", 7) == 0,
              "with 20 bytes of options, the payload starts at the DATA OFFSET, "
              "not at byte 20");
    }

    /* ---- orderly close: FIN / ACK / FIN / ACK ---- */
    {
        an = tcp_close(&A, a, sizeof a);
        CHECK(an > 0 && (a[13] & TCPF_FIN), "close() emits a FIN");
        CHECK(A.state == TCPS_FIN_WAIT_1, "state -> FIN_WAIT_1");
        bn = wire(&B, a, an, b, sizeof b);
        CHECK(B.state == TCPS_CLOSE_WAIT, "the peer moves to CLOSE_WAIT");
        CHECK(bn > 0 && (b[13] & TCPF_ACK) && !(b[13] & TCPF_FIN),
              "it ACKs the FIN without closing its own side yet");
        wire(&A, b, bn, a, sizeof a);
        CHECK(A.state == TCPS_FIN_WAIT_2, "our FIN is acknowledged -> FIN_WAIT_2");

        bn = tcp_close(&B, b, sizeof b);
        CHECK(bn > 0 && (b[13] & TCPF_FIN), "the peer then closes its side");
        CHECK(B.state == TCPS_LAST_ACK, "peer -> LAST_ACK");
        an = wire(&A, b, bn, a, sizeof a);
        CHECK(A.state == TCPS_TIME_WAIT, "we enter TIME_WAIT");
        wire(&B, a, an, b, sizeof b);
        CHECK(B.state == TCPS_CLOSED, "the peer reaches CLOSED");
        for (int i = 0; i < 20; i++) tcp_tick(&A, a, sizeof a);
        CHECK(A.state == TCPS_CLOSED, "TIME_WAIT expires to CLOSED");
    }

    /* ---- retransmission ---- */
    {
        tcp_conn_t R;
        tcp_init(&R, A_IP, 40002, B_IP, 80, 5000);
        uint32_t n = tcp_connect(&R, a, sizeof a);
        CHECK(n > 0, "a SYN goes out");
        uint32_t retries = 0, seq_drift = 0;
        for (int i = 0; i < 200 && R.state == TCPS_SYN_SENT; i++)
            if (tcp_tick(&R, a, sizeof a) > 0) {
                retries++;
                if (be32(a + 4) != 5000u) seq_drift++;
            }
        printf("       unanswered SYN retransmitted %u time(s)\n", retries);
        CHECK(seq_drift == 0, "every retransmitted SYN repeats the original sequence number");
        CHECK(retries == TCP_MAX_RETX,
              "an unanswered SYN is retransmitted exactly TCP_MAX_RETX times");
        CHECK(R.state == TCPS_CLOSED && R.reset,
              "then the connection is declared dead, not retried forever");
    }
    {
        /* data retransmission preserves the bytes and the sequence number */
        tcp_conn_t P, Q;
        tcp_init(&P, A_IP, 40003, B_IP, 80, 700);
        tcp_init(&Q, B_IP, 80, A_IP, 40003, 800);
        tcp_listen(&Q);
        uint32_t n = tcp_connect(&P, a, sizeof a);
        n = wire(&Q, a, n, b, sizeof b);
        n = wire(&P, b, n, a, sizeof a);
        wire(&Q, a, n, b, sizeof b);

        uint32_t used = 0;
        uint32_t sn = tcp_send(&P, (const uint8_t *) "HELLO", 5, a, sizeof a, &used);
        uint32_t seq0 = be32(a + 4);
        CHECK(sn > 0 && used == 5, "five bytes are sent");
        /* the ACK never arrives */
        uint32_t got_retx = 0;
        for (int i = 0; i < 4; i++) {
            uint32_t r = tcp_tick(&P, a, sizeof a);
            if (r) {
                got_retx++;
                CHECK(be32(a + 4) == seq0,
                      "the retransmission repeats the ORIGINAL sequence number");
                CHECK(memcmp(a + 20, "HELLO", 5) == 0, "and the original bytes");
                break;
            }
        }
        CHECK(got_retx == 1, "unacknowledged data is retransmitted");

        /* now deliver it: the retransmit buffer must clear */
        uint32_t qn = wire(&Q, a, 25, b, sizeof b);
        wire(&P, b, qn, a, sizeof a);
        CHECK(P.retx_len == 0, "the ACK clears the retransmit buffer");
    }

    /* ================ sequence numbers wrap ================
     * Start the connection just below 2^32 so the handshake itself crosses the
     * wrap point. A naive `a < b` comparison passes every other test in this
     * file and fails here. */
    {
        tcp_conn_t W, V;
        uint32_t iss = 0xFFFFFFF0u;
        tcp_init(&W, A_IP, 40004, B_IP, 80, iss);
        tcp_init(&V, B_IP, 80, A_IP, 40004, 0xFFFFFFF8u);
        tcp_listen(&V);
        uint32_t n = tcp_connect(&W, a, sizeof a);
        CHECK(be32(a + 4) == iss, "the SYN carries a sequence number near 2^32");
        n = wire(&V, a, n, b, sizeof b);
        n = wire(&W, b, n, a, sizeof a);
        wire(&V, a, n, b, sizeof b);
        CHECK(W.state == TCPS_ESTABLISHED && V.state == TCPS_ESTABLISHED,
              "the handshake completes across the wrap point");

        uint8_t big[64];
        for (int i = 0; i < 64; i++) big[i] = (uint8_t) ('A' + (i % 26));
        uint32_t used = 0;
        n = tcp_send(&W, big, sizeof big, a, sizeof a, &used);
        CHECK(used == sizeof big, "64 bytes are sent from just below the wrap");
        n = wire(&V, a, n, b, sizeof b);
        uint8_t got[64];
        CHECK(tcp_read(&V, got, sizeof got) == sizeof big && memcmp(got, big, sizeof big) == 0,
              "they arrive intact AFTER the sequence number wrapped through zero");
        wire(&W, b, n, a, sizeof a);
        CHECK(W.retx_len == 0, "and the wrapped acknowledgement clears the retransmit buffer "
                               "(a naive unsigned comparison fails exactly here)");
    }

    /* ================ hostile / malformed ================ */
    {
        tcp_conn_t X, Y;
        tcp_init(&X, A_IP, 40005, B_IP, 80, 900);
        tcp_init(&Y, B_IP, 80, A_IP, 40005, 950);
        tcp_listen(&Y);
        uint32_t n = tcp_connect(&X, a, sizeof a);
        n = wire(&Y, a, n, b, sizeof b);
        n = wire(&X, b, n, a, sizeof a);
        wire(&Y, a, n, b, sizeof b);

        /* a segment for a different port must not drive the state machine */
        uint8_t seg[40];
        memcpy(seg, b, 20);
        seg[2] = 0x00;
        seg[3] = 0x50; /* wrong destination port */
        seg[13] = TCPF_RST;
        (void) tcp_input(&X, seg, 20, a, sizeof a);
        CHECK(X.state == TCPS_ESTABLISHED, "a segment addressed to a DIFFERENT port is ignored");

        /* a RST outside the window must not tear the connection down */
        memset(seg, 0, sizeof seg);
        seg[0] = 0;
        seg[1] = 80;
        seg[2] = (uint8_t) (40005 >> 8);
        seg[3] = (uint8_t) 40005;
        seg[4] = 0xDE;
        seg[5] = 0xAD;
        seg[6] = 0xBE;
        seg[7] = 0xEF; /* wild sequence */
        seg[12] = (uint8_t) (5 << 4);
        seg[13] = TCPF_RST;
        (void) tcp_input(&X, seg, 20, a, sizeof a);
        CHECK(X.state == TCPS_ESTABLISHED,
              "a RST with an out-of-window sequence number is REFUSED "
              "(otherwise anyone guessing the four-tuple can kill the connection)");

        /* the in-window RST does end it */
        seg[4] = (uint8_t) (X.rcv_nxt >> 24);
        seg[5] = (uint8_t) (X.rcv_nxt >> 16);
        seg[6] = (uint8_t) (X.rcv_nxt >> 8);
        seg[7] = (uint8_t) X.rcv_nxt;
        (void) tcp_input(&X, seg, 20, a, sizeof a);
        CHECK(X.state == TCPS_CLOSED && X.reset, "an in-window RST closes it");
    }
    {
        /* a lying data offset must be refused, not followed */
        tcp_conn_t Z;
        tcp_init(&Z, A_IP, 40006, B_IP, 80, 1);
        tcp_listen(&Z);
        uint8_t seg[24];
        memset(seg, 0, sizeof seg);
        seg[0] = 0;
        seg[1] = 80;
        seg[2] = (uint8_t) (40006 >> 8);
        seg[3] = (uint8_t) 40006;
        seg[13] = TCPF_SYN;
        seg[12] = (uint8_t) (15 << 4); /* claims a 60-byte header in 24 bytes */
        CHECK(tcp_input(&Z, seg, sizeof seg, a, sizeof a) == 0 && Z.state == TCPS_LISTEN,
              "a data offset larger than the segment is refused");
        seg[12] = (uint8_t) (3 << 4); /* claims 12 bytes: below the minimum */
        CHECK(tcp_input(&Z, seg, sizeof seg, a, sizeof a) == 0,
              "a data offset below the 20-byte minimum is refused");
    }
    {
        /* option lengths that lie must terminate the walk */
        tcp_conn_t Z;
        tcp_init(&Z, A_IP, 40007, B_IP, 80, 1);
        tcp_listen(&Z);
        uint8_t seg[40];
        memset(seg, 0, sizeof seg);
        seg[0] = 0;
        seg[1] = 80;
        seg[2] = (uint8_t) (40007 >> 8);
        seg[3] = (uint8_t) 40007;
        seg[12] = (uint8_t) (10 << 4); /* 40-byte header */
        seg[13] = TCPF_SYN;
        seg[20] = 2;
        seg[21] = 200; /* MSS option claiming 200 bytes */
        uint32_t n = tcp_input(&Z, seg, sizeof seg, a, sizeof a);
        CHECK(n > 0 && Z.mss == TCP_DEFAULT_MSS,
              "an option whose length overruns the header is skipped, and the "
              "RFC 1122 default MSS is kept");
        /* a zero-length option would loop forever if not rejected */
        tcp_init(&Z, A_IP, 40008, B_IP, 80, 1);
        tcp_listen(&Z);
        seg[2] = (uint8_t) (40008 >> 8);
        seg[3] = (uint8_t) 40008;
        seg[20] = 5;
        seg[21] = 0;
        n = tcp_input(&Z, seg, sizeof seg, a, sizeof a);
        CHECK(n > 0, "a zero-length option terminates the walk instead of hanging");
    }
    {
        /* truncated segments of every length must never crash */
        tcp_conn_t Z;
        tcp_init(&Z, A_IP, 40009, B_IP, 80, 1);
        tcp_listen(&Z);
        uint8_t seg[60];
        for (uint32_t i = 0; i < sizeof seg; i++) seg[i] = (uint8_t) (i * 31 + 7);
        seg[2] = (uint8_t) (40009 >> 8);
        seg[3] = (uint8_t) 40009;
        for (uint32_t cut = 0; cut <= sizeof seg; cut++)
            (void) tcp_input(&Z, seg, cut, a, sizeof a);
        CHECK(1, "every prefix of a segment parses without crashing");

        /* randomised segments */
        for (uint32_t s = 0; s < 2000; s++) {
            uint32_t r = s * 2654435761u;
            for (uint32_t i = 0; i < sizeof seg; i++) {
                r = r * 1103515245u + 12345u;
                seg[i] = (uint8_t) (r >> 16);
            }
            seg[2] = (uint8_t) (40009 >> 8);
            seg[3] = (uint8_t) 40009;
            (void) tcp_input(&Z, seg, sizeof seg, a, sizeof a);
        }
        CHECK(1, "2000 randomised segments parsed without crashing");
    }

    /* ---- the initial sequence number must not be a constant ---- */
    {
        tcp_conn_t S1, S2;
        tcp_init(&S1, A_IP, 41000, B_IP, 80, 0xCAFEBABEu);
        tcp_init(&S2, A_IP, 41001, B_IP, 80, 0x0BADF00Du);
        tcp_connect(&S1, a, sizeof a);
        tcp_connect(&S2, b, sizeof b);
        CHECK(be32(a + 4) != be32(b + 4), "two connections do not share an initial sequence number "
                                          "(the old code hard-coded 1000 for every one)");
        CHECK(be32(a + 4) == 0xCAFEBABEu && be32(b + 4) == 0x0BADF00Du,
              "the ISS the caller supplies is the ISS that goes on the wire — "
              "so it can come from a real entropy source");
    }

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS", failures);
    return failures ? 1 : 0;
}
