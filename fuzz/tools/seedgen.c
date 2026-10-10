/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* seedgen.c — writes the binary seed corpora (fuzz/corpus/<target>/seed-*)
 * using the modules' own encoders and the kernel test fixtures, so every
 * seed is a VALID input the fuzzer can mutate from. Run by `make -C fuzz
 * seeds`; its output is committed. Text seeds (copybooks, XML, manifests)
 * are written by seeds.sh. Deterministic: same tree, same bytes. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "freight.h"
#include "handshake.h"
#include "ipfs_node.h"
#include "ubh.h"
#include "vna_agree.h"
#include "vna_cmd.h"
#include "vna_frame.h"
#include "vna_wire.h"
#include "zt_gguf.h"
#include "../../kernel/src/tensor/test_gguf_fixture.h"
#include "../../kernel/src/update/test_zx_upcheck_vectors.h"

static const char *g_root;

static void put(const char *target, const char *name, const void *a, size_t na, const void *b,
                size_t nb)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", g_root, target);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/%s/seed-%s", g_root, target, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    if (na) fwrite(a, 1, na, f);
    if (nb) fwrite(b, 1, nb, f);
    fclose(f);
}

static void le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void le32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void gguf(void)
{
    put("fuzz_gguf_loader", "fixture", GGUF_FIXTURE, sizeof GGUF_FIXTURE, 0, 0);
    /* header only, and the fixture cut short in the tensor data */
    put("fuzz_gguf_loader", "header", GGUF_FIXTURE, 24, 0, 0);
    put("fuzz_gguf_loader", "truncated", GGUF_FIXTURE, sizeof GGUF_FIXTURE - 100, 0, 0);
}

/* the layout test_fat32.c builds: 1 reserved, 1 FAT, root + a file + a dir */
static void fat32(void)
{
    static uint8_t d[16 * 512];
    memset(d, 0, sizeof d);
    le16(d + 11, 512);
    d[13] = 1;
    le16(d + 14, 1);
    d[16] = 1;
    le32(d + 36, 1);
    le32(d + 44, 2);
    uint8_t *fat = d + 512;
    le32(fat + 0, 0x0FFFFFF8u);
    le32(fat + 4, 0x0FFFFFFFu);
    le32(fat + 8, 0x0FFFFFFFu);  /* root */
    le32(fat + 12, 5);           /* HELLO.TXT: clusters 3 -> 5 */
    le32(fat + 16, 0x0FFFFFFFu); /* SUB dir */
    le32(fat + 20, 0x0FFFFFFFu);
    uint8_t *root = d + 2 * 512;
    memcpy(root, "HELLO   TXT", 11);
    root[11] = 0x20;
    le16(root + 26, 3);
    le32(root + 28, 700);
    memcpy(root + 32, "SUB        ", 11);
    root[32 + 11] = 0x10;
    le16(root + 32 + 26, 4);
    memcpy(root + 64, "\xE5OLD    TXT", 11);
    uint8_t *sub = d + 4 * 512;
    memcpy(sub, ".          ", 11);
    sub[11] = 0x10;
    le16(sub + 26, 4);
    memcpy(sub + 32, "INNER   BIN", 11);
    sub[32 + 11] = 0x20;
    le16(sub + 32 + 26, 3);
    le32(sub + 32 + 28, 10);
    memset(d + 3 * 512, 'h', 512);
    memset(d + 5 * 512, 'i', 188);
    put("fuzz_fat32_mount", "volume", d, 6 * 512, 0, 0);
}

static void ubh(void)
{
    uint8_t out[21 * 8];
    for (int c = 0; c < 8; c++) {
        ubh_168_header_t h;
        ubh_168_header_init(&h, (ubh_frame_class_t) c);
        h.payload_length = 21u * (uint32_t) c;
        h.schema_id = 0x32414E56u;
        ubh_168_header_pack(&h, out + 21 * c);
    }
    put("fuzz_ubh_frame", "frames", out, sizeof out, 0, 0);
    put("fuzz_ubh_frame", "one", out, 21, 0, 0);
}

static void vinea(void)
{
    static uint8_t rec[8192], fr[9000];
    vna_agreement_t *a = calloc(1, sizeof *a);
    vna_id_t owner;
    memset(&owner, 0x11, sizeof owner);
    vna_agree_default(a, &owner);
    int32_t n = vna_agree_encode(a, rec, sizeof rec);
    uint8_t sel = 11; /* index of vna_agreement_schema in fuzz_vinea.c */
    if (n > 0) {
        put("fuzz_vinea", "agreement", &sel, 1, rec, (size_t) n);
        int32_t w = vna_frame_wrap(&vna_agreement_schema, rec, (uint32_t) n, fr, sizeof fr);
        if (w > 0) put("fuzz_vinea", "agreement-ubh", &sel, 1, fr, (size_t) w);
    }
    free(a);
    vna_cmd_t c;
    memset(&c, 0, sizeof c);
    c.verb = VNA_V_FETCH;
    c.resource = VNA_RES_FILE;
    c.has = VNA_CMD_ROOT | VNA_CMD_INDEX;
    for (int i = 0; i < 32; i++) c.root.b[i] = (uint8_t) (i * 7);
    c.index = 12345678901234ull;
    n = vna_cmd_encode(&c, rec, sizeof rec);
    sel = 0;
    if (n > 0) put("fuzz_vinea", "cmd", &sel, 1, rec, (size_t) n);
    const char *hk = "ask(compute,price:21,units:100)";
    put("fuzz_vinea", "hk", &sel, 1, hk, strlen(hk));
    /* an ack body (schema 4) plain and framed */
    vna_b_ack_t ack = {.status = 1};
    sel = 4;
    n = vna_schema_pack(&vna_b_ack_schema, &ack, rec, sizeof rec, true);
    if (n > 0) {
        put("fuzz_vinea", "ack", &sel, 1, rec, (size_t) n);
        int32_t w = vna_frame_wrap(&vna_b_ack_schema, rec, (uint32_t) n, fr, sizeof fr);
        if (w > 0) put("fuzz_vinea", "ack-ubh", &sel, 1, fr, (size_t) w);
    }
}

static void tls(void)
{
    static uint8_t rec[TLS_REC_BUF], buf[4096];
    uint8_t mode = 0;
    uint32_t w =
        tls_record_write(0, TLS_CT_HANDSHAKE, (const uint8_t *) "\x02\0\0\0", 4, rec, sizeof rec);
    put("fuzz_tls_record", "clear", &mode, 1, rec, w);
    /* a protected record under an all-0x42 key/IV */
    uint8_t ki[1 + 44];
    ki[0] = 1;
    memset(ki + 1, 0x42, 44);
    tls_keys_t k;
    tls_keys_set(&k, ki + 1, ki + 33);
    w = tls_record_write(&k, TLS_CT_APPLICATION_DATA, (const uint8_t *) "GET / HTTP/1.1\r\n", 16,
                         rec, sizeof rec);
    memcpy(buf, ki, sizeof ki);
    memcpy(buf + sizeof ki, rec, w);
    put("fuzz_tls_record", "protected", buf, sizeof ki + w, 0, 0);

    /* handshake RAW mode: the ServerHello the harness would build */
    tls_client_t *c = calloc(1, sizeof *c);
    uint8_t rnd[32], priv[32], ch[2048];
    for (int i = 0; i < 32; i++) {
        rnd[i] = (uint8_t) (i * 7 + 1);
        priv[i] = (uint8_t) (0x55 ^ (i * 13));
    }
    tls_client_init(c, "example.org", rnd, priv, TLS_VERIFY_INSECURE_ACKNOWLEDGED);
    tls_client_hello(c, ch, sizeof ch);
    uint8_t spriv[32], spub[32], sh[4 + 128];
    for (int i = 0; i < 32; i++) spriv[i] = (uint8_t) (0x77 + i * 3);
    x25519_public(spub, spriv);
    uint32_t n = 4;
    sh[n++] = 3;
    sh[n++] = 3;
    for (int i = 0; i < 32; i++) sh[n++] = (uint8_t) (0xA0 + i);
    sh[n++] = 32;
    memcpy(sh + n, c->session_id, 32);
    n += 32;
    sh[n++] = 0x13;
    sh[n++] = 0x03;
    sh[n++] = 0;
    static const uint8_t ext[] = {0, 43, 0, 2, 3, 4, 0, 51, 0, 36, 0, 0x1D, 0, 32};
    sh[n++] = 0;
    sh[n++] = (uint8_t) (sizeof ext + 32);
    memcpy(sh + n, ext, sizeof ext);
    n += sizeof ext;
    memcpy(sh + n, spub, 32);
    n += 32;
    sh[0] = TLS_HS_SERVER_HELLO;
    sh[1] = 0;
    sh[2] = 0;
    sh[3] = (uint8_t) (n - 4);
    w = tls_record_write(0, TLS_CT_HANDSHAKE, sh, n, rec, sizeof rec);
    mode = 0;
    put("fuzz_tls_handshake", "raw-serverhello", &mode, 1, rec, w);
    mode = 4; /* RAW, 7-byte feeds */
    put("fuzz_tls_handshake", "raw-chunked", &mode, 1, rec, w);
    /* FLIGHT mode: EE, empty Certificate, CertificateVerify, Finished */
    static const uint8_t flight[] = {
        8,  0,  0,  2,  0,  0,                         /* EncryptedExtensions */
        11, 0,  0,  4,  0,  0,  0,  0,                 /* Certificate         */
        15, 0,  0,  4,  8,  7,  0,  0,                 /* CertificateVerify   */
        20, 0,  0,  32, 1,  2,  3,  4,  5,  6,  7,  8, /* Finished (wrong MAC) */
        9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
        21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
    mode = 1;
    put("fuzz_tls_handshake", "flight", &mode, 1, flight, sizeof flight);
    mode = 1 | (1 << 2); /* FLIGHT, 64-byte records */
    put("fuzz_tls_handshake", "flight-split", &mode, 1, flight, sizeof flight);
    free(c);
}

static void freight(void)
{
    static uint8_t pk[FREIGHT_BYTES], buf[FREIGHT_BYTES + 128];
    const char *msg = "freight seed payload: twenty bytes per row, any k rows rebuild it.";
    uint32_t len = (uint32_t) strlen(msg);
    freight_header_t h;
    freight_header_init(&h, 42, 8, 0, 0, (const uint8_t *) msg, len);
    uint8_t hb[FREIGHT_HEADER_BYTES];
    freight_header_serialize(&h, hb);
    freight_encode(&h, (const uint8_t *) msg, pk);
    buf[0] = 0;
    memcpy(buf + 1, hb, sizeof hb);
    memcpy(buf + 1 + sizeof hb, pk + 21 * 3, 21 * 8); /* rows 3..10 */
    put("fuzz_freight", "decode", buf, 1 + sizeof hb + 21 * 8, 0, 0);
    /* op stream */
    static uint8_t ops[512];
    freight_ops_writer_t ow;
    freight_ops_writer_init(&ow, ops, sizeof ops, 64, NULL);
    freight_ops_put_literal(&ow, (const uint8_t *) "abcd", 4);
    freight_ops_put_copy(&ow, 4, 12);
    uint8_t rep[2] = {'x', 'y'};
    freight_ops_put_seed(&ow, FREIGHT_GEN_REPEAT, rep, 2, 16);
    uint8_t xs[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    freight_ops_put_seed(&ow, FREIGHT_GEN_XORSHIFT, xs, 8, 16);
    freight_ops_put_cidref(&ow, (const uint8_t *) "cid", 3, 16);
    uint64_t olen = 0;
    if (freight_ops_writer_finish(&ow, &olen) == FREIGHT_OK) {
        uint8_t m = 1;
        put("fuzz_freight", "ops", &m, 1, ops, (size_t) olen);
    }
    uint8_t m = 2;
    put("fuzz_freight", "encode", &m, 1, msg, len);
    /* erasure mode: k = 4, erase rows 0..2 */
    uint8_t er[1 + 1 + 21];
    memset(er, 0, sizeof er);
    er[0] = 3;
    er[1] = 4;
    er[2] = 0x07;
    put("fuzz_freight", "erasure", er, sizeof er, msg, len);
}

static void ipfs(void)
{
    ipfsn_cid_t c;
    char s[IPFSN_CID_STR_MAX];
    uint8_t b[IPFSN_CID_BIN_MAX];
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "hello", 5, &c);
    int n = ipfsn_cid_to_string(&c, s, sizeof s);
    put("fuzz_ipfs_cid", "v1-raw", s, (size_t) n, 0, 0);
    n = ipfsn_cid_encode(&c, b, sizeof b);
    put("fuzz_ipfs_cid", "v1-bin", b, (size_t) n, 0, 0);
    ipfsn_cid_t v0 = c;
    v0.version = 0;
    v0.codec = IPFSN_MC_DAG_PB;
    n = ipfsn_cid_to_string(&v0, s, sizeof s);
    if (n > 0) put("fuzz_ipfs_cid", "v0", s, (size_t) n, 0, 0);
    n = ipfsn_ubh_cid_to_text(&c, s, sizeof s);
    if (n > 0) put("fuzz_ipfs_cid", "ubh-text", s, (size_t) n, 0, 0);
    const char *uri = "ipfs://9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    put("fuzz_ipfs_cid", "uri", uri, strlen(uri), 0, 0);

    uint8_t m = 1;
    put("fuzz_ipfs_dag", "car", &m, 1, KUBO_BUCKET_CAR, sizeof KUBO_BUCKET_CAR);
    ipfsn_car_t car;
    if (ipfsn_car_open(&car, KUBO_BUCKET_CAR, sizeof KUBO_BUCKET_CAR) == IPFSN_OK) {
        const uint8_t *blk;
        uint32_t bl;
        int i = 0;
        m = 0;
        while (ipfsn_car_next(&car, &c, &blk, &bl) == IPFSN_OK && i < 8) {
            char nm[16];
            snprintf(nm, sizeof nm, "block%d", i++);
            put("fuzz_ipfs_dag", nm, &m, 1, blk, bl);
        }
    }
    static uint8_t env[4096];
    uint32_t el = 0;
    ipfsn_cid_sha256(IPFSN_MC_RAW, (const uint8_t *) "hello", 5, &c);
    if (ipfsn_ubh_encode_block(&c, (const uint8_t *) "hello", 5, env, sizeof env, &el) ==
        IPFSN_OK) {
        m = 2;
        put("fuzz_ipfs_dag", "ubh-block", &m, 1, env, el);
    }
    if (ipfsn_ubh_encode_cid(&c, env, sizeof env, &el) == IPFSN_OK) {
        m = 2;
        put("fuzz_ipfs_dag", "ubh-cid", &m, 1, env, el);
    }
}

static void upcheck(void)
{
    static uint8_t buf[2048];
    static const struct {
        const char *name;
        const unsigned char *p;
        size_t n;
    } recs[] = {{"ipns-c-seq0", REC_C_SEQ0, sizeof REC_C_SEQ0},
                {"ipns-c-seq1-v2", REC_C_SEQ1_V2, sizeof REC_C_SEQ1_V2},
                {"ipns-spec-v1", SPEC_REC_V1, sizeof SPEC_REC_V1},
                {"ipns-spec-v1v2", SPEC_REC_V1V2, sizeof SPEC_REC_V1V2},
                {"ipns-spec-v2", SPEC_REC_V2, sizeof SPEC_REC_V2}};
    for (size_t i = 0; i < sizeof recs / sizeof recs[0]; i++) {
        buf[0] = 1;
        memset(buf + 1, 0, 32);
        memcpy(buf + 33, recs[i].p, recs[i].n);
        put("fuzz_upcheck", recs[i].name, buf, 33 + recs[i].n, 0, 0);
    }
}

static void bootlegger(void)
{
    uint8_t b[1 + 1 + 8 + 8 + 32 + 64];
    memset(b, 0, sizeof b);
    b[0] = 0;
    b[1] = 0;
    memcpy(b + 2, "TCPZEDEC", 8);
    b[11] = 1; /* version 1, big-endian */
    put("fuzz_bootlegger", "handshake", b, sizeof b, 0, 0);
    uint8_t m[1 + 32 + 3 + 5 + 16];
    memset(m, 0, sizeof m);
    m[0] = 1;
    m[1 + 32] = 1;
    m[1 + 32 + 2] = 5;
    put("fuzz_bootlegger", "msg", m, sizeof m, 0, 0);
    uint8_t k = 2;
    put("fuzz_bootlegger", "kem", &k, 1, 0, 0);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s CORPUS_DIR\n", argv[0]);
        return 2;
    }
    g_root = argv[1];
    gguf();
    fat32();
    ubh();
    vinea();
    tls();
    freight();
    ipfs();
    upcheck();
    bootlegger();
    return 0;
}
