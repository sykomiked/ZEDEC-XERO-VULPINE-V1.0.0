/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Aerie's model against zxvd's real dispatcher: ehop.bands, ehop.seal,
 * ehop.open, harmonic.encode, harmonic.decode, freight.pack and
 * freight.unpack as zxvd runs them. Nothing is transmitted: zxvd has no
 * radio method, and sealing a frame sends nothing anywhere. */
#include "aerie_model.h"

#define AE_KEY  "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"
#define AE_KEY2 "ff0102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"

APPS_TEST(aerie)
{
    static aerie_t A, B;
    static uint8_t msg[AERIE_FREIGHT_MAX];
    static char frame[AERIE_FRAME_HEX + 1], packets[AERIE_PACKETS_HEX + 1];
    const int64_t NOW = 1700000000000LL; /* 22:13 UTC */
    for (uint32_t i = 0; i < AERIE_FREIGHT_MAX; i++) msg[i] = (uint8_t) (i * 7u + 3u);

    /* AM1: the band plan */
    aerie_init(&A, &LINK);
    aerie_chan_t ch = {AERIE_PRIVATE, AE_KEY, 3, 7, 1, 11};
    int before = sent;
    CHECK(aerie_seal(&A, &ch, msg, 5, NOW) == ZXM_E_STATE && sent == before,
          "AM3: no band plan read yet: nothing sent");
    CHECK(aerie_bands(&A) == ZXM_OK && A.nbands == 5 && aerie_band_index(&A, "control") == 0 &&
              aerie_band_index(&A, "chat") == 3 && aerie_band_index(&A, "radio") == -1 &&
              A.band[0].max_frame == 512 + AERIE_OVERHEAD && A.band[3].max_frame == 2096 &&
              A.band[2].max_frame == 1400 && A.threshold == AERIE_THR_DEFAULT,
          "AM1: ehop.bands: %s", LINK.err);

    /* AM2: the owner chooses the Endian Hopping bands, messages only */
    CHECK(aerie_set_bands(&A, 1u << AERIE_SETTLE) == ZXM_E_ARG &&
              aerie_set_bands(&A, 1u << 5) == ZXM_E_ARG && A.allowed == AERIE_BANDS_NONE,
          "AM2: the settle band and a band ehop lacks are refused");
    before = sent;
    CHECK(aerie_seal(&A, &ch, msg, 5, NOW) == ZXM_E_STATE && sent == before && A.nlog == 0,
          "AM2: a band the owner did not choose: nothing sent");
    CHECK(aerie_set_bands(&A, (1u << 0) | (1u << 3)) == ZXM_OK, "AM2: control and chat allowed");

    /* AM3: checked before anything is sent */
    before = sent;
    aerie_chan_t bad = {AERIE_PRIVATE, "00", 3, 7, 1, 11},
                 ctl = {AERIE_PRIVATE, AE_KEY, 0, 9, 1, 11};
    CHECK(aerie_seal(&A, &bad, msg, 5, NOW) == ZXM_E_ARG &&
              aerie_seal(&A, &ctl, msg, 513, NOW) == ZXM_E_ARG &&
              aerie_seal(&A, &ch, msg, 2049, NOW) == ZXM_E_ARG &&
              aerie_seal(&A, &ch, msg, 5, -1) == ZXM_E_ARG && sent == before && A.next_seq == 0,
          "AM3: a key that is no key, a message past its band, a time before 1970: nothing sent");
    CHECK(aerie_seal(&A, &ctl, msg, 512, NOW) == ZXM_OK && strlen(A.frame_hex) == 2u * 560u &&
              A.next_seq == 1 && A.nlog == 1,
          "AM3, AR2: 512 bytes exactly fill the control band: %s", LINK.err);
    CHECK(aerie_seal(&A, &ch, msg, 2048, NOW) == ZXM_OK && strlen(A.frame_hex) == 2u * 2096u,
          "AM3: 2048 bytes on the chat band: %s", LINK.err);
    CHECK(aerie_seal(&A, &ch, (const uint8_t *) "hello aerie", 11, NOW) == ZXM_OK &&
              A.next_seq == 3,
          "AM3: a short chat message: %s", LINK.err);
    memcpy(frame, A.frame_hex, strlen(A.frame_hex) + 1u);
    const aerie_msg_t *m = aerie_log_at(&A, 2);
    CHECK(A.nlog == 3 && m && m->seq == 3 && m->band == 3 && m->dir == AERIE_OUT &&
              m->sender == 11 && m->bytes == 11 && m->stamp == NOW && !aerie_log_at(&A, 3),
          "AM7: three messages logged going out");

    /* AM4: the far end opens it, once */
    aerie_init(&B, &LINK);
    aerie_chan_t rx = {AERIE_PRIVATE, AE_KEY, 3, 7, 1, 12},
                 wrong = {AERIE_PRIVATE, AE_KEY2, 3, 7, 1, 12};
    CHECK(aerie_bands(&B) == ZXM_OK && aerie_set_bands(&B, 1u << 3) == ZXM_OK, "B: bands chosen");
    CHECK(aerie_open(&B, &rx, frame, NOW) == ZXM_OK && B.ndata == 11 &&
              !memcmp(B.data, "hello aerie", 11) && B.from == 11 && !B.alert && B.nlog == 1 &&
              aerie_log_at(&B, 0)->dir == AERIE_IN,
          "AM4: opened, a stranger never alerts: %s", LINK.err);
    CHECK(aerie_open(&B, &rx, frame, NOW) == ZXM_E_STATE && B.nlog == 1,
          "AM4: the same (sender, seq) again is a replay: not kept");
    CHECK(aerie_open(&B, &wrong, frame, NOW) == ZXM_E_REMOTE && B.nlog == 1,
          "AM4: another key does not open it");
    frame[100] = frame[100] == '0' ? '1' : '0';
    CHECK(aerie_open(&B, &rx, frame, NOW) == ZXM_E_REMOTE && B.nlog == 1,
          "AM4: a changed byte does not open");

    /* AM4 with Halloo's rules: a known contact alerts, except in quiet hours */
    CHECK(aerie_set_contact(&B, 11, true, false) == ZXM_OK && aerie_may_alert(&B, 11, 600) &&
              !aerie_may_alert(&B, 12, 600) && aerie_set_quiet(&B, 1320, 420) == ZXM_OK &&
              !aerie_may_alert(&B, 11, 1333) && aerie_may_alert(&B, 11, 600) &&
              aerie_set_quiet(&B, 1440, 0) == ZXM_E_ARG &&
              aerie_set_contact(&B, 11, true, true) == ZXM_OK && !aerie_may_alert(&B, 11, 600),
          "AM4: known, quiet hours, blocked (HR1, HR2)");
    CHECK(aerie_set_contact(&B, 11, true, false) == ZXM_OK &&
              aerie_seal(&A, &ch, (const uint8_t *) "again", 5, NOW) == ZXM_OK &&
              aerie_open(&B, &rx, A.frame_hex, NOW + 10 * 60000) == ZXM_OK && !B.alert &&
              aerie_seal(&A, &ch, (const uint8_t *) "later", 5, NOW) == ZXM_OK &&
              aerie_open(&B, &rx, A.frame_hex, NOW + 10 * 3600000LL) == ZXM_OK && B.alert,
          "AM4: in quiet hours no alert, after them an alert: %s", LINK.err);

    /* AM5: line frames */
    static const int64_t SP[3] = {65536, 65536, 0}, SM[2] = {65536, 65536},
                         SN[2] = {-65536, -65536}, BIG[3] = {0, 4294967296LL, 0};
    CHECK(aerie_line_encode(&A, 1, 2, 3, SP, SM) == ZXM_OK && A.line.tag == 123 &&
              strlen(A.line.frame_hex) == AERIE_LINE_HEX && A.line.trunk_hz > 0,
          "AM5: encode sync 1 shell 2 trunk 3: tag 123: %s", LINK.err);
    char line[AERIE_LINE_HEX + 1];
    memcpy(line, A.line.frame_hex, sizeof line);
    CHECK(aerie_line_decode(&A, line) == ZXM_OK && A.line.tag == 123 && A.line.sync == 1 &&
              A.line.shell == 2 && A.line.trunk == 3 && A.line.truth == AERIE_TRUE &&
              !A.line.reserved && A.line.s_plus[0] == 65536 && A.line.s_minus[1] == 65536,
          "AM5: decoded and split back, the rails agree: TRUE: %s", LINK.err);
    CHECK(aerie_line_encode(&A, 0, 0, 9, SP, SN) == ZXM_OK && A.line.tag == 9 &&
              aerie_line_decode(&A, A.line.frame_hex) == ZXM_OK && A.line.truth == AERIE_GLUT,
          "AM5: two strong rails directly opposed: GLUT: %s", LINK.err);
    before = sent;
    CHECK(aerie_line_encode(&A, 2, 0, 0, SP, SM) == ZXM_E_ARG &&
              aerie_line_encode(&A, 0, 0, 10, SP, SM) == ZXM_E_ARG &&
              aerie_line_encode(&A, 0, 0, 0, BIG, SM) == ZXM_E_ARG &&
              aerie_line_decode(&A, "00") == ZXM_E_ARG && sent == before,
          "AM5, AR1: parts out of range and a word past 32 bits: nothing sent");
    memcpy(line, "d2", 2); /* tag 210: reserved, trunk 0 */
    CHECK(aerie_line_decode(&A, line) == ZXM_OK && A.line.reserved && A.line.tag == 210 &&
              A.line.trunk == 0,
          "AM5: a reserved tag decodes its trunk and is marked: %s", LINK.err);
    CHECK(aerie_set_threshold(&A, 0) == ZXM_E_ARG && aerie_set_threshold(&A, 65537) == ZXM_E_ARG &&
              A.threshold == AERIE_THR_DEFAULT && aerie_set_threshold(&A, 65536) == ZXM_OK &&
              A.threshold == 65536,
          "AR3: the threshold's limits");

    /* AM6: freight, any k of 168 packets */
    CHECK(aerie_freight_pack(&A, msg, 0) == ZXM_E_ARG &&
              aerie_freight_pack(&A, msg, AERIE_FREIGHT_MAX + 1u) == ZXM_E_ARG,
          "AM6: nothing, or more than a freight holds, is refused");
    CHECK(aerie_freight_pack(&A, msg, 1000) == ZXM_OK && A.freight.payload_len == 1000 &&
              A.freight.k == 50 && strlen(A.freight.packets_hex) == AERIE_PACKETS_HEX,
          "AM6: 1000 bytes packed into 50 data rows: %s", LINK.err);
    /* the last 50 packets only: parity rebuilds the message */
    memcpy(packets, A.freight.packets_hex + (168u - 50u) * 42u, 50u * 42u);
    packets[50u * 42u] = 0;
    memset(B.data, 0, sizeof B.data);
    CHECK(aerie_freight_unpack(&B, A.freight.header_hex, packets) == ZXM_OK && B.ndata == 1000 &&
              !memcmp(B.data, msg, 1000),
          "AM6: rebuilt from the 50 parity packets alone: %s", LINK.err);
    packets[49u * 42u] = 0;
    CHECK(aerie_freight_unpack(&B, A.freight.header_hex, packets) == ZXM_E_REMOTE,
          "AM6: 49 packets are one too few");

    /* AM7: the log is bounded */
    uint32_t n0 = A.nlog;
    for (uint32_t i = 0; i < AERIE_LOG; i++)
        if (aerie_seal(&A, &ctl, (const uint8_t *) "x", 1, NOW + i) != ZXM_OK) break;
    CHECK(A.nlog == AERIE_LOG && A.log_dropped == n0 &&
              aerie_log_at(&A, AERIE_LOG - 1)->stamp == NOW + AERIE_LOG - 1,
          "AM7: the oldest go when the log is full");
    CHECK(!strstr(LINK.req, "amount") && !strstr(LINK.req, "ledger") &&
              strstr(LINK.req, "\"ehop.seal\""),
          "messages only: a sealed request carries no amount");

    /* AR5, AM3: never a sealed frame on an amateur bearer, whatever else holds */
    aerie_chan_t ham = {AERIE_AMATEUR, AE_KEY, 3, 7, 1, 11}, odd = {7, AE_KEY, 3, 7, 1, 11};
    uint64_t seq0 = A.next_seq;
    int64_t log0 = A.log_seq;
    before = sent;
    CHECK(aerie_seal(&A, &ham, (const uint8_t *) "help", 4, NOW) == ZXM_E_STATE &&
              aerie_seal(&A, &odd, (const uint8_t *) "help", 4, NOW) == ZXM_E_STATE &&
              aerie_open(&B, &ham, frame, NOW) == ZXM_E_STATE && sent == before &&
              A.next_seq == seq0 && A.log_seq == log0,
          "AR5: sealing for an amateur bearer is refused, nothing sent, no seq taken");

    /* AM8, AM9: the amateur path, off until the owner gives a call sign and
     * confirms a licence; plain text, the call sign in the clear */
    static aerie_t H;
    aerie_init(&H, &LINK);
    CHECK(strstr(AERIE_NOTICE_HAM, "call sign is sent in the clear") &&
              strstr(AERIE_NOTICE_HAM, "No encryption, no payments") &&
              strstr(AERIE_NOTICE_HAM, "hold an amateur licence") &&
              strstr(AERIE_NOTICE_SEALED, "never on amateur radio") &&
              strstr(AERIE_NOTICE_SEALED, "never money"),
          "the notices say what each path is");
    before = sent;
    CHECK(!H.ham_on && aerie_ham_compose(&H, "help", 4, NOW) == ZXM_E_STATE &&
              aerie_ham_enable(&H, "W1AW", false) == ZXM_E_STATE && !H.ham_on &&
              aerie_ham_enable(&H, "WAW", true) == ZXM_E_ARG &&
              aerie_ham_enable(&H, "1234", true) == ZXM_E_ARG &&
              aerie_ham_enable(&H, "W1AWXYZ", true) == ZXM_E_ARG &&
              aerie_ham_enable(&H, "W1AW-16", true) == ZXM_E_ARG &&
              aerie_ham_enable(&H, "W1AW-01", true) == ZXM_E_ARG && !H.ham_on && H.nlog == 0,
          "AM8: off until a call sign and the owner's confirmed licence");
    CHECK(aerie_call_ok("w1aw-15") && aerie_call_ok("K9X") &&
              aerie_ham_enable(&H, "w1aw-7", true) == ZXM_OK && H.ham_on &&
              !strcmp(H.call, "W1AW-7"),
          "AM8: on, the call sign upper-cased");
    const char *say = "Water needed at the school, 40 people.";
    CHECK(aerie_ham_compose(&H, say, (uint32_t) strlen(say), NOW) == ZXM_OK &&
              !strcmp(H.ham_text, "ZXV-EMRG-1\nW1AW-7\nWater needed at the school, 40 people.") &&
              H.nlog == 1 && aerie_log_at(&H, 0)->bearer == AERIE_AMATEUR &&
              aerie_log_at(&H, 0)->sender == 0 && sent == before,
          "AM9: composed in the clear with the call sign, logged, nothing sent");
    CHECK(aerie_ham_compose(&H, "a\x01z", 3, NOW) == ZXM_E_ARG &&
              aerie_ham_compose(&H, "", 0, NOW) == ZXM_E_ARG && H.nlog == 1 && sent == before,
          "AM9: bytes that are not plain text are refused");
    aerie_ham_disable(&H);
    CHECK(!H.ham_on && aerie_ham_compose(&H, "help", 4, NOW) == ZXM_E_STATE,
          "AM8: switched off again");
}
