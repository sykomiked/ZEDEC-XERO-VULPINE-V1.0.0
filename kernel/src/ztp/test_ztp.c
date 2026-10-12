/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ztp.c — host tests for zero-touch provisioning (ztp.h).
 *
 * Built twice by mk/rot.mk: once as shipped (physical-presence reset OFF)
 * and once with -DZTP_PHYSICAL_RESET=1. Known answers (commitment, QR text)
 * were computed independently with Python hashlib/base64 when this test was
 * written. The claim QR is also encoded with qr.h to pin its symbol size. */
#include "ztp.h"
#include "../qr/qr.h"
#include "../robin_debanks/crypto_verify.h"
#include <stdio.h>
#include <string.h>

static int checks, fails;
#define CHECK(c, msg)                                                         \
    do {                                                                      \
        checks++;                                                             \
        if (c) printf("[PASS] %s\n", msg);                                    \
        else { printf("[FAIL] %s (line %d)\n", msg, __LINE__); fails++; }     \
    } while (0)

static uint8_t dev_pk[BA_PK_LEN], dev_sk[BA_SK_LEN];
static uint8_t mk_pk[BA_PK_LEN], mk_sk[BA_SK_LEN];
static uint8_t a_pk[BA_PK_LEN], a_sk[BA_SK_LEN];
static uint8_t b_pk[BA_PK_LEN], b_sk[BA_SK_LEN];
static uint8_t c_pk[BA_PK_LEN], c_sk[BA_SK_LEN];
static uint8_t msg[ZTP_MSG_LEN], msg2[ZTP_MSG_LEN];

static ztp_err_t mk(ztp_kind_t kind, const uint8_t id[16], uint32_t epoch, const uint8_t *npk,
                    const uint8_t *secret, const uint8_t *signer_sk, uint8_t *out)
{
    ba_soft_key_t k = {signer_sk, NULL, false};
    return ztp_msg_build(kind, id, epoch, 1760000000u, npk, secret, ba_soft_signer, &k, out);
}

static bool fail_signer(void *u, const uint8_t *m, size_t l, const uint8_t *c, size_t cl,
                        uint8_t sig[BA_SIG_LEN])
{
    (void) u; (void) m; (void) l; (void) c; (void) cl;
    sig[0] = 1;
    return false;
}

int main(void)
{
    static ztp_device_t d, d2;
    static ztp_profile_t prof, got;
    static uint8_t rec[ZTP_PROFILE_MAX], rec2[ZTP_PROFILE_MAX];
    static uint8_t man[BA_MANIFEST_MAX], quote[BA_QUOTE_MAX];
    uint8_t seed[32], ent[64], id[16], sec[32], sec2[32], id2[16];
    char qr[ZTP_QR_TEXT_LEN + 1];
    size_t rl = 0;

    memset(seed, 0x41, 32); ba_soft_keygen(seed, mk_pk, mk_sk);
    memset(seed, 0x42, 32); ba_soft_keygen(seed, a_pk, a_sk);
    memset(seed, 0x43, 32); ba_soft_keygen(seed, b_pk, b_sk);
    memset(seed, 0x44, 32); ba_soft_keygen(seed, c_pk, c_sk);

    /* ---- known answers ---- */
    {
        uint8_t kid[16], ksec[32], c[32], want[32];
        for (int i = 0; i < 16; i++) kid[i] = (uint8_t) i;
        for (int i = 0; i < 32; i++) ksec[i] = (uint8_t) (100 + i);
        ztp_commit(kid, ksec, c);
        static const char *h = "f94be8fab392f19f5a913d66edc8f7aa105fc94934b49e33b2dae38944db2c02";
        for (int i = 0; i < 32; i++) {
            unsigned v;
            sscanf(h + 2 * i, "%2x", &v);
            want[i] = (uint8_t) v;
        }
        CHECK(memcmp(c, want, 32) == 0, "claim commitment matches the hashlib SHA3-256 value");
        static uint8_t body[ZTP_MSG_MAC_OFF];
        uint8_t key[32], mac[32];
        for (int i = 0; i < 32; i++) key[i] = (uint8_t) i;
        for (unsigned i = 0; i < ZTP_MSG_MAC_OFF; i++) body[i] = (uint8_t) (i % 251u);
        crypto_hmac_sha256(body, ZTP_MSG_MAC_OFF, key, mac);
        static const char *hm = "55b133c74b8dbf79c45855f4aa1011d12ba2388cda69317ac965edbd41bb78e3";
        for (int i = 0; i < 32; i++) {
            unsigned v;
            sscanf(hm + 2 * i, "%2x", &v);
            want[i] = (uint8_t) v;
        }
        CHECK(memcmp(mac, want, 32) == 0,
              "HMAC-SHA256 over a full 2632-byte claim body matches Python hmac (no truncation)");
        memset(&d2, 0, sizeof d2);
        d2.state = ZTP_UNCLAIMED;
        memcpy(d2.device_key_hash, kid, 16);
        memcpy(d2.claim_secret, ksec, 32);
        CHECK(ztp_qr_text(&d2, qr) == ZTP_OK &&
                  strcmp(qr, "ZTP1:aaaqeayeaudaocajbifqydiob5sgkzthnbuwu23mnvxg64drojzxi5lwo54hs6t3pr6x474aqgbig") == 0,
              "QR text matches the Python base32 value");
        CHECK(ztp_qr_parse(qr, strlen(qr), id, sec) == ZTP_OK && memcmp(id, kid, 16) == 0 &&
                  memcmp(sec, ksec, 32) == 0,
              "QR text parses back to the same id and secret");
    }

    /* ---- first boot ---- */
    memset(&d, 0, sizeof d);
    for (int i = 0; i < 64; i++) ent[i] = (uint8_t) (7 * i + 3);
    {
        uint8_t zid[16] = {0}, s1[32];
        memset(s1, 1, 32);
        mk(ZTP_MSG_CLAIM, zid, 0, a_pk, s1, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_STATE && d.state == ZTP_FACTORY,
              "a factory-fresh device accepts no claim: ZTP_E_STATE");
    }
    CHECK(ztp_first_boot(&d, ent, dev_pk, dev_sk) == ZTP_OK && d.state == ZTP_UNCLAIMED &&
              d.epoch == 0 && memcmp(d.claim_secret, ent + 32, 32) == 0,
          "first boot: key pair made, secret installed, UNCLAIMED at epoch 0");
    {
        uint8_t kh[32];
        ba_key_hash(dev_pk, kh);
        ztp_device_id(&d, id);
        CHECK(memcmp(d.device_key_hash, kh, 32) == 0 && memcmp(id, kh, 16) == 0,
              "device id = first 16 bytes of SHA3-256(device public key)");
    }
    CHECK(ztp_first_boot(&d, ent, dev_pk, dev_sk) == ZTP_E_STATE &&
              ztp_device_init(&d, dev_pk, ent + 32) == ZTP_E_STATE,
          "a second first boot is refused");
    {
        uint8_t z[32] = {0};
        memset(&d2, 0, sizeof d2);
        CHECK(ztp_device_init(&d2, dev_pk, z) == ZTP_E_ARG && d2.state == ZTP_FACTORY,
              "an all-zero claim secret is refused");
    }

    /* ---- maker profile ---- */
    memset(&prof, 0, sizeof prof);
    memcpy(prof.device_key_hash, d.device_key_hash, 32);
    ba_key_hash(mk_pk, prof.maker_key_hash);
    {
        /* the golden manifest this unit ships with */
        ba_manifest_t m;
        memset(&m, 0, sizeof m);
        m.image_version = 3;
        ba_key_hash(mk_pk, m.release_key_hash);
        m.n = 1;
        m.stage[0].name_len = 6;
        memcpy(m.stage[0].name, "kernel", 6);
        memset(m.stage[0].digest, 0x99, 32);
        ba_soft_key_t k = {mk_sk, NULL, false};
        size_t ml;
        ba_manifest_build(&m, ba_soft_signer, &k, man, sizeof man, &ml);
        ba_cid(man, ml, prof.manifest_id);

        /* end to end: the device attests the boot the profile names */
        ba_log_t log;
        ba_log_init(&log);
        ba_extend(&log, "kernel", m.stage[0].digest);
        uint8_t nonce[32];
        memset(nonce, 0x10, 32);
        ba_soft_key_t dk = {dev_sk, NULL, false};
        size_t ql;
        ba_verifier_t v;
        ba_verifier_init(&v, 0);
        ba_verifier_issue(&v, nonce);
        ba_result_t r;
        CHECK(ba_quote(&log, nonce, prof.manifest_id, d.device_key_hash, ba_soft_signer, &dk, quote,
                       sizeof quote, &ql) == BA_OK &&
                  ba_verify_quote(&v, quote, ql, dev_pk, man, ml, mk_pk, &r) == BA_OK,
              "the device's boot quote verifies against the manifest the profile names");
    }
    ztp_commit(id, d.claim_secret, prof.claim_commit);
    memset(prof.hw_profile_digest, 0xAB, 32);
    prof.issued_at = 1760000000u;
    prof.model_len = 9;
    memcpy(prof.model, "ZXV-EDGE1", 9);
    prof.serial_len = 12;
    memcpy(prof.serial, "SN-000000042", 12);
    {
        ba_soft_key_t k = {mk_sk, NULL, false};
        CHECK(ztp_profile_build(&prof, ba_soft_signer, &k, rec, sizeof rec, &rl) == ZTP_OK &&
                  rl == ZTP_PROFILE_HDR + 21 + BA_SIG_LEN,
              "maker signs the profile; length is header + model + serial + signature");
        CHECK(ztp_profile_verify(rec, rl, mk_pk, &got) == ZTP_OK &&
                  memcmp(got.device_key_hash, d.device_key_hash, 32) == 0 &&
                  memcmp(got.manifest_id, prof.manifest_id, BA_ID_LEN) == 0 &&
                  strcmp(got.model, "ZXV-EDGE1") == 0 && strcmp(got.serial, "SN-000000042") == 0 &&
                  got.issued_at == 1760000000u && got.hw_profile_digest[31] == 0xAB,
              "profile verifies and every field reads back");
        CHECK(ztp_profile_verify(rec, rl, a_pk, &got) == ZTP_E_PROFILE_KEY,
              "profile checked against another maker key: ZTP_E_PROFILE_KEY");
        memcpy(rec2, rec, rl);
        rec2[ZTP_PROFILE_HDR + 20] ^= 1; /* last serial char */
        CHECK(ztp_profile_verify(rec2, rl, mk_pk, &got) == ZTP_E_PROFILE_SIG,
              "serial changed after signing: ZTP_E_PROFILE_SIG");
        memcpy(rec2, rec, rl);
        rec2[ZTP_PROFILE_HDR] = 0x07;
        int bad = ztp_profile_verify(rec2, rl, mk_pk, &got) == ZTP_E_PROFILE_FORMAT;
        bad &= ztp_profile_verify(rec, rl - 1, mk_pk, &got) == ZTP_E_PROFILE_FORMAT;
        memcpy(rec2, rec, rl);
        rec2[3] = '2';
        bad &= ztp_profile_verify(rec2, rl, mk_pk, &got) == ZTP_E_PROFILE_FORMAT;
        memcpy(rec2, rec, rl);
        rec2[5] = 10; /* model length disagrees with the record length */
        bad &= ztp_profile_verify(rec2, rl, mk_pk, &got) == ZTP_E_PROFILE_FORMAT;
        CHECK(bad, "control char in model, truncation, bad magic, wrong length: ZTP_E_PROFILE_FORMAT");
        ztp_profile_t p2 = prof;
        p2.model_len = 0;
        int a = ztp_profile_build(&p2, ba_soft_signer, &k, rec2, sizeof rec2, &rl) == ZTP_E_ARG;
        p2.model_len = 33;
        a &= ztp_profile_build(&p2, ba_soft_signer, &k, rec2, sizeof rec2, &rl) == ZTP_E_ARG;
        p2 = prof;
        a &= ztp_profile_build(&p2, ba_soft_signer, &k, rec2, ZTP_PROFILE_HDR + 21 + BA_SIG_LEN - 1,
                               &rl) == ZTP_E_SPACE;
        a &= ztp_profile_build(&p2, fail_signer, NULL, rec2, sizeof rec2, &rl) == ZTP_E_SIGNER &&
             rec2[0] == 0;
        CHECK(a, "profile build refuses empty or 33-char model, short buffer, failed signer");
        rl = ZTP_PROFILE_HDR + 21 + BA_SIG_LEN;
    }

    /* ---- QR ---- */
    {
        static qr_code_t code;
        CHECK(ztp_qr_text(&d, qr) == ZTP_OK && strlen(qr) == ZTP_QR_TEXT_LEN &&
                  qr_encode_bytes(&code, (const uint8_t *) qr, ZTP_QR_TEXT_LEN, 0, QR_MASK_AUTO) ==
                      QR_OK &&
                  code.version == 5 && code.size == 37,
              "claim QR: 82 characters, a 37x37 (version 5) level-M symbol");
        CHECK(ztp_profile_verify(rec, rl, mk_pk, &got) == ZTP_OK &&
                  ztp_qr_parse(qr, ZTP_QR_TEXT_LEN, id, sec) == ZTP_OK &&
                  ztp_qr_check(&got, id, sec) == ZTP_OK,
              "owner app: QR parses and opens the maker's commitment");
        uint8_t s2[32];
        memcpy(s2, sec, 32);
        s2[0] ^= 1;
        memcpy(id2, id, 16);
        id2[15] ^= 1;
        CHECK(ztp_qr_check(&got, id, s2) == ZTP_E_QR_MISMATCH &&
                  ztp_qr_check(&got, id2, sec) == ZTP_E_QR_MISMATCH,
              "QR from another device or with another secret: ZTP_E_QR_MISMATCH");
        char bad[ZTP_QR_TEXT_LEN + 1];
        int f = ztp_qr_parse(qr, ZTP_QR_TEXT_LEN - 1, id2, s2) == ZTP_E_QR_FORMAT;
        memcpy(bad, qr, sizeof bad);
        bad[3] = '2';
        f &= ztp_qr_parse(bad, ZTP_QR_TEXT_LEN, id2, s2) == ZTP_E_QR_FORMAT;
        memcpy(bad, qr, sizeof bad);
        bad[10] = 'A';
        f &= ztp_qr_parse(bad, ZTP_QR_TEXT_LEN, id2, s2) == ZTP_E_QR_FORMAT;
        memcpy(bad, qr, sizeof bad);
        bad[ZTP_QR_TEXT_LEN - 1] = (char) (bad[ZTP_QR_TEXT_LEN - 1] == 'b' ? 'd' : 'b'); /* odd value */
        f &= ztp_qr_parse(bad, ZTP_QR_TEXT_LEN, id2, s2) == ZTP_E_QR_FORMAT;
        CHECK(f, "QR text: short, wrong prefix, upper case, non-zero spare bit: ZTP_E_QR_FORMAT");
    }

    /* ---- claim ---- */
    ztp_device_id(&d, id);
    memcpy(sec, d.claim_secret, 32);
    {
        uint8_t wrong[32];
        memcpy(wrong, sec, 32);
        wrong[5] ^= 0x20;
        mk(ZTP_MSG_CLAIM, id, 0, c_pk, wrong, c_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_MAC && d.state == ZTP_UNCLAIMED,
              "claim with a guessed secret: ZTP_E_MAC, still unclaimed");
        mk(ZTP_MSG_CLAIM, id, 0, a_pk, sec, c_sk, msg); /* carries A's key, signed by C */
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_SIG,
              "claim carrying one key but signed by another: ZTP_E_SIG");
        memcpy(id2, id, 16);
        id2[0] ^= 1;
        mk(ZTP_MSG_CLAIM, id2, 0, a_pk, sec, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_DEVICE,
              "claim addressed to another device: ZTP_E_DEVICE");
        mk(ZTP_MSG_CLAIM, id, 1, a_pk, sec, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_EPOCH,
              "claim for epoch 1 on an epoch-0 device: ZTP_E_EPOCH");
        mk(ZTP_MSG_CLAIM, id, 0, a_pk, sec, a_sk, msg);
        uint8_t mac[32];
        crypto_hmac_sha256(msg, ZTP_MSG_MAC_OFF, sec, mac);
        CHECK(memcmp(msg + ZTP_MSG_MAC_OFF, mac, 32) == 0 && msg[5] == ZTP_MSG_CLAIM &&
                  memcmp(msg, "ZTM1", 4) == 0 && memcmp(msg + 40, a_pk, BA_PK_LEN) == 0,
              "claim message layout: magic, kind, owner key at 40, MAC over bytes 0..2631");
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[40 + 100] ^= 1; /* owner key swapped in transit */
        int t = ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_MAC;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[ZTP_MSG_MAC_OFF - 1] ^= 1; /* last byte the MAC covers */
        t &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_MAC;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[33] ^= 1; /* issued_at */
        t &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_MAC;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[ZTP_MSG_LEN - 3] ^= 1;
        t &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_SIG;
        CHECK(t && d.state == ZTP_UNCLAIMED,
              "tampered owner key (first or last byte) or time: ZTP_E_MAC; tampered signature: ZTP_E_SIG");
        int f = ztp_device_accept(&d, msg, ZTP_MSG_LEN - 1, NULL) == ZTP_E_FORMAT;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[5] = 4;
        f &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_FORMAT;
        msg2[5] = 0;
        f &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_FORMAT;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[29] = 1; /* reserved */
        f &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_FORMAT;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[0] = 'X';
        f &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_FORMAT;
        memcpy(msg2, msg, ZTP_MSG_LEN);
        memset(msg2 + 40, 0, BA_PK_LEN); /* claim without an owner key */
        f &= ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_FORMAT;
        CHECK(f, "short, kind 0 or 4, reserved set, bad magic, claim without key: ZTP_E_FORMAT");

        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_OK && d.state == ZTP_OWNED &&
                  d.epoch == 1 && memcmp(d.owner_pk, a_pk, BA_PK_LEN) == 0,
              "first valid claim: device OWNED by A at epoch 1");
        uint8_t z[32] = {0};
        CHECK(memcmp(d.claim_secret, z, 32) == 0 && ztp_qr_text(&d, qr) == ZTP_E_STATE,
              "claim secret wiped; no QR is shown once owned");
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_ALREADY_CLAIMED,
              "the same claim replayed: ZTP_E_ALREADY_CLAIMED");
        mk(ZTP_MSG_CLAIM, id, 1, b_pk, sec, b_sk, msg2);
        CHECK(ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_ALREADY_CLAIMED &&
                  memcmp(d.owner_pk, a_pk, BA_PK_LEN) == 0,
              "a second owner with the same QR: ZTP_E_ALREADY_CLAIMED, A still owns it");
    }

    /* ---- transfer ---- */
    {
        mk(ZTP_MSG_TRANSFER, id, 1, b_pk, NULL, b_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_SIG,
              "transfer signed by the would-be owner: ZTP_E_SIG");
        mk(ZTP_MSG_TRANSFER, id, 0, b_pk, NULL, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_EPOCH,
              "transfer for an old epoch: ZTP_E_EPOCH");
        mk(ZTP_MSG_TRANSFER, id, 1, a_pk, NULL, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_SAME_OWNER && d.epoch == 1,
              "transfer to the current owner: ZTP_E_SAME_OWNER, nothing changes");
        mk(ZTP_MSG_TRANSFER, id, 1, b_pk, NULL, a_sk, msg);
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[ZTP_MSG_MAC_OFF] = 1;
        CHECK(ztp_device_accept(&d, msg2, ZTP_MSG_LEN, NULL) == ZTP_E_FORMAT,
              "transfer with a non-zero MAC field: ZTP_E_FORMAT");
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_OK && d.epoch == 2 &&
                  memcmp(d.owner_pk, b_pk, BA_PK_LEN) == 0,
              "transfer signed by A: B owns it at epoch 2");
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_EPOCH,
              "the same transfer replayed: ZTP_E_EPOCH");
        mk(ZTP_MSG_TRANSFER, id, 2, c_pk, NULL, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_SIG,
              "the previous owner can no longer transfer: ZTP_E_SIG");
    }

    /* ---- reset ---- */
    {
        for (int i = 0; i < 32; i++) sec2[i] = (uint8_t) (0xC0 + i);
        mk(ZTP_MSG_RESET, id, 2, NULL, NULL, a_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, sec2) == ZTP_E_SIG && d.state == ZTP_OWNED,
              "reset signed by a former owner: ZTP_E_SIG");
        mk(ZTP_MSG_RESET, id, 2, NULL, NULL, b_sk, msg);
        uint8_t z[32] = {0};
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_ARG &&
                  ztp_device_accept(&d, msg, ZTP_MSG_LEN, z) == ZTP_E_ARG,
              "reset without a fresh secret: ZTP_E_ARG");
        memcpy(msg2, msg, ZTP_MSG_LEN);
        msg2[40] = 1;
        CHECK(ztp_device_accept(&d, msg2, ZTP_MSG_LEN, sec2) == ZTP_E_FORMAT,
              "reset carrying an owner key: ZTP_E_FORMAT");
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, sec2) == ZTP_OK && d.state == ZTP_UNCLAIMED &&
                  d.epoch == 3 && memcmp(d.claim_secret, sec2, 32) == 0 &&
                  d.owner_pk[0] == 0 && d.owner_key_hash[0] == 0,
              "reset by B: UNCLAIMED at epoch 3 with the new secret, owner wiped");
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, sec2) == ZTP_E_STATE,
              "reset on an unclaimed device: ZTP_E_STATE");
        mk(ZTP_MSG_TRANSFER, id, 3, c_pk, NULL, b_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_STATE,
              "transfer on an unclaimed device: ZTP_E_STATE");
        uint8_t i2[16], s3[32];
        CHECK(ztp_qr_text(&d, qr) == ZTP_OK && ztp_qr_parse(qr, ZTP_QR_TEXT_LEN, i2, s3) == ZTP_OK &&
                  ztp_qr_check(&got, i2, s3) == ZTP_E_QR_MISMATCH,
              "after reset the new QR no longer matches the maker's commitment");
        mk(ZTP_MSG_CLAIM, id, 3, c_pk, sec, c_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_E_MAC,
              "the old QR secret cannot claim after reset: ZTP_E_MAC");
        mk(ZTP_MSG_CLAIM, id, 3, c_pk, s3, c_sk, msg);
        CHECK(ztp_device_accept(&d, msg, ZTP_MSG_LEN, NULL) == ZTP_OK && d.epoch == 4 &&
                  memcmp(d.owner_pk, c_pk, BA_PK_LEN) == 0,
              "C claims with the new QR: OWNED at epoch 4");
    }

    /* ---- physical presence (rule Z7) ---- */
#if ZTP_PHYSICAL_RESET
    CHECK(ztp_reset_physical(&d, sec2) == ZTP_OK && d.state == ZTP_UNCLAIMED && d.epoch == 5 &&
              d.owner_pk[0] == 0,
          "physical-presence reset (compiled in): UNCLAIMED at epoch 5");
    CHECK(ztp_reset_physical(&d, sec2) == ZTP_E_STATE, "physical reset of an unclaimed device: ZTP_E_STATE");
#else
    CHECK(ztp_reset_physical(&d, sec2) == ZTP_E_DISABLED && d.state == ZTP_OWNED && d.epoch == 4,
          "physical-presence reset is off by default: ZTP_E_DISABLED, nothing changes");
#endif

    /* ---- builder arguments and errors ---- */
    {
        ba_soft_key_t k = {a_sk, NULL, false};
        int a = ztp_msg_build(ZTP_MSG_CLAIM, id, 0, 0, a_pk, NULL, ba_soft_signer, &k, msg) == ZTP_E_ARG;
        a &= ztp_msg_build(ZTP_MSG_TRANSFER, id, 0, 0, a_pk, sec, ba_soft_signer, &k, msg) == ZTP_E_ARG;
        a &= ztp_msg_build(ZTP_MSG_RESET, id, 0, 0, a_pk, NULL, ba_soft_signer, &k, msg) == ZTP_E_ARG;
        a &= ztp_msg_build(ZTP_MSG_TRANSFER, id, 0, 0, NULL, NULL, ba_soft_signer, &k, msg) == ZTP_E_ARG;
        a &= ztp_msg_build((ztp_kind_t) 9, id, 0, 0, a_pk, NULL, ba_soft_signer, &k, msg) == ZTP_E_ARG;
        CHECK(a, "builder: claim without secret, transfer with secret, reset with key, bad kind refused");
        memset(msg, 0x77, ZTP_MSG_LEN);
        CHECK(ztp_msg_build(ZTP_MSG_RESET, id, 0, 0, NULL, NULL, fail_signer, NULL, msg) ==
                      ZTP_E_SIGNER &&
                  msg[0] == 0 && msg[ZTP_MSG_LEN - 1] == 0,
              "builder: failed signer gives ZTP_E_SIGNER and a wiped buffer");
        int ok = 1;
        for (int i = 0; i < ZTP_E_COUNT_; i++)
            for (int j = 0; j < i; j++)
                if (strcmp(ztp_strerror((ztp_err_t) i), ztp_strerror((ztp_err_t) j)) == 0) ok = 0;
        CHECK(ok && strcmp(ztp_strerror(ZTP_E_COUNT_), "unknown") == 0, "every error has its own name");
    }

    printf("%s: %d checks, %d failed\n", fails ? "FAIL" : "OK", checks, fails);
    return fails ? 1 : 0;
}
