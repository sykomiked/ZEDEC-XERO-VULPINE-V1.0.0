/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_bearer.c — capabilities, policy gates, EWMA measurements, link
 * selection (exact scores, class weights, tie-breaks) and failover. */
#include <string.h>

#include "bearer.h"
#include "test_check.h"

static uint8_t g_text[9000];

typedef struct {
    int rc;
    int calls;
} fake_t;

static int fake_send(void *ctx, const uint8_t to[BR_ID_BYTES], const uint8_t *d, uint32_t n)
{
    fake_t *f = (fake_t *) ctx;
    (void) to;
    (void) d;
    (void) n;
    f->calls++;
    return f->rc;
}

static void test_caps(void)
{
    const br_caps_t *c;
    CHECK("caps: unknown kind 0 is NULL", br_caps(0) == NULL);
    CHECK("caps: unknown kind 9 is NULL", br_caps(9) == NULL);
    CHECK("caps: name of unknown kind", !strcmp(br_kind_name(42), "unknown"));
    for (uint8_t k = 1; k < BR_KIND_COUNT; k++) {
        c = br_caps(k);
        CHECK("caps: every kind has a table entry with its own kind", c && c->kind == k);
        CHECK("caps: every kind has a name", strcmp(br_kind_name(k), "unknown") != 0);
        CHECK("caps: mtu <= mtu_max and bit rate > 0",
              c->mtu > 0 && c->mtu <= c->mtu_max && c->bitrate_bps > 0);
    }
    c = br_caps(BR_HAM_SDR);
    CHECK("caps: HAM no encryption, no settlement, text only, emergency only",
          !c->encrypt_ok && !c->settle_ok && c->text_only && c->needs_auth &&
              c->classes == BR_CLS(BR_CLASS_EMERGENCY));
    CHECK("caps: HAM 1200 bit/s, 256-byte AX.25 info field",
          c->bitrate_bps == 1200 && c->mtu == 256);
    c = br_caps(BR_LORA);
    CHECK("caps: LoRa never AI, never chat",
          !(c->classes & BR_CLS(BR_CLASS_AI)) && !(c->classes & BR_CLS(BR_CLASS_CHAT)));
    CHECK("caps: LoRa carries settlement, records, emergency",
          (c->classes & BR_CLS(BR_CLASS_SETTLEMENT)) && (c->classes & BR_CLS(BR_CLASS_RECORD)) &&
              (c->classes & BR_CLS(BR_CLASS_EMERGENCY)));
    CHECK("caps: LoRa 255-byte PHY payload, 6144-byte message cap",
          c->mtu == 255 && c->mtu_max == 255 && c->max_msg == BR_LORA_MAX_MSG && c->needs_auth);
    CHECK("caps: cellular and modem are metered",
          br_caps(BR_CELLULAR)->metered && br_caps(BR_MODEM)->metered);
    CHECK("caps: LAN, BLE, Wi-Fi Direct, serial, LoRa, HAM are not metered",
          !br_caps(BR_LAN)->metered && !br_caps(BR_BLE)->metered &&
              !br_caps(BR_WIFI_DIRECT)->metered && !br_caps(BR_SERIAL)->metered &&
              !br_caps(BR_LORA)->metered && !br_caps(BR_HAM_SDR)->metered);
    CHECK("caps: BLE default payload 20 (ATT_MTU 23 - 3), max 514",
          br_caps(BR_BLE)->mtu == 20 && br_caps(BR_BLE)->mtu_max == 514);
}

static void test_policy(void)
{
    const uint8_t *t = (const uint8_t *) "MAYDAY water needed at the school\r\n\t40 people";
    uint32_t tl = (uint32_t) strlen((const char *) t);
    const uint8_t E = BR_CLASS_EMERGENCY, S = BR_F_SIGNED;
    /* HAM */
    CHECK("policy HAM: signed cleartext emergency passes",
          br_policy_check(BR_HAM_SDR, E, S, t, tl) == BR_OK);
    CHECK("policy HAM: AI refused (class)",
          br_policy_check(BR_HAM_SDR, BR_CLASS_AI, S, t, tl) == BR_ERR_POLICY_CLASS);
    CHECK("policy HAM: settlement refused (class)",
          br_policy_check(BR_HAM_SDR, BR_CLASS_SETTLEMENT, S, t, tl) == BR_ERR_POLICY_CLASS);
    CHECK("policy HAM: chat refused (class)",
          br_policy_check(BR_HAM_SDR, BR_CLASS_CHAT, S, t, tl) == BR_ERR_POLICY_CLASS);
    CHECK("policy HAM: record refused (class)",
          br_policy_check(BR_HAM_SDR, BR_CLASS_RECORD, S, t, tl) == BR_ERR_POLICY_CLASS);
    CHECK("policy HAM: encrypted refused",
          br_policy_check(BR_HAM_SDR, E, S | BR_F_ENCRYPTED, t, tl) == BR_ERR_POLICY_CRYPTO);
    CHECK("policy HAM: AEAD session refused even when signed",
          br_policy_check(BR_HAM_SDR, E, S | BR_F_AEAD, t, tl) == BR_ERR_POLICY_CRYPTO);
    CHECK("policy HAM: AEAD alone refused as crypto",
          br_policy_check(BR_HAM_SDR, E, BR_F_AEAD, t, tl) == BR_ERR_POLICY_CRYPTO);
    CHECK("policy HAM: payment flag refused",
          br_policy_check(BR_HAM_SDR, E, S | BR_F_PAYMENT, t, tl) == BR_ERR_POLICY_SETTLE);
    CHECK("policy HAM: unsigned refused",
          br_policy_check(BR_HAM_SDR, E, 0, t, tl) == BR_ERR_POLICY_UNSIGNED);
    uint8_t bin[8] = {'O', 'K', 0x00, 'x', 'y', 'z', '1', '2'};
    CHECK("policy HAM: NUL byte refused as not text",
          br_policy_check(BR_HAM_SDR, E, S, bin, 8) == BR_ERR_POLICY_TEXT);
    bin[2] = 0x80;
    CHECK("policy HAM: high-bit byte refused as not text",
          br_policy_check(BR_HAM_SDR, E, S, bin, 8) == BR_ERR_POLICY_TEXT);
    bin[2] = 0x7F;
    CHECK("policy HAM: DEL refused as not text",
          br_policy_check(BR_HAM_SDR, E, S, bin, 8) == BR_ERR_POLICY_TEXT);
    bin[2] = 0x1B;
    CHECK("policy HAM: ESC refused as not text",
          br_policy_check(BR_HAM_SDR, E, S, bin, 8) == BR_ERR_POLICY_TEXT);
    bin[2] = '~';
    CHECK("policy HAM: '~' (0x7E) accepted", br_policy_check(BR_HAM_SDR, E, S, bin, 8) == BR_OK);
    bin[2] = ' ';
    CHECK("policy HAM: ' ' (0x20) accepted", br_policy_check(BR_HAM_SDR, E, S, bin, 8) == BR_OK);
    memset(g_text, 'A', sizeof g_text);
    CHECK("policy HAM: 8192 bytes accepted",
          br_policy_check(BR_HAM_SDR, E, S, g_text, BR_HAM_MAX_MSG) == BR_OK);
    CHECK("policy HAM: 8193 bytes refused",
          br_policy_check(BR_HAM_SDR, E, S, g_text, BR_HAM_MAX_MSG + 1) == BR_ERR_SIZE);
    /* LoRa */
    CHECK("policy LoRa: AI refused even when tiny and signed",
          br_policy_check(BR_LORA, BR_CLASS_AI, S, g_text, 10) == BR_ERR_POLICY_CLASS);
    CHECK("policy LoRa: chat refused",
          br_policy_check(BR_LORA, BR_CLASS_CHAT, S, g_text, 10) == BR_ERR_POLICY_CLASS);
    CHECK("policy LoRa: signed settlement record passes",
          br_policy_check(BR_LORA, BR_CLASS_SETTLEMENT, S | BR_F_PAYMENT, g_text, 200) == BR_OK);
    CHECK("policy LoRa: AEAD settlement record passes",
          br_policy_check(BR_LORA, BR_CLASS_SETTLEMENT, BR_F_AEAD | BR_F_ENCRYPTED, g_text, 200) ==
              BR_OK);
    CHECK("policy LoRa: unauthenticated settlement refused",
          br_policy_check(BR_LORA, BR_CLASS_SETTLEMENT, 0, g_text, 200) == BR_ERR_POLICY_UNSIGNED);
    CHECK("policy LoRa: encrypted-only record refused (needs auth)",
          br_policy_check(BR_LORA, BR_CLASS_RECORD, BR_F_ENCRYPTED, g_text, 200) ==
              BR_ERR_POLICY_UNSIGNED);
    CHECK("policy LoRa: 6144-byte record accepted",
          br_policy_check(BR_LORA, BR_CLASS_RECORD, S, g_text, BR_LORA_MAX_MSG) == BR_OK);
    CHECK("policy LoRa: 6145-byte record refused",
          br_policy_check(BR_LORA, BR_CLASS_RECORD, S, g_text, BR_LORA_MAX_MSG + 1) == BR_ERR_SIZE);
    CHECK("policy LoRa: binary allowed (not text-only)",
          br_policy_check(BR_LORA, BR_CLASS_RECORD, S, bin, 8) == BR_OK);
    CHECK("policy LoRa: encrypted signed emergency allowed (ISM band)",
          br_policy_check(BR_LORA, E, S | BR_F_ENCRYPTED, g_text, 100) == BR_OK);
    /* general */
    CHECK("policy LAN: unsigned AI passes",
          br_policy_check(BR_LAN, BR_CLASS_AI, 0, g_text, 1000) == BR_OK);
    CHECK("policy cellular: settlement with payment passes",
          br_policy_check(BR_CELLULAR, BR_CLASS_SETTLEMENT, BR_F_PAYMENT, g_text, 1000) == BR_OK);
    CHECK("policy: unknown flag bit refused",
          br_policy_check(BR_LAN, BR_CLASS_AI, 0x10, g_text, 10) == BR_ERR_ARG);
    CHECK("policy: class 0 refused", br_policy_check(BR_LAN, 0, 0, g_text, 10) == BR_ERR_ARG);
    CHECK("policy: class 6 refused", br_policy_check(BR_LAN, 6, 0, g_text, 10) == BR_ERR_ARG);
    CHECK("policy: kind 0 refused", br_policy_check(0, BR_CLASS_AI, 0, g_text, 10) == BR_ERR_ARG);
    CHECK("policy: kind 9 refused", br_policy_check(9, BR_CLASS_AI, 0, g_text, 10) == BR_ERR_ARG);
    CHECK("policy: empty message refused",
          br_policy_check(BR_LAN, BR_CLASS_AI, 0, g_text, 0) == BR_ERR_ARG);
    CHECK("policy: NULL data refused",
          br_policy_check(BR_LAN, BR_CLASS_AI, 0, NULL, 5) == BR_ERR_ARG);
    CHECK("policy: LAN 1 MiB accepted, 1 MiB + 1 refused",
          br_policy_check(BR_LAN, BR_CLASS_AI, 0, g_text, 1u << 20) == BR_OK &&
              br_policy_check(BR_LAN, BR_CLASS_AI, 0, g_text, (1u << 20) + 1) == BR_ERR_SIZE);
}

static void test_math(void)
{
    CHECK("udiv64: 2^63 / 3", br_udiv64((uint64_t) 1 << 63, 3) == 3074457345618258602ull);
    CHECK("udiv64: max / max", br_udiv64(UINT64_MAX, UINT64_MAX) == 1);
    CHECK("udiv64: max / 1", br_udiv64(UINT64_MAX, 1) == UINT64_MAX);
    CHECK("udiv64: by zero is UINT64_MAX", br_udiv64(5, 0) == UINT64_MAX);
    CHECK("udiv64: small", br_udiv64(100, 7) == 14);
    CHECK("udiv64: n < d", br_udiv64(6, 7) == 0);
    CHECK("udiv64: 64-bit divisor", br_udiv64(0xFFFFFFFFFFFFull, 0x100000001ull) == 65535ull);
    uint32_t seed = 12345;
    int ok = 1;
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1103515245u + 12345u;
        uint64_t n = ((uint64_t) seed << 32) ^ (seed * 2654435761u);
        seed = seed * 1103515245u + 12345u;
        uint64_t d = (i & 1) ? (seed | 1u) : (((uint64_t) seed << (i % 31)) | 1u);
        if (br_udiv64(n, d) != n / d) ok = 0;
    }
    CHECK("udiv64: 20000 random quotients match the C operator", ok);
    CHECK("printable: tab CR LF ok", br_printable((const uint8_t *) "\t\r\n", 3));
    CHECK("printable: 0x1F refused", !br_printable((const uint8_t *) "\x1f", 1));
}

static void test_ewma(void)
{
    br_table_t t;
    fake_t f = {0, 0};
    br_link_ops_t ops = {&f, fake_send};
    br_table_init(&t);
    CHECK("link add LAN", br_link_add(&t, BR_LAN, 1, &ops) == BR_OK);
    br_link_t *l = br_link_get(&t, 1);
    CHECK("prior RTT is the table base before samples", br_link_rtt_us(l) == 2000);
    CHECK("prior loss is 0", br_link_loss_ppm(l) == 0);
    br_link_measure(&t, 1, 0, true);
    CHECK("first sample lost: loss 1e6 ppm", br_link_loss_ppm(l) == 1000000);
    CHECK("lost sample leaves RTT at the prior", br_link_rtt_us(l) == 2000 && l->rtt_n == 0);
    br_link_measure(&t, 1, 1000, false);
    CHECK("loss EWMA 1/16: 937500 ppm", br_link_loss_ppm(l) == 937500);
    CHECK("first RTT sample replaces the prior: 1000", br_link_rtt_us(l) == 1000);
    br_link_measure(&t, 1, 2000, false);
    CHECK("RTT EWMA 1/8: 1125", br_link_rtt_us(l) == 1125);
    CHECK("loss EWMA: 878906 ppm", br_link_loss_ppm(l) == 878906);
    br_link_measure(&t, 1, 0, false);
    CHECK("RTT EWMA toward 0: 984", br_link_rtt_us(l) == 984);
    for (int i = 0; i < 400; i++) br_link_measure(&t, 1, 5000, false);
    CHECK("RTT converges to 5000 (within 8)",
          br_link_rtt_us(l) >= 4992 && br_link_rtt_us(l) <= 5000);
    CHECK("loss converges to near 0 (< 16 ppm)", br_link_loss_ppm(l) < 16);
    CHECK("measure unknown link refused", br_link_measure(&t, 99, 1, false) == BR_ERR_ARG);
    CHECK("sample counters", l->rtt_n == 403 && l->loss_n == 404);
}

static void test_select(void)
{
    br_table_t t;
    fake_t lan = {0, 0}, cell = {0, 0}, lora = {0, 0}, ham = {0, 0};
    br_link_ops_t o1 = {&lan, fake_send}, o2 = {&cell, fake_send}, o3 = {&lora, fake_send},
                  o4 = {&ham, fake_send};
    br_table_init(&t);
    br_link_add(&t, BR_LAN, 1, &o1);
    br_link_add(&t, BR_CELLULAR, 2, &o2);
    br_link_add(&t, BR_LORA, 3, &o3);
    br_link_add(&t, BR_HAM_SDR, 4, &o4);
    CHECK("score LAN AI 1000 B = 2 (2080 us, no cost)",
          br_score(br_link_get(&t, 1), BR_CLASS_AI, 1000) == 2);
    CHECK("score cellular AI 1000 B = 60 + 100*100",
          br_score(br_link_get(&t, 2), BR_CLASS_AI, 1000) == 10060);
    CHECK("score cellular settlement 200 B = 60 + 100",
          br_score(br_link_get(&t, 2), BR_CLASS_SETTLEMENT, 200) == 160);
    CHECK("score LoRa settlement 200 B = 2292 + 50",
          br_score(br_link_get(&t, 3), BR_CLASS_SETTLEMENT, 200) == 2342);
    CHECK("score bad class is UINT64_MAX", br_score(br_link_get(&t, 1), 0, 10) == UINT64_MAX);
    uint16_t id = 0;
    memset(g_text, 'x', 2000);
    CHECK("AI picks LAN", br_select(&t, BR_CLASS_AI, 0, g_text, 1000, &id) == BR_OK && id == 1);
    CHECK("settlement picks LAN",
          br_select(&t, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 200, &id) == BR_OK && id == 1);
    br_link_set_up(&t, 1, false);
    CHECK("LAN down: settlement picks cellular over LoRa",
          br_select(&t, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 200, &id) == BR_OK && id == 2);
    br_link_set_up(&t, 2, false);
    CHECK("cellular down: settlement goes to LoRa",
          br_select(&t, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 200, &id) == BR_OK && id == 3);
    CHECK("only LoRa and HAM up: AI has no link",
          br_select(&t, BR_CLASS_AI, BR_F_SIGNED, g_text, 100, &id) == BR_ERR_NOLINK);
    CHECK("only LoRa and HAM up: unsigned settlement has no link",
          br_select(&t, BR_CLASS_SETTLEMENT, 0, g_text, 100, &id) == BR_ERR_NOLINK);
    memcpy(g_text, "HELP needed", 11);
    CHECK("signed text emergency: LoRa (2146) beats HAM (5666)",
          br_select(&t, BR_CLASS_EMERGENCY, BR_F_SIGNED, g_text, 100, &id) == BR_OK && id == 3);
    br_link_set_up(&t, 3, false);
    CHECK("LoRa down: signed text emergency goes to HAM",
          br_select(&t, BR_CLASS_EMERGENCY, BR_F_SIGNED, g_text, 100, &id) == BR_OK && id == 4);
    CHECK("LoRa down: encrypted emergency has no link (HAM refuses)",
          br_select(&t, BR_CLASS_EMERGENCY, BR_F_SIGNED | BR_F_ENCRYPTED, g_text, 100, &id) ==
              BR_ERR_NOLINK);
    CHECK("select NULL out refused", br_select(&t, BR_CLASS_AI, 0, g_text, 10, NULL) == BR_ERR_ARG);
    CHECK("set_up unknown link refused", br_link_set_up(&t, 77, true) == BR_ERR_ARG);

    /* class weights: metered cost vs latency */
    br_table_init(&t);
    br_link_add(&t, BR_LAN, 1, &o1);
    br_link_add(&t, BR_CELLULAR, 2, &o2);
    br_link_measure(&t, 1, 50000, false);
    br_link_measure(&t, 2, 1000, false);
    CHECK("AI: slow LAN (50 ms) beats fast metered cellular",
          br_select(&t, BR_CLASS_AI, 0, g_text, 1000, &id) == BR_OK && id == 1);
    CHECK("chat: slow LAN still beats metered cellular",
          br_select(&t, BR_CLASS_CHAT, 0, g_text, 1000, &id) == BR_OK && id == 1);
    CHECK("emergency ignores cost: fast cellular wins",
          br_select(&t, BR_CLASS_EMERGENCY, 0, g_text, 1000, &id) == BR_OK && id == 2);
    CHECK("tariff tuned to 1/KiB: AI cost weight 100 still outweighs 49 ms",
          br_link_tune(&t, 2, 0, 0, 1) == BR_OK &&
              br_score(br_link_get(&t, 2), BR_CLASS_AI, 1000) == 101 &&
              br_select(&t, BR_CLASS_AI, 0, g_text, 1000, &id) == BR_OK && id == 1);

    /* loss */
    br_table_init(&t);
    br_link_add(&t, BR_LAN, 1, &o1);
    br_link_add(&t, BR_LAN, 2, &o2);
    br_link_measure(&t, 1, 1000, false);
    for (int i = 0; i < 200; i++) br_link_measure(&t, 1, 1000, (i & 1) != 0);
    br_link_measure(&t, 2, 1500, false);
    CHECK("lossy link has ~50% loss", br_link_loss_ppm(br_link_get(&t, 1)) > 400000 &&
                                          br_link_loss_ppm(br_link_get(&t, 1)) < 600000);
    CHECK("settlement prefers the clean, slower link",
          br_select(&t, BR_CLASS_SETTLEMENT, 0, g_text, 200, &id) == BR_OK && id == 2);

    /* tie-breaks */
    br_table_init(&t);
    br_link_add(&t, BR_LAN, 7, &o1);
    br_link_add(&t, BR_LAN, 5, &o2);
    CHECK("tie between two LAN links: lower id wins",
          br_select(&t, BR_CLASS_AI, 0, g_text, 100, &id) == BR_OK && id == 5);
    br_table_init(&t);
    br_link_add(&t, BR_WIFI_DIRECT, 1, &o1);
    br_link_add(&t, BR_LAN, 2, &o2);
    br_link_tune(&t, 1, 0, 100000000u, 0);
    br_link_measure(&t, 1, 3000, false);
    br_link_measure(&t, 2, 3000, false);
    CHECK("tie LAN vs Wi-Fi Direct: LAN by kind order even with higher id",
          br_select(&t, BR_CLASS_AI, 0, g_text, 100, &id) == BR_OK && id == 2);
    br_table_init(&t);
    br_link_add(&t, BR_BLE, 1, &o1);
    br_link_add(&t, BR_SERIAL, 2, &o2);
    br_link_tune(&t, 1, 0, 115200, 0);
    br_link_measure(&t, 1, 3000, false);
    br_link_measure(&t, 2, 3000, false);
    CHECK("tie serial vs BLE: serial by kind order",
          br_select(&t, BR_CLASS_CHAT, 0, g_text, 100, &id) == BR_OK && id == 2);

    /* tune and table limits */
    CHECK("tune BLE mtu to 244 ok",
          br_link_tune(&t, 1, 244, 0, 0) == BR_OK && br_link_get(&t, 1)->caps.mtu == 244);
    CHECK("tune BLE mtu 515 refused", br_link_tune(&t, 1, 515, 0, 0) == BR_ERR_SIZE);
    CHECK("tune unknown link refused", br_link_tune(&t, 9, 10, 0, 0) == BR_ERR_ARG);
    CHECK("duplicate id refused", br_link_add(&t, BR_LAN, 2, &o1) == BR_ERR_ARG);
    CHECK("unknown kind refused", br_link_add(&t, 12, 3, &o1) == BR_ERR_ARG);
    br_link_ops_t bad = {NULL, NULL};
    CHECK("NULL send refused", br_link_add(&t, BR_LAN, 3, &bad) == BR_ERR_ARG);
    br_table_init(&t);
    for (uint16_t i = 0; i < BR_MAX_LINKS; i++) br_link_add(&t, BR_LAN, i, &o1);
    CHECK("table full at 16", br_link_add(&t, BR_LAN, 100, &o1) == BR_ERR_FULL);
    br_table_init(&t);
    br_link_add(&t, BR_HAM_SDR, 1, &o1);
    br_link_tune(&t, 1, 100, 9600, 5);
    const br_link_t *hl = br_link_get(&t, 1);
    CHECK("tuning a HAM link cannot widen its gates",
          hl->caps.encrypt_ok == 0 && hl->caps.settle_ok == 0 && hl->caps.text_only == 1 &&
              hl->caps.classes == BR_CLS(BR_CLASS_EMERGENCY));
    CHECK("gate still applies after tuning",
          br_select(&t, BR_CLASS_AI, BR_F_SIGNED, g_text, 10, &id) == BR_ERR_NOLINK);
}

static void test_failover(void)
{
    br_table_t t;
    fake_t lan = {BR_ERR_SEND, 0}, cell = {0, 0}, lora = {BR_ERR_BUSY, 0};
    br_link_ops_t o1 = {&lan, fake_send}, o2 = {&cell, fake_send}, o3 = {&lora, fake_send};
    br_table_init(&t);
    br_link_add(&t, BR_LAN, 1, &o1);
    br_link_add(&t, BR_CELLULAR, 2, &o2);
    br_link_add(&t, BR_LORA, 3, &o3);
    uint16_t used = 0;
    memset(g_text, 7, 300);
    CHECK("send fails on LAN, fails over to cellular",
          br_send(&t, NULL, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 300, &used) == BR_OK &&
              used == 2);
    CHECK("LAN marked down after its error",
          !br_link_get(&t, 1)->up && br_link_get(&t, 1)->fails == 1);
    CHECK("one failover counted", t.failovers == 1);
    CHECK("each link tried once", lan.calls == 1 && cell.calls == 1 && lora.calls == 0);
    CHECK("next send goes straight to cellular",
          br_send(&t, NULL, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 300, &used) == BR_OK &&
              used == 2 && lan.calls == 1 && cell.calls == 2);
    cell.rc = -1;
    CHECK("cellular fails, LoRa busy: BUSY returned",
          br_send(&t, NULL, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 300, &used) == BR_ERR_BUSY);
    CHECK("busy LoRa stays up, failed cellular is down",
          br_link_get(&t, 3)->up && !br_link_get(&t, 2)->up && t.failovers == 2);
    lora.rc = BR_OK;
    CHECK("LoRa free again: settlement goes out on LoRa",
          br_send(&t, NULL, BR_CLASS_SETTLEMENT, BR_F_SIGNED, g_text, 300, &used) == BR_OK &&
              used == 3);
    CHECK("AI with only LoRa up: no link, LoRa never called",
          br_send(&t, NULL, BR_CLASS_AI, 0, g_text, 300, &used) == BR_ERR_NOLINK &&
              lora.calls == 2);
    lora.rc = BR_ERR_FORMAT;
    CHECK("all links failed: NOLINK",
          br_send(&t, NULL, BR_CLASS_RECORD, BR_F_SIGNED, g_text, 30, &used) == BR_ERR_NOLINK);
    CHECK("host brings LAN back: used again",
          br_link_set_up(&t, 1, true) == BR_OK && (lan.rc = 0, 1) &&
              br_send(&t, NULL, BR_CLASS_AI, 0, g_text, 30, &used) == BR_OK && used == 1);
    CHECK("send NULL table refused", br_send(NULL, NULL, 1, 0, g_text, 1, NULL) == BR_ERR_ARG);
}

int main(void)
{
    test_caps();
    test_policy();
    test_math();
    test_ewma();
    test_select();
    test_failover();
    return check_summary();
}
