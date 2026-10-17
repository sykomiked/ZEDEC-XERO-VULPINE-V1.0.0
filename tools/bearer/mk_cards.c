/* mk_cards.c — the card-shaped presentation layer: turn each Iridium-tier
 * rail identity into a real-looking, ISO-shaped card record — a 16-digit
 * PAN with a valid Luhn check digit, a 3-digit CVV, and a rolling expiry —
 * every field derived deterministically from the card's own secret key, so
 * the card can always be re-derived from the bearer's sealed credentials.
 *
 * The PAN's first 8 digits are the rail's BIN (Dragon 420019, Phoenix
 * 530024, Thunderbird 601136 — the issuer ranges a merchant terminal
 * expects); the next 7 come from the key; the last is Luhn.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 *
 * usage: mk_cards <bearer-dir> <bearer-name> <passphrase>
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "aes256_gcm.h"
#include "sha256.h"

#define SK_BYTES 4896

static void hex(const uint8_t *b, size_t n, char *o)
{
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) {
        o[i * 2] = d[b[i] >> 4];
        o[i * 2 + 1] = d[b[i] & 15];
    }
    o[n * 2] = 0;
}

/* domain-separated derivation: sha256(secret || tag) -> bytes */
static void derive(const uint8_t *secret, size_t n, const char *tag, uint8_t out[32])
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, secret, n);
    sha256_update(&c, (const uint8_t *) tag, strlen(tag));
    sha256_final(&c, out);
}

/* read the card's sealed secret (salt 32 || iv 12 || ct || tag 16) */
static int unseal(const char *dir, const char *name, const char *pass, const char *rail,
                  uint8_t sk[SK_BYTES])
{
    char path[512];
    uint8_t blob[32 + 12 + SK_BYTES + 16], salt[32], iv[12], tag[16], key[32];
    snprintf(path, sizeof path, "%s/%s.sealed", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(blob, 1, sizeof blob, f);
    fclose(f);
    if (n != sizeof blob) return -1;
    memcpy(salt, blob, 32);
    memcpy(iv, blob + 32, 12);
    memcpy(tag, blob + 32 + 12 + SK_BYTES, 16);
    sha256_ctx_t k;
    sha256_init(&k);
    sha256_update(&k, (const uint8_t *) pass, strlen(pass));
    sha256_update(&k, salt, 32);
    sha256_update(&k, (const uint8_t *) rail, strlen(rail));
    sha256_final(&k, key);
    return aes256_gcm_decrypt(key, iv, (const uint8_t *) rail, strlen(rail), blob + 32 + 12,
                              SK_BYTES, tag, sk)
               ? 0
               : -1;
}

static int luhn_digit(const int *d, int n)
{
    /* the check digit that makes the whole number Luhn-valid */
    int sum = 0;
    for (int i = 0; i < n; i++) {
        int v = d[n - 1 - i];
        if (i % 2 == 0) {
            v *= 2;
            if (v > 9) v -= 9;
        }
        sum += v;
    }
    return (10 - (sum % 10)) % 10;
}

static void card_record(const char *dir, const char *bearer, const char *pass, const char *rail,
                        const char *bin, const char *brand)
{
    uint8_t sk[SK_BYTES], d[32];
    int digits[15];
    char cname[128];
    snprintf(cname, sizeof cname, "%s.card-%s", bearer, rail);
    if (unseal(dir, cname, pass, rail, sk) != 0) {
        printf("  %s: cannot unseal\n", rail);
        return;
    }
    /* PAN: 8 BIN digits + 7 from the key + the Luhn check digit */
    for (int i = 0; i < 8; i++) digits[i] = bin[i] - '0';
    derive(sk, sizeof sk, "zxv/card/pan/v1", d);
    for (int i = 0; i < 7; i++) digits[8 + i] = d[i] % 10;
    int check = luhn_digit(digits, 15);
    char pan[24];
    for (int i = 0; i < 15; i++) {
        pan[i] = (char) ('0' + digits[i]);
    }
    pan[15] = (char) ('0' + check);
    pan[16] = 0;
    /* CVV: 3 digits from the key */
    derive(sk, sizeof sk, "zxv/card/cvv/v1", d);
    int cvv = ((d[0] << 16) | (d[1] << 8) | d[2]) % 1000;
    /* the rolling expiry: derived months from the epoch, 4 years out */
    derive(sk, sizeof sk, "zxv/card/expiry/v1", d);
    time_t now = time(0);
    struct tm g;
    gmtime_r(&now, &g);
    int months = 24 + (d[0] % 25); /* 2 to 4 years */
    int year = (g.tm_year + 1900 + (g.tm_mon + months) / 12) % 100;
    int mon = (g.tm_mon + months) % 12 + 1;
    /* the owner's view: the FULL number, never masked (the bearer holds
     * the keys; there is no one else to hide it from) */
    char shown[24];
    snprintf(shown, sizeof shown, "%.4s %.4s %.4s %.4s", pan, pan + 4, pan + 8, pan + 12);
    printf("  %-11s %s  CVV %03d  exp %02d/%02d  (%s)\n", rail, shown, cvv, mon, year, brand);
    /* the machine record */
    char path[512];
    snprintf(path, sizeof path, "%s/%s.card-%s.record", dir, bearer, rail);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "# ZXV Iridium Black Swan — %s card (%s)\n"
                   "bearer:   %s\nrail:     %s\nbrand:    %s\ntier:     iridium-black-swan\n"
                   "pan:      %s\ncvv:      %03d\nexpires:  %02d/%02d\n"
                   "bin:      %s\n",
                brand, rail, bearer, rail, brand, pan, cvv, mon, year, bin);
        fclose(f);
    }
    memset(sk, 0, sizeof sk);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "usage: %s <bearer-dir> <bearer-name> <passphrase>\n", argv[0]);
        return 2;
    }
    printf("the %s cards (Iridium Black Swan):\n", argv[2]);
    card_record(argv[1], argv[2], argv[3], "dragon", "42001900", "Dragon");
    card_record(argv[1], argv[2], argv[3], "phoenix", "53002400", "Phoenix");
    card_record(argv[1], argv[2], argv[3], "thunderbird", "60113600", "Thunderbird");
    return 0;
}