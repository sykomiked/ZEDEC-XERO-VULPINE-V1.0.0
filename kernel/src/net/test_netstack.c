/* test_netstack.c — the TCP/IP stack against real, externally-checkable values.
 *
 * This stack shipped with ZERO tests. Checksums are the classic place for
 * silent breakage: a wrong one does not crash, it just makes every packet
 * quietly discarded by the peer. So the anchors here are RFC-1071 arithmetic
 * and a byte-exact IPv4 header whose checksum is verifiable by hand, not
 * values read back out of our own implementation.
 */
#include <stdio.h>
#include <string.h>
#include "net.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* ---- capture what the stack transmits ---- */
static uint8_t  g_tx[4][2048];
static uint32_t g_tx_len[4];
static uint32_t g_tx_count = 0;
static void cap_tx(const uint8_t *d, uint32_t len) {
    if (g_tx_count >= 4) return;
    if (len > sizeof g_tx[0]) len = sizeof g_tx[0];
    memcpy(g_tx[g_tx_count], d, len);
    g_tx_len[g_tx_count] = len;
    g_tx_count++;
}
static void cap_reset(void) { g_tx_count = 0; }

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

int main(void) {
    printf("=== TCP/IP stack (previously untested) ===\n");

    /* ================= RFC 1071 checksum =================
     * The canonical worked example from RFC 1071 section 3: the octets
     * 00 01 f2 03 f4 f5 f6 f7 have one's-complement sum 0xddf2, so the
     * checksum is its complement, 0x220d. This is an EXTERNAL anchor. */
    {
        uint8_t rfc[8] = { 0x00,0x01, 0xf2,0x03, 0xf4,0xf5, 0xf6,0xf7 };
        uint16_t ck = net_checksum(rfc, sizeof rfc);
        printf("       RFC 1071 example -> 0x%04x (expect 0x220d)\n", ck);
        CHECK(ck == 0x220d, "checksum matches the RFC 1071 worked example");

        /* A checksum over data that already contains its own checksum must
         * come out zero — the property receivers actually rely on. */
        uint8_t v[10];
        memcpy(v, rfc, 8);
        v[8] = (uint8_t)(ck >> 8); v[9] = (uint8_t)(ck & 0xff);
        CHECK(net_checksum(v, sizeof v) == 0,
              "verifying data+checksum yields 0 (the receiver-side property)");

        /* odd length must be handled (pad the final byte, not drop it) */
        uint8_t odd[3] = { 0x12, 0x34, 0x56 };
        uint8_t evn[4] = { 0x12, 0x34, 0x56, 0x00 };
        CHECK(net_checksum(odd, 3) == net_checksum(evn, 4),
              "an odd-length buffer is padded, not truncated");
        CHECK(net_checksum(odd, 0) == 0xffff, "empty buffer -> 0xffff");
    }

    /* ================= IPv4 header checksum =================
     * A byte-exact header with a known-correct checksum (this is the classic
     * worked example: 45 00 00 73 00 00 40 00 40 11 .. 192.168.0.1 ->
     * 192.168.0.199, whose header checksum is 0xb861). */
    {
        uint8_t hdr[20] = {
            0x45,0x00, 0x00,0x73, 0x00,0x00, 0x40,0x00,
            0x40,0x11, 0x00,0x00,                    /* checksum field zeroed */
            0xc0,0xa8,0x00,0x01, 0xc0,0xa8,0x00,0xc7
        };
        uint16_t ck = net_checksum(hdr, sizeof hdr);
        printf("       IPv4 header checksum -> 0x%04x (expect 0xb861)\n", ck);
        CHECK(ck == 0xb861, "IPv4 header checksum matches the worked example");
        hdr[10] = (uint8_t)(ck >> 8); hdr[11] = (uint8_t)(ck & 0xff);
        CHECK(net_checksum(hdr, sizeof hdr) == 0,
              "the completed header verifies to 0");
    }

    /* ================= the stack answers a real ARP request ================= */
    {
        net_state_t net;
        net_init(&net);
        uint8_t mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
        uint8_t ip[4]  = {10,0,2,15};
        uint8_t gw[4]  = {10,0,2,2};
        uint8_t nm[4]  = {255,255,255,0};
        int32_t ifi = net_register_interface(&net, "eth0", NET_IF_ETHERNET,
                                             mac, ip, gw, nm, cap_tx, 0);
        CHECK(ifi >= 0, "an ethernet interface registers");
        net_interface_t *nif = &net.interfaces[ifi];

        /* build an ARP REQUEST asking who has 10.0.2.15 */
        uint8_t frame[42];
        memset(frame, 0, sizeof frame);
        for (int i = 0; i < 6; i++) frame[i] = 0xff;          /* broadcast */
        uint8_t peer[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        memcpy(frame + 6, peer, 6);
        frame[12] = 0x08; frame[13] = 0x06;                   /* ARP */
        frame[14] = 0x00; frame[15] = 0x01;                   /* ethernet */
        frame[16] = 0x08; frame[17] = 0x00;                   /* IPv4 */
        frame[18] = 6; frame[19] = 4;
        frame[20] = 0x00; frame[21] = 0x01;                   /* REQUEST */
        memcpy(frame + 22, peer, 6);
        frame[28]=10; frame[29]=0; frame[30]=2; frame[31]=99; /* sender 10.0.2.99 */
        frame[38]=10; frame[39]=0; frame[40]=2; frame[41]=15; /* target = us */

        cap_reset();
        net_handle_eth(&net, nif, frame, sizeof frame);
        printf("       ARP request in -> %u frame(s) out\n", g_tx_count);
        CHECK(g_tx_count == 1, "the stack REPLIES to an ARP request for its own IP");
        if (g_tx_count == 1) {
            const uint8_t *r = g_tx[0];
            CHECK(be16(r + 12) == 0x0806, "the reply is an ARP frame");
            CHECK(be16(r + 20) == 2, "opcode is REPLY (2)");
            CHECK(memcmp(r + 22, mac, 6) == 0, "sender MAC is OUR mac");
            CHECK(r[28]==10 && r[29]==0 && r[30]==2 && r[31]==15,
                  "sender IP is our IP (10.0.2.15)");
            CHECK(memcmp(r, peer, 6) == 0, "it is addressed back to the asker");
        }

        /* an ARP for someone ELSE's IP must NOT be answered */
        cap_reset();
        frame[41] = 200;                                      /* target 10.0.2.200 */
        net_handle_eth(&net, nif, frame, sizeof frame);
        CHECK(g_tx_count == 0, "an ARP for a DIFFERENT IP is not answered");
    }

    /* ================= ICMP echo: the stack answers a ping ================= */
    {
        net_state_t net;
        net_init(&net);
        uint8_t mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
        uint8_t ip[4]  = {10,0,2,15};
        uint8_t gw[4]  = {10,0,2,2};
        uint8_t nm[4]  = {255,255,255,0};
        int32_t ifi = net_register_interface(&net, "eth0", NET_IF_ETHERNET,
                                             mac, ip, gw, nm, cap_tx, 0);
        net_interface_t *nif = &net.interfaces[ifi];

        /* ethernet + IPv4 + ICMP echo request, 8 bytes of ICMP */
        uint8_t f[14 + 20 + 8];
        memset(f, 0, sizeof f);
        memcpy(f, mac, 6);
        uint8_t peer[6] = {0xaa,0xbb,0xcc,0xdd,0xee,0xff};
        memcpy(f + 6, peer, 6);
        f[12] = 0x08; f[13] = 0x00;                            /* IPv4 */
        uint8_t *iph = f + 14;
        iph[0] = 0x45; iph[1] = 0;
        iph[2] = 0; iph[3] = 20 + 8;                           /* total length */
        iph[8] = 64; iph[9] = 1;                               /* TTL, proto ICMP */
        iph[12]=10; iph[13]=0; iph[14]=2; iph[15]=99;          /* src 10.0.2.99 */
        iph[16]=10; iph[17]=0; iph[18]=2; iph[19]=15;          /* dst = us */
        uint16_t hck = net_checksum(iph, 20);
        iph[10] = (uint8_t)(hck >> 8); iph[11] = (uint8_t)(hck & 0xff);
        uint8_t *icmp = iph + 20;
        icmp[0] = 8; icmp[1] = 0;                              /* echo request */
        icmp[4] = 0x12; icmp[5] = 0x34;                        /* id */
        icmp[6] = 0x00; icmp[7] = 0x01;                        /* seq */
        uint16_t ick = net_checksum(icmp, 8);
        icmp[2] = (uint8_t)(ick >> 8); icmp[3] = (uint8_t)(ick & 0xff);

        cap_reset();
        net_handle_eth(&net, nif, f, sizeof f);
        printf("       ICMP echo request in -> %u frame(s) out\n", g_tx_count);
        CHECK(g_tx_count == 1, "the stack REPLIES to a ping");
        if (g_tx_count == 1) {
            const uint8_t *r = g_tx[0];
            const uint8_t *rip = r + 14;
            const uint8_t *ric = rip + 20;
            CHECK(be16(r + 12) == 0x0800, "the reply is IPv4");
            CHECK(rip[9] == 1, "protocol is ICMP");
            CHECK(ric[0] == 0, "ICMP type is ECHO REPLY (0)");
            CHECK(rip[12]==10 && rip[15]==15, "source is our IP");
            CHECK(rip[16]==10 && rip[19]==99, "destination is the pinger");
            CHECK(net_checksum(rip, 20) == 0,
                  "the reply's IP header checksum VERIFIES (a peer will accept it)");
            CHECK(net_checksum(ric, 8) == 0,
                  "the reply's ICMP checksum VERIFIES");
            CHECK(ric[4]==0x12 && ric[5]==0x34 && ric[7]==0x01,
                  "the echo id and sequence are echoed back unchanged");
        }

        /* a ping for a different host must be ignored */
        cap_reset();
        iph[19] = 200;
        hck = 0; iph[10]=0; iph[11]=0;
        hck = net_checksum(iph, 20);
        iph[10]=(uint8_t)(hck>>8); iph[11]=(uint8_t)(hck&0xff);
        net_handle_eth(&net, nif, f, sizeof f);
        CHECK(g_tx_count == 0, "a ping addressed to another host is ignored");
    }

    /* ---- malformed input must never crash the stack ---- */
    {
        net_state_t net; net_init(&net);
        uint8_t mac[6]={1,2,3,4,5,6}, ip[4]={10,0,2,15}, gw[4]={10,0,2,2}, nm[4]={255,255,255,0};
        int32_t ifi = net_register_interface(&net,"eth0",NET_IF_ETHERNET,mac,ip,gw,nm,cap_tx,0);
        net_interface_t *nif = &net.interfaces[ifi];
        uint8_t junk[64];
        for (uint32_t i = 0; i < sizeof junk; i++) junk[i] = (uint8_t)(i * 37 + 11);
        cap_reset();
        net_handle_eth(&net, nif, junk, 0);          /* zero length */
        net_handle_eth(&net, nif, junk, 1);          /* shorter than a header */
        net_handle_eth(&net, nif, junk, 13);         /* truncated ethernet */
        junk[12]=0x08; junk[13]=0x00;
        net_handle_eth(&net, nif, junk, 15);         /* IPv4 claim, no header */
        junk[12]=0x08; junk[13]=0x06;
        net_handle_eth(&net, nif, junk, 20);         /* ARP claim, truncated */
        CHECK(1, "malformed and truncated frames did not crash the stack");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
