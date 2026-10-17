/* test_curzi_uri.c — the 36n9:// parser: routes, offsets, the gateway
 * fallback through Filebase. Copyright (c) 2024-2026 Michael Laurence
 * Curzi and 36N9 Genetics, LLC; SPDX-License-Identifier: Apache-2.0. */
#include <stdio.h>
#include <string.h>

#include "curzi_uri.h"

#define U(s) (uint32_t) (sizeof(s) - 1)

#define CHECK(cond, ...)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                            \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
            fails++;                                                                               \
        } else {                                                                                   \
            passed++;                                                                              \
        }                                                                                          \
    } while (0)

int main(void)
{
    int passed = 0, fails = 0;
    curzi_uri_t u;
    char gw[160];

    CHECK(curzi_uri_parse("36n9://bank.a1/0x04A880:12", U("36n9://bank.a1/0x04A880:12"), &u) == 0 &&
              u.route == CURZI_ROUTE_BANK && u.basis_bank_id == 0xa1 &&
              u.byte_offset == 0x04A880 && u.frame_count == 12 && u.port == 8724,
          "bank route: id, offset, frames");
    CHECK(curzi_uri_parse("36n9://bank.1/1234", U("36n9://bank.1/1234"), &u) == 0 &&
              u.route == CURZI_ROUTE_BANK && u.byte_offset == 0x1234 &&
              u.frame_count == 1,
          "bank route without frames");
    CHECK(curzi_uri_parse("36n9://ipfs/bafybeicih4utmpxyuyhbskc3wfx2tn5yr7phzvcwii7xen2nrps4spagcq", U("36n9://ipfs/bafybeicih4utmpxyuyhbskc3wfx2tn5yr7phzvcwii7xen2nrps4spagcq"), &u) == 0 &&
              u.route == CURZI_ROUTE_IPFS &&
              !strcmp(u.cid, "bafybeicih4utmpxyuyhbskc3wfx2tn5yr7phzvcwii7xen2nrps4spagcq"),
          "ipfs intercept route");
    CHECK(curzi_uri_parse("36n9://aipi.s0:8724/tensor/7", U("36n9://aipi.s0:8724/tensor/7"), &u) == 0 &&
              u.route == CURZI_ROUTE_AIPI && !strcmp(u.node, "s0") && u.slot == 7,
          "aipi socket route");
    CHECK(curzi_uri_parse("36n9://stream.alpha:8724/live?basis=2", U("36n9://stream.alpha:8724/live?basis=2"), &u) == 0 &&
              u.route == CURZI_ROUTE_STREAM && !strcmp(u.node, "alpha"),
          "stream route");
    CHECK(curzi_uri_parse("http://example.com", U("http://example.com"), &u) == -1, "legacy scheme refused");
    CHECK(curzi_uri_parse("36n9://bank./1", U("36n9://bank./1"), &u) == -1, "empty bank id refused");
    CHECK(curzi_uri_parse("36n9://bank.1/", U("36n9://bank.1/"), &u) == -1, "empty offset refused");
    CHECK(curzi_uri_parse("36n9://aipi./1", U("36n9://aipi./1"), &u) == -1, "empty node refused");

    CHECK(curzi_uri_parse("36n9://ipfs/bafybeicih4utmpxyuyhbskc3wfx2tn5yr7phzvcwii7xen2nrps4spagcq", U("36n9://ipfs/bafybeicih4utmpxyuyhbskc3wfx2tn5yr7phzvcwii7xen2nrps4spagcq"), &u) == 0 &&
              curzi_uri_gateway(&u, "", gw, sizeof gw) == (int32_t)(sizeof("https://ipfs.filebase.io/ipfs/") - 1 + 59) &&
              !strcmp(gw, "https://ipfs.filebase.io/ipfs/bafybeicih4utmpxyuyhbskc3wfx2tn5yr7phzvcwii7xen2nrps4spagcq"),
          "ipfs route maps to the exact CID on the Filebase gateway");
    CHECK(curzi_uri_parse("36n9://bank.a1/0x04A880", U("36n9://bank.a1/0x04A880"), &u) == 0 &&
              curzi_uri_gateway(&u, "bafybeib23pkydq6iwyfbe4nxygaiocxtyahi4hxqozyqxcz5avp23y44yy",
                                gw, sizeof gw) == (int32_t)(sizeof("https://ipfs.filebase.io/ipfs/") - 1 + 59) &&
              !strcmp(gw, "https://ipfs.filebase.io/ipfs/bafybeib23pkydq6iwyfbe4nxygaiocxtyahi4hxqozyqxcz5avp23y44yy"),
          "bank route maps to the bank CID on the Filebase gateway");
    CHECK(curzi_uri_parse("36n9://stream.a:8724/x", U("36n9://stream.a:8724/x"), &u) == 0 &&
              curzi_uri_gateway(&u, "", gw, sizeof gw) == -1,
          "streams are not gateway-mappable");

    printf("curzi_uri: %d checks, %d failures\n", passed, fails);
    return fails ? 1 : 0;
}