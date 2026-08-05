/* tcp.h — a TCP endpoint that actually completes a handshake
 *
 * WHAT WAS THERE BEFORE
 * ---------------------
 * The stack had a tcp_state_t enum, a net_connect() that set the state to
 * SYN_SENT with the comment "Would send SYN packet here", and a net_handle_tcp()
 * that transmitted NOTHING in any state. It was a state variable, not a
 * protocol. Four concrete defects, each of which this module exists to not
 * have:
 *
 *   1. No segment was ever sent — no SYN, no SYN-ACK, no ACK, no FIN.
 *   2. `s->ack_num = tcp->seq_num + 1` never byte-swapped, so the
 *      acknowledgement number was garbage on any little-endian host.
 *   3. The data offset was ignored, so a peer's SYN options (every real peer
 *      sends MSS, and usually SACK/timestamps/window scale) were handed to the
 *      application as if they were payload.
 *   4. The initial sequence number was the constant 1000, which lets anyone
 *      who can guess a port inject data into a connection they cannot see.
 *
 * DESIGN
 * ------
 * Pure functions over byte buffers, like the DHCP and DNS modules: every entry
 * point takes the segment that arrived and writes the segment to transmit into
 * a caller-supplied buffer. No I/O, no timers of its own, no allocation — so
 * the whole state machine is testable against byte-exact segments, and one
 * connection can be driven from any event loop.
 *
 * The connection owns its addresses, so it computes its own checksum over the
 * pseudo-header. A segment produced here is complete and can go straight into
 * an IP packet.
 *
 * SEQUENCE NUMBER ARITHMETIC
 * --------------------------
 * Sequence numbers wrap. Every comparison goes through tcp_seq_lt/leq, which
 * compare the SIGNED difference — the only correct way. A naive `a < b` breaks
 * exactly once per 4 GiB transferred, which is the kind of bug that survives
 * every test that does not deliberately start near the wrap point. There is
 * such a test.
 *
 * WHAT THIS DELIBERATELY DOES NOT DO
 * ----------------------------------
 * Stated plainly rather than implied by omission:
 *   - No out-of-order reassembly. A segment that is not the next expected one
 *     is dropped and re-ACKed, which forces the peer to retransmit. That is
 *     correct but slow on a lossy path; it is not a correctness gap.
 *   - No window scaling, SACK, or timestamps. The options are PARSED and
 *     skipped safely; they are not negotiated.
 *   - Congestion control is send-one-window-and-wait, not Reno. Fine for a
 *     control channel; do not mistake it for a bulk transport.
 *   - TIME_WAIT is entered and exited by tick count, not by a real 2*MSL clock.
 *
 * Freestanding: integer only, no libc, no allocation.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV TCP slice)
 * License: SEL-3.3
 */
#ifndef ZXV_TCP_H
#define ZXV_TCP_H

#include <stdint.h>
#include <stdbool.h>

#define TCP_SND_BUF   2048u    /* unacknowledged data held for retransmit */
#define TCP_RCV_BUF   4096u
#define TCP_MAX_SEG   1460u
#define TCP_HDR_MIN   20u
#define TCP_DEFAULT_MSS 536u   /* RFC 1122 default when the peer offers none */
#define TCP_MAX_RETX  6u       /* give up after this many retransmissions */
#define TCP_TIME_WAIT_TICKS 8u

#define TCPF_FIN  0x01u
#define TCPF_SYN  0x02u
#define TCPF_RST  0x04u
#define TCPF_PSH  0x08u
#define TCPF_ACK  0x10u
#define TCPF_URG  0x20u

typedef enum {
    TCPS_CLOSED = 0,
    TCPS_LISTEN,
    TCPS_SYN_SENT,
    TCPS_SYN_RCVD,
    TCPS_ESTABLISHED,
    TCPS_FIN_WAIT_1,
    TCPS_FIN_WAIT_2,
    TCPS_CLOSE_WAIT,
    TCPS_CLOSING,
    TCPS_LAST_ACK,
    TCPS_TIME_WAIT
} tcp_conn_state_t;

typedef struct {
    tcp_conn_state_t state;
    uint8_t  local_ip[4], remote_ip[4];
    uint16_t local_port, remote_port;

    uint32_t snd_una;      /* oldest byte we sent that is still unacknowledged */
    uint32_t snd_nxt;      /* next sequence number we will send               */
    uint32_t rcv_nxt;      /* next sequence number we expect to receive       */
    uint16_t snd_wnd;      /* what the peer says it can accept                */
    uint16_t mss;          /* the peer's MSS, or the RFC 1122 default         */

    /* in-flight data, kept so it can be retransmitted */
    uint8_t  retx[TCP_SND_BUF];
    uint32_t retx_len;
    uint32_t retx_seq;
    uint32_t retx_ticks;
    uint32_t retx_count;

    /* delivered, in-order data waiting for the application */
    uint8_t  rx[TCP_RCV_BUF];
    uint32_t rx_len;

    bool     fin_sent;      /* our FIN is in flight or acknowledged */
    bool     fin_acked;
    bool     peer_fin;      /* the peer has closed its side         */
    bool     reset;         /* the connection was reset             */
    uint32_t wait_ticks;    /* TIME_WAIT countdown                  */
} tcp_conn_t;

/* Wrapping-safe sequence comparison. */
static inline bool tcp_seq_lt(uint32_t a, uint32_t b)  { return (int32_t)(a - b) <  0; }
static inline bool tcp_seq_leq(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

/* Set up a connection. `iss` is the initial sequence number and MUST come from
 * a real entropy source: a predictable ISS lets an off-path attacker inject
 * data into a connection it cannot observe. */
void tcp_init(tcp_conn_t *c, const uint8_t local_ip[4], uint16_t local_port,
              const uint8_t remote_ip[4], uint16_t remote_port, uint32_t iss);

/* Active open. Writes a SYN (with our MSS option) into `out`. Returns its
 * length, 0 on error. */
uint32_t tcp_connect(tcp_conn_t *c, uint8_t *out, uint32_t cap);

/* Passive open: wait for a SYN. */
void tcp_listen(tcp_conn_t *c);

/* Feed one received segment (TCP header + options + payload). Writes the
 * segment to transmit in response into `out`, and returns its length — 0 when
 * there is nothing to send. Payload accepted in order is appended to the
 * receive buffer; read it with tcp_read(). */
uint32_t tcp_input(tcp_conn_t *c, const uint8_t *seg, uint32_t len,
                   uint8_t *out, uint32_t cap);

/* Queue and send application data. Returns the length of the segment written
 * to `out`, 0 if nothing can be sent right now (not established, window full,
 * or previous data still unacknowledged). *consumed gets the number of bytes
 * of `data` actually taken. */
uint32_t tcp_send(tcp_conn_t *c, const uint8_t *data, uint32_t len,
                  uint8_t *out, uint32_t cap, uint32_t *consumed);

/* Begin an orderly close: writes a FIN. */
uint32_t tcp_close(tcp_conn_t *c, uint8_t *out, uint32_t cap);

/* Abort: writes a RST and moves to CLOSED. */
uint32_t tcp_reset(tcp_conn_t *c, uint8_t *out, uint32_t cap);

/* Call periodically. Retransmits unacknowledged data with backoff, and expires
 * TIME_WAIT. Returns the length of any segment to send. After TCP_MAX_RETX
 * attempts the connection is declared dead (state CLOSED, reset = true).
 *
 * ONE TICK MUST BE ROUGHLY 200 ms. This is a real contract, not a hint: the
 * first retransmission happens one tick after a segment goes unacknowledged,
 * so calling this in a tight polling loop retransmits every segment before the
 * round trip can possibly complete — every packet goes out twice and half the
 * link's bandwidth is wasted. The caller owns the clock; drive it from a
 * monotonic counter, not from loop iterations. */
uint32_t tcp_tick(tcp_conn_t *c, uint8_t *out, uint32_t cap);

/* Copy up to `cap` bytes of received data out; returns how many. */
uint32_t tcp_read(tcp_conn_t *c, uint8_t *dst, uint32_t cap);

static inline bool tcp_is_established(const tcp_conn_t *c) {
    return c && c->state == TCPS_ESTABLISHED;
}

/* Compute the checksum a receiver would; 0 means the segment verifies. */
uint16_t tcp_checksum(const uint8_t src_ip[4], const uint8_t dst_ip[4],
                      const uint8_t *seg, uint32_t len);

#endif /* ZXV_TCP_H */
