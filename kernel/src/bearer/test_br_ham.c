/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_br_ham.c — signed cleartext emergency messages over AX.25, with real
 * post-quantum signatures from kernel/src/pqsec (pq_matrix at
 * PQM_LEVEL_STANDARD = ML-DSA-65, and HIGH = ML-DSA-87). Every gate of
 * br_ham.h H1..H5 is exercised, plus base64 against RFC 4648 section 10. */
#include <string.h>

#include "br_ham.h"
#include "pq_matrix.h"
#include "test_check.h"

static uint8_t *find(const uint8_t *h, uint32_t hn, const char *needle, uint32_t nn)
{
    for (uint32_t i = 0; i + nn <= hn; i++)
        if (!memcmp(h + i, needle, nn)) return (uint8_t *) (uintptr_t) (h + i);
    return NULL;
}

#define SIG_CTX     "zxv-ham-emergency"
#define SIG_CTX_LEN 17u

typedef struct {
    br_ax25_addr_t who;
    pqm_sig_pk_t pk;
} keyent_t;

static keyent_t g_keys[2];
static pqm_sig_sk_t g_sk[2];
static int g_verifies;

static int verify(void *ctx, const char *call, const uint8_t *msg, uint32_t len, const uint8_t *sig,
                  uint32_t sl)
{
    (void) ctx;
    g_verifies++;
    for (int i = 0; i < 2; i++) {
        char c[10];
        br_ax25_addr_format(&g_keys[i].who, c);
        if (strcmp(c, call)) continue;
        static pqm_sig_t s;
        if (!pqm_sig_decode(&s, sig, sl)) return 0;
        return pqm_verify(&g_keys[i].pk, msg, len, (const uint8_t *) SIG_CTX, SIG_CTX_LEN, &s) ? 1
                                                                                               : 0;
    }
    return 0;
}

static uint8_t g_sig[8192];
static uint32_t g_sl;

static void sign(int who, const br_ax25_addr_t *as, const char *text)
{
    uint8_t m[BR_HAM_SIGNED_MAX];
    int32_t n =
        br_ham_signed_bytes(as, (const uint8_t *) text, (uint32_t) strlen(text), m, sizeof m);
    static pqm_sig_t s;
    pqm_sign(&g_sk[who], m, (size_t) n, (const uint8_t *) SIG_CTX, SIG_CTX_LEN, NULL, &s);
    g_sl = (uint32_t) pqm_sig_encode(&s, g_sig, sizeof g_sig);
}

static br_ham_tx_t g_tx;
static br_ham_rx_t g_rx;
static uint8_t g_frames[BR_HAM_MAX_FRAMES][BR_AX25_FRAME_MAX];
static uint32_t g_flen[BR_HAM_MAX_FRAMES];

static int build_all(void)
{
    int n = 0;
    int32_t r;
    while ((r = br_ham_tx_next(&g_tx, g_frames[n], BR_AX25_FRAME_MAX)) > 0)
        g_flen[n++] = (uint32_t) r;
    return r == 0 ? n : -1;
}

static void test_b64(void)
{
    static const char *const in[7] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    static const char *const out[7] = {"",         "Zg==",     "Zm8=",    "Zm9v",
                                       "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    char o[16];
    uint8_t d[16];
    int ok = 1;
    for (int i = 0; i < 7; i++) {
        int32_t n = br_b64_encode((const uint8_t *) in[i], (uint32_t) strlen(in[i]), o, sizeof o);
        if (n != (int32_t) strlen(out[i]) || memcmp(o, out[i], (size_t) n)) ok = 0;
        if (i && (br_b64_decode(out[i], (uint32_t) n, d, sizeof d) != (int32_t) strlen(in[i]) ||
                  memcmp(d, in[i], strlen(in[i]))))
            ok = 0;
    }
    CHECK("base64: all RFC 4648 section 10 vectors both ways", ok);
    CHECK("base64: padding in the middle refused",
          br_b64_decode("Zg==Zm8=", 8, d, 16) == BR_ERR_FORMAT);
    CHECK("base64: bad length refused", br_b64_decode("Zm9", 3, d, 16) == BR_ERR_FORMAT);
    CHECK("base64: bad character refused", br_b64_decode("Zm9-", 4, d, 16) == BR_ERR_FORMAT);
    CHECK("base64: non-canonical trailing bits refused",
          br_b64_decode("Zh==", 4, d, 16) == BR_ERR_FORMAT);
    CHECK("base64: '=' in position 1 refused", br_b64_decode("Z===", 4, d, 16) == BR_ERR_FORMAT);
    CHECK("base64: output too small refused", br_b64_decode("Zm9vYmFy", 8, d, 5) == BR_ERR_SIZE);
}

static void test_ham(void)
{
    uint8_t seed[32];
    for (int i = 0; i < 2; i++) {
        for (int k = 0; k < 32; k++) seed[k] = (uint8_t) (k * 3 + i * 101);
        pqm_sig_keygen(PQM_LEVEL_STANDARD, seed, &g_keys[i].pk, &g_sk[i]);
    }
    br_ax25_addr_parse("KC1ABC-9", &g_keys[0].who);
    br_ax25_addr_parse("G4XYZ", &g_keys[1].who);
    br_ham_cfg_t cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.src = g_keys[0].who;
    br_ax25_addr_parse("EMERG", &cfg.dst);
    cfg.verify = verify;
    const char *text = "EMERGENCY: bridge on Route 9 is out. 12 people at the fire station need "
                       "water and insulin. Contact on 146.520 MHz.";
    uint32_t tl = (uint32_t) strlen(text);
    sign(0, &cfg.src, text);
    printf("  ML-DSA-65 signature (pq_matrix encoding): %u bytes\n", g_sl);
    const uint8_t E = BR_CLASS_EMERGENCY, S = BR_F_SIGNED;
    const uint8_t *t = (const uint8_t *) text;

    CHECK("H1: off by default (no licensed operator)",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, t, tl, g_sig, g_sl) == BR_ERR_DISABLED);
    cfg.licensed_operator = 1;
    br_ham_cfg_t c2 = cfg;
    br_ax25_addr_parse("CQ", &c2.src);
    CHECK("H1: source must look like an amateur call sign",
          br_ham_tx_begin(&g_tx, &c2, 1, E, S, t, tl, g_sig, g_sl) == BR_ERR_ARG);
    CHECK("H2: AI class refused", br_ham_tx_begin(&g_tx, &cfg, 1, BR_CLASS_AI, S, t, tl, g_sig,
                                                  g_sl) == BR_ERR_POLICY_CLASS);
    CHECK("H2: settlement class refused", br_ham_tx_begin(&g_tx, &cfg, 1, BR_CLASS_SETTLEMENT, S, t,
                                                          tl, g_sig, g_sl) == BR_ERR_POLICY_CLASS);
    CHECK("H2: payment flag refused", br_ham_tx_begin(&g_tx, &cfg, 1, E, S | BR_F_PAYMENT, t, tl,
                                                      g_sig, g_sl) == BR_ERR_POLICY_SETTLE);
    CHECK("H2: encrypted flag refused", br_ham_tx_begin(&g_tx, &cfg, 1, E, S | BR_F_ENCRYPTED, t,
                                                        tl, g_sig, g_sl) == BR_ERR_POLICY_CRYPTO);
    CHECK("H2: unsigned refused",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, 0, t, tl, g_sig, g_sl) == BR_ERR_POLICY_UNSIGNED);
    CHECK("H2: binary (ciphertext-like) text refused",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, g_sig, 64, g_sig, g_sl) == BR_ERR_POLICY_TEXT);
    CHECK("H4: empty signature refused",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, t, tl, g_sig, 0) == BR_ERR_SIZE);
    CHECK("H4: oversize signature refused",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, t, tl, g_sig, BR_HAM_SIG_MAX + 1) == BR_ERR_SIZE);
    static uint8_t longt[BR_HAM_TEXT_MAX + 1];
    memset(longt, 'A', sizeof longt);
    CHECK("H4: 1025-byte text refused",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, longt, BR_HAM_TEXT_MAX + 1, g_sig, g_sl) ==
              BR_ERR_SIZE);
    g_sig[100] ^= 1;
    CHECK("H3: tampered signature refused before transmitting",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, t, tl, g_sig, g_sl) == BR_ERR_AUTH);
    g_sig[100] ^= 1;
    c2 = cfg;
    c2.verify = NULL;
    CHECK("H3: no verifier refused",
          br_ham_tx_begin(&g_tx, &c2, 1, E, S, t, tl, g_sig, g_sl) == BR_ERR_AUTH);
    c2 = cfg;
    c2.src = g_keys[1].who;
    CHECK("H3: KC1ABC-9's signature does not verify under G4XYZ",
          br_ham_tx_begin(&g_tx, &c2, 1, E, S, t, tl, g_sig, g_sl) == BR_ERR_AUTH);
    c2.src.ssid = 0;
    br_ax25_addr_parse("KC1ABC", &c2.src);
    CHECK("H3: nor under the same call with another SSID",
          br_ham_tx_begin(&g_tx, &c2, 1, E, S, t, tl, g_sig, g_sl) == BR_ERR_AUTH);
    CHECK("H3: different text, same signature refused",
          br_ham_tx_begin(&g_tx, &cfg, 1, E, S, t, tl - 1, g_sig, g_sl) == BR_ERR_AUTH);

    CHECK("good message accepted",
          br_ham_tx_begin(&g_tx, &cfg, 0xBEEF, E, S, t, tl, g_sig, g_sl) == BR_OK);
    int nf = build_all();
    uint32_t body = tl + 8 + (g_sl + 2) / 3 * 4;
    CHECK("frame count = ceil(body / 200)", nf == (int) ((body + 199) / 200));
    printf("  %u-byte text + signature -> %d AX.25 frames\n", tl, nf);
    br_ax25_ui_t ui;
    int ok = 1;
    for (int i = 0; i < nf; i++) {
        if (br_ax25_ui_decode(g_frames[i], g_flen[i], &ui) != BR_OK) ok = 0;
        if (!br_printable(ui.info, ui.info_len) || ui.info_len > BR_AX25_INFO_MAX) ok = 0;
        if (strcmp(ui.src.call, "KC1ABC") || ui.src.ssid != 9 || strcmp(ui.dst.call, "EMERG"))
            ok = 0;
    }
    CHECK("every frame: valid UI, source KC1ABC-9, printable info <= 256", ok);
    br_ax25_ui_decode(g_frames[0], g_flen[0], &ui);
    CHECK("first frame starts \"ZXV1 BEEF 1/n \" then the plain text",
          !memcmp(ui.info, "ZXV1 BEEF 1/", 12) && !memcmp(ui.info + 15, "EMERGENCY: bridge", 17));
    br_ax25_ui_decode(g_frames[0], g_flen[0], &ui);
    CHECK("the message text is readable on air as is (no encoding)",
          find(ui.info, ui.info_len, "12 people at the fire station", 29) != NULL);

    /* receive */
    br_ham_rx_init(&g_rx, verify, NULL);
    const uint8_t *rt = NULL;
    uint32_t rl = 0;
    br_ax25_addr_t from;
    int r = 0;
    for (int i = nf - 1; i >= 0; i--)
        r = br_ham_rx_push(&g_rx, g_frames[i], g_flen[i], &rt, &rl, &from);
    CHECK("receiver: frames in reverse order reassemble and verify",
          r == 1 && rl == tl && !memcmp(rt, text, tl) && !strcmp(from.call, "KC1ABC") &&
              from.ssid == 9);
    br_ham_rx_init(&g_rx, verify, NULL);
    r = 0;
    for (int i = 0; i + 1 < nf; i++)
        r |= br_ham_rx_push(&g_rx, g_frames[i], g_flen[i], &rt, &rl, &from);
    CHECK("receiver: a missing frame means no message yet", r == 0);
    /* tamper with the text, keep a valid FCS (an attacker re-encodes) */
    br_ham_rx_init(&g_rx, verify, NULL);
    static uint8_t ff[BR_AX25_FRAME_MAX];
    br_ax25_ui_decode(g_frames[0], g_flen[0], &ui);
    uint8_t info[256];
    memcpy(info, ui.info, ui.info_len);
    uint8_t *p = find(info, ui.info_len, "12 people", 9);
    p[0] = '9';
    p[1] = '0';
    int32_t fl =
        br_ax25_ui_encode(&ui.dst, &ui.src, NULL, 0, 0xF0, info, ui.info_len, ff, sizeof ff);
    r = br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from);
    for (int i = 1; i < nf; i++) r = br_ham_rx_push(&g_rx, g_frames[i], g_flen[i], &rt, &rl, &from);
    CHECK("receiver: altered text with a good FCS fails the signature", r == BR_ERR_AUTH);
    /* same frames re-sent under another call sign */
    br_ham_rx_init(&g_rx, verify, NULL);
    r = 0;
    for (int i = 0; i < nf; i++) {
        br_ax25_ui_decode(g_frames[i], g_flen[i], &ui);
        memcpy(info, ui.info, ui.info_len);
        fl = br_ax25_ui_encode(&ui.dst, &g_keys[1].who, NULL, 0, 0xF0, info, ui.info_len, ff,
                               sizeof ff);
        r = br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from);
    }
    CHECK("receiver: replay under G4XYZ's call sign fails", r == BR_ERR_AUTH);
    br_ham_rx_t *rx2 = &g_rx;
    br_ham_rx_init(rx2, NULL, NULL);
    for (int i = 0; i < nf; i++) r = br_ham_rx_push(rx2, g_frames[i], g_flen[i], &rt, &rl, &from);
    CHECK("receiver with no verifier refuses (fails closed)", r == BR_ERR_AUTH);
    /* chunk format refusals */
    br_ham_rx_init(&g_rx, verify, NULL);
    br_ax25_ui_decode(g_frames[0], g_flen[0], &ui);
    memcpy(info, ui.info, ui.info_len);
    fl = br_ax25_ui_encode(&ui.dst, &ui.src, NULL, 0, 0xF0, info, ui.info_len - 1, ff, sizeof ff);
    CHECK("receiver: short non-final chunk refused",
          br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from) == BR_ERR_FORMAT);
    memcpy(info, ui.info, ui.info_len);
    info[5] = 'g';
    fl = br_ax25_ui_encode(&ui.dst, &ui.src, NULL, 0, 0xF0, info, ui.info_len, ff, sizeof ff);
    CHECK("receiver: bad message id refused",
          br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from) == BR_ERR_FORMAT);
    static const char *bad[] = {"ZXV2 BEEF 1/2 x",  "ZXV1 BEEF 0/2 x",  "ZXV1 BEEF 3/2 x",
                                "ZXV1 BEEF 1/49 x", "ZXV1 BEEF 01/2 x", "ZXV1 BEEF 1-2 x",
                                "ZXV1 BEEF 1/1x",   "ZXV1 BEEF 1/1 "};
    ok = 1;
    for (int i = 0; i < 8; i++) {
        fl = br_ax25_ui_encode(&ui.dst, &ui.src, NULL, 0, 0xF0, (const uint8_t *) bad[i],
                               (uint32_t) strlen(bad[i]), ff, sizeof ff);
        if (br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from) != BR_ERR_FORMAT) ok = 0;
    }
    CHECK("receiver: 8 malformed headers refused", ok);
    br_ax25_addr_t cq;
    br_ax25_addr_parse("CQ", &cq);
    fl = br_ax25_ui_encode(&ui.dst, &cq, NULL, 0, 0xF0, ui.info, ui.info_len, ff, sizeof ff);
    CHECK("receiver: source that is not a station refused",
          br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from) == BR_ERR_FORMAT);
    fl = br_ax25_ui_encode(&ui.dst, &ui.src, NULL, 0, 0xCF, ui.info, ui.info_len, ff, sizeof ff);
    CHECK("receiver: PID other than F0 refused",
          br_ham_rx_push(&g_rx, ff, (uint32_t) fl, &rt, &rl, &from) == BR_ERR_FORMAT);
    g_frames[0][20] ^= 4;
    CHECK("receiver: FCS error refused",
          br_ham_rx_push(&g_rx, g_frames[0], g_flen[0], &rt, &rl, &from) == BR_ERR_CRC);
    g_frames[0][20] ^= 4;
    CHECK("receiver: refusals counted", g_rx.refused >= 13);
    /* a new message id restarts reassembly */
    br_ham_rx_init(&g_rx, verify, NULL);
    for (int i = 0; i + 1 < nf; i++) br_ham_rx_push(&g_rx, g_frames[i], g_flen[i], &rt, &rl, &from);
    const char *t2 = "MAYDAY test 2: all safe at the shelter.";
    sign(0, &cfg.src, t2);
    br_ham_tx_begin(&g_tx, &cfg, 0x0002, E, S, (const uint8_t *) t2, (uint32_t) strlen(t2), g_sig,
                    g_sl);
    int nf2 = build_all();
    for (int i = 0; i < nf2; i++)
        r = br_ham_rx_push(&g_rx, g_frames[i], g_flen[i], &rt, &rl, &from);
    CHECK("receiver: a new message id restarts and the new message verifies",
          r == 1 && rl == strlen(t2) && !memcmp(rt, t2, rl));
    /* ML-DSA-87 (PQM_LEVEL_HIGH) still fits the frame budget */
    for (int k = 0; k < 32; k++) seed[k] = (uint8_t) (k + 7);
    pqm_sig_keygen(PQM_LEVEL_HIGH, seed, &g_keys[0].pk, &g_sk[0]);
    sign(0, &cfg.src, text);
    printf("  ML-DSA-87 signature (pq_matrix encoding): %u bytes\n", g_sl);
    CHECK("ML-DSA-87 signature fits BR_HAM_SIG_MAX", g_sl <= BR_HAM_SIG_MAX);
    CHECK("ML-DSA-87 signed message accepted",
          br_ham_tx_begin(&g_tx, &cfg, 7, E, S, t, tl, g_sig, g_sl) == BR_OK);
    nf = build_all();
    br_ham_rx_init(&g_rx, verify, NULL);
    for (int i = 0; i < nf; i++) r = br_ham_rx_push(&g_rx, g_frames[i], g_flen[i], &rt, &rl, &from);
    printf("  with ML-DSA-87: %d frames\n", nf);
    CHECK("ML-DSA-87 message round trip", r == 1 && rl == tl && nf <= (int) BR_HAM_MAX_FRAMES);
    CHECK("tx_next before begin refused",
          (memset(&g_tx, 0, sizeof g_tx),
           br_ham_tx_next(&g_tx, g_frames[0], BR_AX25_FRAME_MAX) == BR_ERR_STATE));
}

int main(void)
{
    test_b64();
    test_ham();
    return check_summary();
}
