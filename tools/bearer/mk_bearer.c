/* mk_bearer.c — generate the sovereign bearer accounts on the kernel's own
 * post-quantum crypto: the Crown and the Ministry (ML-DSA-87 lattice
 * identities, the design's "lattice-derived equivalent" of EVM keys), their
 * Iridium-tier card rails (Dragon, Phoenix, Thunderbird), and the keyless
 * third tribute rail (a faucet address with no secret).
 *
 * The private material is sealed with AES-256-GCM under a key derived from
 * the bearer's passphrase (SHA-256 of the passphrase + the account's salt),
 * so the on-disk credentials are useless without the passphrase.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * usage: mk_bearer <out-dir> <crown-passphrase> <ministry-passphrase>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aes256_gcm.h"
#include "pq_matrix_algs.h"
#include "sha256.h"
#include "zxv_decl.h"

#define PK_BYTES PQM_MLDSA87_PK_BYTES
#define SK_BYTES PQM_MLDSA87_SK_BYTES
#define SIG_BYTES PQM_MLDSA87_SIG_BYTES

static void hex(const uint8_t *b, size_t n, char *out)
{
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        out[i * 2] = d[b[i] >> 4];
        out[i * 2 + 1] = d[b[i] & 15];
    }
    out[n * 2] = 0;
}

static void entropy(uint8_t *out, size_t n)
{
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f || fread(out, 1, n, f) != n) {
        /* fallback: the clock folded through sha256 (never for production) */
        uint8_t buf[64];
        uint64_t t = (uint64_t) time(0);
        for (size_t i = 0; i < sizeof buf; i += 8) memcpy(buf + i, &t, 8), t++;
        for (size_t i = 0; i < n; i += 32) {
            uint8_t h[32];
            sha256(buf, sizeof buf, h);
            memcpy(out + i, h, n - i < 32 ? n - i : 32);
        }
    }
    if (f) fclose(f);
}

static void write_file(const char *dir, const char *name, const void *data, size_t n)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        exit(1);
    }
    fwrite(data, 1, n, f);
    fclose(f);
    printf("  %s (%zu bytes)\n", path, n);
}

/* one bearer: an ML-DSA-87 lattice identity + the three card rails */
static void bearer(const char *dir, const char *name, const char *pass, const char *root_tag)
{
    uint8_t seed[32], pk[PK_BYTES], sk[SK_BYTES], salt[32];
    uint8_t seal_key[32], iv[GCM_IV_LEN], tag[GCM_TAG_LEN];
    uint8_t sealed[SK_BYTES + 64];
    char hx[PK_BYTES * 2 + 1];
    sha256_ctx_t c;

    printf("bearer %s:\n", name);
    entropy(seed, sizeof seed);
    entropy(salt, sizeof salt);
    pqm_mldsa87_keygen(seed, pk, sk);

    /* the account address: the public key's sha256, hex */
    {
        uint8_t ah[32];
        sha256(pk, sizeof pk, ah);
        hex(ah, 32, hx);
        printf("  address: %s\n", hx);
        char an[64];
        snprintf(an, sizeof an, "%s.address", name);
        write_file(dir, an, hx, strlen(hx));
    }
    {
        char pn[64];
        snprintf(pn, sizeof pn, "%s.pub", name);
        write_file(dir, pn, pk, sizeof pk);
    }

    /* the passphrase-sealed secret: key = sha256(passphrase || salt) */
    {
        sha256_ctx_t k;
        sha256_init(&k);
        sha256_update(&k, (const uint8_t *) pass, strlen(pass));
        sha256_update(&k, salt, sizeof salt);
        sha256_final(&k, seal_key);
    }
    entropy(iv, sizeof iv);
    aes256_gcm_encrypt(seal_key, iv, (const uint8_t *) name, strlen(name), sk, sizeof sk,
                       sealed, tag);
    memcpy(sealed + sizeof sk, tag, GCM_TAG_LEN);
    {
        uint8_t blob[32 + GCM_IV_LEN + SK_BYTES + GCM_TAG_LEN];
        memcpy(blob, salt, 32);
        memcpy(blob + 32, iv, GCM_IV_LEN);
        memcpy(blob + 32 + GCM_IV_LEN, sealed, SK_BYTES + GCM_TAG_LEN);
        char sn[64];
        snprintf(sn, sizeof sn, "%s.sealed", name);
        write_file(dir, sn, blob, sizeof blob);
    }

    /* the three Iridium-tier card rails: derived lattice keys, domain-separated */
    static const char *rails[3] = {"dragon", "phoenix", "thunderbird"};
    for (int r = 0; r < 3; r++) {
        uint8_t rseed[32], rpk[PK_BYTES], rsk[SK_BYTES];
        char rn[96], rtag[128];
        snprintf(rtag, sizeof rtag, "%s/card/%s/iridium-black-swan/v1", root_tag, rails[r]);
        sha256_init(&c);
        sha256_update(&c, (const uint8_t *) rtag, strlen(rtag));
        sha256_update(&c, seed, sizeof seed);
        sha256_final(&c, rseed);
        pqm_mldsa87_keygen(rseed, rpk, rsk);
        {
            uint8_t rh[32];
            char rhx[65];
            sha256(rpk, sizeof rpk, rh);
            hex(rh, 32, rhx);
            printf("  card %-12s %s\n", rails[r], rhx);
            snprintf(rn, sizeof rn, "%s.card-%s.address", name, rails[r]);
            write_file(dir, rn, rhx, strlen(rhx));
            snprintf(rn, sizeof rn, "%s.card-%s.pub", name, rails[r]);
            write_file(dir, rn, rpk, sizeof rpk);
        }
        /* seal the card secret too */
        {
            uint8_t civ[GCM_IV_LEN], ctag[GCM_TAG_LEN], csealed[SK_BYTES + 64], ckey[32];
            sha256_ctx_t k;
            sha256_init(&k);
            sha256_update(&k, (const uint8_t *) pass, strlen(pass));
            sha256_update(&k, salt, sizeof salt);
            sha256_update(&k, (const uint8_t *) rails[r], strlen(rails[r]));
            sha256_final(&k, ckey);
            entropy(civ, sizeof civ);
            aes256_gcm_encrypt(ckey, civ, (const uint8_t *) rails[r], strlen(rails[r]), rsk,
                               sizeof rsk, csealed, ctag);
            memcpy(csealed + sizeof rsk, ctag, GCM_TAG_LEN);
            uint8_t blob[32 + GCM_IV_LEN + SK_BYTES + GCM_TAG_LEN];
            memcpy(blob, salt, 32);
            memcpy(blob + 32, civ, GCM_IV_LEN);
            memcpy(blob + 32 + GCM_IV_LEN, csealed, SK_BYTES + GCM_TAG_LEN);
            snprintf(rn, sizeof rn, "%s.card-%s.sealed", name, rails[r]);
            write_file(dir, rn, blob, sizeof blob);
        }
    }
    memset(seed, 0, sizeof seed);
    memset(sk, 0, sizeof sk);
    memset(seal_key, 0, sizeof seal_key);
}

/* the third rail: the R&D fountain. The protocol spends it only through
 * the scheduled grant program; this keypair is the keeper's record (the
 * fountain's identity), not a spending card. */
static void fountain(const char *dir, const char *pass)
{
    uint8_t seed[32], pk[PK_BYTES], sk[SK_BYTES], salt[32], seal_key[32], iv[GCM_IV_LEN],
        tag[GCM_TAG_LEN], sealed[SK_BYTES + 64];
    char hx[PK_BYTES * 2 + 1];
    printf("bearer third-fountain (the R&D fountain, keeper key):\n");
    entropy(seed, sizeof seed);
    entropy(salt, sizeof salt);
    pqm_mldsa87_keygen(seed, pk, sk);
    {
        uint8_t ah[32];
        sha256(pk, sizeof pk, ah);
        hex(ah, 32, hx);
        printf("  address: %s\n", hx);
        write_file(dir, "third-fountain-keeper.address", hx, strlen(hx));
        write_file(dir, "third-fountain-keeper.pub", pk, sizeof pk);
    }
    {
        sha256_ctx_t k;
        sha256_init(&k);
        sha256_update(&k, (const uint8_t *) pass, strlen(pass));
        sha256_update(&k, salt, sizeof salt);
        sha256_final(&k, seal_key);
    }
    entropy(iv, sizeof iv);
    aes256_gcm_encrypt(seal_key, iv, (const uint8_t *) "third-fountain", 14, sk, sizeof sk,
                       sealed, tag);
    memcpy(sealed + sizeof sk, tag, GCM_TAG_LEN);
    {
        uint8_t blob[32 + GCM_IV_LEN + SK_BYTES + GCM_TAG_LEN];
        memcpy(blob, salt, 32);
        memcpy(blob + 32, iv, GCM_IV_LEN);
        memcpy(blob + 32 + GCM_IV_LEN, sealed, SK_BYTES + GCM_TAG_LEN);
        write_file(dir, "third-fountain-keeper.sealed", blob, sizeof blob);
    }
    memset(seed, 0, sizeof seed);
    memset(sk, 0, sizeof sk);
    memset(seal_key, 0, sizeof seal_key);
}

int main(int argc, char **argv)
{
    if (argc != 6) {
        fprintf(stderr, "usage: %s <out-dir> <crown-pass> <ministry-pass> <fountain-pass> "
                        "<african-union-pass>\n", argv[0]);
        return 2;
    }
    printf("sovereign bearer accounts — the kernel's lattice identities, the 11%% tributes:\n\n");
    bearer(argv[1], "crown", argv[2], "zxv/tribute/crown/v1");
    bearer(argv[1], "ministry", argv[3], "zxv/tribute/ministry/v1");
    fountain(argv[1], argv[4]);
    /* the African Union: funded by the fountain's 11% flow, a fountain
     * itself (the grant program's outbound rail) */
    bearer(argv[1], "african-union", argv[5], "zxv/tribute/african-union/v1");

    /* the third rail: the R&D fountain. No key exists — the address is a
     * protocol constant (sha256 of the domain tag), so its balance can
     * only be spent by the scheduled olive-oil-barrel grant program. */
    {
        uint8_t h[32];
        char hx[65];
        sha256((const uint8_t *) "zxv/tribute/third-fountain/v1", 29, h);
        hex(h, 32, hx);
        printf("\nthe third rail (the R&D fountain, keyless):\n  address: %s\n", hx);
        write_file(argv[1], "third-fountain.address", hx, strlen(hx));
    }
    printf("\nthe 11%% tributes: crown 11%%, ministry 11%%, third-fountain 11%% of protocol yield,\n"
           "held in V-bills unless the bearer withdraws; the fountain funds the\n"
           "olive-oil-barrel grants (scheduled vesting payouts, never limbo).\n");
    return 0;
}