/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_ble.c — BLE ATT payload sizes, fragmentation and reassembly
 * (exact wire bytes, every refusal path, counter wrap) and the
 * length-prefixed L2CAP / Wi-Fi Direct stream. */
#include <string.h>

#include "br_ble.h"
#include "test_check.h"

static uint8_t g_msg[131072], g_back[131072], g_frag[600];

static int roundtrip(uint32_t len, uint32_t mtu, uint8_t ctr)
{
    br_frag_tx_t tx;
    br_frag_rx_t rx;
    for (uint32_t i = 0; i < len; i++) g_msg[i] = (uint8_t) (i * 13u + len);
    if (br_frag_tx_init(&tx, g_msg, len, mtu, ctr) != BR_OK) return 0;
    br_frag_rx_init(&rx, g_back, sizeof g_back);
    uint32_t frags = 0, got = 0;
    int r = 0;
    int32_t n;
    while ((n = br_frag_tx_next(&tx, g_frag, sizeof g_frag)) > 0) {
        if ((uint32_t) n > mtu) return 0;
        frags++;
        r = br_frag_rx_push(&rx, g_frag, (uint32_t) n, &got);
        if (r < 0) return 0;
    }
    return n == 0 && r == 1 && got == len && frags == br_frag_count(len, mtu) &&
           !memcmp(g_back, g_msg, len);
}

static void test_mtu(void)
{
    CHECK("ATT_MTU 23 -> 20-byte payload", br_ble_payload_mtu(23) == 20);
    CHECK("ATT_MTU 247 -> 244", br_ble_payload_mtu(247) == 244);
    CHECK("ATT_MTU 515 -> 512", br_ble_payload_mtu(515) == 512);
    CHECK("ATT_MTU 517 -> 512 (attribute value cap)", br_ble_payload_mtu(517) == 512);
    CHECK("ATT_MTU 22 refused", br_ble_payload_mtu(22) == BR_ERR_ARG);
    CHECK("ATT_MTU 518 refused", br_ble_payload_mtu(518) == BR_ERR_ARG);
}

static void test_frag(void)
{
    CHECK("count: 15 bytes fit one 20-byte fragment", br_frag_count(15, 20) == 1);
    CHECK("count: 16 bytes need two", br_frag_count(16, 20) == 2);
    CHECK("count: 33 bytes need two (15 + 18)", br_frag_count(33, 20) == 2);
    CHECK("count: 34 bytes need three", br_frag_count(34, 20) == 3);
    CHECK("count: 98304-byte handshake at ATT_MTU 23 = 5462", br_frag_count(98304, 20) == 5462);
    CHECK("count: mtu 5 refused", br_frag_count(10, 5) == 0);
    CHECK("count: empty refused", br_frag_count(0, 20) == 0);
    CHECK("count: 16 MiB refused", br_frag_count(0x1000000u, 512) == 0);
    br_frag_tx_t tx;
    const uint8_t *m = (const uint8_t *) "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    CHECK("tx init", br_frag_tx_init(&tx, m, 26, 20, 0x85) == BR_OK);
    int32_t n = br_frag_tx_next(&tx, g_frag, sizeof g_frag);
    static const uint8_t f1[] = {0x85, 0x00, 0x00, 0x00, 0x1A, 'A', 'B', 'C', 'D', 'E', 'F', 'G',
                                 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O'};
    CHECK("first fragment: 80|ctr(5, high bit masked), idx 0, len 26 (24-bit BE), 15 data",
          n == 20 && !memcmp(g_frag, f1, 20));
    n = br_frag_tx_next(&tx, g_frag, sizeof g_frag);
    static const uint8_t f2[] = {0x05, 0x01, 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z'};
    CHECK("second fragment: ctr, idx 1, remaining 11 data", n == 13 && !memcmp(g_frag, f2, 13));
    CHECK("then done", br_frag_tx_next(&tx, g_frag, sizeof g_frag) == 0);
    CHECK("tx init refuses NULL message", br_frag_tx_init(&tx, NULL, 5, 20, 0) == BR_ERR_ARG);
    CHECK("tx init refuses mtu 5", br_frag_tx_init(&tx, m, 5, 5, 0) == BR_ERR_ARG);
    br_frag_tx_init(&tx, m, 26, 20, 1);
    CHECK("tx next refuses a buffer too small for a header", br_frag_tx_next(&tx, g_frag, 5) == BR_ERR_SIZE);
    CHECK("round trip 1 byte at mtu 20", roundtrip(1, 20, 0));
    CHECK("round trip 15/16/33/34 bytes at mtu 20", roundtrip(15, 20, 1) && roundtrip(16, 20, 2) &&
                                                       roundtrip(33, 20, 3) && roundtrip(34, 20, 4));
    CHECK("round trip 98304 bytes at ATT_MTU 23 (devmesh handshake size)", roundtrip(98304, 20, 9));
    CHECK("round trip 98304 bytes at ATT_MTU 247", roundtrip(98304, 244, 10));
    CHECK("round trip 131072 bytes at ATT_MTU 517", roundtrip(131072, 512, 11));
    CHECK("round trip at minimum mtu 6", roundtrip(1000, 6, 12));
    CHECK("round trip with index wrapping past 255 (mtu 10, 3000 B)", roundtrip(3000, 10, 127));

    /* receive refusals */
    br_frag_rx_t rx;
    uint32_t got = 0;
    br_frag_tx_init(&tx, m, 26, 10, 3);
    uint8_t a[16], b[16], c[16], d[16];
    int32_t na = br_frag_tx_next(&tx, a, 16), nb = br_frag_tx_next(&tx, b, 16),
            nc = br_frag_tx_next(&tx, c, 16), nd = br_frag_tx_next(&tx, d, 16);
    CHECK("26 bytes at mtu 10: 4 fragments (5+8+8+5)", na == 10 && nb == 10 && nc == 10 && nd == 7);
    br_frag_rx_init(&rx, g_back, sizeof g_back);
    CHECK("later fragment with no first one refused",
          br_frag_rx_push(&rx, b, (uint32_t) nb, &got) == BR_ERR_STATE && rx.drops == 1);
    br_frag_rx_push(&rx, a, (uint32_t) na, &got);
    CHECK("missing fragment (idx 1 skipped) drops the message",
          br_frag_rx_push(&rx, c, (uint32_t) nc, &got) == BR_ERR_STATE && rx.drops == 2 && !rx.active);
    CHECK("the rest of a dropped message is refused too",
          br_frag_rx_push(&rx, d, (uint32_t) nd, &got) == BR_ERR_STATE);
    br_frag_rx_push(&rx, a, (uint32_t) na, &got);
    uint8_t bx[16];
    memcpy(bx, b, 16);
    bx[0] ^= 1;
    CHECK("wrong counter refused", br_frag_rx_push(&rx, bx, (uint32_t) nb, &got) == BR_ERR_STATE);
    br_frag_rx_push(&rx, a, (uint32_t) na, &got);
    br_frag_rx_push(&rx, b, (uint32_t) nb, &got);
    uint32_t drops = rx.drops;
    CHECK("a new first fragment mid-message restarts (counted as a drop)",
          br_frag_rx_push(&rx, a, (uint32_t) na, &got) == 0 && rx.drops == drops + 1);
    CHECK("and the restarted message completes",
          br_frag_rx_push(&rx, b, (uint32_t) nb, &got) == 0 &&
              br_frag_rx_push(&rx, c, (uint32_t) nc, &got) == 0 &&
              br_frag_rx_push(&rx, d, (uint32_t) nd, &got) == 1 && got == 26 && !memcmp(g_back, m, 26));
    uint8_t over[12];
    memcpy(over, d, 7);
    over[7] = 'x';
    over[8] = 'y';
    br_frag_rx_push(&rx, a, (uint32_t) na, &got);
    br_frag_rx_push(&rx, b, (uint32_t) nb, &got);
    br_frag_rx_push(&rx, c, (uint32_t) nc, &got);
    CHECK("data past the announced length refused",
          br_frag_rx_push(&rx, over, 9, &got) == BR_ERR_FORMAT && !rx.active);
    br_frag_rx_t small;
    uint8_t sbuf[20];
    br_frag_rx_init(&small, sbuf, sizeof sbuf);
    CHECK("announced length over the buffer refused at the first fragment",
          br_frag_rx_push(&small, a, (uint32_t) na, &got) == BR_ERR_SIZE && !small.active);
    uint8_t z[6] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x41};
    CHECK("zero announced length refused", br_frag_rx_push(&rx, z, 6, &got) == BR_ERR_FORMAT);
    uint8_t bi[6] = {0x80, 0x01, 0x00, 0x00, 0x01, 0x41};
    CHECK("first fragment with idx != 0 refused", br_frag_rx_push(&rx, bi, 6, &got) == BR_ERR_FORMAT);
    uint8_t ov[8] = {0x80, 0x00, 0x00, 0x00, 0x01, 0x41, 0x42, 0x43};
    CHECK("first fragment carrying more than announced refused",
          br_frag_rx_push(&rx, ov, 8, &got) == BR_ERR_FORMAT);
    CHECK("1-byte fragment refused", br_frag_rx_push(&rx, ov, 1, &got) == BR_ERR_FORMAT);
    uint8_t one[6] = {0x80, 0x00, 0x00, 0x00, 0x01, 0x41};
    CHECK("single-fragment 1-byte message", br_frag_rx_push(&rx, one, 6, &got) == 1 && got == 1 &&
                                                g_back[0] == 0x41);
    uint8_t empty_later[2] = {0x03, 0x01};
    br_frag_rx_push(&rx, a, (uint32_t) na, &got);
    CHECK("later fragment with no data refused",
          br_frag_rx_push(&rx, empty_later, 2, &got) == BR_ERR_FORMAT);
    CHECK("rx init refuses NULL buffer", br_frag_rx_init(&rx, NULL, 10) == BR_ERR_ARG);
}

static void test_lps(void)
{
    uint8_t h[4];
    CHECK("length header big-endian", br_lps_encode_hdr(0x01020304u, h) == 4 && h[0] == 1 &&
                                          h[1] == 2 && h[2] == 3 && h[3] == 4);
    CHECK("length 0 refused", br_lps_encode_hdr(0, h) == BR_ERR_ARG);
    uint8_t buf[64], s[64];
    br_lps_rx_t rx;
    CHECK("init", br_lps_rx_init(&rx, buf, 32) == BR_OK);
    uint32_t sn = 0;
    br_lps_encode_hdr(5, s);
    memcpy(s + 4, "hello", 5);
    br_lps_encode_hdr(3, s + 9);
    memcpy(s + 13, "abc", 3);
    sn = 16;
    uint32_t used = 0, len = 0;
    CHECK("two messages in one read: first", br_lps_rx_push(&rx, s, sn, &used, &len) == 1 && used == 9 &&
                                                 len == 5 && !memcmp(buf, "hello", 5));
    CHECK("then the second", br_lps_rx_push(&rx, s + used, sn - used, &used, &len) == 1 && len == 3 &&
                                 !memcmp(buf, "abc", 3));
    int r = 0, got = 0;
    for (uint32_t i = 0; i < sn; i++) {
        r = br_lps_rx_push(&rx, s + i, 1, &used, &len);
        if (r == 1) got++;
    }
    CHECK("byte-at-a-time delivery gives the same two messages", got == 2 && len == 3);
    CHECK("partial input consumed, no message", br_lps_rx_push(&rx, s, 6, &used, &len) == 0 && used == 6);
    br_lps_rx_reset(&rx);
    uint8_t big[4] = {0, 0, 0, 33};
    CHECK("length over the maximum kills the stream",
          br_lps_rx_push(&rx, big, 4, &used, &len) == BR_ERR_SIZE && rx.dead);
    CHECK("dead stream refuses even good bytes", br_lps_rx_push(&rx, s, 9, &used, &len) == BR_ERR_SIZE);
    br_lps_rx_reset(&rx);
    CHECK("reset revives it", br_lps_rx_push(&rx, s, 9, &used, &len) == 1 && len == 5);
    uint8_t zero[4] = {0, 0, 0, 0};
    CHECK("zero length kills the stream", br_lps_rx_push(&rx, zero, 4, &used, &len) == BR_ERR_SIZE);
    br_lps_rx_reset(&rx);
    uint8_t exact[4 + 32];
    br_lps_encode_hdr(32, exact);
    memset(exact + 4, 'q', 32);
    CHECK("exactly the maximum accepted", br_lps_rx_push(&rx, exact, 36, &used, &len) == 1 && len == 32);
    CHECK("NULL used refused", br_lps_rx_push(&rx, s, 1, NULL, &len) == BR_ERR_ARG);
}

int main(void)
{
    test_mtu();
    test_frag();
    test_lps();
    return check_summary();
}
