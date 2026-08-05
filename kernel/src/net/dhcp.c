/* dhcp.c — DHCP client. See dhcp.h. */
#include "dhcp.h"

static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}
static uint32_t get32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i=0;i<n;i++) d[i]=s[i];
}
static void zero(uint8_t *d, uint32_t n) { for (uint32_t i=0;i<n;i++) d[i]=0; }

void dhcp_init(dhcp_client_t *c, const uint8_t mac[6], uint32_t xid) {
    if (!c) return;
    zero((uint8_t*)c, sizeof(*c));
    c->state = DHCP_STATE_INIT;
    c->xid = xid;
    if (mac) cpy(c->mac, mac, 6);
}

bool dhcp_is_bound(const dhcp_client_t *c) { return c && c->state == DHCP_STATE_BOUND; }

/* Common fixed header. op=1 (BOOTREQUEST), ethernet, 6-byte hardware address,
 * our xid, broadcast flag set so the server replies to the broadcast address —
 * we cannot receive a unicast reply before we have an address. */
static uint32_t build_header(const dhcp_client_t *c, uint8_t *out) {
    zero(out, DHCP_MIN_LEN);
    out[0] = 1;                 /* op: BOOTREQUEST */
    out[1] = 1;                 /* htype: ethernet */
    out[2] = 6;                 /* hlen */
    out[3] = 0;                 /* hops */
    put32(out + 4, c->xid);     /* xid */
    out[10] = 0x80;             /* flags: BROADCAST */
    cpy(out + 28, c->mac, 6);   /* chaddr */
    put32(out + 236, DHCP_MAGIC);
    return DHCP_MIN_LEN;
}

static uint32_t put_opt(uint8_t *out, uint32_t at, uint32_t cap,
                        uint8_t opt, const uint8_t *val, uint8_t len) {
    if (at + 2u + len > cap) return at;
    out[at++] = opt;
    out[at++] = len;
    for (uint8_t i = 0; i < len; i++) out[at++] = val[i];
    return at;
}

uint32_t dhcp_build_discover(dhcp_client_t *c, uint8_t *out, uint32_t cap) {
    if (!c || !out || cap < DHCP_MIN_LEN + 16u) return 0;
    uint32_t at = build_header(c, out);
    uint8_t t = DHCP_DISCOVER;
    at = put_opt(out, at, cap, DHCP_OPT_MSGTYPE, &t, 1);
    /* ask for the things we need to be usable on this network */
    uint8_t params[4] = { DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS, DHCP_OPT_LEASE };
    at = put_opt(out, at, cap, DHCP_OPT_PARAMLIST, params, 4);
    if (at < cap) out[at++] = DHCP_OPT_END;
    c->state = DHCP_STATE_SELECTING;
    return at;
}

uint32_t dhcp_build_request(dhcp_client_t *c, uint8_t *out, uint32_t cap) {
    if (!c || !out || cap < DHCP_MIN_LEN + 24u) return 0;
    if (c->state != DHCP_STATE_SELECTING && c->state != DHCP_STATE_REQUESTING) return 0;
    /* nothing offered yet -> nothing to request */
    bool have = false;
    for (int i = 0; i < 4; i++) if (c->offered_ip[i]) have = true;
    if (!have) return 0;

    uint32_t at = build_header(c, out);
    uint8_t t = DHCP_REQUEST;
    at = put_opt(out, at, cap, DHCP_OPT_MSGTYPE, &t, 1);
    /* Name BOTH the address and the server. This is broadcast, so every
     * server that offered sees which one won and can release the rest. */
    at = put_opt(out, at, cap, DHCP_OPT_REQUESTED, c->offered_ip, 4);
    at = put_opt(out, at, cap, DHCP_OPT_SERVERID, c->server_id, 4);
    uint8_t params[4] = { DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS, DHCP_OPT_LEASE };
    at = put_opt(out, at, cap, DHCP_OPT_PARAMLIST, params, 4);
    if (at < cap) out[at++] = DHCP_OPT_END;
    c->state = DHCP_STATE_REQUESTING;
    return at;
}

uint32_t dhcp_get_option(const uint8_t *msg, uint32_t len, uint8_t opt,
                         const uint8_t **val) {
    if (!msg || len < DHCP_MIN_LEN) return 0;
    if (get32(msg + 236) != DHCP_MAGIC) return 0;
    uint32_t at = DHCP_MIN_LEN;
    /* Bounded on BOTH the buffer length and each option's own length byte:
     * the options come from an untrusted server. */
    while (at < len) {
        uint8_t o = msg[at];
        if (o == DHCP_OPT_END) break;
        if (o == 0) { at++; continue; }             /* pad */
        if (at + 1u >= len) break;                  /* truncated: no length */
        uint8_t l = msg[at + 1];
        if (at + 2u + l > len) break;               /* length runs off the end */
        if (o == opt) { if (val) *val = &msg[at + 2]; return l; }
        at += 2u + l;
    }
    return 0;
}

uint32_t dhcp_input(dhcp_client_t *c, const uint8_t *msg, uint32_t len) {
    if (!c || !msg || len < DHCP_MIN_LEN) return 0;
    if (msg[0] != 2) return 0;                      /* not a BOOTREPLY */
    if (get32(msg + 4) != c->xid) return 0;         /* not our transaction */
    if (get32(msg + 236) != DHCP_MAGIC) return 0;

    const uint8_t *v = 0;
    if (dhcp_get_option(msg, len, DHCP_OPT_MSGTYPE, &v) != 1) return 0;
    uint32_t type = v[0];

    if (type == DHCP_NAK) { c->state = DHCP_STATE_FAILED; return type; }

    if (type == DHCP_OFFER && c->state == DHCP_STATE_SELECTING) {
        cpy(c->offered_ip, msg + 16, 4);            /* yiaddr */
        if (dhcp_get_option(msg, len, DHCP_OPT_SERVERID, &v) == 4)
            cpy(c->server_id, v, 4);
        return type;
    }

    if (type == DHCP_ACK && c->state == DHCP_STATE_REQUESTING) {
        cpy(c->ip, msg + 16, 4);                    /* yiaddr is the lease */
        if (dhcp_get_option(msg, len, DHCP_OPT_SUBNET, &v) == 4) cpy(c->netmask, v, 4);
        if (dhcp_get_option(msg, len, DHCP_OPT_ROUTER, &v) >= 4)  cpy(c->gateway, v, 4);
        if (dhcp_get_option(msg, len, DHCP_OPT_DNS, &v) >= 4)     cpy(c->dns, v, 4);
        if (dhcp_get_option(msg, len, DHCP_OPT_LEASE, &v) == 4)   c->lease_secs = get32(v);
        c->state = DHCP_STATE_BOUND;
        return type;
    }
    return type;
}
