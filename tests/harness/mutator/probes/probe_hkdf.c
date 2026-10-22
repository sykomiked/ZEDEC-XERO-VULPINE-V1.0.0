/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* probe_hkdf.c: differential probe for kernel/src/tls/hkdf.c (classify.py).
 * Prints every result of every call over a fixed input set; no assertions. */
#include <stdio.h>
#include <string.h>
#include "hkdf.h"

static void hex(const char *tag, const uint8_t *p, uint32_t n) {
    printf("%s ", tag);
    for (uint32_t i = 0; i < n; i++) printf("%02x", p[i]);
    printf("\n");
}

static uint8_t buf[9000];
static void pat(uint8_t *p, uint32_t n, uint32_t seed) {
    for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(seed + 7u * i);
}

int main(void) {
    uint8_t k[200], m[700], out[32];
    pat(k, sizeof k, 3);
    pat(m, sizeof m, 11);
    static const uint32_t kl[] = {0, 1, 31, 32, 63, 64, 65, 130, 200};
    static const uint32_t ml[] = {0, 1, 55, 56, 64, 119, 600, 700};
    for (unsigned i = 0; i < sizeof kl / 4; i++)
        for (unsigned j = 0; j < sizeof ml / 4; j++) {
            memset(out, 0xAA, 32);
            hmac_sha256(k, kl[i], m, ml[j], out);
            printf("hmac k%u m%u ", kl[i], ml[j]);
            hex("", out, 32);
        }
    /* NULL pointers with zero lengths */
    hmac_sha256(NULL, 0, NULL, 0, out); hex("hmac null null", out, 32);
    hmac_sha256(NULL, 0, m, 5, out); hex("hmac null m5", out, 32);
    hmac_sha256(k, 5, NULL, 0, out); hex("hmac k5 null", out, 32);
    hmac_sha256(NULL, 5, m, 5, out); hex("hmac null5 m5", out, 32); /* NULL reads as empty */
    hmac_sha256(k, 5, NULL, 5, out); hex("hmac k5 null5", out, 32);

    uint8_t prk[32];
    static const uint32_t sl[] = {0, 1, 32, 64, 65};
    for (unsigned i = 0; i < sizeof sl / 4; i++) {
        hkdf_extract(k, sl[i], m, 22, prk);
        printf("extract s%u ", sl[i]); hex("", prk, 32);
        hkdf_extract(NULL, sl[i], m, 22, prk);
        printf("extract null s%u ", sl[i]); hex("", prk, 32);
    }
    hkdf_extract(k, 0, NULL, 0, prk); hex("extract empty", prk, 32);
    hkdf_extract(k, 5, NULL, 5, prk); hex("extract nullikm5", prk, 32);

    hkdf_extract(NULL, 0, m, 22, prk);
    static const uint32_t ol[] = {0, 1, 31, 32, 33, 64, 65, 300, 8159, 8160, 8161, 65535, 65536};
    static const uint32_t il[] = {0, 1, 10, 478, 479, 480, 600};
    for (unsigned i = 0; i < sizeof ol / 4; i++)
        for (unsigned j = 0; j < sizeof il / 4; j++) {
            memset(buf, 0x55, sizeof buf);
            uint32_t n = ol[i] < sizeof buf - 8 ? ol[i] : sizeof buf - 8;
            bool r = ol[i] < sizeof buf - 8 ? hkdf_expand(prk, m, il[j], buf, ol[i]) : false;
            printf("expand o%u i%u %d ", ol[i], il[j], r);
            hex("", buf, n + 8 < 400 ? n + 8 : 400);
            if (n + 8 >= 400) hex("  tail", buf + n - 16, 24);
        }
    printf("expand nullprk %d\n", hkdf_expand(NULL, m, 3, buf, 5));
    printf("expand nullout %d\n", hkdf_expand(prk, m, 3, NULL, 5));
    printf("expand nullinfo %d ", hkdf_expand(prk, NULL, 3, buf, 40)); hex("", buf, 40);
    printf("expand nullinfo0 %d ", hkdf_expand(prk, NULL, 0, buf, 40)); hex("", buf, 40);

    /* HkdfLabel encoding */
    char lab[300];
    memset(lab, 'a', sizeof lab);
    static const uint32_t ll[] = {0, 1, 63, 64, 65, 248, 249, 250};
    static const uint32_t cl[] = {0, 1, 32, 33, 254, 255, 256};
    static const uint32_t caps[] = {0, 9, 10, 50, 600};
    for (unsigned i = 0; i < sizeof ll / 4; i++) {
        lab[ll[i]] = 0;
        for (unsigned j = 0; j < sizeof cl / 4; j++)
            for (unsigned c = 0; c < sizeof caps / 4; c++) {
                memset(buf, 0x33, 620);
                uint32_t n = tls13_hkdf_label(buf, caps[c], 0x1234, lab, m, cl[j]);
                printf("label l%u c%u cap%u n%u ", ll[i], cl[j], caps[c], n);
                hex("", buf, n + 2 < 620 ? n + 2 : 620);
            }
        for (unsigned j = 0; j < sizeof cl / 4; j++) {
            memset(buf, 0x44, 80);
            bool r = tls13_expand_label(prk, lab, m, cl[j], buf, 70);
            printf("xlabel l%u c%u %d ", ll[i], cl[j], r); hex("", buf, 72);
        }
        lab[ll[i]] = 'a';
    }
    printf("label null %u %u\n", tls13_hkdf_label(NULL, 99, 1, "x", m, 1),
           tls13_hkdf_label(buf, 99, 1, NULL, m, 1));
    printf("label nullctx0 %u ", tls13_hkdf_label(buf, 99, 7, "x", NULL, 0)); hex("", buf, 12);
    static const uint32_t xo[] = {0, 1, 8160, 8161, 65535, 65536, 65537};
    for (unsigned i = 0; i < sizeof xo / 4; i++) {
        printf("xlabel out%u %d\n", xo[i],
               xo[i] <= 8160 ? tls13_expand_label(prk, "key", 0, 0, buf, xo[i])
                             : tls13_expand_label(prk, "key", 0, 0, NULL, xo[i]));
        if (xo[i] <= 8160) hex("  first", buf, xo[i] < 40 ? xo[i] : 40);
    }

    uint8_t d[32];
    printf("derive %d ", tls13_derive_secret(prk, "derived", (const uint8_t *)"", 0, d)); hex("", d, 32);
    printf("derive m %d ", tls13_derive_secret(prk, "c hs traffic", m, 300, d)); hex("", d, 32);
    printf("derive_h %d ", tls13_derive_secret_h(prk, "x", prk, d)); hex("", d, 32);

    /* the ladder */
    static const uint32_t pl[] = {0, 1, 32, 48};
    for (unsigned i = 0; i < sizeof pl / 4; i++)
        for (int nullpsk = 0; nullpsk < 2; nullpsk++) {
            tls13_schedule_t s;
            memset(&s, 0x77, sizeof s);
            tls13_early_secret(&s, nullpsk ? NULL : k, pl[i]);
            printf("ladder p%u n%d ", pl[i], nullpsk); hex("", (uint8_t *)&s, sizeof s);
            printf("  master-early %d\n", tls13_master_secret(&s, m, 10));
            printf("  hs nulldhe %d %d\n", tls13_handshake_secret(&s, NULL, 32, m, 10),
                   tls13_handshake_secret(&s, k, 0, m, 10));
            printf("  hs %d ", tls13_handshake_secret(&s, k, 32, m, 100 + i));
            hex("", (uint8_t *)&s, sizeof s);
            printf("  ms %d ", tls13_master_secret(&s, m, 200 + i)); hex("", (uint8_t *)&s, sizeof s);
            uint8_t key[40], iv[20];
            memset(key, 0, sizeof key); memset(iv, 0, sizeof iv);
            printf("  tk %d ", tls13_traffic_keys(s.c_ap_traffic, key, 16, iv, 12));
            hex("", key, 40); hex("  iv", iv, 20);
            printf("  tk keyonly %d ", tls13_traffic_keys(s.s_ap_traffic, key, 32, NULL, 12)); hex("", key, 40);
            printf("  tk ivonly %d ", tls13_traffic_keys(s.s_ap_traffic, NULL, 32, iv, 12)); hex("", iv, 20);
            printf("  tk none %d\n", tls13_traffic_keys(s.s_ap_traffic, NULL, 0, NULL, 0));
            printf("  fin %d ", tls13_finished(s.c_hs_traffic, prk, d)); hex("", d, 32);
        }
    tls13_early_secret(NULL, k, 3);
    printf("hs null %d ms null %d\n", tls13_handshake_secret(NULL, k, 3, m, 3), tls13_master_secret(NULL, m, 3));
    printf("tk null %d %d %d\n", tls13_traffic_keys(NULL, NULL, 0, NULL, 0),
           tls13_traffic_keys(NULL, buf, 16, NULL, 0), tls13_traffic_keys(NULL, NULL, 0, buf, 12));
    printf("fin null %d\n", tls13_finished(NULL, prk, d));

    uint8_t a[40], b[40];
    pat(a, 40, 1); pat(b, 40, 1);
    for (uint32_t n = 0; n <= 40; n += 13) printf("ct eq %u %d\n", n, ct_equal(a, b, n));
    for (uint32_t p = 0; p < 40; p += 3) {
        b[p] ^= 0x80;
        printf("ct diff%u %d %d %d\n", p, ct_equal(a, b, 40), ct_equal(a, b, p), ct_equal(a, b, p + 1));
        b[p] ^= 0x80;
    }
    printf("ct null %d %d %d\n", ct_equal(NULL, b, 0), ct_equal(a, NULL, 0), ct_equal(a, b, 0));
    return 0;
}
