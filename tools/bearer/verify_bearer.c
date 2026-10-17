#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aes256_gcm.h"
#include "sha256.h"
#define SK_BYTES 4896
/* usage: verify_bearer <sealed> <passphrase> <aad> [rail] */
int main(int argc, char **argv) {
    FILE *f = fopen(argv[1], "rb");
    uint8_t blob[32 + 12 + SK_BYTES + 16];
    size_t n = fread(blob, 1, sizeof blob, f);
    fclose(f);
    if (n != 32 + 12 + SK_BYTES + 16) { printf("%s: bad size %zu\n", argv[1], n); return 1; }
    uint8_t salt[32], iv[12], tag[16], key[32], out[SK_BYTES];
    memcpy(salt, blob, 32);
    memcpy(iv, blob + 32, 12);
    memcpy(tag, blob + 32 + 12 + SK_BYTES, 16);
    sha256_ctx_t k;
    sha256_init(&k);
    sha256_update(&k, (const uint8_t *) argv[2], strlen(argv[2]));
    sha256_update(&k, salt, 32);
    if (argc > 4) sha256_update(&k, (const uint8_t *) argv[4], strlen(argv[4]));
    sha256_final(&k, key);
    const uint8_t *ct = blob + 32 + 12;
    int ok = aes256_gcm_decrypt(key, iv, (const uint8_t *) argv[3], strlen(argv[3]), ct, SK_BYTES,
                                tag, out);
    printf("%s: decrypt %s\n", argv[1], ok ? "OK" : "FAILED");
    return ok ? 0 : 1;
}
