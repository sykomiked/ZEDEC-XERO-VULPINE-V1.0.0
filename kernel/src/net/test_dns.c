/* test_dns.c — the DNS parser against RFC 1035 wire format and against
 * responses written to attack it.
 *
 * The benign anchors are byte layouts from the RFC. The hostile ones are the
 * three bugs this parser exists to not have: pointer loops, reads past the
 * end, and unbounded decompression. Each of those, unguarded, is a hang or an
 * out-of-bounds read reachable by anyone who can answer a query.
 */
#include <stdio.h>
#include <string.h>
#include "dns.h"

static int failures = 0;
#define CHECK(c,m) do{ if(!(c)){printf("[FAIL] %s\n",m);failures++;} \
    else printf("[PASS] %s\n",m);}while(0)

/* put a wire-format name; returns new offset */
static uint32_t put_name(uint8_t *b, uint32_t at, const char *n) {
    while (*n) {
        uint32_t l = 0;
        while (n[l] && n[l] != '.') l++;
        b[at++] = (uint8_t)l;
        for (uint32_t i = 0; i < l; i++) b[at++] = (uint8_t)n[i];
        n += l;
        if (*n == '.') n++;
    }
    b[at++] = 0;
    return at;
}
static uint32_t put16(uint8_t *b, uint32_t at, uint16_t v) {
    b[at++] = (uint8_t)(v >> 8); b[at++] = (uint8_t)v; return at;
}
static uint32_t put32(uint8_t *b, uint32_t at, uint32_t v) {
    b[at++]=(uint8_t)(v>>24); b[at++]=(uint8_t)(v>>16);
    b[at++]=(uint8_t)(v>>8);  b[at++]=(uint8_t)v; return at;
}

/* A well-formed response: header, echoed question, then `an` answers appended
 * by the caller. Returns the offset where answers begin. */
static uint32_t begin_reply(uint8_t *b, uint16_t id, const char *q,
                            uint16_t an, uint16_t rcode) {
    memset(b, 0, DNS_MAX_MSG);
    put16(b, 0, id);
    put16(b, 2, (uint16_t)(0x8180u | rcode));   /* QR|RD|RA */
    put16(b, 4, 1);
    put16(b, 6, an);
    uint32_t at = put_name(b, DNS_HDR_LEN, q);
    at = put16(b, at, DNS_TYPE_A);
    at = put16(b, at, DNS_CLASS_IN);
    return at;
}

int main(void) {
    printf("=== DNS resolver (RFC 1035 + hostile responses) ===\n");
    const char *host = "zxv.example.com";
    uint8_t q[DNS_MAX_MSG];

    /* ---------- query construction ---------- */
    uint32_t qn = dns_build_query(q, sizeof q, host, 0xBEEF);
    printf("       query for %s is %u bytes\n", host, qn);
    CHECK(qn == DNS_HDR_LEN + 1+3 + 1+7 + 1+3 + 1 + 4,
          "query length is header + labels + root + qtype/qclass");
    CHECK(q[0]==0xBE && q[1]==0xEF, "the transaction id is at offset 0");
    CHECK((q[2] & 0x80) == 0, "QR=0: it is a query");
    CHECK((q[2] & 0x01) != 0, "RD=1: recursion desired");
    CHECK(q[4]==0 && q[5]==1, "QDCOUNT = 1");
    CHECK(q[6]==0 && q[7]==0, "ANCOUNT = 0");
    CHECK(q[12]==3 && q[13]=='z' && q[14]=='x' && q[15]=='v',
          "the first label is length-prefixed: 3 'z' 'x' 'v'");
    CHECK(q[16]==7 && q[24]==3 && q[28]==0,
          "'example' and 'com' follow, terminated by the root label");
    CHECK(q[qn-4]==0 && q[qn-3]==DNS_TYPE_A, "QTYPE = A");
    CHECK(q[qn-2]==0 && q[qn-1]==DNS_CLASS_IN, "QCLASS = IN");
    CHECK(dns_build_query(q, sizeof q, "zxv.example.com.", 1) ==
          dns_build_query(q, sizeof q, "zxv.example.com", 1),
          "a trailing dot encodes identically");

    /* invalid names must be refused, not encoded */
    CHECK(dns_build_query(q, sizeof q, "", 1) == 0, "an empty name is refused");
    CHECK(dns_build_query(q, sizeof q, ".", 1) == 0, "a bare dot is refused");
    CHECK(dns_build_query(q, sizeof q, "a..b", 1) == 0, "an empty label is refused");
    {
        char big[400];
        memset(big, 'a', sizeof big - 1); big[sizeof big - 1] = 0;
        CHECK(dns_build_query(q, sizeof q, big, 1) == 0,
              "a label longer than 63 bytes is refused");
        char many[300];
        uint32_t k = 0;
        for (int i = 0; i < 100; i++) { many[k++]='a'; many[k++]='b'; many[k++]='.'; }
        many[k-1] = 0;
        CHECK(dns_build_query(q, sizeof q, many, 1) == 0,
              "a name longer than 255 bytes is refused");
    }
    CHECK(dns_build_query(q, 20, host, 1) == 0, "a too-small buffer is refused");

    /* ---------- a normal answer ---------- */
    uint8_t r[DNS_MAX_MSG];
    uint8_t ip[4]; uint32_t ttl = 0;
    {
        uint32_t at = begin_reply(r, 0xBEEF, host, 1, 0);
        at = put_name(r, at, host);
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 300);
        at = put16(r, at, 4);
        r[at++]=93; r[at++]=184; r[at++]=216; r[at++]=34;
        CHECK(dns_parse_response(r, at, 0xBEEF, host, ip, &ttl) == DNS_OK,
              "a well-formed A answer resolves");
        CHECK(ip[0]==93 && ip[1]==184 && ip[2]==216 && ip[3]==34,
              "the address is 93.184.216.34");
        CHECK(ttl == 300, "the TTL is read");

        /* the same reply must be rejected for a DIFFERENT transaction */
        CHECK(dns_parse_response(r, at, 0x1234, host, ip, &ttl) == DNS_BAD,
              "a reply whose id does not match ours is refused");
        /* ...and for a different question */
        CHECK(dns_parse_response(r, at, 0xBEEF, "evil.example.com", ip, &ttl) == DNS_BAD,
              "a reply whose QUESTION does not echo our name is refused "
              "(the minimum anti-spoofing check)");
        /* case-insensitivity is required by RFC 4343, not optional */
        CHECK(dns_parse_response(r, at, 0xBEEF, "ZXV.Example.COM", ip, &ttl) == DNS_OK,
              "name comparison is case-insensitive");
        /* a query, not a response */
        r[2] &= (uint8_t)~0x80;
        CHECK(dns_parse_response(r, at, 0xBEEF, host, ip, &ttl) == DNS_BAD,
              "a message with QR=0 is not accepted as an answer");
        r[2] |= 0x80;
    }

    /* ---------- compressed answer (the normal case on the wire) ---------- */
    {
        uint32_t at = begin_reply(r, 1, host, 1, 0);
        at = put16(r, at, 0xC000u | DNS_HDR_LEN);   /* pointer to the question */
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        at = put16(r, at, 4);
        r[at++]=10; r[at++]=1; r[at++]=2; r[at++]=3;
        CHECK(dns_parse_response(r, at, 1, host, ip, &ttl) == DNS_OK && ip[3]==3,
              "a compressed owner name (pointer to the question) resolves");
    }

    /* ---------- CNAME chain ---------- */
    {
        uint32_t at = begin_reply(r, 2, host, 2, 0);
        at = put16(r, at, 0xC000u | DNS_HDR_LEN);
        at = put16(r, at, DNS_TYPE_CNAME);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        uint32_t lenpos = at; at = put16(r, at, 0);
        uint32_t s = at;
        at = put_name(r, at, "real.example.net");
        put16(r, lenpos, (uint16_t)(at - s));
        at = put_name(r, at, "real.example.net");
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        at = put16(r, at, 4);
        r[at++]=8; r[at++]=8; r[at++]=8; r[at++]=8;
        CHECK(dns_parse_response(r, at, 2, host, ip, &ttl) == DNS_OK &&
              ip[0]==8 && ip[3]==8,
              "a CNAME is followed to the A record for its target");
    }

    /* an A record for an unrelated name must NOT be handed back */
    {
        uint32_t at = begin_reply(r, 3, host, 1, 0);
        at = put_name(r, at, "attacker.example.org");
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        at = put16(r, at, 4);
        r[at++]=6; r[at++]=6; r[at++]=6; r[at++]=6;
        CHECK(dns_parse_response(r, at, 3, host, ip, &ttl) == DNS_NO_ANSWER,
              "an A record for a name we did not ask about is ignored "
              "(cache-poisoning by bystander record)");
    }

    /* NXDOMAIN and truncation are answers, not addresses */
    {
        uint32_t at = begin_reply(r, 4, host, 0, 3);   /* RCODE 3 = NXDOMAIN */
        CHECK(dns_parse_response(r, at, 4, host, ip, &ttl) == DNS_NO_ANSWER,
              "NXDOMAIN yields no address");
        at = begin_reply(r, 5, host, 0, 0);
        r[2] |= 0x02;                                   /* TC */
        CHECK(dns_parse_response(r, at, 5, host, ip, &ttl) == DNS_NO_ANSWER,
              "a TRUNCATED reply is not treated as an answer");
    }

    /* ================= hostile: the three parser bugs ================= */

    /* 1a. a pointer to itself */
    {
        uint8_t b[64];
        memset(b, 0, sizeof b);
        put16(b, 0, 9); put16(b, 2, 0x8180); put16(b, 4, 1); put16(b, 6, 0);
        put16(b, DNS_HDR_LEN, 0xC000u | DNS_HDR_LEN);
        char out[DNS_MAX_NAME];
        CHECK(dns_read_name(b, sizeof b, DNS_HDR_LEN, out, sizeof out) == 0,
              "a name pointing at ITSELF is refused (would hang forever)");
    }
    /* 1b. two pointers referring to each other */
    {
        uint8_t b[64];
        memset(b, 0, sizeof b);
        put16(b, 20, 0xC000u | 24);
        put16(b, 24, 0xC000u | 20);
        char out[DNS_MAX_NAME];
        CHECK(dns_read_name(b, sizeof b, 20, out, sizeof out) == 0,
              "a two-pointer cycle is refused");
    }
    /* 1c. a FORWARD pointer — legal-looking, but the loop vehicle */
    {
        uint8_t b[64];
        memset(b, 0, sizeof b);
        put16(b, 12, 0xC000u | 40);
        char out[DNS_MAX_NAME];
        CHECK(dns_read_name(b, sizeof b, 12, out, sizeof out) == 0,
              "a FORWARD pointer is refused — a pointer must go strictly back");
    }
    /* 2. a label whose length runs past the end of the message */
    {
        uint8_t b[32];
        memset(b, 0, sizeof b);
        b[12] = 60;                       /* claims 60 bytes in a 32-byte buffer */
        char out[DNS_MAX_NAME];
        CHECK(dns_read_name(b, sizeof b, 12, out, sizeof out) == 0,
              "a label that overruns the message is refused (OOB read)");
        CHECK(dns_skip_name(b, sizeof b, 12) == 0,
              "and skipping it fails rather than walking off the end");
    }
    /* 2b. a pointer whose second byte is past the end */
    {
        uint8_t b[14];
        memset(b, 0, sizeof b);
        b[13] = 0xC0;
        char out[DNS_MAX_NAME];
        CHECK(dns_read_name(b, sizeof b, 13, out, sizeof out) == 0,
              "a truncated pointer (only one of its two bytes present) is refused");
    }
    /* 3. decompression must respect the caller's buffer */
    {
        uint8_t b[DNS_MAX_MSG];
        memset(b, 0, sizeof b);
        uint32_t at = put_name(b, 12, "aaaaaaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbbbb.cc");
        char small[8];
        CHECK(dns_read_name(b, at, 12, small, sizeof small) == 0,
              "a name longer than the output buffer is refused, not truncated "
              "into it");
    }
    /* 4. RDLENGTH that lies about the record size */
    {
        uint32_t at = begin_reply(r, 6, host, 1, 0);
        at = put16(r, at, 0xC000u | DNS_HDR_LEN);
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        at = put16(r, at, 4000);          /* RDLENGTH far beyond the message */
        r[at++]=1; r[at++]=2; r[at++]=3; r[at++]=4;
        CHECK(dns_parse_response(r, at, 6, host, ip, &ttl) == DNS_BAD,
              "an RDLENGTH larger than the message is refused");
    }
    /* 5. ANCOUNT that claims more records than are present */
    {
        uint32_t at = begin_reply(r, 7, host, 40, 0);   /* claims 40 answers */
        CHECK(dns_parse_response(r, at, 7, host, ip, &ttl) == DNS_BAD,
              "an ANCOUNT larger than the record data is refused");
    }
    /* 6. an A record with the wrong RDLENGTH is not an address */
    {
        uint32_t at = begin_reply(r, 8, host, 1, 0);
        at = put16(r, at, 0xC000u | DNS_HDR_LEN);
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        at = put16(r, at, 16);            /* 16 bytes: an AAAA-sized A record */
        for (int i = 0; i < 16; i++) r[at++] = (uint8_t)i;
        CHECK(dns_parse_response(r, at, 8, host, ip, &ttl) == DNS_NO_ANSWER,
              "an A record that is not 4 bytes is not read as an address");
    }
    /* 7. truncated messages of every length must never crash */
    {
        uint32_t at = begin_reply(r, 9, host, 1, 0);
        at = put16(r, at, 0xC000u | DNS_HDR_LEN);
        at = put16(r, at, DNS_TYPE_A);
        at = put16(r, at, DNS_CLASS_IN);
        at = put32(r, at, 60);
        at = put16(r, at, 4);
        r[at++]=1; r[at++]=1; r[at++]=1; r[at++]=1;
        for (uint32_t cut = 0; cut <= at; cut++) {
            int rc = dns_parse_response(r, cut, 9, host, ip, &ttl);
            (void)rc;
        }
        CHECK(1, "every prefix of a valid response parses without crashing");
    }
    /* 8. arbitrary junk must never crash */
    {
        uint8_t junk[DNS_MAX_MSG];
        for (uint32_t seed = 0; seed < 512; seed++) {
            uint32_t s = seed * 2654435761u;
            for (uint32_t i = 0; i < sizeof junk; i++) {
                s = s * 1103515245u + 12345u;
                junk[i] = (uint8_t)(s >> 16);
            }
            put16(junk, 0, (uint16_t)seed);
            char out[DNS_MAX_NAME];
            (void)dns_read_name(junk, sizeof junk, 12, out, sizeof out);
            (void)dns_skip_name(junk, sizeof junk, 12);
            (void)dns_parse_response(junk, sizeof junk, (uint16_t)seed, host, ip, &ttl);
        }
        CHECK(1, "512 randomised messages parsed without crashing");
    }

    printf("\n%s: %d failure(s)\n", failures?"*** FAILED ***":"ALL PASS", failures);
    return failures?1:0;
}
