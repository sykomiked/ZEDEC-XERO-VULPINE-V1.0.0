/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Birdsong's model against zxvd's real dispatcher (LINK): its rules, then a
 * memo taken, edited, saved to the Hoard as a WAV file and read back. */
#include "birdsong_model.h"

APPS_TEST(birdsong)
{
    static birdsong_t B, B2;
    static int16_t pcm[2u * 20000u];

    /* the rules, as the blocks run them */
    CHECK(birdsong_gain(1000, 10000) == 1000 && birdsong_gain(-1, 1) == -1 &&
              birdsong_gain(1, 1) == 0 && birdsong_gain(-32768, 1000000) == -32768 &&
              birdsong_gain(20000, 20000) == 32767 && birdsong_gain(-3, 5000) == -2,
          "birdsong: gain floors toward minus infinity and clips to 16 bits");
    CHECK(birdsong_gain(32768, 10000) == BIRDSONG_REFUSED &&
              birdsong_gain(-32769, 10000) == BIRDSONG_REFUSED &&
              birdsong_gain(1, -1) == BIRDSONG_REFUSED &&
              birdsong_gain(1, BIRDSONG_LEVEL_MAX + 1) == BIRDSONG_REFUSED,
          "birdsong: gain refuses outside its domain, as the C++ block does");
    int64_t num = 0, den = 0;
    CHECK(birdsong_clip_ms(1, 44100, &num, &den) == ZXM_OK && num == 10 && den == 441 &&
              birdsong_clip_ms(441, 44100, &num, &den) == ZXM_OK && num == 10 && den == 1 &&
              birdsong_clip_ms(0, 8000, &num, &den) == ZXM_OK && num == 0 && den == 1,
          "birdsong: clip_ms is an exact reduced fraction");
    CHECK(birdsong_clip_ms(1, 0, &num, &den) == ZXM_E_ARG &&
              birdsong_clip_ms(-1, 8000, &num, &den) == ZXM_E_ARG &&
              birdsong_clip_ms(INT64_MAX / 1000 + 1, 8000, &num, &den) == ZXM_E_ARG,
          "birdsong: clip_ms refuses a rate of 0, a negative count and an overflow");
    CHECK(birdsong_rec_ok(8000, 1, 0) && birdsong_rec_ok(48000, 2, 480000) &&
              !birdsong_rec_ok(7999, 1, 0) && !birdsong_rec_ok(48001, 1, 0) &&
              !birdsong_rec_ok(8000, 0, 0) && !birdsong_rec_ok(8000, 3, 0) &&
              !birdsong_rec_ok(8000, 1, -1) && !birdsong_rec_ok(8000, 1, 480001),
          "birdsong: the recording limits (Eve's ranges)");
    CHECK(birdsong_memo_ok(999999999, 99999, 9, 999999999, INT64_C(999999999999)) &&
              !birdsong_memo_ok(1000000000, 0, 0, 0, 0) && !birdsong_memo_ok(0, 100000, 0, 0, 0) &&
              !birdsong_memo_ok(0, 0, 10, 0, 0) &&
              !birdsong_memo_ok(0, 0, 0, 0, INT64_C(1000000000000)),
          "birdsong: the memo record's digit widths (MYRA's)");

    /* a memo: nothing until one is taken; limits are refused, never cut */
    birdsong_init(&B, &LINK);
    CHECK(birdsong_apply_gain(&B, 10000) == ZXM_E_STATE && birdsong_save(&B) == ZXM_E_STATE &&
              birdsong_length_ms(&B, &num, &den) == ZXM_E_STATE,
          "birdsong: no memo yet");
    for (uint32_t i = 0; i < 2u * 20000u; i++)
        pcm[i] = (int16_t) ((int32_t) (i * 7919u % 65536u) - 32768);
    CHECK(birdsong_take(&B, 7999, 1, pcm, 10) == ZXM_E_ARG &&
              birdsong_take(&B, 48000, 2, pcm, 11) == ZXM_E_ARG &&
              birdsong_take(&B, 48000, 3, pcm, 12) == ZXM_E_ARG && B.rate == 0,
          "birdsong: a rate, channel count or part frame outside the limits is refused");
    CHECK(birdsong_take(&B, 48000, 2, pcm, 2u * 20000u) == ZXM_OK && B.frames == 20000 && B.dirty,
          "birdsong: a stereo memo taken");
    CHECK(birdsong_length_ms(&B, &num, &den) == ZXM_OK && num == 1250 && den == 3,
          "birdsong: 20000 frames at 48000 Hz last 1250/3 ms");

    /* edits: gain that clips, and a trim */
    CHECK(birdsong_apply_gain(&B, 20000) == ZXM_OK && B.clipped > 0 &&
              B.pcm[0] == birdsong_gain(pcm[0], 20000) &&
              B.pcm[39999] == birdsong_gain(pcm[39999], 20000),
          "birdsong: gain over the memo, %u samples clipped", B.clipped);
    CHECK(birdsong_apply_gain(&B, -1) == ZXM_E_ARG, "birdsong: a negative level is refused");
    CHECK(birdsong_trim(&B, 10, 5) == ZXM_E_ARG && birdsong_trim(&B, 0, 20001) == ZXM_E_ARG,
          "birdsong: a trim outside the memo is refused");
    int16_t keep0 = B.pcm[2 * 100], keep1 = B.pcm[2 * 100 + 1];
    CHECK(birdsong_trim(&B, 100, 19100) == ZXM_OK && B.frames == 19000 && B.pcm[0] == keep0 &&
              B.pcm[1] == keep1,
          "birdsong: trimmed to frames 100..19100");

    /* BS1: into the Hoard (several base64 pieces), and back */
    CHECK(birdsong_save_as(&B, "", "") == ZXM_E_ARG, "birdsong: no path");
    CHECK(birdsong_save_as(&B, "home", "memo-one.wav") == ZXM_OK && !B.dirty &&
              !strcmp(B.path, "memo-one.wav") && B.space[0] == 0,
          "birdsong: saved: %s", LINK.err);
    birdsong_init(&B2, &LINK);
    CHECK(birdsong_open(&B2, "", "memo-one.wav") == ZXM_OK && B2.rate == 48000 &&
              B2.channels == 2 && B2.frames == 19000 &&
              !memcmp(B2.pcm, B.pcm, 2u * 19000u * sizeof B.pcm[0]) && !B2.dirty,
          "birdsong: read back sample for sample: %s", LINK.err);
    CHECK(birdsong_trim(&B2, 0, 10) == ZXM_OK && birdsong_save(&B2) == ZXM_OK &&
              birdsong_open(&B, "", "memo-one.wav") == ZXM_OK && B.frames == 10 &&
              B.pcm[19] == B2.pcm[19],
          "birdsong: saved in place and read again: %s", LINK.err);

    /* a mono memo at the lowest rate, and files that are not memos */
    CHECK(birdsong_take(&B2, 8000, 1, pcm, 3) == ZXM_OK &&
              birdsong_save_as(&B2, "", "memo-two.wav") == ZXM_OK &&
              birdsong_open(&B, "", "memo-two.wav") == ZXM_OK && B.frames == 3 && B.channels == 1 &&
              B.pcm[2] == pcm[2],
          "birdsong: a three-sample mono memo: %s", LINK.err);
    zxm_begin(&LINK, "files.write");
    zxm_p_str(&LINK, "path", "not-a-memo.wav");
    zxm_p_str(&LINK, "text", "RIFF, but only in name");
    CHECK(zxm_call(&LINK) == ZXM_OK, "birdsong: a text file written: %s", LINK.err);
    CHECK(birdsong_open(&B, "", "not-a-memo.wav") == ZXM_E_ARG && B.frames == 3 &&
              !strcmp(B.path, "memo-two.wav"),
          "birdsong: a file that is not a WAV leaves the memo as it was");
    CHECK(birdsong_open(&B, "", "no-such.wav") == ZXM_E_REMOTE && B.frames == 3,
          "birdsong: a missing memo is zxvd's error: %s", LINK.err);
}
