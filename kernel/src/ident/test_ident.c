/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_ident.c -- host tests for kernel/src/ident.
 *
 * Known-answer: BLAKE2b (RFC 7693 appendix A), Argon2id (RFC 9106 5.3).
 * Synthetic biometrics: random 1020-bit feature vectors; a genuine
 * re-scan flips ~4% of bits, an impostor is an independent vector.
 * HONEST LIMITS: synthetic uniform noise is kinder than real sensors.
 */
#include <stdio.h>
#include <string.h>

#include "ident.h"

static int g_pass, g_fail;
#define CHECK(c, msg)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            g_pass++;                                                                              \
        } else {                                                                                   \
            g_fail++;                                                                              \
            printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, msg);                                  \
        }                                                                                          \
    } while (0)

/* ---- deterministic test RNG (not a CSPRNG) ---- */
static uint64_t rs = 0x9e3779b97f4a7c15ull;
static uint64_t xr(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 7;
    rs ^= rs << 17;
    return rs;
}
static void t_random(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (xr() >> 24);
}

/* ---- in-memory DHT for vault publish / fetch ---- */
#define DHT_MAX 8
static struct {
    uint8_t key[32];
    uint32_t len;
    uint8_t blob[ID_VAULT_MAX];
} dht[DHT_MAX];
static int dht_n;

static int t_publish(void *ctx, const uint8_t lookup[32], const ipfsn_cid_t *cid,
                     const uint8_t *blob, uint32_t len)
{
    (void) ctx;
    if (dht_n >= DHT_MAX || ipfsn_cid_verify(cid, blob, len) != IPFSN_OK) return -1;
    memcpy(dht[dht_n].key, lookup, 32);
    memcpy(dht[dht_n].blob, blob, len);
    dht[dht_n].len = len;
    dht_n++;
    return 0;
}

static int t_fetch(void *ctx, const uint8_t lookup[32], uint32_t index, uint8_t *buf, uint32_t cap,
                   uint32_t *len)
{
    (void) ctx;
    uint32_t seen = 0;
    for (int i = 0; i < dht_n; i++) {
        if (memcmp(dht[i].key, lookup, 32)) continue;
        if (seen++ != index) continue;
        if (dht[i].len > cap) return -1;
        memcpy(buf, dht[i].blob, dht[i].len);
        *len = dht[i].len;
        return 0;
    }
    return -1;
}

static int n_revoked, n_alerts[16];
static uint8_t last_revoked[16];
static void t_revoke(void *ctx, const uint8_t id[16])
{
    (void) ctx;
    n_revoked++;
    memcpy(last_revoked, id, 16);
}
static void t_alert(void *ctx, const id_alert_t *a)
{
    (void) ctx;
    if (a->kind < 16) n_alerts[a->kind]++;
}

static id_host_t H = {0, t_random, t_alert, t_revoke, t_publish, t_fetch};

static int hexeq(const uint8_t *b, const char *hex, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1 || v != b[i]) return 0;
    }
    return 1;
}

/* ---- 1. BLAKE2b and Argon2id ---- */
static uint64_t mem[2048 * 128]; /* 2 MiB */

static void test_kdf(void)
{
    uint8_t out[64];
    printf("[1] BLAKE2b + Argon2id known answers\n");
    id_blake2b((const uint8_t *) "abc", 3, out, 64);
    CHECK(hexeq(out,
                "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923",
                64),
          "BLAKE2b-512(abc)");
    uint8_t pwd[32], salt[16], sec[8], ad[12];
    memset(pwd, 1, 32);
    memset(salt, 2, 16);
    memset(sec, 3, 8);
    memset(ad, 4, 12);
    id_kdf_params_t kp = {32, 3, 4};
    CHECK(id_argon2id(&kp, pwd, 32, salt, 16, sec, 8, ad, 12, mem, 2048, out, 32) == ID_OK,
          "argon2id runs");
    CHECK(hexeq(out, "0d640df58d78766c08c037a34a8b53c9d01ef0452d75b65eb52520e96b01e659", 32),
          "Argon2id RFC 9106 5.3 tag");
    kp.m_kib = 1u << 30;
    CHECK(id_argon2id(&kp, pwd, 32, salt, 16, 0, 0, 0, 0, mem, 2048, out, 32) == ID_ERR_SIZE,
          "argon2id refuses more memory than supplied");
    id_kdf_params_t d;
    id_kdf_default(&d);
    CHECK(d.m_kib == 65536 && d.t == 3 && d.p == 1, "default lookup KDF 64 MiB / 3 / 1");
}

/* ---- 2. BCH ---- */
static void flip(uint8_t *v, uint32_t bit)
{
    v[bit >> 3] ^= (uint8_t) (1u << (bit & 7u));
}

static void test_bch(void)
{
    printf("[2] BCH(255,k,t)\n");
    CHECK(id_bch_k(25) == 91, "BCH(255,91,25)");
    CHECK(id_bch_k(18) == 131, "BCH(255,131,18)");
    CHECK(id_bch_k(8) == 191, "BCH(255,191,8)");
    CHECK(id_bch_k(30) == 63, "BCH(255,63,30)");
    CHECK(id_bch_k(3) == 0 && id_bch_k(31) == 0, "t out of range");
    int ok = 1, beyond_fail = 0;
    for (int trial = 0; trial < 60; trial++) {
        uint32_t t = 25;
        uint8_t msg[32], cw[32], rx[32];
        t_random(0, msg, 32);
        id_bch_encode(t, msg, cw);
        memcpy(rx, cw, 32);
        uint32_t ne = (uint32_t) (xr() % (t + 1));
        for (uint32_t e = 0; e < ne;) {
            uint32_t b = (uint32_t) (xr() % 255);
            if (((rx[b >> 3] ^ cw[b >> 3]) >> (b & 7)) & 1) continue;
            flip(rx, b);
            e++;
        }
        int32_t r = id_bch_decode(t, rx);
        if (r != (int32_t) ne || memcmp(rx, cw, 32)) ok = 0;
        /* 40 errors: far beyond t; decoder should refuse (or land elsewhere) */
        memcpy(rx, cw, 32);
        for (uint32_t e = 0; e < 40;) {
            uint32_t b = (uint32_t) (xr() % 255);
            if (((rx[b >> 3] ^ cw[b >> 3]) >> (b & 7)) & 1) continue;
            flip(rx, b);
            e++;
        }
        if (id_bch_decode(t, rx) < 0 || memcmp(rx, cw, 32)) beyond_fail++;
    }
    CHECK(ok, "corrects every pattern of <= t errors");
    CHECK(beyond_fail == 60, "never returns the sent codeword from 40 errors");
}

/* ---- 3. Shamir ---- */
static void test_shamir(void)
{
    uint8_t s[32], sh[5][32], ys[3][32], out[32], xs[3];
    printf("[3] Shamir 3-of-5 over GF(2^8)\n");
    t_random(0, s, 32);
    CHECK(id_shamir_split(s, 3, 5, &H, sh) == ID_OK, "split");
    int all = 1;
    for (int a = 0; a < 5; a++)
        for (int b = a + 1; b < 5; b++)
            for (int c = b + 1; c < 5; c++) {
                xs[0] = (uint8_t) (a + 1);
                xs[1] = (uint8_t) (b + 1);
                xs[2] = (uint8_t) (c + 1);
                memcpy(ys[0], sh[a], 32);
                memcpy(ys[1], sh[b], 32);
                memcpy(ys[2], sh[c], 32);
                id_shamir_combine(xs, (const uint8_t(*)[32]) ys, 3, out);
                if (memcmp(out, s, 32)) all = 0;
            }
    CHECK(all, "every 3 of 5 shares give the secret");
    id_shamir_combine(xs, (const uint8_t(*)[32]) ys, 2, out);
    CHECK(memcmp(out, s, 32) != 0, "2 shares do not");
    xs[1] = xs[0];
    CHECK(id_shamir_combine(xs, (const uint8_t(*)[32]) ys, 3, out) == ID_ERR_ARG,
          "duplicate x refused");
}

/* ---- synthetic biometrics ---- */
#define NBITS (4u * ID_BCH_N)
static void bio_random(uint8_t *f)
{
    memset(f, 0, ID_FE_MAX_BYTES);
    t_random(0, f, (NBITS + 7) / 8);
    f[(NBITS - 1) / 8] &= (uint8_t) ((1u << (((NBITS - 1) % 8) + 1)) - 1u);
}
static void bio_rescan(const uint8_t *src, uint8_t *dst, uint32_t pct)
{
    memcpy(dst, src, ID_FE_MAX_BYTES);
    for (uint32_t i = 0; i < NBITS; i++)
        if (xr() % 100 < pct) flip(dst, i);
}

static void test_fe(void)
{
    uint8_t w[ID_FE_MAX_BYTES], w2[ID_FE_MAX_BYTES], imp[ID_FE_MAX_BYTES];
    uint8_t helper[ID_FE_HELPER_MAX], salt[16], k1[32], k2[32];
    printf("[4] fuzzy extractor (4 x BCH(255,91,25), 1020 bits)\n");
    bio_random(w);
    t_random(0, salt, 16);
    CHECK(id_fe_gen(25, w, NBITS, &H, salt, helper, k1) == ID_OK, "gen");
    int good = 0;
    for (int i = 0; i < 20; i++) {
        bio_rescan(w, w2, 4);
        if (id_fe_rep(25, w2, NBITS, helper, salt, k2) == ID_OK && !memcmp(k1, k2, 32)) good++;
    }
    CHECK(good == 20, "20/20 genuine re-scans (4% noise) give the key");
    int bad = 0;
    for (int i = 0; i < 20; i++) {
        bio_random(imp);
        if (id_fe_rep(25, imp, NBITS, helper, salt, k2) == ID_OK && !memcmp(k1, k2, 32)) bad++;
    }
    CHECK(bad == 0, "0/20 impostors give the key");
    bio_rescan(w, w2, 20);
    CHECK(id_fe_rep(25, w2, NBITS, helper, salt, k2) == ID_ERR_NOMATCH,
          "20% noise is beyond the code");
    CHECK(id_fe_gen(25, w, 1000, &H, salt, helper, k1) == ID_ERR_ARG, "nbits must be blocks*255");
}

/* ---- 5. passkeys and root ---- */
static id_root_t root, root2;
static id_passkey_t pk_phone, pk_laptop, pk_new;
static id_dev_cert_t cert_phone, cert_laptop, cert_new;
static id_cred_status_t st_cur, st_next;
static id_rp_cred_t rp_phone;

static const uint8_t DEV_PHONE[16] = "phone-device-01";
static const uint8_t DEV_LAPTOP[16] = "laptop-device-1";
static const uint8_t DEV_NEW[16] = "new-phone-dev-2";

static void test_passkeys(void)
{
    uint8_t seed[32], user[16], auth[ID_AUTHDATA_MAX], a2[ID_AUTHDATA_MIN], cdh[32], chal[32];
    static uint8_t sig[ID_MLDSA65_SIG];
    uint32_t alen;
    char did[ID_DID_CHARS];
    printf("[5] passkeys (ML-DSA-65, WebAuthn layout) + root certificates\n");
    t_random(0, seed, 32);
    CHECK(id_root_from_seed(&root, PQM_LEVEL_HIGH, seed) == ID_OK, "root identity (HIGH)");
    id_did_string(root.did, did);
    CHECK(strncmp(did, "did:zxv:", 8) == 0 && strlen(did) == 60, "did string");
    t_random(0, user, 16);
    CHECK(id_passkey_create(&pk_phone, &H, ID_PLATFORM_RP, DEV_PHONE, user) == ID_OK, "create");
    CHECK(id_passkey_register(&pk_phone, true, auth, sizeof auth, &alen) == ID_OK &&
              alen == ID_AUTHDATA_MAX,
          "registration authData");
    CHECK(auth[32] == (ID_AUTH_UP | ID_AUTH_UV | ID_AUTH_AT), "flags UP|UV|AT, BE clear");
    CHECK(auth[53] == 0 && auth[54] == 16 && auth[71] == 0xA3 && auth[75] == 0x38 &&
              auth[76] == 0x30,
          "credIdLen 16 and COSE alg -49");
    CHECK(id_rp_register(&rp_phone, ID_PLATFORM_RP, auth, alen, true) == ID_OK, "RP registers");
    CHECK(id_rp_register(&rp_phone, "evil.example", auth, alen, false) == ID_ERR_AUTH,
          "wrong rp id refused");
    id_rp_register(&rp_phone, ID_PLATFORM_RP, auth, alen, true);

    t_random(0, chal, 32);
    id_client_data_hash("webauthn.get", chal, 32, "https://bank.example", cdh);
    id_passkey_assert(&pk_phone, &H, true, cdh, a2, sig);
    CHECK(id_rp_verify(&rp_phone, ID_PLATFORM_RP, a2, sizeof a2, cdh, sig, true) == ID_OK,
          "assertion verifies");
    CHECK(id_rp_verify(&rp_phone, ID_PLATFORM_RP, a2, sizeof a2, cdh, sig, true) == ID_ERR_REPLAY,
          "replayed assertion refused (counter)");
    id_passkey_assert(&pk_phone, &H, false, cdh, a2, sig);
    CHECK(id_rp_verify(&rp_phone, ID_PLATFORM_RP, a2, sizeof a2, cdh, sig, true) == ID_ERR_AUTH,
          "UV required but device check not done");
    id_passkey_assert(&pk_phone, &H, true, cdh, a2, sig);
    cdh[0] ^= 1;
    CHECK(id_rp_verify(&rp_phone, ID_PLATFORM_RP, a2, sizeof a2, cdh, sig, true) == ID_ERR_AUTH,
          "other challenge refused");
    cdh[0] ^= 1;

    id_passkey_create(&pk_laptop, &H, ID_PLATFORM_RP, DEV_LAPTOP, user);
    CHECK(id_dev_cert_issue(&root, &H, &pk_phone, 1000, &cert_phone) == ID_OK, "cert phone");
    CHECK(id_dev_cert_issue(&root, &H, &pk_laptop, 1000, &cert_laptop) == ID_OK, "cert laptop");
    CHECK(id_dev_cert_verify(&root.pk, &cert_phone, pk_phone.pk) == ID_OK, "cert verifies");
    CHECK(id_dev_cert_verify(&root.pk, &cert_phone, pk_laptop.pk) == ID_ERR_AUTH,
          "cert bound to its key");
    id_status_init(&st_cur, root.did, 1000);
    id_status_add(&st_cur, pk_phone.cred_id, pk_phone.dev_id);
    id_status_add(&st_cur, pk_laptop.cred_id, pk_laptop.dev_id);
    CHECK(id_status_sign(&root, &H, &st_cur) == ID_OK, "status list signed");
    CHECK(id_status_verify(&root.pk, &st_cur) == ID_OK, "status list verifies");

    id_passkey_assert(&pk_phone, &H, true, cdh, a2, sig);
    CHECK(id_rp_login(&root.pk, &st_cur, &cert_phone, &rp_phone, ID_PLATFORM_RP, a2, sizeof a2, cdh,
                      sig, true) == ID_OK,
          "RP login via root chain");
    t_random(0, seed, 32);
    id_root_from_seed(&root2, PQM_LEVEL_HIGH, seed);
    id_passkey_assert(&pk_phone, &H, true, cdh, a2, sig);
    CHECK(id_rp_login(&root2.pk, &st_cur, &cert_phone, &rp_phone, ID_PLATFORM_RP, a2, sizeof a2,
                      cdh, sig, true) == ID_ERR_AUTH,
          "other root refused");
}

static void test_matrix_root(void)
{
    static id_root_t mr;
    static id_dev_cert_t c;
    uint8_t seed[32];
    printf("[6] MATRIX root (ML-DSA-87 + SLH-DSA-SHAKE-256s dual signature)\n");
    t_random(0, seed, 32);
    CHECK(id_root_from_seed(&mr, PQM_LEVEL_MATRIX, seed) == ID_OK, "MATRIX root");
    CHECK(id_dev_cert_issue(&mr, &H, &pk_phone, 5, &c) == ID_OK, "MATRIX cert");
    CHECK(c.sig_len == pqm_encoded_size(PQM_OBJ_SIG, PQM_LEVEL_MATRIX), "dual signature size");
    CHECK(id_dev_cert_verify(&mr.pk, &c, pk_phone.pk) == ID_OK, "MATRIX cert verifies");
    c.sig[c.sig_len - 1] ^= 1;
    CHECK(id_dev_cert_verify(&mr.pk, &c, pk_phone.pk) == ID_ERR_AUTH, "SLH half tamper caught");
    id_root_wipe(&mr);
}

/* ---- 7. KYC ---- */
static id_issuer_t issuer, rogue;
static id_kyc_wallet_t wallet;
static id_kyc_profile_t prof_off, prof_bank;
static id_vp_t vp;
static id_revlist_t rl;
static uint32_t next_status = 7;
static int adapter_calls;

static int test_adapter(void *ctx, const id_kyc_request_t *req, id_vc_t *vc, id_claim_t *claims,
                        uint8_t *nclaims)
{
    id_issuer_t *iss = (id_issuer_t *) ctx;
    adapter_calls++;
    /* The verifier checked documents in its own process; what comes back: */
    id_claim_set(&claims[0], &H, "given_name", (const uint8_t *) "Ada", 3);
    id_claim_set(&claims[1], &H, "birth_date", (const uint8_t *) "1990-12-10", 10);
    id_claim_set(&claims[2], &H, "age_over_18", (const uint8_t *) "\x01", 1);
    id_claim_set(&claims[3], &H, "residency", (const uint8_t *) "GB", 2);
    *nclaims = 4;
    return id_vc_issue(iss, &H, req, claims, 4, 1000, 1000 + 365ull * 86400000ull, next_status++,
                       vc);
}

static void test_kyc(void)
{
    uint8_t seed[32], nonce[32], need = 0;
    id_kyc_status_t ks;
    uint32_t slot = 99;
    static char json[8192];
    printf("[7] optional KYC: credentials, selective disclosure, revocation, tiers\n");

    id_kyc_profile_default(&prof_off, "community market", ID_OP_INDIVIDUAL);
    CHECK(prof_off.required_level == 0 && prof_off.ntiers == 0, "default profile: KYC off");
    CHECK(id_kyc_check_payment(&prof_off, 0, 5, 555, 1000000000ull, 0, &need) == ID_OK,
          "KYC off: payment allowed with no credential");
    CHECK(id_kyc_check_payment(&prof_off, 0, 5, 999, 1, 0, &need) == ID_ERR_ARG,
          "rail must be 555/777/888");

    t_random(0, seed, 32);
    id_issuer_init(&issuer, PQM_LEVEL_STANDARD, seed, "Licensed Verifier Ltd");
    t_random(0, seed, 32);
    id_issuer_init(&rogue, PQM_LEVEL_STANDARD, seed, "Rogue");
    id_kyc_adapter_t ad = {"licensed", &issuer, test_adapter};

    id_kyc_profile_default(&prof_bank, "central bank", ID_OP_CENTRAL_BANK);
    id_kyc_profile_require(&prof_bank, 2);
    id_kyc_profile_trust(&prof_bank, &issuer.pk, 3);
    id_kyc_profile_jurisdiction(&prof_bank, "GB");
    id_kyc_profile_jurisdiction(&prof_bank, "FR");
    id_kyc_profile_tier(&prof_bank, 2, 100000, 500000);
    id_kyc_profile_tier(&prof_bank, 3, 0, 0);
    prof_bank.max_revlist_age_ms = 86400000ull;

    id_kyc_wallet_init(&wallet, root.seed);
    CHECK(id_kyc_request(&wallet, &ad, &prof_bank, &H, 2, "GB", 2000, &slot) == ID_ERR_DISABLED &&
              adapter_calls == 0,
          "no opt-in: nothing is sent to any verifier");
    id_kyc_opt_in(&wallet, true);
    CHECK(id_kyc_request(&wallet, &ad, &prof_bank, &H, 2, "GB", 2000, &slot) == ID_OK && slot == 0,
          "opted-in: level-2 credential stored");
    CHECK(wallet.slot[0].vc.nclaims == 4 && wallet.slot[0].vc.kyc_level == 2, "attestation shape");

    int32_t jl = id_vc_to_json(&wallet.slot[0].vc, json, sizeof json);
    CHECK(jl > 0 && strstr(json, "\"VerifiableCredential\"") &&
              strstr(json, "\"validFrom\":\"1970-01-01T00:00:01Z\"") &&
              strstr(json, "\"statusListIndex\":\"7\"") && strstr(json, "\"kycLevel\":2") &&
              !strstr(json, "Ada"),
          "W3C VC JSON, no personal data in clear");

    id_revlist_init(&rl, issuer.did, 1, 2000);
    id_revlist_sign(&issuer, &H, &rl);

    /* prove "over 18" only (claim 2) */
    t_random(0, nonce, 32);
    CHECK(id_vp_create(&wallet, 0, 1u << 2, nonce, "https://bank.example", &H, &vp) == ID_OK,
          "presentation");
    CHECK(id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://bank.example", 3000, &ks) == ID_OK,
          "presentation verifies");
    CHECK(ks.level == 2 && ks.ndisc == 1 && id_kyc_claim(&ks, "age_over_18") &&
              !id_kyc_claim(&ks, "given_name") && !id_kyc_claim(&ks, "birth_date"),
          "only age_over_18 revealed");
    CHECK(id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://other.example", 3000, &ks) ==
              ID_ERR_AUTH,
          "audience bound");
    nonce[0] ^= 1;
    CHECK(id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://bank.example", 3000, &ks) ==
              ID_ERR_AUTH,
          "nonce bound (no replay)");
    nonce[0] ^= 1;
    vp.disc[2].value[0] = 0;
    CHECK(id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://bank.example", 3000, &ks) ==
              ID_ERR_AUTH,
          "altered opened claim caught");
    vp.disc[2].value[0] = 1;
    CHECK(id_vp_verify(&prof_bank, &vp, 0, nonce, "https://bank.example", 3000, &ks) ==
              ID_ERR_NOTFOUND,
          "KYC required and no revocation list: fail closed");
    CHECK(id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://bank.example",
                       1000 + 366ull * 86400000ull, &ks) == ID_ERR_EXPIRED,
          "expired credential");
    CHECK(id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://bank.example", 2000 + 2 * 86400000ull,
                       &ks) == ID_ERR_EXPIRED,
          "stale revocation list");
    id_revlist_t rl2;
    id_revlist_init(&rl2, issuer.did, 2, 2500);
    id_revlist_revoke(&rl2, 7);
    id_revlist_sign(&issuer, &H, &rl2);
    CHECK(id_vp_verify(&prof_bank, &vp, &rl2, nonce, "https://bank.example", 3000, &ks) ==
              ID_ERR_REVOKED,
          "revoked credential");
    id_revlist_sign(&rogue, &H, &rl2);
    CHECK(id_vp_verify(&prof_bank, &vp, &rl2, nonce, "https://bank.example", 3000, &ks) ==
              ID_ERR_UNTRUSTED,
          "revocation list by another issuer refused");

    /* untrusted issuer */
    id_kyc_adapter_t bad = {"rogue", &rogue, test_adapter};
    CHECK(id_kyc_request(&wallet, &bad, &prof_bank, &H, 2, "GB", 2000, &slot) == ID_ERR_UNTRUSTED,
          "credential from untrusted issuer refused");
    /* jurisdiction */
    id_kyc_profile_t pj = prof_bank;
    pj.njur = 0;
    id_kyc_profile_jurisdiction(&pj, "DE");
    CHECK(id_vp_verify(&pj, &vp, &rl, nonce, "https://bank.example", 3000, &ks) ==
              ID_ERR_JURISDICTION,
          "jurisdiction outside profile");
    /* level */
    id_kyc_profile_t p3 = prof_bank;
    id_kyc_profile_require(&p3, 3);
    CHECK(id_vp_verify(&p3, &vp, &rl, nonce, "https://bank.example", 3000, &ks) == ID_ERR_LEVEL,
          "level 2 below required 3");

    /* tiers */
    id_vp_verify(&prof_bank, &vp, &rl, nonce, "https://bank.example", 3000, &ks);
    CHECK(id_kyc_check_payment(&prof_bank, 0, 3000, 555, 10, 0, &need) == ID_ERR_LEVEL && need == 2,
          "bank requires level 2");
    CHECK(id_kyc_check_payment(&prof_bank, &ks, 3000, 555, 90000, 0, &need) == ID_OK,
          "within level-2 tier");
    CHECK(id_kyc_check_payment(&prof_bank, &ks, 3000, 777, 200000, 0, &need) == ID_ERR_LIMIT &&
              need == 3,
          "over single limit: needs level 3");
    CHECK(id_kyc_check_payment(&prof_bank, &ks, 3000, 888, 90000, 450000, &need) == ID_ERR_LIMIT,
          "over daily limit");
    CHECK(id_kyc_check_payment(&prof_bank, &ks, 1000 + 366ull * 86400000ull, 555, 10, 0, &need) ==
              ID_ERR_LEVEL,
          "expired status counts as no KYC");
}

/* ---- 8. recovery ---- */
static id_guardian_t guard[5];
static id_recovery_t rec;
static id_rec_request_t req;
static id_sealed_share_t sealed[5];
static uint8_t vault[ID_VAULT_MAX];
static uint8_t wire[ID_VAULT_MAX + 32768];

static void run_guardians(int count, uint64_t at)
{
    for (int i = 0; i < count; i++) {
        id_guard_on_request(&guard[i], &req, at);
        id_guard_approve(&guard[i], req.vault_id, req.req_id, at + ID_GUARD_DELAY_MS, &sealed[i]);
        id_rec_add_share(&rec, &sealed[i]);
    }
}

static void test_recovery(void)
{
    uint8_t w[ID_FE_MAX_BYTES], w2[ID_FE_MAX_BYTES], imp[ID_FE_MAX_BYTES], seed[32];
    id_vault_params_t vp_;
    id_guardian_ref_t gr[5];
    id_guardian_share_t sh[5];
    ipfsn_cid_t cid;
    uint32_t vlen = 0;
    pqm_level_t lvl;
    uint8_t ncand = 0, n = 0;
    char norm[ID_NAME_MAX];
    const char *labels[5] = {"Mum", "Sam", "Home server", "Priya", "Old laptop"};
    printf("[8] recovery: name + biometric + 3-of-5 guardians\n");

    CHECK(id_name_normalise("  Blue   HERON \t", norm) == 10 && !strcmp(norm, "blue heron"),
          "name normalisation");
    CHECK(id_name_normalise("ab", norm) < 0, "too-short name refused");

    id_vault_params_default(&vp_);
    CHECK(vp_.k == 3 && vp_.n == 5 && vp_.fe_t == 25, "defaults 3-of-5, t=25");
    vp_.kdf.m_kib = 1024; /* test speed; production uses 64 MiB */
    vp_.kdf.t = 1;
    for (int i = 0; i < 5; i++) {
        memset(&gr[i], 0, sizeof gr[i]);
        t_random(0, gr[i].id, 32);
        strcpy(gr[i].label, labels[i]);
    }
    bio_random(w);
    CHECK(id_recovery_enrol(&H, &root, "Blue Heron", w, NBITS, &vp_, gr, mem, 2048, vault,
                            sizeof vault, &vlen, &cid, sh) == ID_OK,
          "enrol");
    CHECK(dht_n == 1 && vlen < 1400, "vault pinned + announced, compact");
    CHECK(!memcmp(sh[0].vault_id, cid.digest, 32) && sh[4].index == 5 && sh[0].k == 3,
          "shares carry the vault CID digest");
    /* no biometric index: the vault contains neither w nor anything keyed by it */
    int found = 0;
    for (uint32_t i = 0; i + 8 <= vlen; i++)
        if (!memcmp(vault + i, w, 8)) found = 1;
    CHECK(!found, "raw features not in the vault");
    CHECK(id_recovery_enrol(&H, &root, "blue  heron", w, NBITS, &vp_, gr, mem, 2048, vault,
                            sizeof vault, &vlen, &cid, sh) == ID_ERR_NAME,
          "name already in use refused");

    for (int i = 0; i < 5; i++) {
        id_guard_init(&guard[i], &H);
        CHECK(id_guard_keep(&guard[i], &sh[i], &root.pk, 0) == ID_OK, "guardian keeps share");
    }

    /* wrong name */
    CHECK(id_rec_begin(&rec, &H, "Blue Herring", &vp_.kdf, mem, 2048, &ncand) == ID_ERR_NOTFOUND,
          "wrong name finds nothing");
    CHECK(id_rec_begin_with(&rec, &H, "Blue Herring", &vp_.kdf, mem, 2048, dht[0].blob,
                            dht[0].len) == ID_ERR_NOMATCH,
          "wrong name cannot open a vault in hand");

    /* right name */
    CHECK(id_rec_begin(&rec, &H, "BLUE heron", &vp_.kdf, mem, 2048, &ncand) == ID_OK && ncand == 1,
          "right name finds the vault");
    const id_guardian_ref_t *g = id_rec_guardians(&rec, 0, &n);
    CHECK(g && n == 5 && !strcmp(g[2].label, "Home server"), "guardian list readable with name");
    CHECK(id_rec_request(&rec, 0, PQM_LEVEL_STANDARD, 50000, &req) == ID_OK, "request");
    CHECK(!memcmp(req.vault_id, cid.digest, 32), "request names the vault");

    /* wire round trip */
    int32_t wl = id_rec_request_encode(&req, wire, sizeof wire);
    static id_rec_request_t req2;
    CHECK(wl > 0 && id_rec_request_decode(&req2, wire, (uint32_t) wl) == ID_OK &&
              !memcmp(&req2, &req, sizeof req),
          "request encode/decode");

    /* waiting period */
    memset(n_alerts, 0, sizeof n_alerts);
    CHECK(id_guard_on_request(&guard[0], &req, 60000) == ID_OK, "guardian alerted");
    CHECK(n_alerts[ID_ALERT_RECOVERY_REQUESTED] == 1, "alert raised");
    CHECK(id_guard_approve(&guard[0], req.vault_id, req.req_id, 60000 + 1000, &sealed[0]) ==
              ID_ERR_WAIT,
          "no release during waiting period");
    CHECK(id_guard_approve(&guard[0], req.vault_id, req.req_id, 60000 + ID_GUARD_DELAY_MS,
                           &sealed[0]) == ID_OK,
          "release after waiting period");
    wl = id_sealed_share_encode(&sealed[0], wire, sizeof wire);
    static id_sealed_share_t s2;
    CHECK(wl > 0 && id_sealed_share_decode(&s2, wire, (uint32_t) wl) == ID_OK &&
              !memcmp(&s2, &sealed[0], sizeof s2),
          "sealed share encode/decode");
    CHECK(id_rec_add_share(&rec, &sealed[0]) == ID_OK, "share 1 accepted");
    for (int i = 1; i < 2; i++) {
        id_guard_on_request(&guard[i], &req, 60000);
        id_guard_approve(&guard[i], req.vault_id, req.req_id, 60000 + ID_GUARD_DELAY_MS,
                         &sealed[i]);
        CHECK(id_rec_add_share(&rec, &sealed[i]) == ID_OK, "share accepted");
    }
    bio_rescan(w, w2, 4);
    CHECK(id_rec_finish(&rec, w2, NBITS, 70000, seed, &lvl) == ID_ERR_THRESHOLD,
          "2 of 3 guardians: refused");
    /* tampered share */
    id_guard_on_request(&guard[2], &req, 60000);
    id_guard_approve(&guard[2], req.vault_id, req.req_id, 60000 + ID_GUARD_DELAY_MS, &sealed[2]);
    s2 = sealed[2];
    s2.enc[5] ^= 1;
    CHECK(id_rec_add_share(&rec, &s2) == ID_ERR_AUTH, "tampered share refused");
    CHECK(id_rec_add_share(&rec, &sealed[2]) == ID_OK, "share 3 accepted");

    /* impostor */
    bio_random(imp);
    CHECK(id_rec_finish(&rec, imp, NBITS, 70000, seed, &lvl) == ID_ERR_NOMATCH,
          "impostor biometric fails");
    /* genuine noisy re-scan */
    bio_rescan(w, w2, 4);
    CHECK(id_rec_finish(&rec, w2, NBITS, 70000, seed, &lvl) == ID_OK, "genuine re-scan recovers");
    CHECK(!memcmp(seed, root.seed, 32) && lvl == PQM_LEVEL_HIGH &&
              !memcmp(rec.account, root.did, 32),
          "root seed and DID back");
    id_rec_wipe(&rec);

    /* re-enrol a new device; old devices revoked */
    static id_root_t rr;
    id_root_from_seed(&rr, lvl, seed);
    CHECK(!memcmp(rr.did, root.did, 32), "same identity");
    uint8_t user[16] = {0};
    id_passkey_create(&pk_new, &H, ID_PLATFORM_RP, DEV_NEW, user);
    n_revoked = 0;
    CHECK(id_reenrol(&rr, &H, &pk_new, 90000, &cert_new, &st_next, &st_cur) == ID_OK, "re-enrol");
    CHECK(n_revoked == 2, "host told to revoke both old devmesh devices");
    CHECK(!id_status_is_active(&st_cur, pk_phone.cred_id) &&
              id_status_is_active(&st_cur, pk_new.cred_id),
          "status list: only the new passkey");
    uint8_t chal[32], cdh[32], a2[ID_AUTHDATA_MIN];
    static uint8_t sig[ID_MLDSA65_SIG];
    t_random(0, chal, 32);
    id_client_data_hash("webauthn.get", chal, 32, "https://bank.example", cdh);
    id_passkey_assert(&pk_phone, &H, true, cdh, a2, sig);
    CHECK(id_rp_login(&root.pk, &st_cur, &cert_phone, &rp_phone, ID_PLATFORM_RP, a2, sizeof a2, cdh,
                      sig, true) == ID_ERR_REVOKED,
          "lost phone locked out of accounts");
    uint8_t auth[ID_AUTHDATA_MAX];
    uint32_t alen;
    static id_rp_cred_t rp_new;
    id_passkey_register(&pk_new, true, auth, sizeof auth, &alen);
    id_rp_register(&rp_new, ID_PLATFORM_RP, auth, alen, true);
    id_passkey_assert(&pk_new, &H, true, cdh, a2, sig);
    CHECK(id_rp_login(&root.pk, &st_cur, &cert_new, &rp_new, ID_PLATFORM_RP, a2, sizeof a2, cdh,
                      sig, true) == ID_OK,
          "new device logs in to the same account");
    CHECK(id_status_accept(&st_cur, &st_next, &root.pk, &H) == ID_ERR_STATE,
          "old epoch cannot replace newer");
    id_root_wipe(&rr);

    /* lockout after repeated failures */
    id_rec_begin(&rec, &H, "blue heron", &vp_.kdf, mem, 2048, &ncand);
    id_rec_request(&rec, 0, PQM_LEVEL_STANDARD, 200000, &req);
    for (int i = 0; i < 5; i++) id_guard_init(&guard[i], &H);
    for (int i = 0; i < 5; i++) id_guard_keep(&guard[i], &sh[i], &root.pk, 0);
    run_guardians(3, 200000);
    bio_random(imp);
    id_rec_finish(&rec, imp, NBITS, 1, seed, &lvl);
    id_rec_finish(&rec, imp, NBITS, 2, seed, &lvl);
    CHECK(id_rec_finish(&rec, imp, NBITS, 3, seed, &lvl) == ID_ERR_LOCKED, "locked after 3 fails");
    bio_rescan(w, w2, 4);
    CHECK(id_rec_finish(&rec, w2, NBITS, 4, seed, &lvl) == ID_ERR_LOCKED,
          "locked even for the genuine user (new approvals needed)");

    /* guardian rate limit and owner cancel */
    for (int i = 0; i < 5; i++) id_guard_init(&guard[i], &H);
    for (int i = 0; i < 5; i++) id_guard_keep(&guard[i], &sh[i], &root.pk, 0);
    static id_rec_cancel_t cancel;
    uint64_t t0 = 1000000;
    for (int r = 0; r < 3; r++) {
        id_rec_begin(&rec, &H, "blue heron", &vp_.kdf, mem, 2048, &ncand);
        id_rec_request(&rec, 0, PQM_LEVEL_STANDARD, t0, &req);
        CHECK(id_guard_on_request(&guard[0], &req, t0 + (uint64_t) r) == ID_OK, "request counted");
        CHECK(id_rec_cancel_make(&pk_new, &H, &cert_new, req.vault_id, req.req_id, &cancel) ==
                  ID_OK,
              "owner device signs cancel");
        id_status_t cs = id_guard_cancel(&guard[0], &cancel, t0 + 10);
        if (r < 2)
            CHECK(cs == ID_OK, "owner cancels during waiting period");
        else
            CHECK(cs == ID_ERR_RATE, "third cancel refused (stolen device cannot block forever)");
    }
    CHECK(id_guard_approve(&guard[0], req.vault_id, req.req_id, t0 + ID_GUARD_DELAY_MS + 10,
                           &sealed[0]) == ID_OK,
          "after cancel limit, guardian may approve");
    id_rec_begin(&rec, &H, "blue heron", &vp_.kdf, mem, 2048, &ncand);
    id_rec_request(&rec, 0, PQM_LEVEL_STANDARD, t0, &req);
    CHECK(id_guard_on_request(&guard[0], &req, t0 + 20) == ID_ERR_RATE,
          "4th request in 30 days refused");
    CHECK(id_guard_on_request(&guard[0], &req, t0 + ID_GUARD_WINDOW_MS + 20) == ID_OK,
          "new window allows a request");
    /* cancel signed by a passkey of another root */
    static id_dev_cert_t cert_other;
    id_dev_cert_issue(&root2, &H, &pk_laptop, 5, &cert_other);
    id_rec_cancel_make(&pk_laptop, &H, &cert_other, req.vault_id, req.req_id, &cancel);
    CHECK(id_guard_cancel(&guard[0], &cancel, t0 + ID_GUARD_WINDOW_MS + 30) == ID_ERR_AUTH,
          "cancel from a stranger's device refused");
    id_rec_wipe(&rec);
}

int main(void)
{
    test_kdf();
    test_bch();
    test_shamir();
    test_fe();
    test_passkeys();
    test_matrix_root();
    test_kyc();
    test_recovery();
    printf("test_ident: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
