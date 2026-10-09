/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ipfs.c — anchored against FIPS-180-4, not against our own output. */

#include <stdio.h>
#include <string.h>
#include "ipfs.h"

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("  FAIL: %s (line %d)\n", (msg), __LINE__); } \
} while (0)

/* --- a mock transport: returns a fixed blob, optionally with one byte flipped --- */
typedef struct {
    const uint8_t *blob;
    uint32_t       len;
    int            flip;   /* if nonzero, corrupt one byte of the reply */
} mock_ctx_t;

static int mock_fetch(const uint8_t cid[32], uint8_t *buf, uint32_t cap,
                      uint32_t *out_len, void *ctx) {
    (void)cid;
    mock_ctx_t *m = (mock_ctx_t *)ctx;
    if (m->len > cap) return -1;
    for (uint32_t i = 0; i < m->len; i++) buf[i] = m->blob[i];
    if (m->flip && m->len > 0) buf[0] ^= 0x01;   /* one flipped byte */
    *out_len = m->len;
    return 0;
}

int main(void) {
    printf("=== ipfs: the smuggler's hold ===\n");

    /* (1) ANCHOR — FIPS-180-4 SHA-256("abc") is the CID of "abc". */
    static const uint8_t FIPS_ABC[32] = {
        0xba,0x78,0x16,0xbf, 0x8f,0x01,0xcf,0xea, 0x41,0x41,0x40,0xde, 0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3, 0x96,0x17,0x7a,0x9c, 0xb4,0x10,0xff,0x61, 0xf2,0x00,0x15,0xad
    };
    uint8_t cid_abc[32];
    int32_t rc = ipfs_cid_from_bytes((const uint8_t *)"abc", 3, cid_abc);
    CHECK(rc == 0, "cid_from_bytes returns 0");
    CHECK(memcmp(cid_abc, FIPS_ABC, 32) == 0, "CID('abc') == FIPS SHA-256('abc')");

    /* (2) format -> parse round-trips for all three schemes. */
    const ipfs_uri_scheme_t schemes[3] = { IPFS_SCHEME_CID, IPFS_SCHEME_IPFS, IPFS_SCHEME_AIPI };
    const char *pfx[3] = { "cid:", "ipfs://", "aipi://" };
    for (int s = 0; s < 3; s++) {
        char uri[128];
        int32_t w = ipfs_cid_format(cid_abc, schemes[s], uri, sizeof(uri));
        CHECK(w > 0, "cid_format writes chars");
        CHECK(strncmp(uri, pfx[s], strlen(pfx[s])) == 0, "cid_format has right prefix");
        CHECK((uint32_t)w == strlen(pfx[s]) + 64u, "cid_format length = prefix + 64 hex");
        uint8_t back[32];
        ipfs_result_t pr = ipfs_cid_parse(uri, back);
        CHECK(pr == IPFS_OK, "cid_parse accepts our own URI");
        CHECK(memcmp(back, cid_abc, 32) == 0, "round-trip preserves the CID");
    }

    /* cid_format rejects a too-small buffer (no silent truncation). */
    { char tiny[8]; CHECK(ipfs_cid_format(cid_abc, IPFS_SCHEME_IPFS, tiny, sizeof(tiny)) == -1,
                          "cid_format fails closed on a small buffer"); }

    /* (3) reject MD5-length (32 hex) and SHA-1-length (40 hex) references. */
    { uint8_t d[32];
      CHECK(ipfs_cid_parse("cid:d41d8cd98f00b204e9800998ecf8427e", d) == IPFS_ERR_BAD_CID,
            "32-hex (MD5) rejected as BAD_CID");
      CHECK(ipfs_cid_parse("ipfs://da39a3ee5e6b4b0d3255bfef95601890afd80709", d) == IPFS_ERR_BAD_CID,
            "40-hex (SHA-1) rejected as BAD_CID");
      CHECK(ipfs_cid_parse("bogus://0000", d) == IPFS_ERR_BAD_CID, "unknown scheme rejected");
      /* 64 hex + trailing junk must not sneak through */
      char longuri[160]; ipfs_cid_format(cid_abc, IPFS_SCHEME_CID, longuri, sizeof(longuri));
      strcat(longuri, "ff");
      CHECK(ipfs_cid_parse(longuri, d) == IPFS_ERR_BAD_CID, "64-hex + trailing junk rejected");
    }

    /* (4a) mock transport whose bytes really hash to the request -> IPFS_OK + bytes. */
    {
        const char *content = "The port authority cannot rename your hash.";
        uint32_t clen = (uint32_t)strlen(content);
        uint8_t want[32];
        ipfs_cid_from_bytes((const uint8_t *)content, clen, want);

        mock_ctx_t mc = { (const uint8_t *)content, clen, 0 };
        ipfs_transport_t tr = { mock_fetch, &mc };
        ipfs_node_t node; ipfs_node_init(&node); ipfs_set_transport(&node, &tr);

        uint8_t buf[256]; uint32_t got = 0;
        ipfs_result_t r = ipfs_get_verify(&node, want, buf, sizeof(buf), &got);
        CHECK(r == IPFS_OK, "get_verify OK when returned bytes hash to the CID");
        CHECK(got == clen, "get_verify reports the right length");
        CHECK(memcmp(buf, content, clen) == 0, "get_verify hands back the real bytes");

        /* (4b) same mock, one byte flipped in the reply -> CID_MISMATCH. */
        mock_ctx_t mcf = { (const uint8_t *)content, clen, 1 };
        ipfs_transport_t trf = { mock_fetch, &mcf };
        ipfs_set_transport(&node, &trf);
        uint8_t buf2[256]; uint32_t got2 = 0;
        ipfs_result_t r2 = ipfs_get_verify(&node, want, buf2, sizeof(buf2), &got2);
        CHECK(r2 == IPFS_ERR_CID_MISMATCH, "get_verify rejects a single flipped byte");

        /* add_pin over a confirming transport reports OK; the CID is filled. */
        ipfs_set_transport(&node, &tr);
        uint8_t pin_cid[32];
        ipfs_result_t rp = ipfs_add_pin(&node, (const uint8_t *)content, clen, pin_cid);
        CHECK(rp == IPFS_OK, "add_pin confirmed when the network serves the bytes");
        CHECK(memcmp(pin_cid, want, 32) == 0, "add_pin fills the true CID");
    }

    /* (5) an UNBOUND node: NO_TRANSPORT, and the buffer is not touched. */
    {
        ipfs_node_t bare; ipfs_node_init(&bare);
        uint8_t buf[16];
        memset(buf, 0xAB, sizeof(buf));
        uint32_t got = 12345;
        ipfs_result_t r = ipfs_get_verify(&bare, cid_abc, buf, sizeof(buf), &got);
        CHECK(r == IPFS_ERR_NO_TRANSPORT, "unbound get_verify -> NO_TRANSPORT");
        int untouched = 1;
        for (int i = 0; i < 16; i++) if (buf[i] != 0xAB) untouched = 0;
        CHECK(untouched, "unbound get_verify leaves the buffer untouched");
        CHECK(got == 12345, "unbound get_verify does not write out_len");

        /* add_pin without a transport still yields the CID but claims nothing. */
        uint8_t oc[32];
        ipfs_result_t rp = ipfs_add_pin(&bare, (const uint8_t *)"abc", 3, oc);
        CHECK(rp == IPFS_ERR_NO_TRANSPORT, "unbound add_pin -> NO_TRANSPORT (no hollow pin)");
        CHECK(memcmp(oc, FIPS_ABC, 32) == 0, "unbound add_pin still fills the honest CID");
    }

    /* (6) bridge install: a Web3 URI resolves through our resolver end-to-end. */
    {
        ipfs_node_t node; ipfs_node_init(&node);
        bridge_t br; bridge_init(&br);
        CHECK(ipfs_install_as_bridge_resolver(&node, &br) == 0, "install resolver returns 0");

        char uri[128];
        ipfs_cid_format(cid_abc, IPFS_SCHEME_IPFS, uri, sizeof(uri));  /* ipfs://<64hex> */
        bridge_result_t out;
        bridge_status_t bs = bridge_resolve(&br, uri, &out);
        CHECK(bs == BR_OK, "bridge_resolve OK through the installed IPFS resolver");
        CHECK(out.realm == REALM_WEB3, "bridge classified it Web3");
        CHECK(memcmp(out.cid, cid_abc, 32) == 0, "bridge returned the exact CID we addressed");
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
