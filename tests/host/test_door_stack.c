/* test_door_stack.c — the seed's plain-http door, end to end on the wire.
 *
 * The kernel net stack runs here on the host: a client crafts real
 * eth/ipv4/tcp segments, net_rx_packet feeds them, the server's own
 * SYN+ACK/data/FIN come back through the transmit capture. Between them
 * sits the same pump glue vna_host_pump runs: net_accept, net_recv,
 * vna_door_input, vna_door_pull, net_send. What is proven: the door answers
 * over a real TCP handshake, streams the /l2 frames a peer's delta makes,
 * and survives the close/re-listen cycle — the only part left for hardware
 * is the NIC.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../../kernel/src/net/net.h"
#include "../../kernel/src/net/tcp.h"
#include "../../kernel/src/vinea/vna_door.h"

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

/* ---- the door's file table (same shape the kernel's blob header makes) - */
static const char *FILE_A = "<html>the doorway</html>";
static int32_t fsize(void *ctx, const char *path, uint32_t plen)
{
    (void) ctx;
    return plen == 11 && !memcmp(path, "/index.html", 11) ? (int32_t) strlen(FILE_A) : -1;
}
static const char *ftype(void *ctx, const char *path, uint32_t plen)
{
    (void) ctx;
    (void) path;
    return plen == 11 ? "text/html" : 0;
}
static int32_t fbody(void *ctx, const char *path, uint32_t plen, uint64_t off, uint8_t *out,
                     uint32_t cap)
{
    (void) ctx;
    uint32_t len = (uint32_t) strlen(FILE_A);
    if (!(plen == 11 && !memcmp(path, "/index.html", 11))) return -1;
    if (off >= len) return 0;
    uint32_t n = len - (uint32_t) off;
    if (n > cap) n = cap;
    memcpy(out, FILE_A + off, n);
    return (int32_t) n;
}
static uint32_t gossips;
static void gsp(void *ctx, const uint8_t *b, uint32_t n) { (void) ctx; (void) b; (void) n; gossips++; }

/* ---- the wire ---------------------------------------------------------- */
static uint8_t cap[64][2048];
static uint32_t cap_len[64], cap_n;
static void tx_cap(const uint8_t *data, uint32_t len)
{
    if (cap_n >= 64 || len > 2048) return;
    memcpy(cap[cap_n], data, len);
    cap_len[cap_n] = len;
    cap_n++;
}

static const uint8_t CLI_MAC[6] = {0x52, 0x54, 0x00, 0xaa, 0xbb, 0xcc};
static const uint8_t SRV_MAC[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static const uint8_t CLI_IP[4] = {10, 0, 0, 9};
static const uint8_t SRV_IP[4] = {10, 0, 0, 2};
static const uint16_t DPORT = 80;
static uint16_t sport = 43210;

static void p16(uint8_t *p, uint16_t v) { p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; }
static void p32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v;
}
static uint16_t g16(const uint8_t *p) { return (uint16_t) (p[0] << 8 | p[1]); }
static uint32_t g32(const uint8_t *p)
{
    return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3];
}
static uint16_t cksum(const uint8_t *d, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i + 1 < n; i += 2) s += (uint16_t) (d[i] << 8 | d[i + 1]);
    if (n & 1) s += (uint16_t) (d[n - 1] << 8);
    while (s >> 16) s = (s & 0xffff) + (s >> 16);
    return (uint16_t) ~s;
}

static net_state_t net;
static net_interface_t *net_if;
static uint32_t cli_seq = 0x01010101, srv_ack, conn_n;
/* Fresh 4-tuple + isn per connection: same-tuple segments would be dispatched
 * to the previous connection while its socket still drains. */
static void new_conn(void) { cli_seq = 0x01010101 + ++conn_n * 0x1000; sport++; }

/* One client segment: flags, optional payload. Feeds the stack. */
static void client(uint8_t flags, const uint8_t *payload, uint32_t plen)
{
    uint8_t f[1600];
    memset(f, 0, sizeof f);
    memcpy(f + 0, SRV_MAC, 6);
    memcpy(f + 6, CLI_MAC, 6);
    p16(f + 12, 0x0800);
    uint8_t *ip = f + 14, *tcp = ip + 20;
    ip[0] = 0x45; ip[1] = 0;
    p16(ip + 2, (uint16_t) (20 + 20 + plen));
    p16(ip + 4, 0x1234); ip[6] = 0x40; ip[7] = 0; /* DF, not a fragment */
    ip[8] = 64; ip[9] = 6;
    memcpy(ip + 12, CLI_IP, 4);
    memcpy(ip + 16, SRV_IP, 4);
    p16(ip + 10, cksum(ip, 20));
    p16(tcp + 0, sport);
    p16(tcp + 2, DPORT);
    p32(tcp + 4, cli_seq);
    p32(tcp + 8, srv_ack);
    tcp[12] = 5 << 4;
    tcp[13] = flags;
    p16(tcp + 14, 65535);
    if (plen) memcpy(tcp + 20, payload, plen);
    /* pseudo-header checksum */
    {
        uint8_t ph[12];
        memcpy(ph + 0, CLI_IP, 4);
        memcpy(ph + 4, SRV_IP, 4);
        ph[8] = 0; ph[9] = 6;
        p16(ph + 10, (uint16_t) (20 + plen));
        uint32_t s = 0;
        for (int i = 0; i + 1 < 12; i += 2) s += (uint16_t) (ph[i] << 8 | ph[i + 1]);
        uint32_t tn = 20 + plen;
        for (uint32_t i = 0; i + 1 < tn; i += 2) s += (uint16_t) (tcp[i] << 8 | tcp[i + 1]);
        if (tn & 1) s += (uint16_t) (tcp[tn - 1] << 8);
        while (s >> 16) s = (s & 0xffff) + (s >> 16);
        p16(tcp + 16, (uint16_t) ~s);
    }
    CHECK(net_l4_checksum(CLI_IP, SRV_IP, IP_PROTO_TCP, tcp, 20 + plen) == 0);
    net_rx_packet(&net, net_if, f, 14 + 20 + 20 + plen);
    net_poll(&net); /* rx frames dispatch here, not in net_rx_packet */
    cli_seq += plen + ((flags & TCPF_SYN) ? 1 : 0) + ((flags & TCPF_FIN) ? 1 : 0);
}

/* The last server frame's tcp view: flags + payload + its seq. -1 = none. */
static int last_seg(const uint8_t **payload, uint32_t *plen, uint32_t *seq, uint8_t *flags)
{
    if (!cap_n) return -1;
    const uint8_t *f = cap[cap_n - 1];
    if (g16(f + 12) != 0x0800) return -1;
    const uint8_t *ip = f + 14, *tcp = ip + (ip[0] & 0xf) * 4;
    uint32_t thl = (tcp[12] >> 4) * 4;
    *flags = tcp[13];
    *seq = g32(tcp + 4);
    *plen = g16(ip + 2) - (uint16_t) ((ip[0] & 0xf) * 4 + thl);
    *payload = tcp + thl;
    /* consume what we saw: the peer's next expected seq */
    srv_ack = *seq + *plen + ((*flags & TCPF_SYN) ? 1 : 0) + ((*flags & TCPF_FIN) ? 1 : 0);
    cap_n--;
    return 0;
}

/* The pump glue, verbatim in spirit to vna_host_pump's door block — staged
 * because the stack is stop-and-wait: bytes net_send cannot take yet stay
 * staged, never dropped. */
static uint8_t pump_buf[1400], txst[1400];
static uint32_t txl, txo;
static void pump(vna_door_t *d, int32_t sock, int32_t dc)
{
    int32_t n;
    while ((n = net_recv(&net, sock, pump_buf, sizeof pump_buf)) > 0)
        vna_door_input(d, (uint32_t) dc, pump_buf, (uint32_t) n);
    while (txo >= txl) {
        n = (int32_t) vna_door_pull(d, (uint32_t) dc, txst, sizeof txst);
        if (!n) break;
        txl = (uint32_t) n;
        txo = 0;
    }
    if (txo < txl) {
        int32_t w = net_send(&net, sock, txst + txo, txl - txo);
        if (w > 0) txo += (uint32_t) w;
    }
    net_poll(&net); /* retransmission timers */
}

/* Ack every server data segment in the capture, then pump again. The stack
 * is stop-and-wait: the next segment only flies once the last is acked. */
static uint8_t acc[65536];
static uint32_t acc_n;
static int collect(vna_door_t *d, int32_t sock, int32_t dc, int rounds)
{
    const uint8_t *pl;
    uint32_t plen, seq;
    uint8_t fl;
    int got = 0;
    for (int r = 0; r < rounds; r++) {
        pump(d, sock, dc);
        while (last_seg(&pl, &plen, &seq, &fl) == 0) {
            if (acc_n + plen <= sizeof acc) memcpy(acc + acc_n, pl, plen);
            acc_n += plen;
            got++;
            client(TCPF_ACK, 0, 0); /* pure ack, no payload */
            pump(d, sock, dc);
        }
        if (!got) break;
        got = 0;
    }
    acc[acc_n < sizeof acc ? acc_n : sizeof acc - 1] = 0;
    return (int) acc_n;
}

int main(void)
{
    net_init(&net);
    uint8_t gw[4] = {10, 0, 0, 1}, mask[4] = {255, 255, 255, 0};
    int32_t ii = net_register_interface(&net, "eth0", NET_IF_ETHERNET, SRV_MAC, SRV_IP, gw, mask,
                                        tx_cap, 0);
    CHECK(ii >= 0);
    net_if = &net.interfaces[ii];
    net_arp_add(&net, CLI_IP, CLI_MAC); /* the client resolves instantly */

    vna_door_t d;
    vna_door_init(&d, 0, fsize, ftype, fbody, gsp);
    int32_t sock = net_socket(&net, SOCK_TCP);
    CHECK(net_bind(&net, sock, 80) == 0);
    CHECK(net_listen(&net, sock, 1) == 0);

    /* ---- handshake: SYN, SYN+ACK, ACK ---- */
    client(TCPF_SYN, 0, 0);
    const uint8_t *pl;
    uint32_t plen, seq;
    uint8_t fl;
    CHECK(last_seg(&pl, &plen, &seq, &fl) == 0 && (fl & (TCPF_SYN | TCPF_ACK)));
    CHECK(net_accept(&net, sock) < 0); /* not established until the ACK */
    client(TCPF_ACK, 0, 0);
    CHECK(net_accept(&net, sock) == sock);
    int32_t dc = vna_door_open(&d);
    CHECK(dc >= 0);

    /* ---- GET /index.html over the wire ---- */
    const char *req = "GET /index.html HTTP/1.1\r\nHost: s\r\n\r\n";
    client(TCPF_ACK | TCPF_PSH, (const uint8_t *) req, (uint32_t) strlen(req));
    acc_n = 0;
    int got = collect(&d, sock, dc, 64);
    CHECK(got > 0);
    CHECK(strstr((char *) acc, "HTTP/1.1 200") != 0);
    CHECK(strstr((char *) acc, "Content-Type: text/html") != 0);
    CHECK(strstr((char *) acc, FILE_A) != 0);
    CHECK(d.conn[dc].state == VNA_DOOR_DONE);

    /* ---- the conn ends: FIN, and the socket listens again ---- */
    client(TCPF_ACK | TCPF_FIN, 0, 0);
    while (last_seg(&pl, &plen, &seq, &fl) == 0) client(TCPF_ACK, 0, 0);
    vna_door_close(&d, (uint32_t) dc);
    net_close(&net, sock);
    /* let the FIN exchange drain: the peer acks what we sent. The host does
     * not re-listen the draining socket — a fresh socket is bound, like each
     * door slot's recycle. */

    /* ---- POST /l2 then GET /l2, on a second connection ---- */
    new_conn();
    int32_t sock2 = net_socket(&net, SOCK_TCP);
    net_bind(&net, sock2, 80);
    net_listen(&net, sock2, 1);
    client(TCPF_SYN, 0, 0);
    CHECK(last_seg(&pl, &plen, &seq, &fl) == 0 && (fl & TCPF_SYN));
    client(TCPF_ACK, 0, 0);
    CHECK(net_accept(&net, sock2) == sock2);
    dc = vna_door_open(&d);
    CHECK(dc >= 0);

    uint8_t pk[64];
    memset(pk, 0, sizeof pk);
    vna_delta_t dl;
    memset(&dl, 0, sizeof dl);
    dl.cell = 1;
    dl.lattice = LPRES_STATE_TRUE;
    int dn = vna_delta_encode(&dl, pk, VNA_DELTA_HDR);
    char preq[128];
    int rn = snprintf(preq, sizeof preq, "POST /l2 HTTP/1.1\r\nContent-Length: %d\r\n\r\n", dn);
    client(TCPF_ACK | TCPF_PSH, (const uint8_t *) preq, (uint32_t) rn);
    client(TCPF_ACK | TCPF_PSH, pk, (uint32_t) dn);
    acc_n = 0;
    got = collect(&d, sock2, dc, 64);
    CHECK(got > 0 && acc[got - 1] == '1');
    CHECK(gossips == 1);
    /* the POSTed delta answers and the conn ends; the socket drains */
    client(TCPF_ACK | TCPF_FIN, 0, 0);
    while (last_seg(&pl, &plen, &seq, &fl) == 0) client(TCPF_ACK, 0, 0);
    vna_door_close(&d, (uint32_t) dc);
    net_close(&net, sock2);

    /* a stream on the third: the logged delta frames out, then it lives */
    new_conn();
    int32_t sock3 = net_socket(&net, SOCK_TCP);
    net_bind(&net, sock3, 80);
    net_listen(&net, sock3, 1);
    client(TCPF_SYN, 0, 0);
    CHECK(last_seg(&pl, &plen, &seq, &fl) == 0 && (fl & TCPF_SYN));
    client(TCPF_ACK, 0, 0);
    CHECK(net_accept(&net, sock3) == sock3);
    dc = vna_door_open(&d);
    req = "GET /l2 HTTP/1.1\r\n\r\n";
    client(TCPF_ACK | TCPF_PSH, (const uint8_t *) req, (uint32_t) strlen(req));
    acc_n = 0;
    got = collect(&d, sock3, dc, 64);
    CHECK(strstr((char *) acc, "200 OK") != 0);
    CHECK(got > 40 && acc[got - 50] == 40 && acc[got - 49] == 0); /* len u16le = 40 */
    /* a peer delta streams the frame while the conn stays open */
    dl.lattice = LPRES_STATE_FALSE;
    dn = vna_delta_encode(&dl, pk, VNA_DELTA_HDR);
    CHECK(vna_door_delta_hear(&d, pk, (uint32_t) dn, 7) == LPRES_STATE_BOTH);
    acc_n = 0;
    got = collect(&d, sock3, dc, 64);
    CHECK(got == 50 && acc[0] == 40);
    CHECK(d.conn[dc].state == VNA_DOOR_STREAM);
    /* the stream ends on its clock; the conn closes */
    vna_door_tick(&d, 31000);
    acc_n = 0;
    pump(&d, sock3, dc);
    CHECK(d.conn[dc].state == VNA_DOOR_DONE);

    printf("door_stack: %d pass, %d fail\n", passes, failures);
    return failures ? 1 : 0;
}
