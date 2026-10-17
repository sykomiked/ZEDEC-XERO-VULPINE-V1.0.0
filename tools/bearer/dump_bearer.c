#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aes256_gcm.h"
#include "sha256.h"
#define SK_BYTES 4896
static void hex(const uint8_t *b, size_t n, char *o) {
    static const char *d = "0123456789abcdef";
    for (size_t i = 0; i < n; i++) { o[i*2] = d[b[i]>>4]; o[i*2+1] = d[b[i]&15]; }
    o[n*2] = 0;
}
/* usage: dump_bearer <dir> <name> <passphrase> <aad> [rail] */
int main(int argc, char **argv) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.sealed", argv[1], argv[2]);
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "missing %s\n", path); return 1; }
    uint8_t blob[32+12+SK_BYTES+16], salt[32], iv[12], tag[16], key[32], sk[SK_BYTES];
    size_t n = fread(blob, 1, sizeof blob, f);
    fclose(f);
    if (n != sizeof blob) { fprintf(stderr, "%s: bad size %zu\n", path, n); return 1; }
    memcpy(salt, blob, 32); memcpy(iv, blob+32, 12);
    memcpy(tag, blob+32+12+SK_BYTES, 16);
    sha256_ctx_t k;
    sha256_init(&k);
    sha256_update(&k, (const uint8_t *)argv[3], strlen(argv[3]));
    sha256_update(&k, salt, 32);
    if (argc > 5) sha256_update(&k, (const uint8_t *)argv[5], strlen(argv[5]));
    sha256_final(&k, key);
    if (!aes256_gcm_decrypt(key, iv, (const uint8_t *)argv[4], strlen(argv[4]),
                            blob+32+12, SK_BYTES, tag, sk)) {
        fprintf(stderr, "%s: decrypt failed\n", argv[2]);
        return 1;
    }
    /* the public key + the address */
    uint8_t pk[2592], ah[32];
    snprintf(path, sizeof path, "%s/%s.pub", argv[1], argv[2]);
    f = fopen(path, "rb");
    if (f) { if (fread(pk, 1, sizeof pk, f) != sizeof pk) {} fclose(f); }
    sha256(pk, sizeof pk, ah);
    char hsk[SK_BYTES*2+1], hpk[2592*2+1], ha[65];
    hex(sk, SK_BYTES, hsk); hex(pk, sizeof pk, hpk); hex(ah, 32, ha);
    printf("=== %s ===\naddress:     %s\npublic key:  %s\nsecret key:  %s\n\n", argv[2], ha, hpk, hsk);
    return 0;
}
