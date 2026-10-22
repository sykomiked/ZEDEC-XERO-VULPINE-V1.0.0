/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Seedbank's model against zxvd's real dispatcher: web4.cid, files.root,
 * files.stat and spaces.set_cid as zxvd runs them, and aipi.* first with
 * no blockstore (as here) and then through a blockstore below that names
 * each block as zxvd's does ("z" + base58btc of the CIDv1 raw sha2-256 of
 * its bytes) and keeps what it was given. */
#include "seedbank_model.h"

#define SB_BLOCKS 80u

static struct {
    char cid[SEEDBANK_BLOCK_MAX + 1];
    uint8_t data[SEEDBANK_GET_MAX];
    uint32_t len;
} SB_STORE[SB_BLOCKS];
static uint32_t SB_N, SB_PUTS, SB_GETS;
static uint64_t SB_BYTES;
static bool SB_PRIV, SB_LIE;
static char SB_PATH[SEEDBANK_PATH_MAX + 1];
static uint8_t SB_FILE[SEEDBANK_PUT_MAX + 2];

/* "z" + base58btc of 0x01 <codec> 0x12 0x20 sha256(data), as aipi names a block */
static void sb_name(uint8_t codec, const uint8_t *data, uint32_t len, char *out)
{
    static const char B58[] = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    uint8_t bin[SEEDBANK_BIN_LEN], digit[64];
    uint32_t nd = 0;
    bin[0] = 0x01, bin[1] = codec, bin[2] = 0x12, bin[3] = 0x20;
    sha256(data, len, bin + 4);
    for (uint32_t i = 0; i < sizeof bin; i++) { /* digits = digits * 256 + byte */
        uint32_t carry = bin[i];
        for (uint32_t j = 0; j < nd; j++) {
            carry += (uint32_t) digit[j] << 8;
            digit[j] = (uint8_t) (carry % 58u);
            carry /= 58u;
        }
        while (carry) {
            digit[nd++] = (uint8_t) (carry % 58u);
            carry /= 58u;
        }
    }
    out[0] = 'z';
    for (uint32_t i = 0; i < nd; i++) out[1 + i] = B58[digit[nd - 1u - i]];
    out[1 + nd] = 0;
}

static const char *sb_put(const uint8_t *data, uint32_t len, const char *path, bool priv,
                          zxvd_buf_t *out)
{
    char name[SEEDBANK_BLOCK_MAX + 1];
    if (path) { /* zxvd reads the file itself, at most 1 MiB */
        FILE *f = fopen(path, "rb");
        if (!f) return "the file could not be opened";
        size_t n = fread(SB_FILE, 1, SEEDBANK_PUT_MAX + 1u, f);
        fclose(f);
        if (n > SEEDBANK_PUT_MAX) return "the file is larger than 1 MiB (AIPI_MODULE_MAX)";
        memcpy(SB_PATH, path, strlen(path) + 1u);
        data = SB_FILE;
        len = (uint32_t) n;
    }
    if (SB_N >= SB_BLOCKS) return "the blockstore is full";
    sb_name(0x55, SB_LIE ? (const uint8_t *) "other" : data, SB_LIE ? 5u : len, name);
    memcpy(SB_STORE[SB_N].cid, name, strlen(name) + 1u);
    SB_STORE[SB_N].len = len;
    if (len <= SEEDBANK_GET_MAX) memcpy(SB_STORE[SB_N].data, data, len);
    SB_N++;
    SB_PUTS++;
    SB_BYTES += len;
    SB_PRIV = priv;
    zxvd_buf_printf(out, "{\"cid\":\"%s\",\"bytes\":%u,\"private\":%s}", name, len,
                    priv ? "true" : "false");
    return NULL;
}

static const char *sb_get(const char *cid, zxvd_buf_t *out)
{
    static const char HX[] = "0123456789abcdef";
    static char hex[2u * SEEDBANK_GET_MAX + 1u];
    SB_GETS++;
    for (uint32_t i = SB_N; i-- > 0;) {
        if (strcmp(SB_STORE[i].cid, cid)) continue;
        if (SB_STORE[i].len > SEEDBANK_GET_MAX) return "the block is larger than 65536 bytes";
        for (uint32_t k = 0; k < SB_STORE[i].len; k++) {
            hex[2u * k] = HX[SB_STORE[i].data[k] >> 4];
            hex[2u * k + 1u] = HX[SB_STORE[i].data[k] & 15u];
        }
        hex[2u * SB_STORE[i].len] = 0;
        zxvd_buf_printf(out, "{\"cid\":\"%s\",\"bytes\":%u,\"hex\":\"", cid, SB_STORE[i].len);
        zxvd_buf_raw(out, hex, 2u * SB_STORE[i].len);
        zxvd_buf_raw(out, "\"}", 2);
        return NULL;
    }
    return "not found";
}

static void sb_status(zxvd_buf_t *out)
{
    zxvd_buf_printf(out, "{\"online\":false,\"blocks\":%u,\"store_bytes\":%llu,\"store_cap\":%u}",
                    SB_N, (unsigned long long) SB_BYTES, 32u << 20);
}

static int sb_raw(const char *method, const char *params)
{
    zxm_begin(&LINK, method);
    zxm_p_raw(&LINK, params, (uint32_t) strlen(params));
    return zxm_call(&LINK);
}

APPS_TEST(seedbank)
{
    static seedbank_t S;
    static const zxvd_aipi_ops_t AIPI = {sb_put, sb_get, NULL, NULL, sb_status};
    static const char HELLO[] = "bafkreibm6jg3ux5qumhcn2b3flc3tyu6dmlb4xa7u5bf44yegnrjhc4yeq";
    static uint8_t big[SEEDBANK_HEX_PUT + 1u];
    char root[512], name[SEEDBANK_BLOCK_MAX + 1], cid[SEEDBANK_CID_LEN + 1], space[64], q[128];
    uint32_t at = 99, at2 = 99;
    uint64_t size;
    const int64_t NOW = 1760000000;
    seedbank_init(&S, &LINK);

    /* SM1: aipi's name and the CID text spell one CID */
    sb_name(0x55, (const uint8_t *) "hello", 5, name);
    CHECK(name[0] == 'z' && seedbank_block_to_cid(name, cid) == ZXM_OK && !strcmp(cid, HELLO),
          "hello's block name %s turns into its CID text %s", name, cid);
    sb_name(0x70, (const uint8_t *) "hello", 5, name);
    CHECK(seedbank_block_to_cid(name, cid) == ZXM_E_ARG && !cid[0],
          "a dag-pb CID is no raw block: refused");
    CHECK(seedbank_block_to_cid(HELLO, cid) == ZXM_E_ARG &&
              seedbank_block_to_cid("z0abc", cid) == ZXM_E_ARG &&
              seedbank_block_to_cid("z", cid) == ZXM_E_ARG &&
              seedbank_block_to_cid("z1111", cid) == ZXM_E_ARG &&
              seedbank_block_to_cid(NULL, cid) == ZXM_E_ARG,
          "not \"z\" + base58btc of a raw CID: refused");

    /* no blockstore here: zxvd says so */
    CHECK(seedbank_status(&S) == ZXM_E_REMOTE && LINK.err_code == -32601 && S.store_x100 == -1,
          "no blockstore: %s", LINK.err);
    CHECK(seedbank_pin_bytes(&S, (const uint8_t *) "hello", 5, false, NOW, &at) == ZXM_E_REMOTE &&
              LINK.err_code == -32601 && S.npins == 0,
          "no blockstore, no pin: %s", LINK.err);
    zxvd_rpc_set_aipi_ops(&AIPI);

    /* aipi.status: empty, the share in use is used_share.used_x100's */
    CHECK(seedbank_status(&S) == ZXM_OK && !S.online && S.blocks == 0 && S.store_x100 == 0 &&
              S.store_cap == 32u << 20,
          "an empty store: %s", LINK.err);

    /* SM2: pin bytes, named on this device first */
    CHECK(seedbank_pin_bytes(&S, (const uint8_t *) "hello", 5, false, NOW, &at) == ZXM_OK &&
              at == 0 && S.npins == 1 && !strcmp(S.pin[0].cid, HELLO) && S.pin[0].block[0] == 'z' &&
              S.pin[0].bytes == 5 && !S.pin[0].sealed && S.pin[0].stamp == NOW && SB_PUTS == 1 &&
              !SB_PRIV,
          "hello pinned as %s: %s", S.pin[0].cid, LINK.err);
    CHECK(seedbank_pin_bytes(&S, (const uint8_t *) "hello", 5, false, NOW + 1, &at) == ZXM_OK &&
              at == 0 && S.npins == 1 && S.pin[0].stamp == NOW && SB_PUTS == 2,
          "pinned again: one record");
    CHECK(seedbank_pin_bytes(&S, (const uint8_t *) "seed", 4, true, NOW, &at2) == ZXM_OK &&
              at2 == 1 && S.pin[1].sealed && SB_PRIV && seedbank_find(&S, S.pin[1].block) == 1 &&
              seedbank_find(&S, S.pin[1].cid) == 1 && seedbank_find(&S, "bafy") == -1,
          "a sealed pin: %s", LINK.err);
    CHECK(seedbank_status(&S) == ZXM_OK && S.blocks == 3 && S.store_bytes == 14 &&
              S.store_x100 == seedbank_used_x100(32 << 20, (32 << 20) - 14),
          "the store's use: %lld", (long long) S.store_x100);

    /* refusals before anything is sent */
    int before = sent;
    CHECK(seedbank_pin_bytes(&S, big, SEEDBANK_HEX_PUT + 1u, false, NOW, &at) == ZXM_E_TOOBIG &&
              seedbank_pin_bytes(&S, big, 1, false, -1, &at) == ZXM_E_ARG &&
              seedbank_pin_bytes(&S, NULL, 3, false, NOW, &at) == ZXM_E_ARG &&
              seedbank_pin_file(&S, "", false, NOW, &at) == ZXM_E_ARG &&
              seedbank_pin_file(&S, "/etc/passwd", false, NOW, &at) == ZXM_E_ARG &&
              seedbank_fetch(&S, 9) == ZXM_E_ARG &&
              seedbank_publish(&S, 9, "x", true) == ZXM_E_ARG && sent == before,
          "8001 bytes by hex, a bad stamp, no data, no path, an outside path: nothing sent");
    CHECK(seedbank_pin_bytes(&S, big, SEEDBANK_HEX_PUT, false, NOW, &at) == ZXM_OK && at == 2 &&
              S.pin[2].bytes == SEEDBANK_HEX_PUT,
          "8000 bytes by hex: %s", LINK.err);

    /* the store names other bytes: nothing kept */
    SB_LIE = true;
    CHECK(seedbank_pin_bytes(&S, (const uint8_t *) "lie", 3, false, NOW, &at) == ZXM_E_PROTOCOL &&
              S.npins == 3,
          "a block named for other bytes is not kept");
    SB_LIE = false;

    /* SM3: fetch, and the ipfs:// form */
    CHECK(seedbank_fetch(&S, 0) == ZXM_OK && S.got_len == 5 && !memcmp(S.got, "hello", 5) &&
              S.got_pin == 0,
          "fetched hello: %s", LINK.err);
    CHECK(seedbank_uri(&S, 0) == ZXM_OK && !strncmp(S.pin[0].uri, "ipfs://", 7) &&
              !strcmp(S.pin[0].uri + 7, HELLO),
          "its uri: %s %s", S.pin[0].uri, LINK.err);

    /* SM2 by path: a file of the Hoard */
    memset(big, 'q', sizeof big);
    CHECK(zxf_mkdir("seedbank") == 0 &&
              zxf_write("seedbank/note.txt", (const uint8_t *) "pinned note", 11, ZXF_W_REPLACE,
                        false, &size) == 0,
          "a note in the Hoard");
    CHECK(zxf_root(root, sizeof root) == 0 &&
              seedbank_pin_file(&S, "seedbank/note.txt", false, NOW, &at) == ZXM_OK && at == 3 &&
              S.pin[3].bytes == 11 && !strncmp(SB_PATH, root, strlen(root)) &&
              !strcmp(SB_PATH + strlen(root), "/seedbank/note.txt"),
          "pinned by path %s: %s", SB_PATH, LINK.err);
    CHECK(seedbank_fetch(&S, 3) == ZXM_OK && S.got_len == 11 && !memcmp(S.got, "pinned note", 11),
          "the note fetched back: %s", LINK.err);
    {
        static uint8_t wide[SEEDBANK_PUT_MAX + 1u];
        memset(wide, 'w', sizeof wide);
        CHECK(zxf_write("seedbank/wide.bin", wide, SEEDBANK_GET_MAX + 1u, ZXF_W_REPLACE, false,
                        &size) == 0 &&
                  seedbank_pin_file(&S, "seedbank/wide.bin", false, NOW, &at) == ZXM_OK &&
                  S.pin[at].bytes == SEEDBANK_GET_MAX + 1,
              "a block past 65536 bytes pinned by path: %s", LINK.err);
        uint32_t gets = SB_GETS;
        before = sent;
        CHECK(seedbank_fetch(&S, at) == ZXM_E_TOOBIG && sent == before && SB_GETS == gets,
              "it is never asked for (SR4)");
        CHECK(zxf_write("seedbank/full.bin", wide, SEEDBANK_PUT_MAX, ZXF_W_REPLACE, false, &size) ==
                      0 &&
                  seedbank_pin_file(&S, "seedbank/full.bin", false, NOW, &at) == ZXM_OK &&
                  S.pin[at].bytes == SEEDBANK_PUT_MAX,
              "1 MiB exactly: %s", LINK.err);
        uint32_t puts = SB_PUTS;
        CHECK(zxf_write("seedbank/over.bin", wide, SEEDBANK_PUT_MAX + 1u, ZXF_W_REPLACE, false,
                        &size) == 0 &&
                  seedbank_pin_file(&S, "seedbank/over.bin", false, NOW, &at) == ZXM_E_TOOBIG &&
                  SB_PUTS == puts,
              "1 MiB and a byte: nothing put (SR3)");
    }
    CHECK(seedbank_pin_file(&S, "seedbank", false, NOW, &at) == ZXM_E_ARG,
          "a folder is not pinned");
    CHECK(seedbank_pin_file(&S, "seedbank/none.txt", false, NOW, &at) == ZXM_E_REMOTE &&
              LINK.err_code == ZXVD_E_FILES,
          "a file that is not there: zxvd says so");

    /* SM4: publish on a cloud top this node owns, only on the owner's word */
    CHECK(sb_raw("spaces.create", "{\"kind\":\"cloudtop\",\"name\":\"Seed Shelf\"}") == ZXM_OK &&
              zxm_get_str(&LINK, LINK.result, "id", space, sizeof space) == ZXM_OK,
          "a cloud top: %s", LINK.err);
    before = sent;
    CHECK(seedbank_publish(&S, 0, space, false) == ZXM_E_STATE && sent == before &&
              !S.pin[0].space[0] && seedbank_unpublish(&S, space, false) == ZXM_E_STATE &&
              seedbank_publish(&S, 0, "home", true) == ZXM_E_ARG && sent == before,
          "not confirmed, or home: nothing sent");
    CHECK(seedbank_publish(&S, 0, space, true) == ZXM_OK && !strcmp(S.pin[0].space, space),
          "published hello on %s: %s", space, LINK.err);
    snprintf(q, sizeof q, "{\"space\":\"%s\"}", space);
    CHECK(sb_raw("spaces.get", q) == ZXM_OK &&
              zxm_get_str(&LINK, LINK.result, "cid", cid, sizeof cid) == ZXM_OK &&
              !strcmp(cid, HELLO),
          "zxvd keeps the CID: %s %s", cid, LINK.err);
    CHECK(seedbank_publish(&S, 1, space, true) == ZXM_OK && !S.pin[0].space[0] &&
              !strcmp(S.pin[1].space, space),
          "a space has one CID: the mark moves");
    CHECK(seedbank_forget(&S, 1) == ZXM_E_STATE && S.npins == 6, "a published pin stays");
    CHECK(seedbank_unpublish(&S, space, true) == ZXM_OK && !S.pin[1].space[0] &&
              sb_raw("spaces.get", q) == ZXM_OK &&
              zxm_get_str(&LINK, LINK.result, "cid", cid, sizeof cid) == ZXM_OK && !cid[0],
          "unpublished: %s", LINK.err);
    CHECK(seedbank_publish(&S, 0, "nowhere", true) == ZXM_E_REMOTE,
          "a space that is not there: zxvd says so: %s", LINK.err);

    /* SM5: forget */
    CHECK(seedbank_fetch(&S, 3) == ZXM_OK && S.got_pin == 3 && seedbank_forget(&S, 1) == ZXM_OK &&
              S.npins == 5 && S.got_pin == 2 && !strcmp(S.pin[0].cid, HELLO) &&
              seedbank_forget(&S, 2) == ZXM_OK && S.got_pin == -1 && S.got_len == 0 &&
              seedbank_forget(&S, 6) == ZXM_E_ARG,
          "forgotten from the table only");

    /* the table holds SEEDBANK_PINS */
    {
        uint32_t n = S.npins;
        bool ok = true;
        for (uint32_t i = n; ok && i < SEEDBANK_PINS; i++) {
            uint8_t one[4] = {'p', (uint8_t) i, (uint8_t) (i >> 8), '!'};
            ok = seedbank_pin_bytes(&S, one, 4, false, NOW, &at) == ZXM_OK && at == i;
        }
        before = sent;
        CHECK(ok && S.npins == SEEDBANK_PINS &&
                  seedbank_pin_bytes(&S, (const uint8_t *) "one more", 8, false, NOW, &at) ==
                      ZXM_E_TOOBIG &&
                  sent == before,
              "64 pins, then full: %s", LINK.err);
    }
    zxvd_rpc_set_aipi_ops(NULL);
}
