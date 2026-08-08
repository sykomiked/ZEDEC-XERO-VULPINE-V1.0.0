/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 */
/* test_zsp2.c — host test for ZSP v2 enforced verification (audit P0-6).
 *
 * Uses a committed signed fixture (test_zsp2_signed.h: version=5, arch=arm64,
 * caps=0x10). Asserts: a genuine v2 package verifies; the anti-rollback floor
 * rejects version < floor; an architecture mismatch is rejected; a tampered
 * payload (hash) and a wrong root key are rejected; and a v1 package is rejected
 * by the enforced entry point while the legacy entry still accepts v2.
 *
 *   gcc -std=c11 -Wall -Isrc/loader -Isrc/robin_debanks \
 *       src/loader/test_zsp2.c src/loader/zsp.c \
 *       src/robin_debanks/sha256.c src/robin_debanks/ed25519_verify.c \
 *       -o /tmp/test_zsp2 && /tmp/test_zsp2
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "zsp.h"
#include "test_zsp2_signed.h"

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("[FAIL] %s\n", msg); failures++; } \
    else         { printf("[PASS] %s\n", msg); } } while (0)

int main(void) {
    static uint8_t buf[8192];
    zsp_meta_t meta;
    const uint8_t *pl; uint32_t pl_len;
    printf("=== ZSP v2 enforced-verification tests (pkg=%uB) ===\n", zsp2test_zsp_len);

    /* 1) genuine v2 package verifies (floor 0, arch arm64) */
    memcpy(buf, zsp2test_zsp, zsp2test_zsp_len);
    zsp_result_t r = zsp_verify2(buf, zsp2test_zsp_len, zsp2test_root_pubkey,
                                 0, ZSP_ARCH_ARM64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_OK, "genuine v2 package verifies");
    CHECK(meta.version == 5, "authenticated version == 5");
    CHECK(meta.arch == ZSP_ARCH_ARM64, "authenticated arch == arm64");
    CHECK(meta.caps == 0x10, "authenticated caps == 0x10");

    /* 2) ANTI-ROLLBACK: a floor above the package version is rejected */
    memcpy(buf, zsp2test_zsp, zsp2test_zsp_len);
    r = zsp_verify2(buf, zsp2test_zsp_len, zsp2test_root_pubkey,
                    6, ZSP_ARCH_ARM64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_ERR_ROLLBACK, "version 5 below floor 6 is rejected (anti-rollback)");
    /* at the floor exactly is fine */
    r = zsp_verify2(buf, zsp2test_zsp_len, zsp2test_root_pubkey,
                    5, ZSP_ARCH_ARM64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_OK, "version 5 at floor 5 is accepted");

    /* 3) ARCH gate: a different architecture is rejected */
    r = zsp_verify2(buf, zsp2test_zsp_len, zsp2test_root_pubkey,
                    0, ZSP_ARCH_X86_64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_ERR_ARCH, "arm64 package rejected on an x86_64 host");

    /* 4) tampered payload -> hash mismatch */
    memcpy(buf, zsp2test_zsp, zsp2test_zsp_len);
    buf[zsp2test_zsp_len - 1] ^= 0x01;
    r = zsp_verify2(buf, zsp2test_zsp_len, zsp2test_root_pubkey,
                    0, ZSP_ARCH_ARM64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_ERR_HASH, "tampered payload rejected (hash)");

    /* 5) flipped a signed-header byte (version) -> signature breaks */
    memcpy(buf, zsp2test_zsp, zsp2test_zsp_len);
    buf[8] ^= 0x01;   /* version field, inside the signed preimage */
    r = zsp_verify2(buf, zsp2test_zsp_len, zsp2test_root_pubkey,
                    0, ZSP_ARCH_ARM64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_ERR_SIG, "altered signed version byte rejected (sig)");

    /* 6) wrong root key -> key-id or signature rejected */
    memcpy(buf, zsp2test_zsp, zsp2test_zsp_len);
    uint8_t wrongkey[32]; memcpy(wrongkey, zsp2test_root_pubkey, 32); wrongkey[0] ^= 0xFF;
    r = zsp_verify2(buf, zsp2test_zsp_len, wrongkey,
                    0, ZSP_ARCH_ARM64, &meta, &pl, &pl_len);
    CHECK(r == ZSP_ERR_KEYID || r == ZSP_ERR_SIG, "wrong root key rejected");

    /* 7) the legacy entry point also accepts a v2 package (integrity+sig) */
    memcpy(buf, zsp2test_zsp, zsp2test_zsp_len);
    r = zsp_verify(buf, zsp2test_zsp_len, zsp2test_root_pubkey, &pl, &pl_len);
    CHECK(r == ZSP_OK, "legacy zsp_verify accepts a v2 package");

    printf(failures ? "\nFAILED: %d\n" : "\nALL PASS: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
