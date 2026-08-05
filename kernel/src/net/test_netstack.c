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

    /* ================= transport checksum (RFC 768 pseudo-header) =================
     * External anchor: a UDP datagram from 192.168.0.1 to 192.168.0.199,
     * ports 0xc350 -> 0x0035, length 8, no payload. Verified the way a
     * receiver does it — recomputing over header+checksum must yield 0. */
    {
        uint8_t src[4] = {192,168,0,1}, dst[4] = {192,168,0,199};
        uint8_t udp[8] = { 0xc3,0x50, 0x00,0x35, 0x00,0x08, 0x00,0x00 };
        uint16_t ck = net_l4_checksum(src, dst, IP_PROTO_UDP, udp, 8);
        udp[6] = (uint8_t)(ck >> 8); udp[7] = (uint8_t)(ck & 0xff);
        CHECK(net_l4_checksum(src, dst, IP_PROTO_UDP, udp, 8) == 0,
              "a UDP checksum verifies to zero at the receiver");
        /* the pseudo-header genuinely participates: change one address bit
         * and the same bytes must no longer verify */
        dst[3] = 200;
        CHECK(net_l4_checksum(src, dst, IP_PROTO_UDP, udp, 8) != 0,
              "the checksum covers the IP addresses, not just the payload");
        dst[3] = 199;
        /* and so does the protocol number — UDP and TCP must not agree */
        CHECK(net_l4_checksum(src, dst, IP_PROTO_TCP, udp, 8) != 0,
              "the checksum covers the protocol number");
        /* odd-length payload is padded, not dropped */
        uint8_t odd[9] = { 0xc3,0x50, 0x00,0x35, 0x00,0x09, 0x00,0x00, 0x41 };
        uint16_t ock = net_l4_checksum(src, dst, IP_PROTO_UDP, odd, 9);
        odd[6] = (uint8_t)(ock >> 8); odd[7] = (uint8_t)(ock & 0xff);
        CHECK(net_l4_checksum(src, dst, IP_PROTO_UDP, odd, 9) == 0,
              "an odd-length datagram checksums correctly");
    }

    /* ================= DHCP through the real stack =================
     * Not the client in isolation — the whole path: the frame the stack puts
     * on the wire, and the interface configuration it ends up with. */
    {
        net_state_t net; net_init(&net);
        uint8_t mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
        uint8_t zero_ip[4] = {0,0,0,0};
        int32_t ifi = net_register_interface(&net, "eth0", NET_IF_ETHERNET,
                                             mac, zero_ip, zero_ip, zero_ip, cap_tx, 0);
        net_interface_t *nif = &net.interfaces[ifi];

        cap_reset();
        net_dhcp_discover(&net, nif);
        CHECK(g_tx_count == 1, "net_dhcp_discover actually transmits a frame "
                               "(it used to be an empty stub)");
        const uint8_t *d = g_tx[0];
        uint32_t dl = g_tx_len[0];
        CHECK(dl == 14 + 20 + 8 + 250, "DISCOVER frame is eth+ip+udp+dhcp");
        CHECK(memcmp(d, "\xff\xff\xff\xff\xff\xff", 6) == 0,
              "it goes to the ethernet broadcast address");
        CHECK(memcmp(d + 6, mac, 6) == 0, "from our MAC");
        CHECK(be16(d + 12) == 0x0800, "it is IPv4");
        CHECK(d[26]==0 && d[27]==0 && d[28]==0 && d[29]==0,
              "source IP is 0.0.0.0 — we do not have one yet");
        CHECK(d[30]==255 && d[31]==255 && d[32]==255 && d[33]==255,
              "destination IP is the limited broadcast 255.255.255.255");
        CHECK(net_checksum(d + 14, 20) == 0, "the IP header checksum verifies");
        CHECK(be16(d + 34) == 68 && be16(d + 36) == 67,
              "UDP 68 -> 67, the BOOTP ports");
        {   /* the UDP checksum must verify against the pseudo-header we sent */
            uint8_t s0[4] = {0,0,0,0}, b1[4] = {255,255,255,255};
            CHECK(net_l4_checksum(s0, b1, IP_PROTO_UDP, d + 34, dl - 34) == 0,
                  "the UDP checksum verifies (a zero checksum would be legal "
                  "but unverifiable)");
        }
        CHECK(d[42] == 1 && d[42+236]==0x63 && d[42+239]==0x63,
              "the payload is a BOOTREQUEST carrying the DHCP magic cookie");
        uint32_t xid = ((uint32_t)d[46]<<24)|((uint32_t)d[47]<<16)|
                       ((uint32_t)d[48]<<8)|d[49];

        /* --- the server OFFERs; the stack must answer with a REQUEST --- */
        uint8_t frame[600];
        uint32_t flen;
        {
            memset(frame, 0, sizeof frame);
            memcpy(frame, mac, 6);
            uint8_t srvmac[6] = {0x52,0x55,0x0a,0x00,0x02,0x02};
            memcpy(frame + 6, srvmac, 6);
            frame[12]=0x08; frame[13]=0x00;
            uint8_t *iph = frame + 14;
            uint8_t *udph = iph + 20;
            uint8_t *bp = udph + 8;
            /* BOOTREPLY offering 10.0.2.15 from server 10.0.2.2 */
            bp[0]=2; bp[1]=1; bp[2]=6;
            bp[4]=(uint8_t)(xid>>24); bp[5]=(uint8_t)(xid>>16);
            bp[6]=(uint8_t)(xid>>8);  bp[7]=(uint8_t)xid;
            bp[16]=10; bp[17]=0; bp[18]=2; bp[19]=15;      /* yiaddr */
            memcpy(bp + 28, mac, 6);
            bp[236]=0x63; bp[237]=0x82; bp[238]=0x53; bp[239]=0x63;
            uint32_t o = 240;
            bp[o++]=53; bp[o++]=1; bp[o++]=2;              /* OFFER */
            bp[o++]=54; bp[o++]=4; bp[o++]=10; bp[o++]=0; bp[o++]=2; bp[o++]=2;
            bp[o++]=1;  bp[o++]=4; bp[o++]=255; bp[o++]=255; bp[o++]=255; bp[o++]=0;
            bp[o++]=3;  bp[o++]=4; bp[o++]=10; bp[o++]=0; bp[o++]=2; bp[o++]=2;
            bp[o++]=6;  bp[o++]=4; bp[o++]=10; bp[o++]=0; bp[o++]=2; bp[o++]=3;
            bp[o++]=51; bp[o++]=4; bp[o++]=0; bp[o++]=0; bp[o++]=0x0e; bp[o++]=0x10;
            bp[o++]=255;
            uint32_t l4 = 8 + o;
            udph[0]=0; udph[1]=67; udph[2]=0; udph[3]=68;
            udph[4]=(uint8_t)(l4>>8); udph[5]=(uint8_t)l4;
            iph[0]=0x45; iph[2]=(uint8_t)((20+l4)>>8); iph[3]=(uint8_t)(20+l4);
            iph[8]=64; iph[9]=17;
            iph[12]=10; iph[13]=0; iph[14]=2; iph[15]=2;
            /* the server BROADCASTS the reply, as the broadcast flag asked */
            iph[16]=255; iph[17]=255; iph[18]=255; iph[19]=255;
            uint16_t hck = net_checksum(iph, 20);
            iph[10]=(uint8_t)(hck>>8); iph[11]=(uint8_t)(hck&0xff);
            flen = 14 + 20 + l4;
        }
        cap_reset();
        net_handle_eth(&net, nif, frame, flen);
        CHECK(g_tx_count == 1, "an OFFER provokes exactly one reply");
        CHECK(!dhcp_is_bound(&net.dhcp), "and the lease is not bound yet");
        if (g_tx_count == 1) {
            const uint8_t *r = g_tx[0] + 42;
            const uint8_t *v = 0;
            uint32_t rl = g_tx_len[0] - 42;
            CHECK(dhcp_get_option(r, rl, DHCP_OPT_MSGTYPE, &v)==1 && v[0]==DHCP_REQUEST,
                  "the reply is a DHCP REQUEST");
            CHECK(dhcp_get_option(r, rl, DHCP_OPT_REQUESTED, &v)==4 &&
                  v[0]==10 && v[3]==15, "it requests the offered 10.0.2.15");
            CHECK(memcmp(g_tx[0], "\xff\xff\xff\xff\xff\xff", 6) == 0,
                  "the REQUEST is BROADCAST, so losing servers free their offers");
        }
        CHECK(nif->ip[0]==0 && nif->ip[3]==0,
              "the interface is still unconfigured — an OFFER is not a lease");

        /* --- the server ACKs; the interface must adopt the lease --- */
        frame[14 + 20 + 8 + 242] = 5;   /* option 53 value: OFFER -> ACK */
        cap_reset();
        net_handle_eth(&net, nif, frame, flen);
        CHECK(dhcp_is_bound(&net.dhcp), "the ACK binds the lease");
        CHECK(nif->ip[0]==10 && nif->ip[1]==0 && nif->ip[2]==2 && nif->ip[3]==15,
              "the INTERFACE now carries 10.0.2.15 — no longer hard-coded");
        CHECK(nif->netmask[0]==255 && nif->netmask[3]==0, "netmask adopted from the lease");
        CHECK(nif->gateway[0]==10 && nif->gateway[3]==2, "gateway adopted from the lease");
        CHECK(net.dhcp.lease_secs == 3600, "lease time recorded");

        /* the address filter must now be back in force */
        {
            uint8_t png[42];
            memset(png, 0, sizeof png);
            memcpy(png, mac, 6);
            png[12]=0x08; png[13]=0x00;
            uint8_t *iph = png + 14;
            iph[0]=0x45; iph[3]=28; iph[8]=64; iph[9]=1;
            iph[12]=10; iph[15]=99;
            iph[16]=10; iph[17]=0; iph[18]=2; iph[19]=200;  /* someone else */
            uint16_t hck = net_checksum(iph, 20);
            iph[10]=(uint8_t)(hck>>8); iph[11]=(uint8_t)(hck&0xff);
            iph[20+0]=8;
            cap_reset();
            net_handle_eth(&net, nif, png, sizeof png);
            CHECK(g_tx_count == 0,
                  "once configured, traffic for another host is dropped again");
        }
    }

    /* ================= DNS through the real stack =================
     * The lookup only works if DHCP handed us a server, ARP found the next
     * hop, the query left with the right ports, and the answer was accepted
     * from the right source and no other. */
    {
        net_state_t net; net_init(&net);
        uint8_t mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
        uint8_t ip[4]={10,0,2,15}, gw[4]={10,0,2,2}, nm[4]={255,255,255,0};
        int32_t ifi = net_register_interface(&net,"eth0",NET_IF_ETHERNET,mac,ip,gw,nm,cap_tx,0);
        net_interface_t *nif = &net.interfaces[ifi];
        const char *host = "zxv.example.com";
        uint8_t out[4];

        cap_reset();
        CHECK(net_dns_query(&net, nif, host) == -1,
              "with no DNS server known, a lookup fails instead of guessing");
        CHECK(g_tx_count == 0, "and nothing is transmitted");

        /* a server, but its MAC is unknown: the stack must ARP, not send blind */
        net.dns_server[0]=10; net.dns_server[1]=0; net.dns_server[2]=2; net.dns_server[3]=3;
        cap_reset();
        CHECK(net_dns_query(&net, nif, host) == 0, "the lookup starts");
        CHECK(g_tx_count == 1 && be16(g_tx[0] + 12) == 0x0806,
              "an unknown next hop produces an ARP REQUEST, not a blind query");

        /* now teach it the MAC and ask again */
        uint8_t srvmac[6] = {0x52,0x55,0x0a,0x00,0x02,0x03};
        net_arp_add(&net, net.dns_server, srvmac);
        cap_reset();
        CHECK(net_dns_query(&net, nif, host) == 0, "the lookup is sent");
        CHECK(g_tx_count == 1, "exactly one query frame goes out");
        const uint8_t *Q = g_tx[0];
        uint32_t Qn = g_tx_len[0];
        CHECK(memcmp(Q, srvmac, 6) == 0, "addressed to the DNS server's MAC");
        CHECK(Q[30]==10 && Q[31]==0 && Q[32]==2 && Q[33]==3,
              "destination IP is the DHCP-supplied resolver 10.0.2.3");
        CHECK(be16(Q + 36) == 53, "destination port is 53");
        CHECK(be16(Q + 34) >= 49152, "the source port is ephemeral, not fixed");
        {
            uint8_t s[4]={10,0,2,15}, d[4]={10,0,2,3};
            CHECK(net_l4_checksum(s, d, IP_PROTO_UDP, Q + 34, Qn - 34) == 0,
                  "the query's UDP checksum verifies");
        }
        uint16_t qid  = be16(Q + 42);
        uint16_t sprt = be16(Q + 34);
        CHECK((Q[44] & 0x01) != 0, "recursion is requested");

        /* ---- a forged answer from the WRONG source must not be believed ---- */
        uint8_t f[256]; uint32_t fl;
        {
            /* build the answer body once; vary only the envelope */
            uint8_t body[128];
            memset(body, 0, sizeof body);
            body[0]=(uint8_t)(qid>>8); body[1]=(uint8_t)qid;
            body[2]=0x81; body[3]=0x80; body[5]=1; body[7]=1;
            uint32_t a = 12;
            const char *n = host;
            while (*n) {
                uint32_t l = 0; while (n[l] && n[l] != '.') l++;
                body[a++] = (uint8_t)l;
                for (uint32_t i=0;i<l;i++) body[a++] = (uint8_t)n[i];
                n += l; if (*n=='.') n++;
            }
            body[a++]=0; body[a++]=0; body[a++]=1; body[a++]=0; body[a++]=1;
            body[a++]=0xC0; body[a++]=12;
            body[a++]=0; body[a++]=1; body[a++]=0; body[a++]=1;
            body[a++]=0; body[a++]=0; body[a++]=0; body[a++]=60;
            body[a++]=0; body[a++]=4;
            body[a++]=93; body[a++]=184; body[a++]=216; body[a++]=34;
            uint32_t blen = a;

            /* envelope helper: src_ip4 chooses who it claims to be from */
            #define MK(SRC4, SPORT) do {                                        \
                memset(f, 0, sizeof f);                                          \
                memcpy(f, mac, 6); memcpy(f + 6, srvmac, 6);                     \
                f[12]=0x08; f[13]=0x00;                                          \
                uint8_t *iph=f+14, *udh=iph+20;                                  \
                uint32_t l4=8+blen;                                              \
                iph[0]=0x45; iph[2]=(uint8_t)((20+l4)>>8); iph[3]=(uint8_t)(20+l4);\
                iph[8]=64; iph[9]=17;                                            \
                iph[12]=10; iph[13]=0; iph[14]=2; iph[15]=(SRC4);                \
                iph[16]=10; iph[17]=0; iph[18]=2; iph[19]=15;                     \
                { uint16_t h=net_checksum(iph,20);                               \
                  iph[10]=(uint8_t)(h>>8); iph[11]=(uint8_t)(h&0xff); }           \
                udh[0]=0; udh[1]=53;                                             \
                udh[2]=(uint8_t)((SPORT)>>8); udh[3]=(uint8_t)(SPORT);           \
                udh[4]=(uint8_t)(l4>>8); udh[5]=(uint8_t)l4;                     \
                memcpy(udh+8, body, blen);                                       \
                fl = 14+20+l4;                                                   \
            } while (0)

            MK(99, sprt);                    /* claims to be 10.0.2.99 */
            net_handle_eth(&net, nif, f, fl);
            CHECK(net_dns_result(&net, host, out) == 0,
                  "an answer from an address that is NOT our resolver is refused");

            MK(3, (uint16_t)(sprt ^ 0x0001)); /* right server, wrong port */
            net_handle_eth(&net, nif, f, fl);
            CHECK(net_dns_result(&net, host, out) == 0,
                  "an answer to a port we did not ask from is refused");

            MK(3, sprt);                      /* right server, wrong id */
            f[42] ^= 0xFF;
            net_handle_eth(&net, nif, f, fl);
            CHECK(net_dns_result(&net, host, out) == 0,
                  "an answer carrying the wrong transaction id is refused");

            MK(3, sprt);                      /* the genuine article */
            net_handle_eth(&net, nif, f, fl);
            CHECK(net_dns_result(&net, host, out) == 1,
                  "the genuine answer resolves the name");
            CHECK(out[0]==93 && out[1]==184 && out[2]==216 && out[3]==34,
                  "zxv.example.com -> 93.184.216.34");
            #undef MK
        }
        CHECK(net_dns_result(&net, "other.example.com", out) == 0,
              "the cached answer is not handed back for a different name");
        CHECK(net_dns_query(&net, nif, host) == 1,
              "asking again is answered from what we already learned");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
