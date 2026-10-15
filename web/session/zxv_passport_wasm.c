/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_passport_wasm.c — the browser's door to the passport
 * (kernel/src/passport): the same seed, the same identity, the same DID as
 * on every other device, rebuilt inside the page with no server. Built to
 * WebAssembly by tools/web/build_web_root.sh; session.js runs it in a
 * worker so the page keeps drawing while the keys are built.
 *
 * The seed never leaves this module: the page writes what the person typed
 * into IN, each call reads it and zeroes it, and only the DID (public) comes
 * back out. pw_wipe zeroes everything the module holds.
 *
 * Exports (integers and pointers into this module's memory):
 *   pw_in()                  IN: where the page writes its input (PW_IN_MAX)
 *   pw_out()                 OUT: where results are written (NUL-terminated)
 *   pw_seed_new()            IN[0..32) = 32 random bytes from the page's
 *                            CSPRNG -> OUT = the seed as text; its length
 *   pw_seed_text(n)          IN[0..n) = seed text -> the seed; PP status
 *   pw_kdf_memory()          reserve the 64 MiB Argon2id needs; 0 / -1
 *   pw_seed_pass(np, nh)     IN = passphrase (np bytes) then handle (nh
 *                            bytes) -> the seed through Argon2id v1; status
 *   pw_hydrate()             the seed -> the MATRIX root identity; OUT = DID
 *                            text; its length, or a PP status (< 0)
 *   pw_wipe()                zero the seed, the identity and the scratch
 *
 * Every call checks its arguments before it writes anything; a refused call
 * leaves the module as it was apart from wiping IN. No allocation except
 * the one memory.grow for Argon2id's blocks, which is kept and reused.
 *
 * PW_HOST builds the same file natively for its unit test
 * (web/session/test_passport_wasm.c), with a static block for the scratch.
 */
#include "passport.h"

#if defined(PW_HOST)
#    include <string.h>
#    define EXPORT(name)
#else
#    define EXPORT(name) __attribute__((export_name(#name)))
#endif

#define PW_IN_MAX     (PP_PASS_MAX + PP_HANDLE_MAX + 8u)
#define PW_OUT_MAX    128u
#define PW_KDF_BLOCKS PP_KDF_M_KIB /* 1 KiB blocks */

static uint8_t IN[PW_IN_MAX];
static char OUT[PW_OUT_MAX];
static uint8_t SEED[PP_SEED];
static int HAVE_SEED;
static pp_passport_t PASS;
static uint64_t *KDF_MEM;

#if !defined(PW_HOST)
/* the freestanding C library pieces the compiler and the vendored PQ code
 * call for (declared in web/session/libc/string.h) */
void *memcpy(void *d, const void *s, unsigned long n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned long n)
{
    uint8_t *a = d;
    while (n--) *a++ = (uint8_t) c;
    return d;
}
void *memmove(void *d, const void *s, unsigned long n)
{
    uint8_t *a = d;
    const uint8_t *b = s;
    if (a < b)
        while (n--) *a++ = *b++;
    else
        while (n--) a[n] = b[n];
    return d;
}
int memcmp(const void *x, const void *y, unsigned long n)
{
    const uint8_t *a = x, *b = y;
    for (; n; n--, a++, b++)
        if (*a != *b) return *a < *b ? -1 : 1;
    return 0;
}
#endif

/* Zero n bytes so the compiler cannot drop the stores. */
static void wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = p;
    while (n--) *v++ = 0;
}

static void seed_drop(void)
{
    wipe(SEED, sizeof SEED);
    HAVE_SEED = 0;
}

EXPORT(pw_in) uint8_t *pw_in(void)
{
    return IN;
}

EXPORT(pw_out) char *pw_out(void)
{
    return OUT;
}

EXPORT(pw_in_max) uint32_t pw_in_max(void)
{
    return PW_IN_MAX;
}

EXPORT(pw_seed_new) int32_t pw_seed_new(void)
{
    char text[PP_SEED_TEXT];
    pp_status_t r = pp_seed_to_text(IN, text);
    wipe(IN, PP_SEED);
    if (r != PP_OK) return (int32_t) r;
    uint32_t n = 0;
    while (n < PP_SEED_TEXT - 1u && text[n]) n++;
    for (uint32_t i = 0; i <= n; i++) OUT[i] = text[i];
    wipe(text, sizeof text);
    return (int32_t) n;
}

EXPORT(pw_seed_text) int32_t pw_seed_text(uint32_t n)
{
    if (n == 0 || n > PW_IN_MAX) {
        wipe(IN, PW_IN_MAX);
        return PP_ERR_ARG;
    }
    uint8_t s[PP_SEED];
    pp_status_t r = pp_seed_from_text((const char *) IN, n, s);
    wipe(IN, PW_IN_MAX);
    if (r != PP_OK) {
        wipe(s, sizeof s);
        return (int32_t) r;
    }
    seed_drop();
    for (uint32_t i = 0; i < PP_SEED; i++) SEED[i] = s[i];
    wipe(s, sizeof s);
    HAVE_SEED = 1;
    return PP_OK;
}

EXPORT(pw_kdf_memory) int32_t pw_kdf_memory(void)
{
    if (KDF_MEM) return 0;
#if defined(PW_HOST)
    static uint64_t block[PW_KDF_BLOCKS * (ID_ARGON2_BLOCK / 8u)];
    KDF_MEM = block;
#else
    /* 64 KiB pages; the new pages start where the memory used to end */
    const uint32_t pages = (PW_KDF_BLOCKS * ID_ARGON2_BLOCK) / 65536u;
    int32_t old = __builtin_wasm_memory_grow(0, pages);
    if (old < 0) return -1;
    KDF_MEM = (uint64_t *) (uintptr_t) ((uint32_t) old * 65536u);
#endif
    return 0;
}

EXPORT(pw_seed_pass) int32_t pw_seed_pass(uint32_t np, uint32_t nh)
{
    if (np > PP_PASS_MAX || nh == 0 || nh > PP_HANDLE_MAX || np + nh > PW_IN_MAX) {
        wipe(IN, PW_IN_MAX);
        return PP_ERR_ARG;
    }
    if (np < PP_PASS_MIN) {
        wipe(IN, PW_IN_MAX);
        return PP_ERR_WEAK;
    }
    if (!KDF_MEM) {
        wipe(IN, PW_IN_MAX);
        return PP_ERR_KDF;
    }
    id_kdf_params_t kp;
    pp_kdf_v1(&kp);
    uint8_t s[PP_SEED];
    pp_status_t r = pp_seed_from_passphrase(&kp, IN, np, IN + np, nh, KDF_MEM, PW_KDF_BLOCKS, s);
    wipe(IN, PW_IN_MAX);
    wipe(KDF_MEM, PW_KDF_BLOCKS * ID_ARGON2_BLOCK);
    if (r != PP_OK) {
        wipe(s, sizeof s);
        return (int32_t) r;
    }
    seed_drop();
    for (uint32_t i = 0; i < PP_SEED; i++) SEED[i] = s[i];
    wipe(s, sizeof s);
    HAVE_SEED = 1;
    return PP_OK;
}

EXPORT(pw_hydrate) int32_t pw_hydrate(void)
{
    if (!HAVE_SEED) return PP_ERR_STATE;
    pp_wipe(&PASS);
    pp_status_t r = pp_hydrate(&PASS, PQM_LEVEL_MATRIX, SEED);
    seed_drop();
    if (r != PP_OK) return (int32_t) r;
    char did[ID_DID_CHARS];
    id_did_string(PASS.root.did, did);
    uint32_t n = 0;
    while (n < ID_DID_CHARS - 1u && did[n]) n++;
    for (uint32_t i = 0; i <= n; i++) OUT[i] = did[i];
    return (int32_t) n;
}

EXPORT(pw_wipe) void pw_wipe(void)
{
    seed_drop();
    pp_wipe(&PASS);
    wipe(IN, PW_IN_MAX);
    wipe(OUT, PW_OUT_MAX);
    if (KDF_MEM) wipe(KDF_MEM, PW_KDF_BLOCKS * ID_ARGON2_BLOCK);
}
