/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_dtmf.c — generate each of the 16 tones, detect it; reject noise, a
 * single tone, and silence; RFC 4733 and SIP INFO round-trips; menu driver.
 * Host test (stdio). No float. */
#include <stdio.h>
#include <string.h>
#include "dtmf.h"

static int pass = 0, fail = 0;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
        }                                                                                          \
    } while (0)
#define OK(c) CHECK(c, #c)

/* simple xorshift for noise */
static uint32_t rng = 0x2545F491u;
static int16_t noise_sample(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (int16_t) (rng >> 16);
}

static void test_generate_detect(void)
{
    dtmf_detector d;
    dtmf_det_init(&d);
    int16_t pcm[DTMF_BLOCK];
    for (int i = 0; i < 16; i++) {
        char sym = DTMF_SYMBOLS[i];
        uint32_t pr = 0, pc = 0;
        OK(dtmf_generate(sym, pcm, DTMF_BLOCK, 12000, &pr, &pc));
        char got = dtmf_detect_block(&d, pcm);
        CHECK(got == sym, "generate/detect symbol");
        if (got != sym) printf("    wanted %c got %c\n", sym, got ? got : '0');
    }
}

static void test_reject(void)
{
    dtmf_detector d;
    dtmf_det_init(&d);
    int16_t pcm[DTMF_BLOCK];

    /* silence */
    memset(pcm, 0, sizeof pcm);
    OK(dtmf_detect_block(&d, pcm) == 0);

    /* broadband noise */
    for (int i = 0; i < DTMF_BLOCK; i++) pcm[i] = (int16_t) (noise_sample() / 2);
    OK(dtmf_detect_block(&d, pcm) == 0);

    /* a tone at near-silence amplitude is below the energy floor -> rejected */
    int16_t low[DTMF_BLOCK];
    uint32_t x = 0, y = 0;
    (void) dtmf_generate('9', low, DTMF_BLOCK, 30, &x, &y);
    OK(dtmf_detect_block(&d, low) == 0);

    /* valid tone plus heavy additive noise: the digit should survive moderate
     * noise but the same noise alone (above) does not decode. */
    int16_t mix[DTMF_BLOCK];
    uint32_t p = 0, q = 0;
    (void) dtmf_generate('5', mix, DTMF_BLOCK, 12000, &p, &q);
    for (int i = 0; i < DTMF_BLOCK; i++) {
        int32_t v = mix[i] + noise_sample() / 16;
        mix[i] = (int16_t) (v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
    OK(dtmf_detect_block(&d, mix) == '5');
}

static void test_single_tone(void)
{
    /* A genuine single tone: synthesize only one frequency (941 Hz) with an
     * integer sine, no second tone. The detector must NOT report a digit
     * because the opposite group has no energy (floor) and dominance fails. */
    dtmf_detector d;
    dtmf_det_init(&d);
    int16_t pcm[DTMF_BLOCK];
    /* 941 Hz at 8 kHz: use the library phase increment for index 3 via a tiny
     * reimplementation using the symbol generator is two-tone, so instead we
     * approximate a single tone with a coarse square-ish wave period.
     * Period = 8000/941 ~= 8.5 samples. Build +/- amplitude toggling. */
    int period = 9;
    for (int i = 0; i < DTMF_BLOCK; i++)
        pcm[i] = (int16_t) (((i % period) < period / 2) ? 12000 : -12000);
    /* a square-ish single tone has energy spread; it must not decode to a
     * valid two-tone DTMF digit. */
    char got = dtmf_detect_block(&d, pcm);
    OK(got == 0);
}

static void test_rfc4733(void)
{
    dtmf_rtp_event e = {0};
    e.event = 5;
    e.end = true;
    e.volume = 10;
    e.duration = 1600;
    uint8_t out[4];
    OK(dtmf_rtp_encode(&e, out, sizeof out) == 4);
    OK(out[0] == 5 && (out[1] & 0x80) && (out[1] & 0x3F) == 10);
    dtmf_rtp_event r = {0};
    OK(dtmf_rtp_decode(out, 4, &r));
    OK(r.event == 5 && r.end && r.volume == 10 && r.duration == 1600);
    /* symbol<->event */
    OK(dtmf_symbol_to_event('0') == 0);
    OK(dtmf_symbol_to_event('*') == 10);
    OK(dtmf_symbol_to_event('#') == 11);
    OK(dtmf_symbol_to_event('D') == 15);
    OK(dtmf_symbol_to_event('x') == -1);
    OK(dtmf_event_to_symbol(0) == '0');
    OK(dtmf_event_to_symbol(10) == '*');
    OK(dtmf_event_to_symbol(15) == 'D');
}

static void test_sip_info(void)
{
    uint8_t out[64];
    uint32_t n = dtmf_info_build('7', 160, out, sizeof out);
    OK(n > 0);
    char sym = 0;
    uint32_t dur = 0;
    OK(dtmf_info_parse(out, n, &sym, &dur));
    OK(sym == '7' && dur == 160);
    /* a vendor body with LF-only line endings */
    const char *b = "Signal=*\nDuration=240\n";
    OK(dtmf_info_parse((const uint8_t *) b, (uint32_t) strlen(b), &sym, &dur));
    OK(sym == '*' && dur == 240);
    /* malformed: no Signal */
    const char *b2 = "Duration=100\r\n";
    OK(!dtmf_info_parse((const uint8_t *) b2, (uint32_t) strlen(b2), &sym, &dur));
}

static void test_menu(void)
{
    /* Node 0 (main): 1 -> balance(1), 2 -> pay(2). Node 2 (pay): # confirm -> 3 */
    static const dtmf_menu_node nodes[] = {
        {0, {{'1', 1, false}, {'2', 2, false}}, 2},
        {1, {{0, 0, false}}, 0},
        {2, {{'#', 3, true}}, 1}, /* confirm edge */
        {3, {{0, 0, false}}, 0},
    };
    dtmf_menu m;
    dtmf_menu_init(&m, nodes, 4, 0);
    OK(dtmf_menu_press(&m, '2'));
    OK(m.current == 2 && !m.needs_strong_auth);
    OK(dtmf_menu_press(&m, '#'));
    OK(m.current == 3);
    /* confirm edge flags that DTMF alone is NOT authorization */
    OK(m.needs_strong_auth);
    /* unknown digit is ignored */
    OK(!dtmf_menu_press(&m, '5'));
}

int main(void)
{
    test_generate_detect();
    test_reject();
    test_single_tone();
    test_rfc4733();
    test_sip_info();
    test_menu();
    printf("DTMF: %d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
