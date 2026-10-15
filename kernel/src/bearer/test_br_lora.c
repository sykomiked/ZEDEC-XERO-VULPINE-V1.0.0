/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_lora.c — LoRa time on air (exact integer values against the
 * Semtech formula's published results, and a floating-point reference over
 * the whole parameter space), EU868 duty cycle, US915 dwell time, and the
 * paced fragment link.
 *
 * Published values used:
 *   - SF7/125 kHz, CR 4/5, 8-symbol preamble, explicit header, CRC on:
 *     10 bytes = 41.216 ms, 13 bytes = 46.336 ms, 64 bytes = 118.016 ms;
 *     SF12/125 kHz 10 bytes = 991.232 ms (Semtech LoRa Modem Calculator /
 *     TTN LoRaWAN airtime calculator, both implementing SX1276 datasheet
 *     4.1.1.7).
 *   - LoRaWAN Regional Parameters RP002, US902-928 with 400 ms dwell time:
 *     maximum MACPayload M = 19 / 61 / 133 / 250 bytes for DR0..DR3
 *     (SF10..SF7 at 125 kHz). PHYPayload = M + 5 (MHDR + MIC), so the
 *     largest payload within 400 ms must be 24 / 66 / 138 / 255.
 */
#include <math.h>
#include <string.h>

#include "br_lora.h"
#include "test_check.h"

static br_lora_mod_t mod(uint8_t sf, uint16_t bw)
{
    br_lora_mod_t m = {sf, bw, 1, 8, 0, 1, BR_LORA_LDRO_AUTO};
    return m;
}

static uint32_t air(uint8_t sf, uint16_t bw, uint32_t pl)
{
    br_lora_mod_t m = mod(sf, bw);
    uint32_t us = 0;
    if (br_lora_airtime_us(&m, pl, &us) != BR_OK) return 0;
    return us;
}

/* independent floating-point reference (host test only) */
static double ref_us(const br_lora_mod_t *m, uint32_t pl)
{
    double ts = pow(2.0, m->sf) / (m->bw_khz * 1000.0);
    int de = m->ldro == BR_LORA_LDRO_AUTO ? (ts >= 0.016) : m->ldro;
    double num = 8.0 * pl - 4.0 * m->sf + 28 + 16 * m->crc_on - 20 * m->implicit_header;
    double c = ceil(num / (4.0 * (m->sf - 2 * de)));
    if (c < 0) c = 0;
    double n = 8 + c * (m->cr + 4);
    return ((m->preamble + 4.25) * ts + n * ts) * 1e6;
}

static void test_airtime(void)
{
    CHECK("symbol SF7/125 = 1024 us", br_lora_symbol_us(7, 125) == 1024);
    CHECK("symbol SF12/125 = 32768 us", br_lora_symbol_us(12, 125) == 32768);
    CHECK("symbol SF7/500 = 256 us", br_lora_symbol_us(7, 500) == 256);
    CHECK("symbol SF6/250 = 256 us", br_lora_symbol_us(6, 250) == 256);
    CHECK("symbol SF13 invalid", br_lora_symbol_us(13, 125) == 0);
    CHECK("symbol SF5 invalid", br_lora_symbol_us(5, 125) == 0);
    CHECK("symbol 200 kHz invalid", br_lora_symbol_us(7, 200) == 0);
    CHECK("published: SF7 10 B = 41.216 ms", air(7, 125, 10) == 41216);
    CHECK("published: SF7 13 B = 46.336 ms", air(7, 125, 13) == 46336);
    CHECK("published: SF7 64 B = 118.016 ms", air(7, 125, 64) == 118016);
    CHECK("published: SF12 10 B = 991.232 ms", air(12, 125, 10) == 991232);
    CHECK("SF12 51 B = 2465.792 ms", air(12, 125, 51) == 2465792);
    CHECK("SF7 255 B = 399.616 ms", air(7, 125, 255) == 399616);
    CHECK("SF8/500 10 B = 18.048 ms", air(8, 500, 10) == 18048);
    CHECK("SF12/250 10 B (LDRO auto on) = 495.616 ms", air(12, 250, 10) == 495616);
    CHECK("payload 0 is valid (header only)", air(7, 125, 0) > 0);
    br_lora_mod_t m = mod(11, 125);
    uint32_t a = 0, b = 0;
    br_lora_airtime_us(&m, 50, &a);
    m.ldro = BR_LORA_LDRO_ON;
    br_lora_airtime_us(&m, 50, &b);
    CHECK("LDRO auto is on at SF11/125 (same as forced on)", a == b);
    m.ldro = BR_LORA_LDRO_OFF;
    br_lora_airtime_us(&m, 50, &b);
    CHECK("forcing LDRO off at SF11 changes (shortens) the airtime", b < a);
    m = mod(10, 125);
    br_lora_airtime_us(&m, 50, &a);
    m.ldro = BR_LORA_LDRO_OFF;
    br_lora_airtime_us(&m, 50, &b);
    CHECK("LDRO auto is off at SF10/125", a == b);
    m = mod(11, 250);
    br_lora_airtime_us(&m, 50, &a);
    m.ldro = BR_LORA_LDRO_OFF;
    br_lora_airtime_us(&m, 50, &b);
    CHECK("LDRO auto is off at SF11/250", a == b);
    m = mod(6, 125);
    CHECK("SF6 with explicit header refused", br_lora_airtime_us(&m, 10, &a) == BR_ERR_ARG);
    m.implicit_header = 1;
    CHECK("SF6 implicit header ok", br_lora_airtime_us(&m, 10, &a) == BR_OK);
    m = mod(7, 125);
    CHECK("payload 256 refused", br_lora_airtime_us(&m, 256, &a) == BR_ERR_ARG);
    m.cr = 0;
    CHECK("CR 0 refused", br_lora_airtime_us(&m, 10, &a) == BR_ERR_ARG);
    m.cr = 5;
    CHECK("CR 5 refused", br_lora_airtime_us(&m, 10, &a) == BR_ERR_ARG);
    m.cr = 4;
    m.preamble = 5;
    CHECK("preamble 5 refused", br_lora_airtime_us(&m, 10, &a) == BR_ERR_ARG);
    m.preamble = 8;
    m.ldro = 3;
    CHECK("LDRO mode 3 refused", br_lora_airtime_us(&m, 10, &a) == BR_ERR_ARG);
    CHECK("NULL refused", br_lora_airtime_us(NULL, 10, &a) == BR_ERR_ARG);
    /* full sweep against the float reference */
    int bad = 0, n = 0;
    static const uint16_t bws[3] = {125, 250, 500};
    for (uint8_t sf = 6; sf <= 12; sf++)
        for (int bi = 0; bi < 3; bi++)
            for (uint8_t cr = 1; cr <= 4; cr++)
                for (uint8_t ih = 0; ih <= 1; ih++)
                    for (uint8_t crc = 0; crc <= 1; crc++)
                        for (uint8_t de = 0; de <= 2; de++)
                            for (uint32_t pl = 0; pl <= 255; pl += 3) {
                                br_lora_mod_t q = {sf, bws[bi], cr, 8, ih, crc, de};
                                if (sf == 6 && !ih) continue;
                                uint32_t us;
                                if (br_lora_airtime_us(&q, pl, &us) != BR_OK) {
                                    bad++;
                                    continue;
                                }
                                double r = ref_us(&q, pl);
                                if (fabs((double) us - r) > 1e-6 * r) bad++;
                                n++;
                            }
    printf("  airtime sweep: %d parameter sets\n", n);
    CHECK("integer airtime equals the float formula for every SF/BW/CR/IH/CRC/LDRO/PL",
          bad == 0 && n > 20000);
    /* LoRaWAN US915 dwell-time payload caps */
    m = mod(10, 125);
    CHECK("RP002 US915 DR0 (SF10): max payload within 400 ms = 24 (M 19 + 5)",
          br_lora_max_payload(&m, BR_LORA_US_DWELL_US) == 24);
    m = mod(9, 125);
    CHECK("RP002 US915 DR1 (SF9): 66 (M 61 + 5)",
          br_lora_max_payload(&m, BR_LORA_US_DWELL_US) == 66);
    m = mod(8, 125);
    CHECK("RP002 US915 DR2 (SF8): 138 (M 133 + 5)",
          br_lora_max_payload(&m, BR_LORA_US_DWELL_US) == 138);
    m = mod(7, 125);
    CHECK("RP002 US915 DR3 (SF7): 255 (M 250 + 5)",
          br_lora_max_payload(&m, BR_LORA_US_DWELL_US) == 255);
    m = mod(12, 125);
    CHECK("SF12/125: nothing fits 400 ms", br_lora_max_payload(&m, BR_LORA_US_DWELL_US) == 0);
}

static void test_eu(void)
{
    br_lora_duty_t d;
    CHECK("region NONE refused", br_lora_duty_init(&d, BR_LORA_REGION_NONE) == BR_ERR_ARG);
    CHECK("unconfigured duty state fails closed",
          br_lora_duty_check(&d, 868100000u, 1000, 0) == BR_ERR_DISABLED);
    CHECK("init EU868", br_lora_duty_init(&d, BR_LORA_REGION_EU868) == BR_OK);
    CHECK("868.1 MHz in sub-band 2 (1%)",
          br_lora_eu_band(868100000u) == 2 && br_lora_eu_den(2) == 100);
    CHECK("869.525 MHz in sub-band 4 (10%)",
          br_lora_eu_band(869525000u) == 4 && br_lora_eu_den(4) == 10);
    CHECK("863.5 MHz in sub-band 0 (0.1%)",
          br_lora_eu_band(863500000u) == 0 && br_lora_eu_den(0) == 1000);
    CHECK("866.0 MHz in sub-band 1 (1%)", br_lora_eu_band(866000000u) == 1);
    CHECK("868.9 MHz in sub-band 3 (0.1%)",
          br_lora_eu_band(868900000u) == 3 && br_lora_eu_den(3) == 1000);
    CHECK("869.8 MHz in sub-band 5 (1%)",
          br_lora_eu_band(869800000u) == 5 && br_lora_eu_den(5) == 100);
    CHECK("band edges: 868.0 is band 2, 868.6 is outside",
          br_lora_eu_band(868000000u) == 2 && br_lora_eu_band(868600000u) == -1);
    CHECK("870.0 MHz outside", br_lora_eu_band(870000000u) == -1);
    CHECK("bad band index den 0", br_lora_eu_den(6) == 0 && br_lora_eu_den(-1) == 0);
    CHECK("gap 868.65 MHz refused",
          br_lora_duty_check(&d, 868650000u, 41216, 0) == BR_ERR_POLICY_BAND);
    CHECK("915 MHz refused in EU",
          br_lora_duty_check(&d, 915000000u, 41216, 0) == BR_ERR_POLICY_BAND);
    CHECK("zero airtime refused", br_lora_duty_check(&d, 868100000u, 0, 0) == BR_ERR_ARG);
    CHECK("commit 41.216 ms at t=1000", br_lora_duty_commit(&d, 868100000u, 41216, 1000) == BR_OK);
    CHECK("band closed until 1000 + 42 * 100 = 5200", d.band_free_ms[2] == 5200);
    CHECK("busy at 5199", br_lora_duty_check(&d, 868100000u, 41216, 5199) == BR_ERR_BUSY);
    CHECK("same sub-band other channel (868.3) also busy",
          br_lora_duty_check(&d, 868300000u, 41216, 2000) == BR_ERR_BUSY);
    CHECK("open at 5200", br_lora_duty_check(&d, 868100000u, 41216, 5200) == BR_OK);
    CHECK("other sub-band (869.525, 10%) free meanwhile",
          br_lora_duty_commit(&d, 869525000u, 41216, 2000) == BR_OK && d.band_free_ms[4] == 2420);
    CHECK("0.1% sub-band: 42 ms closes it for 42 s",
          br_lora_duty_commit(&d, 868900000u, 41216, 0) == BR_OK && d.band_free_ms[3] == 42000);
    CHECK("busy commit does not record",
          br_lora_duty_commit(&d, 868900000u, 41216, 41999) == BR_ERR_BUSY &&
              d.band_free_ms[3] == 42000);
    /* one hour, transmitting whenever allowed: <= 36 s of air on a 1% band */
    br_lora_duty_init(&d, BR_LORA_REGION_EU868);
    uint64_t t = 0, total = 0, last = 0, mingap = UINT64_MAX;
    uint32_t a = air(9, 125, 51);
    int sent = 0;
    for (t = 0; t < 3600000u; t++)
        if (br_lora_duty_commit(&d, 868500000u, a, t) == BR_OK) {
            if (sent && t - last < mingap) mingap = t - last;
            last = t;
            total += a;
            sent++;
        }
    printf("  EU868 1%%: %d packets, %llu us on air in one hour\n", sent,
           (unsigned long long) total);
    CHECK("EU868 1%: start-to-start gap is exactly 100 x air time (ms, rounded up)",
          mingap == (uint64_t) ((a + 999u) / 1000u) * 100u);
    CHECK("EU868 1%: at most 36 s plus the last packet started in the hour",
          total <= 36000000u + a);
    CHECK("EU868 1%: and not far below it (> 35 s)", total > 35000000u);
    CHECK("air time total tracked", d.air_total_us == total);
}

static void test_us(void)
{
    br_lora_duty_t d;
    CHECK("init US915", br_lora_duty_init(&d, BR_LORA_REGION_US915) == BR_OK);
    CHECK("901 MHz refused", br_lora_duty_check(&d, 901000000u, 1000, 0) == BR_ERR_POLICY_BAND);
    CHECK("928.1 MHz refused", br_lora_duty_check(&d, 928100000u, 1000, 0) == BR_ERR_POLICY_BAND);
    CHECK("400.001 ms refused (dwell)",
          br_lora_duty_check(&d, 902300000u, 400001, 0) == BR_ERR_POLICY_DWELL);
    CHECK("400.000 ms allowed", br_lora_duty_check(&d, 902300000u, 400000, 0) == BR_OK);
    CHECK("SF10 24 B (370.688 ms) allowed",
          br_lora_duty_commit(&d, 902300000u, air(10, 125, 24), 0) == BR_OK);
    CHECK("same channel again inside 20 s: 370+370 > 400 ms busy",
          br_lora_duty_check(&d, 902300000u, air(10, 125, 24), 1000) == BR_ERR_BUSY);
    CHECK("29.312 ms more fits exactly (370.688 + 29.312 = 400)",
          br_lora_duty_check(&d, 902300000u, 29312, 1000) == BR_OK);
    CHECK("29.313 ms more does not",
          br_lora_duty_check(&d, 902300000u, 29313, 1000) == BR_ERR_BUSY);
    CHECK("another channel is independent",
          br_lora_duty_check(&d, 902500000u, air(10, 125, 24), 1000) == BR_OK);
    CHECK("window passed at t=20000: allowed again",
          br_lora_duty_check(&d, 902300000u, air(10, 125, 24), 20000) == BR_OK);
    CHECK("still busy at t=19999",
          br_lora_duty_check(&d, 902300000u, air(10, 125, 24), 19999) == BR_ERR_BUSY);
    br_lora_duty_init(&d, BR_LORA_REGION_US915);
    int ok = 1;
    for (uint32_t i = 0; i < BR_LORA_HIST; i++)
        if (br_lora_duty_commit(&d, 902300000u + 200000u * i, 50000, 100) != BR_OK) ok = 0;
    CHECK("64 transmissions on 64 channels recorded", ok);
    CHECK("65th inside the window refused: history full, never under-count",
          br_lora_duty_check(&d, 915000000u, 50000, 200) == BR_ERR_BUSY);
    CHECK("after the window the oldest slot is reusable",
          br_lora_duty_commit(&d, 915000000u, 50000, 20100) == BR_OK);
}

/* ===== the link ===== */
typedef struct {
    uint8_t frag[64][BR_LORA_PL_MAX];
    uint32_t len[64], freq[64];
    int n, fail;
} radio_t;

static int radio_tx(void *ctx, uint32_t f, const uint8_t *p, uint32_t n)
{
    radio_t *r = (radio_t *) ctx;
    if (r->fail) return -1;
    if (r->n >= 64) return -1;
    memcpy(r->frag[r->n], p, n);
    r->len[r->n] = n;
    r->freq[r->n] = f;
    r->n++;
    return 0;
}

static br_lora_link_t g_lk;
static radio_t g_radio;

static void test_link(void)
{
    static const uint32_t eu[3] = {868100000u, 868300000u, 868500000u};
    br_lora_mod_t m = mod(7, 125);
    uint8_t msg[600], back[BR_LORA_MAX_MSG];
    for (int i = 0; i < 600; i++) msg[i] = (uint8_t) (i * 7 + 3);
    memset(&g_radio, 0, sizeof g_radio);
    CHECK("link: no region refused",
          br_lora_link_init(&g_lk, 0, &m, eu, 3, radio_tx, &g_radio) == BR_ERR_ARG);
    static const uint32_t badch[1] = {868650000u};
    CHECK("link: channel outside EU sub-bands refused",
          br_lora_link_init(&g_lk, BR_LORA_REGION_EU868, &m, badch, 1, radio_tx, &g_radio) ==
              BR_ERR_POLICY_BAND);
    CHECK("link: EU init",
          br_lora_link_init(&g_lk, BR_LORA_REGION_EU868, &m, eu, 3, radio_tx, &g_radio) == BR_OK &&
              g_lk.mtu == 255);
    CHECK("link: nothing queued, poll does nothing", br_lora_link_poll(&g_lk, 0) == 0);
    CHECK("link: queue 600 bytes", br_lora_link_send(&g_lk, NULL, msg, 600) == BR_OK);
    CHECK("link: second message while busy -> BUSY",
          br_lora_link_send(&g_lk, NULL, msg, 10) == BR_ERR_BUSY);
    CHECK("link: 6145 bytes refused", br_lora_link_send(&g_lk, NULL, back, 6145) == BR_ERR_SIZE);
    CHECK("link: first fragment out at t=0", br_lora_link_poll(&g_lk, 0) == 1 && g_radio.n == 1);
    CHECK("link: first fragment is 255 bytes on 868.1",
          g_radio.len[0] == 255 && g_radio.freq[0] == 868100000u);
    uint32_t a = air(7, 125, 255);
    uint64_t reopen = (uint64_t) ((a + 999) / 1000) * 100u;
    CHECK("link: all three channels share sub-band 2, so it waits",
          br_lora_link_poll(&g_lk, reopen - 1) == 0 && g_radio.n == 1);
    CHECK("link: second fragment when the band reopens",
          br_lora_link_poll(&g_lk, reopen) == 1 && g_radio.n == 2 && g_radio.freq[1] == 868300000u);
    uint64_t t = reopen;
    int guard = 0;
    while (g_lk.busy && guard++ < 1000000) {
        t += 100;
        br_lora_link_poll(&g_lk, t);
    }
    CHECK("link: 3 fragments for 600 bytes",
          g_radio.n == 3 && g_lk.frags_sent == 3 && g_lk.msgs_sent == 1);
    CHECK("link: channels rotate", g_radio.freq[2] == 868500000u);
    br_frag_rx_t rx;
    br_frag_rx_init(&rx, back, sizeof back);
    uint32_t got = 0;
    int r = 0;
    for (int i = 0; i < g_radio.n; i++)
        r = br_frag_rx_push(&rx, g_radio.frag[i], g_radio.len[i], &got);
    CHECK("link: receiver reassembles the exact message",
          r == 1 && got == 600 && !memcmp(back, msg, 600));
    CHECK("link: total air time obeys 1% (wall time >= 100x air time of first two)",
          t >= 2u * reopen);
    /* radio failure */
    CHECK("link: next message accepted", br_lora_link_send(&g_lk, NULL, msg, 300) == BR_OK);
    g_radio.fail = 1;
    CHECK("link: radio failure drops the message",
          br_lora_link_poll(&g_lk, t + 1000000) == BR_ERR_SEND && !g_lk.busy &&
              g_lk.radio_errors == 1);
    g_radio.fail = 0;
    /* US link: fragment size cut to fit dwell */
    static const uint32_t us[2] = {902300000u, 902500000u};
    m = mod(10, 125);
    CHECK("link: US SF10 init, fragment size 24",
          br_lora_link_init(&g_lk, BR_LORA_REGION_US915, &m, us, 2, radio_tx, &g_radio) == BR_OK &&
              g_lk.mtu == 24);
    memset(&g_radio, 0, sizeof g_radio);
    br_lora_link_send(&g_lk, NULL, msg, 100);
    t = 0;
    guard = 0;
    while (g_lk.busy && guard++ < 100000) {
        br_lora_link_poll(&g_lk, t);
        t += 50;
    }
    int ok = !g_lk.busy;
    for (int i = 0; i < g_radio.n; i++) {
        uint32_t aa;
        br_lora_airtime_us(&m, g_radio.len[i], &aa);
        if (aa > 400000u || g_radio.len[i] > 24) ok = 0;
    }
    CHECK("link: US every fragment within 400 ms dwell",
          ok && g_radio.n == (int) br_frag_count(100, 24));
    br_frag_rx_init(&rx, back, sizeof back);
    for (int i = 0; i < g_radio.n; i++)
        r = br_frag_rx_push(&rx, g_radio.frag[i], g_radio.len[i], &got);
    CHECK("link: US message reassembles", r == 1 && got == 100 && !memcmp(back, msg, 100));
    m = mod(12, 125);
    CHECK("link: US SF12/125 refused (nothing fits the dwell time)",
          br_lora_link_init(&g_lk, BR_LORA_REGION_US915, &m, us, 2, radio_tx, &g_radio) ==
              BR_ERR_POLICY_DWELL);
    m = mod(7, 125);
    CHECK("link: 9 channels refused", br_lora_link_init(&g_lk, BR_LORA_REGION_US915, &m, us, 9,
                                                        radio_tx, &g_radio) == BR_ERR_ARG);
}

int main(void)
{
    test_airtime();
    test_eu();
    test_us();
    test_link();
    return check_summary();
}
