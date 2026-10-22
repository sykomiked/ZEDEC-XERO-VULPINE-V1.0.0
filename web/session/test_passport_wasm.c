/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_passport_wasm.c — the browser's passport glue (zxv_passport_wasm.c),
 * built natively (PW_HOST) against the same passport and ident sources: each
 * export gives what the passport itself gives for the same input, refuses
 * what it should, and leaves no secret behind. The two DIDs below are what
 * tests/browser/test_browser.sh expects the WebAssembly build to show in the
 * page for the same seed and passphrase, which ties the wasm to this build.
 *
 *   make -C kernel test-passport-wasm */
#include <stdio.h>
#include <string.h>

#include "passport.h"

int32_t pw_seed_new(void);
int32_t pw_seed_text(uint32_t n);
int32_t pw_seed_confirm(uint32_t n);
int32_t pw_kdf_memory(void);
int32_t pw_seed_pass(uint32_t np, uint32_t nh);
int32_t pw_hydrate(void);
void pw_wipe(void);
uint8_t *pw_in(void);
char *pw_out(void);
uint32_t pw_in_max(void);
void pw_out_wipe(void);

#define OUT_MAX 128u /* zxv_passport_wasm.c PW_OUT_MAX */

/* seed bytes 0, 1, ..., 31: 000G4-0R40M-30E20-9185G-R38E1-W8124-GK2GA-HC5RR-34D1P-70X3R-FHHQ0 */
static const char DID_SEED[] = "did:zxv:2xin6nai2r2rmwk5wqv25nwmatah75fp2uukhsftpniqksm5zxcq";
/* passphrase "correct horse battery staple", handle "ana" */
static const char DID_PASS[] = "did:zxv:yxprmmrkcsabstevsnynhlmlutiqa7cceizvg2vrjgwgrudnd74q";

static int checks, fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL line %d: ", __LINE__);                                                  \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static int in_clear(void)
{
    const uint8_t *p = pw_in();
    for (uint32_t i = 0; i < pw_in_max(); i++)
        if (p[i]) return 0;
    return 1;
}

static uint32_t put(const char *s)
{
    uint32_t n = (uint32_t) strlen(s);
    memcpy(pw_in(), s, n);
    return n;
}

/* OUT holds its NUL-terminated text and nothing after it. */
static int out_clean_after(void)
{
    const char *o = pw_out();
    uint32_t i = 0;
    while (i < OUT_MAX && o[i]) i++;
    for (; i < OUT_MAX; i++)
        if (o[i]) return 0;
    return 1;
}

static const char B32[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

/* Every one-character mistake in a seed's text: each of its 55 characters
 * replaced by each other base32 character, each pair of neighbours swapped,
 * each character dropped, and each control or non-ASCII byte put in. None
 * may log in; returns how many did (0 when all were refused). */
static int typos_accepted(const char *text)
{
    char t[PP_SEED_TEXT + 2];
    uint32_t len = (uint32_t) strlen(text);
    int bad = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (text[i] == '-') continue;
        for (uint32_t c = 0; c < 32; c++) {
            if (B32[c] == text[i]) continue;
            memcpy(t, text, len + 1);
            t[i] = B32[c];
            int32_t r = pw_seed_text(put(t));
            if (r == PP_OK) {
                printf("  accepted: %s (position %u)\n", t, i);
                bad++;
                pw_hydrate();
            } else if (r != PP_ERR_CHECKSUM && r != PP_ERR_FORMAT) {
                bad++;
            }
        }
        uint32_t j = i + 1 < len && text[i + 1] == '-' ? i + 2 : i + 1;
        if (j < len && text[j] != text[i]) {
            memcpy(t, text, len + 1);
            t[i] = text[j];
            t[j] = text[i];
            if (pw_seed_text(put(t)) == PP_OK) {
                printf("  accepted: %s (swap at %u)\n", t, i);
                bad++;
                pw_hydrate();
            }
        }
        memcpy(t, text, i);
        memcpy(t + i, text + i + 1, len - i);
        if (pw_seed_text(put(t)) != PP_ERR_FORMAT) bad++;
    }
    for (uint32_t b = 1; b < 256; b++) {
        if (b == ' ' || b == '-' || (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') ||
            (b >= 'a' && b <= 'z'))
            continue;
        memcpy(t, text, 30);
        t[30] = (char) b;
        memcpy(t + 31, text + 30, len - 30 + 1);
        if (pw_seed_text(put(t)) != PP_ERR_FORMAT) {
            printf("  byte 0x%02x inside the seed was not refused\n", b);
            bad++;
        }
    }
    return bad;
}

static uint32_t put2(const char *a, const char *b)
{
    uint32_t na = (uint32_t) strlen(a), nb = (uint32_t) strlen(b);
    memcpy(pw_in(), a, na);
    memcpy(pw_in() + na, b, nb);
    return na;
}

/* The DID the passport itself gives for a seed. */
static void direct_did(const uint8_t seed[PP_SEED], char did[ID_DID_CHARS])
{
    static pp_passport_t p;
    did[0] = 0;
    if (pp_hydrate(&p, PQM_LEVEL_MATRIX, seed) == PP_OK) id_did_string(p.root.did, did);
    pp_wipe(&p);
}

int main(void)
{
    uint8_t seed[PP_SEED];
    char text[PP_SEED_TEXT], did[ID_DID_CHARS], out[128];
    for (uint32_t i = 0; i < PP_SEED; i++) seed[i] = (uint8_t) i;

    /* a new seed: the page's 32 random bytes as text, and nothing kept */
    memcpy(pw_in(), seed, PP_SEED);
    int32_t n = pw_seed_new();
    CHECK(pp_seed_to_text(seed, text) == PP_OK && n == (int32_t) strlen(text) &&
              !strcmp(pw_out(), text),
          "pw_seed_new is pp_seed_to_text (%s)", pw_out());
    CHECK(in_clear(), "the random bytes are wiped");
    CHECK(pw_hydrate() == PP_ERR_STATE, "making a seed does not log in");

    /* seed text: the same identity as the passport gives */
    direct_did(seed, did);
    CHECK(pw_seed_text(put(text)) == PP_OK && in_clear(), "seed text read, the text wiped");
    n = pw_hydrate();
    CHECK(n == (int32_t) strlen(did) && !strcmp(pw_out(), did), "seed DID %s = passport %s",
          pw_out(), did);
    printf("seed text %s\nseed DID %s\n", text, pw_out());
    CHECK(!strcmp(pw_out(), DID_SEED), "the seed DID is the browser test's (%s)", DID_SEED);
    CHECK(pw_hydrate() == PP_ERR_STATE, "the seed is dropped once the keys are built");

    /* the same seed, typed loosely: lower case, spaces, O for 0 */
    char loose[PP_SEED_TEXT + 16];
    uint32_t k = 0;
    for (uint32_t i = 0; text[i]; i++) {
        char ch = text[i];
        if (ch == '-')
            ch = ' ';
        else if (ch == '0')
            ch = 'O';
        else if (ch >= 'A' && ch <= 'Z')
            ch = (char) (ch - 'A' + 'a');
        loose[k++] = ch;
    }
    loose[k] = 0;
    CHECK(pw_seed_text(put(loose)) == PP_OK && pw_hydrate() > 0 && !strcmp(pw_out(), did),
          "a loosely typed seed is the same identity");

    /* refusals: the module is unchanged, the input still wiped */
    memcpy(out, text, sizeof text);
    out[3] = out[3] == 'A' ? 'B' : 'A'; /* one wrong character */
    CHECK(pw_seed_text(put(out)) == PP_ERR_CHECKSUM && in_clear(), "a typo fails the checksum");
    CHECK(pw_seed_text(put("hello")) == PP_ERR_FORMAT && in_clear(), "not a seed");
    CHECK(pw_seed_text(put("UUUUU")) == PP_ERR_FORMAT, "a character outside base32");
    CHECK(pw_seed_text(0) == PP_ERR_ARG, "nothing typed");
    CHECK(pw_seed_text(pw_in_max() + 1) == PP_ERR_ARG && in_clear(), "longer than the input");
    CHECK(pw_hydrate() == PP_ERR_STATE, "no refused call left a seed");

    /* a NUL is not the end of what was typed: the seed, then a NUL and
     * anything, is refused, not read as the seed */
    memcpy(pw_in(), text, strlen(text));
    memcpy(pw_in() + strlen(text), "\0XYZ", 4);
    CHECK(pw_seed_text((uint32_t) strlen(text) + 4) == PP_ERR_FORMAT && in_clear(),
          "a seed with a NUL and more after it is refused");
    memcpy(pw_in(), text, strlen(text) + 1);
    CHECK(pw_seed_text((uint32_t) strlen(text) + 1) == PP_ERR_FORMAT,
          "a seed followed by one NUL is refused");
    memcpy(pw_in(), "\0", 1);
    memcpy(pw_in() + 1, text, strlen(text));
    CHECK(pw_seed_text((uint32_t) strlen(text) + 1) == PP_ERR_FORMAT,
          "a NUL before the seed is refused");
    CHECK(pw_hydrate() == PP_ERR_STATE, "no NUL let a seed in");

    /* every one-character mistake, at every position, is refused */
    CHECK(typos_accepted(text) == 0, "every mistyped position of %s is refused", text);
    CHECK(pw_hydrate() == PP_ERR_STATE, "no mistake left a seed");

    /* passphrase and handle */
    CHECK(pw_seed_pass(put2("correct horse battery staple", "ana"), 3) == PP_ERR_KDF && in_clear(),
          "no stretch before its memory is set aside");
    CHECK(pw_kdf_memory() == 0 && pw_kdf_memory() == 0, "64 MiB set aside, once");
    CHECK(pw_seed_pass(put2("fifteen chars!!", "ana"), 3) == PP_ERR_WEAK && in_clear(),
          "15 characters are too few");
    CHECK(pw_seed_pass(put2("correct horse battery staple", ""), 0) == PP_ERR_ARG,
          "a handle is needed");
    CHECK(pw_seed_pass(28, PP_HANDLE_MAX + 1) == PP_ERR_ARG, "a handle over 64 bytes");
    CHECK(pw_seed_pass(PP_PASS_MAX + 1, 3) == PP_ERR_ARG && in_clear(), "a passphrase over 256");
    CHECK(pw_hydrate() == PP_ERR_STATE, "still no seed");

    id_kdf_params_t kp;
    pp_kdf_v1(&kp);
    static uint64_t mem[PP_KDF_M_KIB * (ID_ARGON2_BLOCK / 8u)];
    uint8_t ps[PP_SEED];
    CHECK(pp_seed_from_passphrase(&kp, (const uint8_t *) "correct horse battery staple", 28,
                                  (const uint8_t *) "ana", 3, mem, PP_KDF_M_KIB, ps) == PP_OK,
          "the passport stretches the passphrase");
    direct_did(ps, did);
    CHECK(pw_seed_pass(put2("correct horse battery staple", "ana"), 3) == PP_OK && in_clear(),
          "passphrase read, both wiped");
    CHECK(pw_hydrate() > 0 && !strcmp(pw_out(), did), "passphrase DID %s = passport %s", pw_out(),
          did);
    printf("passphrase DID %s\n", pw_out());
    CHECK(!strcmp(pw_out(), DID_PASS), "the passphrase DID is the browser test's (%s)", DID_PASS);
    CHECK(pw_seed_pass(put2("correct horse battery staple", "Ana"), 3) == PP_OK &&
              pw_hydrate() > 0 && strcmp(pw_out(), did),
          "another handle is another identity");

    /* a refused call keeps a seed already read */
    CHECK(pw_seed_text(put(text)) == PP_OK, "seed read");
    CHECK(pw_seed_text(put("hello")) == PP_ERR_FORMAT, "then a bad one refused");
    direct_did(seed, did);
    CHECK(pw_hydrate() > 0 && !strcmp(pw_out(), did), "the first seed is still the one used");

    /* first time: a new seed, shown once, typed back to log in */
    pw_wipe();
    CHECK(pw_seed_confirm(put(text)) == PP_ERR_STATE && in_clear(), "nothing to confirm yet");
    uint8_t seed2[PP_SEED];
    char text2[PP_SEED_TEXT];
    for (uint32_t i = 0; i < PP_SEED; i++) seed2[i] = (uint8_t) (0xA5u ^ (i * 7u));
    memcpy(pw_in(), seed2, PP_SEED);
    CHECK(pw_seed_new() > 0 && pp_seed_to_text(seed2, text2) == PP_OK && !strcmp(pw_out(), text2),
          "a seed to create is made and shown");
    CHECK(pw_seed_confirm(put(text)) == PP_ERR_MISMATCH && in_clear(),
          "typing back another valid seed is refused");
    CHECK(pw_hydrate() == PP_ERR_STATE, "a refused confirmation logs nobody in");
    memcpy(out, text2, sizeof text2);
    out[3] = out[3] == 'A' ? 'B' : 'A';
    CHECK(pw_seed_confirm(put(out)) == PP_ERR_CHECKSUM, "a typo in the confirmation is caught");
    CHECK(pw_seed_confirm(0) == PP_ERR_ARG, "an empty confirmation is refused");
    memcpy(pw_in(), text2, strlen(text2));
    memcpy(pw_in() + strlen(text2), "\0Q", 2);
    CHECK(pw_seed_confirm((uint32_t) strlen(text2) + 2) == PP_ERR_FORMAT && in_clear(),
          "the pending seed with a NUL and more after it is refused");
    CHECK(typos_accepted(text2) == 0, "every mistyped position of %s is refused", text2);
    direct_did(seed2, did);
    CHECK(pw_seed_confirm(put(text2)) == PP_OK && in_clear(), "the right seed typed back is taken");
    CHECK(pw_hydrate() > 0 && !strcmp(pw_out(), did), "created identity %s = passport %s", pw_out(),
          did);
    CHECK(out_clean_after(), "no byte of the shown seed's text is left in OUT after the DID");
    CHECK(pw_seed_text(put(text2)) == PP_OK && pw_hydrate() > 0 && !strcmp(pw_out(), did),
          "logging in with that seed later gives the same DID");
    CHECK(pw_seed_confirm(put(text2)) == PP_ERR_STATE, "a confirmed seed is no longer pending");
    memcpy(pw_in(), seed2, PP_SEED);
    pw_seed_new();
    pw_out_wipe();
    {
        int z = 1;
        for (uint32_t i = 0; i < OUT_MAX; i++) z &= pw_out()[i] == 0;
        CHECK(z, "pw_out_wipe leaves no byte of a new seed's text");
    }
    pw_wipe();
    CHECK(pw_seed_confirm(put(text2)) == PP_ERR_STATE, "a wipe drops the pending seed");

    /* wipe */
    CHECK(pw_seed_text(put(text)) == PP_OK, "seed read again");
    pw_wipe();
    CHECK(pw_hydrate() == PP_ERR_STATE && pw_out()[0] == 0 && in_clear(), "wiped: no seed, no DID");

    printf("test_passport_wasm: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
