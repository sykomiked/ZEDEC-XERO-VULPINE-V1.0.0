/* dns.c — DNS resolver. See dns.h. */
#include "dns.h"

static uint16_t g16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0] << 8) | p[1]); }
static uint32_t g32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static char lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool dns_name_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (lower(*a) != lower(*b)) return false;
        a++; b++;
    }
    /* a single trailing dot is the same name */
    if (*a == '.' && a[1] == 0) a++;
    if (*b == '.' && b[1] == 0) b++;
    return *a == 0 && *b == 0;
}

uint32_t dns_build_query(uint8_t *out, uint32_t cap, const char *name, uint16_t id) {
    if (!out || !name || cap < DNS_HDR_LEN + 6u) return 0;

    /* header: id, RD set, one question */
    out[0] = (uint8_t)(id >> 8); out[1] = (uint8_t)id;
    out[2] = 0x01; out[3] = 0x00;      /* QR=0, opcode=0, RD=1 */
    out[4] = 0x00; out[5] = 0x01;      /* QDCOUNT = 1 */
    out[6] = 0; out[7] = 0;            /* ANCOUNT */
    out[8] = 0; out[9] = 0;            /* NSCOUNT */
    out[10] = 0; out[11] = 0;          /* ARCOUNT */

    /* QNAME: each dotted label becomes <len><bytes>, terminated by a zero */
    uint32_t at = DNS_HDR_LEN;
    uint32_t total = 0;
    const char *p = name;
    while (*p) {
        if (*p == '.') return 0;                   /* empty label */
        uint32_t l = 0;
        while (p[l] && p[l] != '.') l++;
        if (l > DNS_MAX_LABEL) return 0;
        total += l + 1;
        if (total > 255u) return 0;                /* protocol name limit */
        if (at + 1u + l + 5u > cap) return 0;      /* + terminator + qtype/qclass */
        out[at++] = (uint8_t)l;
        for (uint32_t i = 0; i < l; i++) out[at++] = (uint8_t)p[i];
        p += l;
        if (*p == '.') {
            p++;
            if (*p == 0) break;                    /* a trailing dot is fine */
        }
    }
    if (at == DNS_HDR_LEN) return 0;               /* empty name */
    out[at++] = 0;                                 /* root label */
    out[at++] = 0; out[at++] = DNS_TYPE_A;
    out[at++] = 0; out[at++] = DNS_CLASS_IN;
    return at;
}

uint32_t dns_read_name(const uint8_t *msg, uint32_t len, uint32_t at,
                       char *out, uint32_t out_cap) {
    if (!msg || !out || out_cap == 0) return 0;
    uint32_t w = 0;
    uint32_t jumps = 0;
    uint32_t limit = at;   /* every pointer must land STRICTLY before this */

    for (;;) {
        if (at >= len) return 0;
        uint8_t l = msg[at];

        if ((l & 0xC0u) == 0xC0u) {                /* compression pointer */
            if (at + 1u >= len) return 0;
            uint32_t ptr = ((uint32_t)(l & 0x3Fu) << 8) | msg[at + 1];
            /* Strictly backward. This alone makes a loop impossible, because
             * the offset must decrease on every jump; the counter below is a
             * second, independent guard. */
            if (ptr >= limit) return 0;
            if (++jumps > DNS_MAX_JUMPS) return 0;
            limit = ptr;
            at = ptr;
            continue;
        }
        if (l & 0xC0u) return 0;                   /* reserved label type */
        if (l == 0) break;                         /* root: name complete */
        if (l > DNS_MAX_LABEL) return 0;
        if (at + 1u + l > len) return 0;

        if (w) { if (w + 1u >= out_cap) return 0; out[w++] = '.'; }
        if (w + l >= out_cap) return 0;
        if (w + l >= 255u) return 0;               /* protocol name limit */
        for (uint32_t i = 0; i < l; i++) out[w++] = (char)msg[at + 1u + i];
        at += 1u + l;
    }
    out[w] = 0;
    return w;
}

uint32_t dns_skip_name(const uint8_t *msg, uint32_t len, uint32_t at) {
    if (!msg) return 0;
    uint32_t steps = 0;
    for (;;) {
        if (at >= len) return 0;
        if (++steps > 128u) return 0;              /* pathological label chain */
        uint8_t l = msg[at];
        if ((l & 0xC0u) == 0xC0u) {
            if (at + 1u >= len) return 0;
            return at + 2u;                        /* a pointer ends the name */
        }
        if (l & 0xC0u) return 0;
        if (l == 0) return at + 1u;
        if (l > DNS_MAX_LABEL) return 0;
        at += 1u + l;
    }
}

int dns_parse_response(const uint8_t *msg, uint32_t len, uint16_t id,
                       const char *name, uint8_t ip_out[4], uint32_t *ttl_out) {
    if (!msg || !name || !ip_out || len < DNS_HDR_LEN) return DNS_BAD;
    if (g16(msg) != id) return DNS_BAD;            /* not our transaction */
    uint16_t flags = g16(msg + 2);
    if (!(flags & 0x8000u)) return DNS_BAD;        /* not a response */

    uint16_t qd = g16(msg + 4);
    uint16_t an = g16(msg + 6);
    if (qd != 1) return DNS_BAD;                   /* we always ask exactly one */

    /* The question must echo what we asked. Together with the id this is the
     * standard minimum check; see the header for what it does NOT achieve. */
    char qname[DNS_MAX_NAME];
    uint32_t at = DNS_HDR_LEN;
    if (dns_read_name(msg, len, at, qname, sizeof qname) == 0) return DNS_BAD;
    if (!dns_name_eq(qname, name)) return DNS_BAD;
    at = dns_skip_name(msg, len, at);
    if (at == 0 || at + 4u > len) return DNS_BAD;
    at += 4u;                                      /* QTYPE + QCLASS */

    if ((flags & 0x000Fu) != 0) return DNS_NO_ANSWER;   /* RCODE != NOERROR */
    if (flags & 0x0200u) return DNS_NO_ANSWER;          /* truncated: retry over TCP */

    /* Follow the answer section. A server commonly answers an A query with a
     * CNAME followed by the A record for the target, so track the name we are
     * currently chasing rather than assuming the first A record is ours. */
    char want[DNS_MAX_NAME];
    for (uint32_t i = 0; i < sizeof want && (want[i] = name[i]) != 0; i++) {
        if (i + 1u == sizeof want) return DNS_BAD;
    }

    for (uint32_t rec = 0; rec < an; rec++) {
        char rname[DNS_MAX_NAME];
        if (dns_read_name(msg, len, at, rname, sizeof rname) == 0) return DNS_BAD;
        uint32_t next = dns_skip_name(msg, len, at);
        if (next == 0 || next + 10u > len) return DNS_BAD;
        uint16_t rtype  = g16(msg + next);
        uint16_t rclass = g16(msg + next + 2);
        uint32_t rttl   = g32(msg + next + 4);
        uint16_t rdlen  = g16(msg + next + 8);
        uint32_t rdata  = next + 10u;
        if (rdata + rdlen > len) return DNS_BAD;   /* RDLENGTH lies */

        if (rclass == DNS_CLASS_IN && dns_name_eq(rname, want)) {
            if (rtype == DNS_TYPE_A && rdlen == 4) {
                ip_out[0] = msg[rdata];     ip_out[1] = msg[rdata + 1];
                ip_out[2] = msg[rdata + 2]; ip_out[3] = msg[rdata + 3];
                if (ttl_out) *ttl_out = rttl;
                return DNS_OK;
            }
            if (rtype == DNS_TYPE_CNAME) {
                /* chase the alias — bounded, because the answer count is */
                if (dns_read_name(msg, len, rdata, want, sizeof want) == 0)
                    return DNS_BAD;
            }
        }
        at = rdata + rdlen;
    }
    return DNS_NO_ANSWER;
}
