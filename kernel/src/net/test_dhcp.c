/* test_dhcp.c — the DHCP client against the RFC 2131 wire format.
 *
 * The anchors are byte offsets from the RFC, not values read back out of our
 * own builder: op/htype/hlen at 0..2, xid at 4, flags at 10, chaddr at 28,
 * the magic cookie 0x63825363 at 236, options from 240. If our layout drifts
 * from the standard, a real server stops answering — so the test checks the
 * bytes, not the intent.
 */
#include <stdio.h>
#include <string.h>
#include "dhcp.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

static uint32_t g32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}

/* Build a server reply of `type` offering `ip`, as a real server would. */
static uint32_t make_reply(uint8_t *out, uint32_t xid, const uint8_t mac[6],
                           uint8_t type, const uint8_t ip[4],
                           const uint8_t srv[4], bool with_opts) {
    memset(out, 0, DHCP_MAX_LEN);
    out[0] = 2; out[1] = 1; out[2] = 6;
    out[4]=(uint8_t)(xid>>24); out[5]=(uint8_t)(xid>>16);
    out[6]=(uint8_t)(xid>>8);  out[7]=(uint8_t)xid;
    memcpy(out + 16, ip, 4);                 /* yiaddr */
    memcpy(out + 28, mac, 6);                /* chaddr */
    out[236]=0x63; out[237]=0x82; out[238]=0x53; out[239]=0x63;
    uint32_t at = 240;
    out[at++] = DHCP_OPT_MSGTYPE; out[at++] = 1; out[at++] = type;
    out[at++] = DHCP_OPT_SERVERID; out[at++] = 4;
    memcpy(out + at, srv, 4); at += 4;
    if (with_opts) {
        uint8_t mask[4] = {255,255,255,0};
        uint8_t rtr[4]  = {10,0,2,2};
        uint8_t dns[4]  = {10,0,2,3};
        out[at++]=DHCP_OPT_SUBNET; out[at++]=4; memcpy(out+at,mask,4); at+=4;
        out[at++]=DHCP_OPT_ROUTER; out[at++]=4; memcpy(out+at,rtr,4);  at+=4;
        out[at++]=DHCP_OPT_DNS;    out[at++]=4; memcpy(out+at,dns,4);  at+=4;
        out[at++]=DHCP_OPT_LEASE;  out[at++]=4;
        out[at++]=0; out[at++]=0; out[at++]=0x0e; out[at++]=0x10;  /* 3600s */
    }
    out[at++] = DHCP_OPT_END;
    return at;
}

int main(void) {
    printf("=== DHCP client (RFC 2131 wire format) ===\n");
    uint8_t mac[6] = {0x52,0x54,0x00,0x12,0x34,0x56};
    dhcp_client_t c;
    dhcp_init(&c, mac, 0xDEADBEEF);
    CHECK(c.state == DHCP_STATE_INIT && !dhcp_is_bound(&c), "starts unbound");

    /* ---- DISCOVER has the RFC layout ---- */
    uint8_t pkt[DHCP_MAX_LEN];
    uint32_t n = dhcp_build_discover(&c, pkt, sizeof pkt);
    printf("       DISCOVER is %u bytes\n", n);
    CHECK(n >= DHCP_MIN_LEN, "DISCOVER is at least the fixed header");
    CHECK(pkt[0] == 1, "op = BOOTREQUEST (1)");
    CHECK(pkt[1] == 1 && pkt[2] == 6, "htype = ethernet, hlen = 6");
    CHECK(g32(pkt + 4) == 0xDEADBEEF, "xid is carried at offset 4");
    CHECK((pkt[10] & 0x80) != 0,
          "BROADCAST flag is set — we cannot receive a unicast reply before "
          "we have an address");
    CHECK(memcmp(pkt + 28, mac, 6) == 0, "chaddr holds our MAC at offset 28");
    CHECK(g32(pkt + 236) == DHCP_MAGIC, "magic cookie 0x63825363 at offset 236");
    const uint8_t *v = 0;
    CHECK(dhcp_get_option(pkt, n, DHCP_OPT_MSGTYPE, &v) == 1 && v[0] == DHCP_DISCOVER,
          "option 53 says DISCOVER");
    CHECK(dhcp_get_option(pkt, n, DHCP_OPT_PARAMLIST, &v) == 4,
          "we ask for subnet/router/DNS/lease");
    CHECK(pkt[n-1] == DHCP_OPT_END, "options terminate with 255");
    CHECK(c.state == DHCP_STATE_SELECTING, "state -> SELECTING");

    /* ---- OFFER is accepted ---- */
    uint8_t reply[DHCP_MAX_LEN];
    uint8_t offer_ip[4] = {10,0,2,15};
    uint8_t srv[4]      = {10,0,2,2};
    uint32_t rn = make_reply(reply, 0xDEADBEEF, mac, DHCP_OFFER, offer_ip, srv, false);
    CHECK(dhcp_input(&c, reply, rn) == DHCP_OFFER, "an OFFER is recognised");
    CHECK(memcmp(c.offered_ip, offer_ip, 4) == 0, "the offered address is recorded");
    CHECK(memcmp(c.server_id, srv, 4) == 0, "the server id is recorded");
    CHECK(!dhcp_is_bound(&c), "an OFFER alone does NOT bind a lease");

    /* ---- REQUEST names both the address and the server ---- */
    n = dhcp_build_request(&c, pkt, sizeof pkt);
    CHECK(n >= DHCP_MIN_LEN, "REQUEST builds");
    CHECK(dhcp_get_option(pkt, n, DHCP_OPT_MSGTYPE, &v) == 1 && v[0] == DHCP_REQUEST,
          "option 53 says REQUEST");
    CHECK(dhcp_get_option(pkt, n, DHCP_OPT_REQUESTED, &v) == 4 && memcmp(v, offer_ip, 4) == 0,
          "option 50 names the address we accept");
    CHECK(dhcp_get_option(pkt, n, DHCP_OPT_SERVERID, &v) == 4 && memcmp(v, srv, 4) == 0,
          "option 54 names WHICH server won — so the losers release their offers");
    CHECK(c.state == DHCP_STATE_REQUESTING, "state -> REQUESTING");

    /* ---- ACK binds the lease and configures the interface ---- */
    rn = make_reply(reply, 0xDEADBEEF, mac, DHCP_ACK, offer_ip, srv, true);
    CHECK(dhcp_input(&c, reply, rn) == DHCP_ACK, "an ACK is recognised");
    CHECK(dhcp_is_bound(&c), "the lease is BOUND");
    CHECK(c.ip[0]==10 && c.ip[3]==15, "address 10.0.2.15 taken from yiaddr");
    CHECK(c.netmask[0]==255 && c.netmask[3]==0, "netmask from option 1");
    CHECK(c.gateway[0]==10 && c.gateway[3]==2, "gateway from option 3");
    CHECK(c.dns[3]==3, "DNS server from option 6");
    CHECK(c.lease_secs == 3600, "lease time from option 51 (3600s)");

    /* ================ hostile / malformed input ================ */
    {
        /* a reply for someone ELSE's transaction must be ignored */
        dhcp_client_t d; dhcp_init(&d, mac, 0x11111111);
        dhcp_build_discover(&d, pkt, sizeof pkt);
        rn = make_reply(reply, 0x99999999, mac, DHCP_OFFER, offer_ip, srv, false);
        CHECK(dhcp_input(&d, reply, rn) == 0,
              "a reply with a DIFFERENT xid is ignored (not our transaction)");
        CHECK(d.offered_ip[0] == 0, "and nothing was recorded from it");

        /* a BOOTREQUEST (op=1) is not a server reply */
        rn = make_reply(reply, 0x11111111, mac, DHCP_OFFER, offer_ip, srv, false);
        reply[0] = 1;
        CHECK(dhcp_input(&d, reply, rn) == 0, "op=BOOTREQUEST is not accepted as a reply");

        /* bad magic cookie */
        rn = make_reply(reply, 0x11111111, mac, DHCP_OFFER, offer_ip, srv, false);
        reply[238] = 0x00;
        CHECK(dhcp_input(&d, reply, rn) == 0, "a wrong magic cookie is rejected");

        /* NAK fails the exchange rather than binding anything */
        dhcp_client_t e; dhcp_init(&e, mac, 0x22222222);
        dhcp_build_discover(&e, pkt, sizeof pkt);
        rn = make_reply(reply, 0x22222222, mac, DHCP_NAK, offer_ip, srv, false);
        CHECK(dhcp_input(&e, reply, rn) == DHCP_NAK && e.state == DHCP_STATE_FAILED,
              "a NAK moves to FAILED, never to BOUND");
        CHECK(!dhcp_is_bound(&e), "and no lease is held");
    }

    /* option walking must be safe on truncated / lying input */
    {
        uint8_t bad[DHCP_MAX_LEN];
        uint32_t bn = make_reply(bad, 0xDEADBEEF, mac, DHCP_OFFER, offer_ip, srv, true);
        /* an option whose length runs past the end of the buffer */
        bad[240] = 99; bad[241] = 200;
        CHECK(dhcp_get_option(bad, bn, 99, &v) == 0,
              "an option whose length overruns the buffer is refused");
        /* truncated below the fixed header */
        CHECK(dhcp_get_option(bad, 100, DHCP_OPT_MSGTYPE, &v) == 0,
              "a message shorter than the fixed header yields nothing");
        /* an option list with no END terminator must still terminate */
        uint8_t noend[DHCP_MAX_LEN];
        uint32_t nn = make_reply(noend, 0xDEADBEEF, mac, DHCP_OFFER, offer_ip, srv, true);
        for (uint32_t i = 240; i < nn; i++) if (noend[i] == DHCP_OPT_END) noend[i] = 0;
        CHECK(dhcp_get_option(noend, nn, 200, &v) == 0,
              "an unterminated option list still terminates the walk (no overrun)");
    }

    /* ---- cannot REQUEST before anything is offered ---- */
    {
        dhcp_client_t f; dhcp_init(&f, mac, 0x33333333);
        dhcp_build_discover(&f, pkt, sizeof pkt);
        CHECK(dhcp_build_request(&f, pkt, sizeof pkt) == 0,
              "REQUEST is refused when nothing has been offered");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
