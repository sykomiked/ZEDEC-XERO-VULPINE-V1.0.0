/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Signpost's model against zxvd's real dispatcher: web4.cid and
 * web4.card_verify as zxvd runs them, net.peers through view ops that list
 * the peers below (and none set: networking off). */
#include "signpost_model.h"

#define SP_ID_A "aa00000000000000000000000000000000000000000000000000000000000001"
#define SP_ID_B "bb00000000000000000000000000000000000000000000000000000000000002"
#define SP_PEER(id, addr, bucket, seen, member)                                                    \
    "{\"id\":\"" id "\",\"addr\":\"" addr "\",\"bucket\":" #bucket ",\"ubh\":true,"                \
    "\"last_seen_ms\":\"" seen "\",\"in_book\":false,\"member\":" #member ",\"held\":\"0\","       \
    "\"receivable\":\"0\",\"social\":\"0\",\"pending\":0}"

static const char *SP_PEERS;

static void sp_peers(zxvd_buf_t *o)
{
    zxvd_buf_raw(o, SP_PEERS, strlen(SP_PEERS));
}

static bool sp_scan(const char *name, int32_t fault, uint32_t label, uint32_t labels,
                    uint32_t total)
{
    signpost_scan_t sc;
    int32_t f = signpost_name_check(name, (uint32_t) strlen(name), &sc);
    return f == fault && sc.fault == fault && sc.label == label && sc.labels == labels &&
           sc.total == total;
}

APPS_TEST(signpost)
{
    static signpost_t S;
    static char name63[64], name64[65], long253[254], long254[255];
    static char pk[SIGNPOST_PK_HEX + 1], sig[SIGNPOST_SIG_HEX + 1], id[SIGNPOST_ID_HEX + 1];
    static const zxvd_view_ops_t VIEW = {sp_peers, NULL, NULL};

    /* SN1: names */
    memset(name63, 'a', 63);
    memset(name64, 'b', 64);
    for (uint32_t i = 0; i < 253; i++) long253[i] = i % 2 ? '.' : 'x';
    memcpy(long254, long253, 253);
    long254[253] = 'y';
    long254[252] = 'x';
    CHECK(sp_scan("example.com", SIGNPOST_FITS, 0, 2, 11) &&
              sp_scan("Example.COM.", SIGNPOST_FITS, 0, 2, 11) && sp_scan("x", 0, 0, 1, 1) &&
              sp_scan(name63, SIGNPOST_FITS, 0, 1, 63) &&
              sp_scan(long253, SIGNPOST_FITS, 0, 127, 253),
          "names that fit: labels and lengths (SP1)");
    CHECK(sp_scan("", SIGNPOST_BAD_LABEL, 0, 0, 0) && sp_scan(".", SIGNPOST_BAD_LABEL, 0, 0, 0) &&
              sp_scan("a..b", SIGNPOST_BAD_LABEL, 1, 2, 4) &&
              sp_scan("a..", SIGNPOST_BAD_LABEL, 1, 2, 2) &&
              sp_scan(name64, SIGNPOST_BAD_LABEL, 0, 1, 64) &&
              sp_scan(long254, SIGNPOST_BAD_TOTAL, 0, 1, 254),
          "an empty label, a label past 63, a name past 253 (SP2)");
    CHECK(sp_scan("-a.com", SIGNPOST_BAD_BYTE, 0, 1, 6) &&
              sp_scan("ok.a-.com", SIGNPOST_BAD_BYTE, 1, 2, 9) &&
              sp_scan("a_b.com", SIGNPOST_BAD_BYTE, 0, 1, 7) &&
              sp_scan("a-b.c0m", SIGNPOST_FITS, 0, 2, 7) &&
              signpost_name_check(NULL, 4, NULL) == SIGNPOST_BAD_LABEL,
          "bytes not allowed where they stand (SP2)");

    /* SN2: addresses */
    uint8_t o[4];
    uint16_t port = 7;
    CHECK(signpost_parse_ipv4("192.168.1.1", 11, o, &port) && o[0] == 192 && o[3] == 1 &&
              port == 0 && signpost_addr_number(o) == 3232235777 &&
              signpost_parse_ipv4("10.0.0.255:8723", 15, o, &port) && port == 8723 && o[3] == 255,
          "an address, and one with a port (SN2)");
    CHECK(!signpost_parse_ipv4("256.1.1.1", 9, o, &port) &&
              !signpost_parse_ipv4("01.2.3.4", 8, o, &port) &&
              !signpost_parse_ipv4("1.2.3", 5, o, &port) &&
              !signpost_parse_ipv4("1.2.3.4:0", 9, o, &port) &&
              !signpost_parse_ipv4("1.2.3.4:65536", 13, o, &port) &&
              !signpost_parse_ipv4("1.2.3.4x", 8, o, &port) &&
              !signpost_parse_ipv4("1.2.3.4.5", 9, o, &port) &&
              !signpost_parse_ipv4("", 0, o, &port),
          "not addresses: past 255, a leading zero, three octets, ports 0 and 65536 (SN2)");

    /* SN3, SN4: the cache, nothing sent */
    signpost_init(&S, &LINK);
    int before = sent;
    int64_t left = -1;
    signpost_answer_t ans;
    CHECK(signpost_remember(&S, "Hall.Example", 12, "192.168.1.20", 300, 1000) == ZXM_OK &&
              signpost_remember(&S, "relay.example.", 14, "8.8.4.4", 60, 1000) == ZXM_OK &&
              S.ncache == 2 && S.cache[0].cls == SIGNPOST_PRIVATE &&
              S.cache[1].cls == SIGNPOST_PUBLIC && !strcmp(S.cache[0].name, "hall.example"),
          "remembered: names lower-cased, addresses classed");
    CHECK(signpost_lookup(&S, "hall.EXAMPLE.", 13, 1000, &ans, &left) == ZXM_OK && left == 300 &&
              ans.octet[3] == 20 &&
              signpost_lookup(&S, "hall.example", 12, 1299, &ans, &left) == ZXM_OK && left == 1,
          "looked up, letters without case: seconds left (SP6)");
    CHECK(signpost_lookup(&S, "hall.example", 12, 1300, &ans, &left) == ZXM_E_STATE &&
              S.ncache == 1 &&
              signpost_lookup(&S, "nowhere.example", 15, 1000, &ans, &left) == ZXM_E_STATE,
          "a lapsed answer is dropped (SN4); a name not cached is not answered");
    CHECK(signpost_remember(&S, "relay.example", 13, "9.9.9.9", 600, 2000) == ZXM_OK &&
              S.ncache == 1 && S.cache[0].octet[0] == 9 &&
              signpost_lookup(&S, "relay.example", 13, 2000, &ans, &left) == ZXM_OK && left == 600,
          "the same name is replaced");
    CHECK(signpost_remember(&S, "bad_name.example", 16, "1.2.3.4", 60, 0) == ZXM_E_ARG &&
              signpost_remember(&S, "ok.example", 10, "1.2.3.4:80", 60, 0) == ZXM_E_ARG &&
              signpost_remember(&S, "ok.example", 10, "1.2.3", 60, 0) == ZXM_E_ARG &&
              signpost_remember(&S, "ok.example", 10, "1.2.3.4", -1, 0) == ZXM_E_ARG &&
              signpost_remember(&S, "ok.example", 10, "1.2.3.4", 2147483648, 0) == ZXM_E_ARG &&
              signpost_remember(&S, "ok.example", 10, "1.2.3.4", 60, 100000000000) == ZXM_E_ARG &&
              signpost_remember(&S, "ok.example", 10, "1.2.3.4", 60, -1) == ZXM_E_ARG &&
              signpost_lookup(&S, "a..b", 4, 0, &ans, &left) == ZXM_E_ARG && S.ncache == 1,
          "refused before anything is kept: a bad name, a port, a bad time to live, a time "
          "past the record (SN3, SP5)");
    for (uint32_t i = 1; i < SIGNPOST_CACHE; i++) {
        char nm[16] = "host00.example";
        nm[4] = (char) ('0' + i / 10u);
        nm[5] = (char) ('0' + i % 10u);
        CHECK(signpost_remember(&S, nm, 14, "10.0.0.1", 1000 + i, 2000) == ZXM_OK, "fill %u", i);
    }
    CHECK(S.ncache == SIGNPOST_CACHE &&
              signpost_remember(&S, "new.example", 11, "10.0.0.2", 60, 2000) == ZXM_OK &&
              S.ncache == SIGNPOST_CACHE &&
              signpost_lookup(&S, "relay.example", 13, 2000, &ans, &left) == ZXM_E_STATE &&
              signpost_lookup(&S, "new.example", 11, 2000, &ans, &left) == ZXM_OK,
          "full: the answer with the fewest seconds left makes room (SN3)");
    CHECK(signpost_sweep(&S, 3001) == 2 && S.ncache == SIGNPOST_CACHE - 2 &&
              signpost_sweep(&S, 100000) == SIGNPOST_CACHE - 2 && S.ncache == 0 && sent == before,
          "lapsed answers swept; the cache never asks zxvd");

    /* web4.cid */
    CHECK(signpost_cid_of(&S, (const uint8_t *) "hello", 5) == ZXM_OK &&
              !strcmp(S.cid.cid, "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq") &&
              !strcmp(S.cid.uri,
                      "ipfs://bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq") &&
              S.cid.version == 1 && S.cid.codec == 85 && S.cid.has_digest &&
              !strcmp(S.cid.digest,
                      "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824"),
          "the CID of hello: %s", LINK.err);
    CHECK(signpost_cid(&S, "QmWATWQ7fVPP2EFGu71UkfnqhYXDYH566qy47CnJDgvs8u") == ZXM_OK &&
              S.cid.version == 0 && S.cid.codec == 112 && S.cid.has_digest,
          "a CIDv0's forms: %s", LINK.err);
    CHECK(signpost_cid(&S, "bafkqaaa") == ZXM_OK && !S.cid.has_digest && S.cid.version == 1,
          "an identity CID has no digest form: %s", LINK.err);
    before = sent;
    CHECK(signpost_cid(&S, "bafy-not-a-cid") == ZXM_E_REMOTE && LINK.err_code == -32602 &&
              !S.cid.has_digest && sent == before + 1,
          "not a CID: zxvd refuses, the last answer stays");
    CHECK(signpost_cid(&S, "") == ZXM_E_ARG && signpost_cid(&S, NULL) == ZXM_E_ARG &&
              signpost_cid_of(&S, NULL, 1) == ZXM_E_ARG &&
              signpost_cid_of(&S, (const uint8_t *) "x", SIGNPOST_DATA_MAX + 1u) == ZXM_E_ARG &&
              sent == before + 1,
          "nothing sent for what cannot be addressed");

    /* web4.card_verify on a card zxvd made from a test seed */
    char seed[65];
    memset(seed, '7', 64);
    seed[64] = 0;
    zxm_begin(&LINK, "web4.agent_card");
    zxm_p_str(&LINK, "seed_hex", seed);
    zxm_p_str(&LINK, "name", "signpost-1");
    zxm_p_u64(&LINK, "bound_ms", 1700000000000u);
    CHECK(zxm_call(&LINK) == ZXM_OK && !zxm_get_str(&LINK, LINK.result, "pk_hex", pk, sizeof pk) &&
              !zxm_get_str(&LINK, LINK.result, "bind_sig_hex", sig, sizeof sig) &&
              !zxm_get_str(&LINK, LINK.result, "agent_id", id, sizeof id),
          "a card to check: %s", LINK.err);
    signpost_card_t card = {"signpost-1", 1700000000000u, pk, sig, id};
    CHECK(signpost_card_verify(&S, &card) == ZXM_OK && S.card.valid && !S.card.reason[0] &&
              !strcmp(S.card.agent_id, id),
          "the card verifies: %s", LINK.err);
    card.name = "signpost-2";
    CHECK(signpost_card_verify(&S, &card) == ZXM_OK && !S.card.valid && S.card.reason[0],
          "another name: not valid, with the reason (%s)", S.card.reason);
    card.name = "signpost-1";
    sig[100] = sig[100] == '0' ? '1' : '0';
    CHECK(signpost_card_verify(&S, &card) == ZXM_OK && !S.card.valid,
          "a flipped signature digit: not valid (%s)", S.card.reason);
    before = sent;
    card.name = "Signpost";
    CHECK(signpost_card_verify(&S, &card) == ZXM_E_ARG, "an upper-case name");
    card.name = "signpost-1";
    card.agent_id = "abc";
    CHECK(signpost_card_verify(&S, &card) == ZXM_E_ARG, "a short agent_id");
    card.agent_id = NULL;
    card.pk_hex = "00";
    CHECK(signpost_card_verify(&S, &card) == ZXM_E_ARG &&
              signpost_card_verify(&S, NULL) == ZXM_E_ARG && sent == before,
          "a key of the wrong length: nothing sent");

    /* net.peers: networking off, then two peers */
    zxvd_rpc_set_view_ops(NULL);
    CHECK(signpost_peers(&S) == ZXM_E_REMOTE && LINK.err_code == -32601 && S.npeers == 0,
          "no networking: zxvd says so: %s", LINK.err);
    SP_PEERS =
        "{\"peers\":[" SP_PEER(SP_ID_A, "192.168.1.7:8723", 3, "1700000000123",
                               true) "," SP_PEER(SP_ID_B, "", 250, "0", false) "],\"count\":2}";
    zxvd_rpc_set_view_ops(&VIEW);
    const signpost_peer_t *pa, *pb;
    CHECK(signpost_peers(&S) == ZXM_OK && S.npeers == 2 && S.peers_left_out == 0 &&
              (pa = signpost_peer(&S, SP_ID_A)) != NULL && pa->has_addr && pa->port == 8723 &&
              pa->cls == SIGNPOST_PRIVATE && pa->bucket == 3 && pa->member &&
              pa->last_seen_ms == 1700000000123u && (pb = signpost_peer(&S, SP_ID_B)) != NULL &&
              !pb->has_addr && pb->cls == SIGNPOST_REFUSED && !pb->member,
          "two peers by NodeID: %s", LINK.err);
    CHECK(!signpost_peer(&S, "AA00000000000000000000000000000000000000000000000000000000000001") &&
              !signpost_peer(&S, "aa") && !signpost_peer(&S, NULL),
          "a NodeID is 64 lower-case hex digits");
    SP_PEERS = "{\"peers\":[" SP_PEER(SP_ID_A, "300.1.1.1:8723", 3, "1", true) "],\"count\":1}";
    CHECK(signpost_peers(&S) == ZXM_E_PROTOCOL && S.npeers == 2,
          "a peer list that does not parse leaves the last one (SN5)");
    zxvd_rpc_set_view_ops(NULL);
}
