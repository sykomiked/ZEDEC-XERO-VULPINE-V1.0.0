/* test_zsp.c — host test for signed-package verification.
 *
 * Uses the real signed hello.zsp + root pubkey (userapp/hello_signed.h).
 * Asserts: a genuine package verifies; a flipped payload byte is
 * rejected (hash); a flipped signature byte is rejected (sig); a wrong
 * root key is rejected; a truncated/short buffer is rejected.
 *
 *   gcc -std=c11 -Wall -Isrc/loader -Isrc/robin_debanks -I../userapp \
 *       src/loader/test_zsp.c src/loader/zsp.c \
 *       src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c \
 *       -o /tmp/test_zsp && /tmp/test_zsp
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "zsp.h"
#include "hello_signed.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s\n", msg); failures++; } \
    else         { printf("[PASS] %s\n", msg); } } while (0)

int main(void) {
    static uint8_t buf[8192];
    const uint8_t *pl; uint32_t pl_len;
    printf("=== ZSP signed-package tests (pkg=%uB) ===\n", hello_zsp_len);

    /* genuine package verifies */
    memcpy(buf, hello_zsp, hello_zsp_len);
    zsp_result_t r = zsp_verify(buf, hello_zsp_len, hello_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_OK, "genuine package accepted");
    CHECK(pl_len > 0 && pl[0] == 0x7f, "payload points at the ELF (0x7f 'E')");

    /* tampered payload -> hash mismatch */
    memcpy(buf, hello_zsp, hello_zsp_len);
    buf[ZSP_HEADER_LEN + 32] ^= 0x01;   /* flip a payload byte */
    r = zsp_verify(buf, hello_zsp_len, hello_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_ERR_HASH, "tampered payload rejected (hash)");

    /* tampered signature -> sig invalid */
    memcpy(buf, hello_zsp, hello_zsp_len);
    buf[40] ^= 0x01;                    /* flip a signature byte */
    r = zsp_verify(buf, hello_zsp_len, hello_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_ERR_SIG, "tampered signature rejected");

    /* tampered hash field only -> hash mismatch caught before sig */
    memcpy(buf, hello_zsp, hello_zsp_len);
    buf[8] ^= 0x01;
    r = zsp_verify(buf, hello_zsp_len, hello_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_ERR_HASH, "tampered hash field rejected");

    /* wrong root key -> sig invalid */
    memcpy(buf, hello_zsp, hello_zsp_len);
    uint8_t wrongkey[32];
    memcpy(wrongkey, hello_root_pubkey, 32);
    wrongkey[0] ^= 0x01;
    r = zsp_verify(buf, hello_zsp_len, wrongkey, &pl, &pl_len);
    CHECK(r == ZSP_ERR_SIG, "wrong root key rejected");

    /* bad magic */
    memcpy(buf, hello_zsp, hello_zsp_len);
    buf[0] = 'X';
    r = zsp_verify(buf, hello_zsp_len, hello_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_ERR_MAGIC, "bad magic rejected");

    /* truncated buffer */
    r = zsp_verify(hello_zsp, 50, hello_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_ERR_SHORT, "truncated package rejected");

    printf("\n%s: %d failure(s)\n", failures ? "*** FAILED ***" : "ALL PASS",
           failures);
    return failures ? 1 : 0;
}
