/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ba.c — host tests for boot-state attestation (ba.h).
 *
 * Known answers: RFC 4648 section 10 base32 vectors; the CIDv1 raw sha2-256
 * ids of "" and "hello world\n" as published by IPFS (bafkreihdwd... and
 * bafkreifjjc...); the B1 chain values were computed independently with
 * Python hashlib when this test was written. Every BA_E_* verification
 * error is provoked on purpose. */
#include "ba.h"
#include "../robin_debanks/sha256.h"
#include <stdio.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (c)                                                                                     \
            printf("[PASS] %s\n", msg);                                                            \
        else {                                                                                     \
            printf("[FAIL] %s (line %d)\n", msg, __LINE__);                                        \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void hex(const char *h, uint8_t *out, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned v;
        sscanf(h + 2 * i, "%2x", &v);
        out[i] = (uint8_t) v;
    }
}

static uint8_t dev_pk[BA_PK_LEN], dev_sk[BA_SK_LEN];
static uint8_t rel_pk[BA_PK_LEN], rel_sk[BA_SK_LEN];
static uint8_t oth_pk[BA_PK_LEN], oth_sk[BA_SK_LEN];
static uint8_t dev_kh[BA_KEYHASH_LEN];
static const char *STAGES[4] = {"efi-stub", "kernel", "initrd", "config"};

static void stage_digest(const char *name, uint8_t d[32])
{
    char buf[64];
    int n = snprintf(buf, sizeof buf, "stage:%s", name);
    sha256((const uint8_t *) buf, (size_t) n, d);
}

static void make_log(ba_log_t *log, const char *const *names, unsigned n)
{
    ba_log_init(log);
    for (unsigned i = 0; i < n; i++) {
        char buf[64];
        int l = snprintf(buf, sizeof buf, "stage:%s", names[i]);
        ba_measure(log, names[i], (const uint8_t *) buf, (size_t) l);
    }
}

static uint8_t manifest[BA_MANIFEST_MAX];
static size_t manifest_len;
static uint8_t manifest_id[BA_ID_LEN];

static ba_err_t build_manifest(uint32_t ver, const uint8_t *sk, const uint8_t *pk_for_hash,
                               uint8_t *out, size_t *len)
{
    ba_manifest_t m;
    memset(&m, 0, sizeof m);
    m.image_version = ver;
    m.issued_at = 1760000000u;
    ba_key_hash(pk_for_hash, m.release_key_hash);
    m.n = 4;
    for (unsigned i = 0; i < 4; i++) {
        m.stage[i].name_len = (uint8_t) strlen(STAGES[i]);
        memcpy(m.stage[i].name, STAGES[i], m.stage[i].name_len);
        stage_digest(STAGES[i], m.stage[i].digest);
    }
    ba_soft_key_t k = {sk, NULL, false};
    return ba_manifest_build(&m, ba_soft_signer, &k, out, BA_MANIFEST_MAX, len);
}

static size_t quote_of(const ba_log_t *log, const uint8_t nonce[32], const uint8_t *sk, uint8_t *q)
{
    ba_soft_key_t k = {sk, NULL, false};
    size_t len = 0;
    if (ba_quote(log, nonce, manifest_id, dev_kh, ba_soft_signer, &k, q, BA_QUOTE_MAX, &len) !=
        BA_OK)
        return 0;
    return len;
}

static unsigned hw_calls;
static bool hw_ctx_ok;
static bool fake_hw_signer(void *user, const uint8_t *msg, size_t len, const uint8_t *ctx,
                           size_t ctx_len, uint8_t sig[BA_SIG_LEN])
{
    hw_calls++;
    hw_ctx_ok = ctx_len == 13 && memcmp(ctx, "ZXV-BA1-QUOTE", 13) == 0;
    return ba_soft_signer(user, msg, len, ctx, ctx_len, sig);
}

static bool failing_signer(void *user, const uint8_t *msg, size_t len, const uint8_t *ctx,
                           size_t ctx_len, uint8_t sig[BA_SIG_LEN])
{
    (void) user;
    (void) msg;
    (void) len;
    (void) ctx;
    (void) ctx_len;
    memset(sig, 0xAA, BA_SIG_LEN);
    return false;
}

static ba_err_t verify(ba_verifier_t *v, const uint8_t *q, size_t qlen, ba_result_t *r)
{
    return ba_verify_quote(v, q, qlen, dev_pk, manifest, manifest_len, rel_pk, r);
}

int main(void)
{
    static uint8_t q[BA_QUOTE_MAX], q2[BA_QUOTE_MAX];
    static ba_result_t r;
    char txt[128];
    uint8_t d[32], e[32];

    /* ---- base32 (RFC 4648 section 10) ---- */
    {
        static const char *in[7] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
        static const char *out[7] = {"",        "my",       "mzxq",      "mzxw6",
                                     "mzxw6yq", "mzxw6ytb", "mzxw6ytboi"};
        int ok = 1;
        for (int i = 0; i < 7; i++) {
            size_t n = ba_base32((const uint8_t *) in[i], strlen(in[i]), txt, sizeof txt);
            if (n != strlen(out[i]) || strcmp(txt, out[i]) != 0) ok = 0;
        }
        CHECK(ok, "base32 matches all seven RFC 4648 vectors");
        CHECK(ba_base32((const uint8_t *) "foobar", 6, txt, 10) == 0 &&
                  ba_base32((const uint8_t *) "foobar", 6, txt, 11) == 10,
              "base32 refuses a buffer one byte short, accepts the exact size");
    }

    /* ---- CID ids (published IPFS values) ---- */
    {
        uint8_t id[BA_ID_LEN];
        char t[BA_ID_TEXT_LEN + 1];
        ba_cid((const uint8_t *) "", 0, id);
        ba_id_text(id, t);
        CHECK(strcmp(t, "bafkreihdwdcefgh4dqkjv67uzcmw7ojee6xedzdetojuzjevtenxquvyku") == 0,
              "CID of empty block = bafkreihdwd... (IPFS)");
        ba_cid((const uint8_t *) "hello world\n", 12, id);
        ba_id_text(id, t);
        CHECK(strcmp(t, "bafkreifjjcie6lypi6ny7amxnfftagclbuxndqonfipmb64f2km2devei4") == 0 &&
                  strlen(t) == BA_ID_TEXT_LEN,
              "CID of \"hello world\\n\" = bafkreifjjc... (IPFS), 59 chars");
    }

    /* ---- measuring: chain KAT (rule B1) ---- */
    {
        static const char *want[4] = {
            "66b9ea1c7d9a365817c8f90a42b88898be32f6f987f8f2f54c13e6cdb0533323",
            "5bac07b46633b12d9b70a19135be42974cc47c05e7dcb87604ce9a59da2ca07d",
            "1a8c2aa865aef2c89172d857f972d65a7a874c4bf9cf66db48ef9fae4fcc17b1",
            "690d471cb6d5d3fb7e6cb99efbabd6c2c9239dcf9962b52b685031be3329e9ff",
        };
        ba_log_t log;
        ba_log_init(&log);
        int ok = 1;
        uint8_t zero[32] = {0};
        ok &= memcmp(log.chain, zero, 32) == 0;
        for (unsigned i = 0; i < 4; i++) {
            char buf[64];
            int l = snprintf(buf, sizeof buf, "stage:%s", STAGES[i]);
            ok &= ba_measure(&log, STAGES[i], (const uint8_t *) buf, (size_t) l) == BA_OK;
            hex(want[i], d, 32);
            ok &= memcmp(log.chain, d, 32) == 0;
        }
        CHECK(ok && log.n == 4, "chain after each of 4 stages matches the hashlib values");
        ba_replay(log.ev, log.n, e);
        CHECK(memcmp(e, log.chain, 32) == 0, "replaying the event log reproduces the chain");

        ba_log_t l2;
        ba_log_init(&l2);
        stage_digest("kernel", d);
        ba_extend(&l2, "kernel", d);
        ba_log_t l3;
        ba_log_init(&l3);
        ba_extend(&l3, "kernel2", d);
        CHECK(memcmp(l2.chain, l3.chain, 32) != 0,
              "same digest under another name gives another chain");
        stage_digest("efi-stub", e);
        ba_log_t a, b;
        ba_log_init(&a);
        ba_log_init(&b);
        ba_extend(&a, "x", d);
        ba_extend(&a, "y", e);
        ba_extend(&b, "y", e);
        ba_extend(&b, "x", d);
        CHECK(memcmp(a.chain, b.chain, 32) != 0, "same stages in another order give another chain");
    }

    /* ---- name rules and capacity ---- */
    {
        ba_log_t log;
        ba_log_init(&log);
        memset(d, 1, 32);
        CHECK(ba_extend(&log, "", d) == BA_E_NAME && ba_extend(&log, NULL, d) == BA_E_NAME &&
                  ba_extend(&log, "Kernel", d) == BA_E_NAME &&
                  ba_extend(&log, "a b", d) == BA_E_NAME &&
                  ba_extend(&log, "abcdefghijklmnopqrstuvwxyz01234x", d) == BA_E_NAME,
              "empty, NULL, upper-case, space and 32-char names refused");
        CHECK(ba_extend(&log, "abcdefghijklmnopqrstuvwxyz01234", d) == BA_OK &&
                  ba_extend(&log, "a.b_c-9", d) == BA_OK && log.n == 2,
              "31-char name and [a-z0-9._-] accepted");
        CHECK(ba_extend(NULL, "x", d) == BA_E_ARG && ba_extend(&log, "x", NULL) == BA_E_ARG &&
                  ba_measure(&log, "x", NULL, 1) == BA_E_ARG,
              "NULL log, digest or data refused");
        while (log.n < BA_MAX_EVENTS) ba_extend(&log, "fill", d);
        uint8_t before[32];
        memcpy(before, log.chain, 32);
        CHECK(ba_extend(&log, "more", d) == BA_E_FULL && log.n == BA_MAX_EVENTS &&
                  memcmp(before, log.chain, 32) == 0,
              "17th extend refused with BA_E_FULL, chain unchanged");
    }

    /* ---- keys and manifest ---- */
    uint8_t seed[32];
    memset(seed, 0x11, 32);
    ba_soft_keygen(seed, dev_pk, dev_sk);
    memset(seed, 0x22, 32);
    ba_soft_keygen(seed, rel_pk, rel_sk);
    memset(seed, 0x33, 32);
    ba_soft_keygen(seed, oth_pk, oth_sk);
    ba_key_hash(dev_pk, dev_kh);

    CHECK(build_manifest(7, rel_sk, rel_pk, manifest, &manifest_len) == BA_OK &&
              manifest_len == BA_MANIFEST_HDR + 4 * 33 + 8 + 6 + 6 + 6 + BA_SIG_LEN,
          "manifest builds with the exact expected length");
    ba_cid(manifest, manifest_len, manifest_id);
    {
        static uint8_t m2[BA_MANIFEST_MAX];
        size_t l2 = 0;
        build_manifest(7, rel_sk, rel_pk, m2, &l2);
        CHECK(l2 == manifest_len && memcmp(m2, manifest, l2) == 0,
              "deterministic signing: same manifest, same bytes, same id");
        ba_manifest_t m;
        CHECK(ba_manifest_verify(manifest, manifest_len, rel_pk, &m) == BA_OK && m.n == 4 &&
                  m.image_version == 7 && m.issued_at == 1760000000u &&
                  strcmp(m.stage[1].name, "kernel") == 0,
              "manifest verifies and parses back its fields");
        char t[BA_ID_TEXT_LEN + 1];
        ba_id_text(manifest_id, t);
        CHECK(strncmp(t, "bafkrei", 7) == 0 && strlen(t) == 59,
              "manifest id has the bafkrei... form");
        CHECK(ba_manifest_verify(manifest, manifest_len, oth_pk, &m) == BA_E_MANIFEST_KEY,
              "manifest checked against another release key: BA_E_MANIFEST_KEY");
        memcpy(m2, manifest, manifest_len);
        m2[BA_MANIFEST_HDR + 10] ^= 1;
        CHECK(ba_manifest_verify(m2, manifest_len, rel_pk, &m) == BA_E_MANIFEST_SIG,
              "one flipped digest bit in the manifest: BA_E_MANIFEST_SIG");
        CHECK(ba_manifest_parse(manifest, manifest_len - 1, &m) == BA_E_MANIFEST_FORMAT &&
                  ba_manifest_parse(manifest, BA_MANIFEST_HDR, &m) == BA_E_MANIFEST_FORMAT,
              "truncated manifest: BA_E_MANIFEST_FORMAT");
        memcpy(m2, manifest, manifest_len);
        m2[3] = '2';
        int bad = ba_manifest_parse(m2, manifest_len, &m) == BA_E_MANIFEST_FORMAT;
        memcpy(m2, manifest, manifest_len);
        m2[12] = 1; /* reserved */
        bad &= ba_manifest_parse(m2, manifest_len, &m) == BA_E_MANIFEST_FORMAT;
        memcpy(m2, manifest, manifest_len);
        m2[5] = 3; /* stage count says 3, 4 present */
        bad &= ba_manifest_parse(m2, manifest_len, &m) == BA_E_MANIFEST_FORMAT;
        memcpy(m2, manifest, manifest_len);
        m2[BA_MANIFEST_HDR + 1] = 'E'; /* upper-case name */
        bad &= ba_manifest_parse(m2, manifest_len, &m) == BA_E_MANIFEST_FORMAT;
        CHECK(bad, "bad magic, nonzero reserved, wrong count, bad name: BA_E_MANIFEST_FORMAT");

        ba_manifest_t dup;
        memset(&dup, 0, sizeof dup);
        dup.n = 2;
        dup.stage[0].name_len = dup.stage[1].name_len = 1;
        dup.stage[0].name[0] = dup.stage[1].name[0] = 'a';
        ba_soft_key_t k = {rel_sk, NULL, false};
        size_t l3;
        CHECK(ba_manifest_build(&dup, ba_soft_signer, &k, m2, BA_MANIFEST_MAX, &l3) == BA_E_NAME,
              "duplicate stage names refused at build");
        dup.n = 0;
        CHECK(ba_manifest_build(&dup, ba_soft_signer, &k, m2, BA_MANIFEST_MAX, &l3) == BA_E_NAME,
              "empty manifest refused at build");
    }

    /* ---- quote: success, then every failure ---- */
    ba_verifier_t v;
    ba_verifier_init(&v, 7);
    uint8_t nonce[32];
    memset(nonce, 0x5A, 32);
    ba_log_t good;
    make_log(&good, STAGES, 4);

    {
        ba_soft_key_t k = {dev_sk, NULL, false};
        size_t len = 0;
        hw_calls = 0;
        CHECK(ba_quote(&good, nonce, manifest_id, dev_kh, fake_hw_signer, &k, q, BA_QUOTE_MAX,
                       &len) == BA_OK &&
                  hw_calls == 1 && hw_ctx_ok,
              "quote signed through a plug-in signer callback with context ZXV-BA1-QUOTE");
        ba_verifier_issue(&v, nonce);
        CHECK(verify(&v, q, len, &r) == BA_OK && r.n_events == 4 &&
                  memcmp(r.chain, good.chain, 32) == 0 && r.manifest.image_version == 7,
              "good quote verifies: signature, chain, nonce, all four stages");
        CHECK(verify(&v, q, len, &r) == BA_E_NONCE_REPLAY,
              "same quote presented again: BA_E_NONCE_REPLAY");
    }
    {
        uint8_t n2[32];
        memset(n2, 0x61, 32);
        size_t len = quote_of(&good, n2, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_NONCE_UNKNOWN,
              "nonce the verifier never issued: BA_E_NONCE_UNKNOWN");
        ba_verifier_issue(&v, n2);

        memcpy(q2, q, len);
        q2[len - BA_SIG_LEN - 5] ^= 1; /* last event's digest */
        CHECK(verify(&v, q2, len, &r) == BA_E_SIG, "tampered event-log byte: BA_E_SIG");
        memcpy(q2, q, len);
        q2[8] ^= 1;
        CHECK(verify(&v, q2, len, &r) == BA_E_SIG, "tampered nonce byte: BA_E_SIG");
        memcpy(q2, q, len);
        q2[len - 1] ^= 1;
        CHECK(verify(&v, q2, len, &r) == BA_E_SIG, "tampered signature byte: BA_E_SIG");

        CHECK(ba_verify_quote(&v, q, len, oth_pk, manifest, manifest_len, rel_pk, &r) ==
                  BA_E_WRONG_KEY,
              "checked against another device key: BA_E_WRONG_KEY");
        size_t l2 = quote_of(&good, n2, oth_sk, q2); /* claims dev_kh, signed by other */
        CHECK(verify(&v, q2, l2, &r) == BA_E_SIG,
              "signed by another key while naming this device: BA_E_SIG");
        CHECK(verify(&v, q, len, &r) == BA_OK, "the untampered quote still verifies afterwards");
    }
    {
        uint8_t n3[32];
        memset(n3, 0x62, 32);
        ba_verifier_issue(&v, n3);
        ba_log_t lie = good;
        lie.chain[0] ^= 0x80; /* device signs a log that does not give its chain */
        size_t len = quote_of(&lie, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_CHAIN, "signed log that does not replay: BA_E_CHAIN");

        static const char *unk[4] = {"efi-stub", "kernel", "rootkit", "config"};
        ba_log_t l;
        make_log(&l, unk, 4);
        len = quote_of(&l, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_STAGE_UNKNOWN && r.bad_index == 2,
              "unknown stage: BA_E_STAGE_UNKNOWN at index 2");

        make_log(&l, STAGES, 3);
        len = quote_of(&l, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_STAGE_MISSING && r.bad_index == 3,
              "last stage missing: BA_E_STAGE_MISSING at index 3");

        static const char *swp[4] = {"efi-stub", "initrd", "kernel", "config"};
        make_log(&l, swp, 4);
        len = quote_of(&l, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_STAGE_ORDER && r.bad_index == 1,
              "two stages swapped: BA_E_STAGE_ORDER at index 1");

        static const char *dup[5] = {"efi-stub", "kernel", "initrd", "config", "kernel"};
        make_log(&l, dup, 5);
        len = quote_of(&l, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_STAGE_ORDER && r.bad_index == 4,
              "a known stage measured twice: BA_E_STAGE_ORDER");

        make_log(&l, STAGES, 1);
        uint8_t wrong[32];
        memset(wrong, 0xEE, 32);
        ba_extend(&l, "kernel", wrong);
        ba_measure(&l, "initrd", (const uint8_t *) "stage:initrd", 12);
        ba_measure(&l, "config", (const uint8_t *) "stage:config", 12);
        len = quote_of(&l, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_E_STAGE_DIGEST && r.bad_index == 1,
              "modified kernel: BA_E_STAGE_DIGEST at index 1");

        len = quote_of(&good, n3, dev_sk, q);
        CHECK(verify(&v, q, len, &r) == BA_OK,
              "nonce stays usable after failed quotes, then is consumed");
    }
    {
        uint8_t n4[32];
        memset(n4, 0x63, 32);
        ba_verifier_issue(&v, n4);
        size_t len = quote_of(&good, n4, dev_sk, q);
        static uint8_t m8[BA_MANIFEST_MAX];
        size_t m8len;
        build_manifest(8, rel_sk, rel_pk, m8, &m8len);
        CHECK(ba_verify_quote(&v, q, len, dev_pk, m8, m8len, rel_pk, &r) == BA_E_MANIFEST_ID,
              "quote names manifest v7, checked against v8: BA_E_MANIFEST_ID");
        ba_verifier_t strict;
        ba_verifier_init(&strict, 8);
        ba_verifier_issue(&strict, n4);
        CHECK(ba_verify_quote(&strict, q, len, dev_pk, manifest, manifest_len, rel_pk, &r) ==
                  BA_E_ROLLBACK,
              "verifier requires image >= 8, manifest is 7: BA_E_ROLLBACK");
        static uint8_t mo[BA_MANIFEST_MAX];
        size_t molen;
        build_manifest(7, oth_sk, rel_pk, mo, &molen); /* names rel key, signed by other */
        CHECK(ba_verify_quote(&v, q, len, dev_pk, mo, molen, rel_pk, &r) == BA_E_MANIFEST_SIG,
              "manifest naming the release key but signed by another: BA_E_MANIFEST_SIG");
        CHECK(ba_verify_quote(&v, q, len, dev_pk, manifest, 10, rel_pk, &r) == BA_E_MANIFEST_FORMAT,
              "truncated manifest at verify time: BA_E_MANIFEST_FORMAT");

        memcpy(q2, q, len);
        CHECK(verify(&v, q2, len - 1, &r) == BA_E_FORMAT, "quote one byte short: BA_E_FORMAT");
        q2[4] = 2;
        CHECK(verify(&v, q2, len, &r) == BA_E_FORMAT, "quote version 2: BA_E_FORMAT");
        memcpy(q2, q, len);
        q2[5] = 3;
        CHECK(verify(&v, q2, len, &r) == BA_E_FORMAT, "quote event count disagrees: BA_E_FORMAT");
        memcpy(q2, q, len);
        q2[6] = 1;
        CHECK(verify(&v, q2, len, &r) == BA_E_FORMAT, "quote reserved byte set: BA_E_FORMAT");
        CHECK(verify(&v, q2, 10, &r) == BA_E_FORMAT &&
                  verify(&v, q, BA_QUOTE_MAX + 1, &r) == BA_E_FORMAT,
              "quote far too short or too long: BA_E_FORMAT");
    }
    {
        /* empty log: a valid quote format that must fail as missing stages */
        uint8_t n5[32];
        memset(n5, 0x64, 32);
        ba_verifier_issue(&v, n5);
        ba_log_t empty;
        ba_log_init(&empty);
        size_t len = quote_of(&empty, n5, dev_sk, q);
        CHECK(len == BA_QUOTE_HDR + BA_SIG_LEN && verify(&v, q, len, &r) == BA_E_STAGE_MISSING &&
                  r.bad_index == 0,
              "empty event log: BA_E_STAGE_MISSING at index 0");
    }
    {
        /* nonce ring: the 17th issue forgets the first */
        ba_verifier_t w;
        ba_verifier_init(&w, 0);
        uint8_t first[32];
        memset(first, 0x70, 32);
        ba_verifier_issue(&w, first);
        for (unsigned i = 1; i <= BA_NONCE_SLOTS; i++) {
            uint8_t nn[32];
            memset(nn, (int) (0x70 + i), 32);
            ba_verifier_issue(&w, nn);
        }
        size_t len = quote_of(&good, first, dev_sk, q);
        CHECK(ba_verify_quote(&w, q, len, dev_pk, manifest, manifest_len, rel_pk, &r) ==
                  BA_E_NONCE_UNKNOWN,
              "nonce pushed out of the 16-slot ring: BA_E_NONCE_UNKNOWN");
        uint8_t last[32];
        memset(last, 0x70 + BA_NONCE_SLOTS, 32);
        len = quote_of(&good, last, dev_sk, q);
        CHECK(ba_verify_quote(&w, q, len, dev_pk, manifest, manifest_len, rel_pk, &r) == BA_OK,
              "most recent nonce in the ring still verifies");
    }
    {
        size_t len = 99;
        ba_soft_key_t k = {dev_sk, NULL, false};
        memset(q, 0x55, BA_QUOTE_HDR + 4 * 40 + BA_SIG_LEN);
        CHECK(ba_quote(&good, nonce, manifest_id, dev_kh, failing_signer, NULL, q, BA_QUOTE_MAX,
                       &len) == BA_E_SIGNER &&
                  len == 99 && q[0] == 0 && q[BA_QUOTE_HDR + 10] == 0,
              "signer failure: BA_E_SIGNER, output wiped, length untouched");
        size_t need = BA_QUOTE_HDR + 4 * 33 + 8 + 6 + 6 + 6 + BA_SIG_LEN;
        CHECK(ba_quote(&good, nonce, manifest_id, dev_kh, ba_soft_signer, &k, q, need - 1, &len) ==
                      BA_E_SPACE &&
                  ba_quote(&good, nonce, manifest_id, dev_kh, ba_soft_signer, &k, q, need, &len) ==
                      BA_OK &&
                  len == need,
              "quote buffer one byte short: BA_E_SPACE; exact size works");
        CHECK(ba_quote(&good, nonce, manifest_id, dev_kh, NULL, &k, q, BA_QUOTE_MAX, &len) ==
                      BA_E_ARG &&
                  ba_verify_quote(NULL, q, len, dev_pk, manifest, manifest_len, rel_pk, &r) ==
                      BA_E_ARG,
              "NULL signer or verifier: BA_E_ARG");
        uint8_t sig[BA_SIG_LEN];
        ba_soft_key_t nok = {NULL, NULL, false};
        CHECK(!ba_soft_signer(&nok, q, 1, NULL, 0, sig) &&
                  !ba_soft_signer(NULL, q, 1, NULL, 0, sig) && !k.hardware_backed,
              "soft signer refuses a missing key and is flagged not hardware-backed");
    }
    {
        int ok = 1;
        for (int i = 0; i < BA_E_COUNT_; i++)
            for (int j = 0; j < i; j++)
                if (strcmp(ba_strerror((ba_err_t) i), ba_strerror((ba_err_t) j)) == 0) ok = 0;
        CHECK(ok && strcmp(ba_strerror(BA_E_CHAIN), "chain") == 0 &&
                  strcmp(ba_strerror(BA_E_COUNT_), "unknown") == 0,
              "every error has its own name");
    }

    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "OK", checks, fails);
    return fails ? 1 : 0;
}
