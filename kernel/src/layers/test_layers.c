/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_layers.c — the kernel reads mk_layers.py's manifests, names them by
 * the same CIDs, refuses every other layout and checks their lineage. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "layers.h"
#include "test_layers_vec.h"

static int checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

static const char *const VEC[5] = {VEC_L0, VEC_L1, VEC_L2, VEC_L3, VEC_L4};
static const char *const CID[5] = {CID_L0, CID_L1, CID_L2, CID_L3, CID_L4};

static char buf[1u << 21];

/* src with its first `old` replaced by `rep`, into buf; the new length, or
 * 0 if `old` is not there. */
static uint32_t sub(const char *src, const char *old, const char *rep)
{
    const char *at = strstr(src, old);
    if (!at) return 0;
    size_t a = (size_t) (at - src), o = strlen(old), r = strlen(rep), n = strlen(src);
    memcpy(buf, src, a);
    memcpy(buf + a, rep, r);
    memcpy(buf + a + r, at + o, n - a - o + 1);
    return (uint32_t) (n - o + r);
}

static int parse_ok(const char *t, uint32_t n, zxv_layer_t *L)
{
    const char *why = 0;
    return zxv_layer_parse(t, n, L, &why) == 0 && why == 0;
}

/* parse refuses t[0..n) and says why it did */
static int refused(const char *t, uint32_t n, const char *want)
{
    zxv_layer_t L;
    const char *why = 0;
    int r = zxv_layer_parse(t, n, &L, &why);
    if (r != -1 || !why || strcmp(why, want) != 0) {
        printf("  parse gave %d, \"%s\"; wanted \"%s\"\n", r, why ? why : "(none)", want);
        return 0;
    }
    return 1;
}

static zxv_layer_t V[5];

static void test_vectors(void)
{
    char text[61];
    for (int i = 0; i < 5; i++) {
        CHECK(parse_ok(VEC[i], (uint32_t) strlen(VEC[i]), &V[i]));
        CHECK(V[i].depth == i);
        zxv_layer_cid_text(V[i].cid, text);
        CHECK(strlen(text) == 59 && strcmp(text, CID[i]) == 0);
    }
    for (int i = 1; i < 5; i++) {
        zxv_layer_cid_text(V[i].parent, text);
        CHECK(strcmp(text, CID[i - 1]) == 0);
    }
    uint8_t zero[ZXV_LAYER_CID_BYTES] = {0};
    CHECK(memcmp(V[0].parent, zero, sizeof zero) == 0);
    CHECK(V[0].nobj == 2 && V[1].nobj == 2 && V[2].nobj == 1 && V[3].nobj == 1 && V[4].nobj == 1);
    for (int i = 0; i < 5; i++) /* the object lines, through the last newline */
        CHECK(V[i].objs_len > 0 && V[i].objs[V[i].objs_len - 1] == '\n' &&
              V[i].objs[V[i].objs_len] == ' ' && V[i].objs[V[i].objs_len + 1] == ']');
    CHECK(!V[0].compat && !V[1].compat && !V[2].compat && V[3].compat && !V[4].compat);
}

static void test_objects(void)
{
    zxv_layer_obj_t o;
    uint32_t pos = 0;
    CHECK(zxv_layer_next(&V[0], &pos, &o) == 1);
    CHECK(!strcmp(o.kind, "machine") && !strcmp(o.arch, "x86_64") && !strcmp(o.firmware, "UEFI"));
    CHECK(o.page_bytes == 4096 && o.sector_bytes == 512 && !o.has_bytes && !o.has_sha);
    CHECK(zxv_layer_next(&V[0], &pos, &o) == 1 && !strcmp(o.arch, "arm64"));
    CHECK(zxv_layer_next(&V[0], &pos, &o) == 0);

    pos = 0;
    CHECK(zxv_layer_next(&V[1], &pos, &o) == 1);
    CHECK(!strcmp(o.kind, "boot") && !strcmp(o.name, "BOOTX64.EFI") && o.bytes == 9);
    CHECK(o.has_bytes && o.has_sha && o.sha[0] == 0xb5 && o.sha[31] == 0xbd);
    CHECK(zxv_layer_next(&V[1], &pos, &o) == 1);
    CHECK(!strcmp(o.kind, "model") && !strcmp(o.arch, "all") && o.bytes == 491400032u);
    CHECK(!strcmp(o.name, "qwen2.5-0.5b-instruct-q4_k_m.gguf") && o.sha[0] == 0x74);
    CHECK(zxv_layer_next(&V[1], &pos, &o) == 0);

    pos = 0;
    CHECK(zxv_layer_next(&V[2], &pos, &o) == 1 && !strcmp(o.group, "REASON"));
    CHECK(!strcmp(o.name, "qwen2.5-1.5b-instruct-q4_k_m.gguf"));
    CHECK(zxv_layer_next(&V[2], &pos, &o) == 0);
}

static void test_refusals(void)
{
    uint32_t n0 = (uint32_t) strlen(VEC_L0), n1 = (uint32_t) strlen(VEC_L1);
    const char *L1P = " \"parent_layer_cid\": \"bafkrei";
    CHECK(refused(VEC_L0, n0 - 1, "does not end with a newline"));
    CHECK(refused(VEC_L0, 0, "does not end with a newline"));
    {
        const char *head = "{\n \"format\": \"zxv-layer 1\",\n";
        CHECK(refused(head, (uint32_t) strlen(head), "too short"));
    }
    CHECK(refused(buf, sub(VEC_L0, "zxv-layer 1", "zxv-layer 2"), "not a 'zxv-layer 1' manifest"));
    CHECK(refused(buf, sub(VEC_L0, "{\n", "[\n"), "not a 'zxv-layer 1' manifest"));
    CHECK(refused(buf, sub(VEC_L0, "\"layer_depth\": 0", "\"layer_depth\": 5"),
                  "layer_depth is not 0..4"));
    CHECK(refused(buf, sub(VEC_L0, "\"layer_depth\": 0,", "\"layer_depth\": 00,"),
                  "layer_depth is not 0..4"));
    CHECK(refused(buf, sub(VEC_L0, "\"layer_depth\": 0,", "\"layer_depth\": 0 "),
                  "layer_depth is not 0..4"));
    CHECK(refused(buf, sub(VEC_L0, "\"layer_depth\": 0,", "\"layer_depth\": 0,,"),
                  "layer_depth is not 0..4"));
    CHECK(refused(buf, sub(VEC_L0, "HARDWARE_ENVELOPE", "CORE_BOOT_ENGINE"),
                  "layer_name does not match its depth"));
    CHECK(refused(buf, sub(VEC_L1, "\"layer_depth\": 1", "\"layer_depth\": 2"),
                  "layer_name does not match its depth"));
    /* L0 alone has no parent */
    CHECK(refused(buf,
                  sub(VEC_L0, "\"layer_depth\": 0,\n \"layer_name\": \"HARDWARE_ENVELOPE\"",
                      "\"layer_depth\": 1,\n \"layer_name\": \"CORE_BOOT_ENGINE\""),
                  "parent_layer_cid is not a layer CID (bafkrei...)"));
    char l0p[128];
    snprintf(l0p, sizeof l0p, "\"parent_layer_cid\": \"%s\",", CID_L0);
    CHECK(refused(buf, sub(VEC_L0, "\"parent_layer_cid\": null,", l0p), "L0 names a parent"));
    /* a parent that is not a raw sha2-256 CIDv1 */
    CHECK(refused(buf, sub(VEC_L1, L1P, " \"parent_layer_cid\": \"bafyrei"),
                  "parent_layer_cid is not a layer CID (bafkrei...)"));
    CHECK(refused(buf, sub(VEC_L1, L1P, " \"parent_layer_cid\": \"Bafkrei"),
                  "parent_layer_cid is not a layer CID (bafkrei...)"));
    CHECK(refused(buf, sub(VEC_L1, CID_L0 + 55, "1ena"),
                  "parent_layer_cid is not a layer CID (bafkrei...)")); /* 1 is not base32 */
    CHECK(refused(buf, sub(VEC_L1, CID_L0 + 54, "z3enb"),
                  "parent_layer_cid is not a layer CID (bafkrei...)")); /* nonzero pad bits */
    CHECK(refused(buf, sub(VEC_L1, CID_L0 + 54, "z3enaa"),
                  "parent_layer_cid is not a layer CID (bafkrei...)")); /* 60 characters */
    CHECK(refused(buf, sub(VEC_L1, CID_L0 + 54, "z3en"),
                  "parent_layer_cid is not a layer CID (bafkrei...)")); /* 58 */
    /* compat */
    const char *PC = "parent_compat\": \"exact\"";
    CHECK(refused(buf, sub(VEC_L2, PC, "parent_compat\": \"evo_comm\""),
                  "parent_compat is not exact (or evo_comm at L3 and L4)"));
    CHECK(refused(buf, sub(VEC_L1, PC, "parent_compat\": \"loose\""),
                  "parent_compat is not exact (or evo_comm at L3 and L4)"));
    zxv_layer_t L;
    CHECK(parse_ok(buf, sub(VEC_L4, PC, "parent_compat\": \"evo_comm\""), &L) && L.compat);
    CHECK(refused(buf, sub(VEC_L0, " \"objects\": [", " \"objects\":["), "no objects list"));
    /* objects */
    {
        static char empty[512];
        const char *a = strstr(VEC_L2, "  {");
        size_t h = (size_t) (a - VEC_L2);
        memcpy(empty, VEC_L2, h);
        memcpy(empty + h, " ]\n}\n", 6);
        CHECK(refused(empty, (uint32_t) strlen(empty), "the layer holds nothing"));
    }
    CHECK(refused(buf, sub(VEC_L1, "\"bytes\": 9,", "\"bytes\": 9 ,"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"bytes\": 9,", "\"bytes\": 09,"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"bytes\": 9,", "\"bytes\": \"9\","),
                  "an object line is malformed"));
    CHECK(
        refused(buf, sub(VEC_L1, "\"bytes\": 9,", "\"size\": 9,"), "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"bytes\": 9,", "\"bytes\": 9, \"bytes\": 9,"),
                  "an object line is malformed"));
    CHECK(refused(buf,
                  sub(VEC_L1, "\"kind\": \"boot\", \"arch\": \"x86_64\"",
                      "\"arch\": \"x86_64\", \"kind\": \"boot\""),
                  "an object line is malformed")); /* out of order */
    CHECK(refused(buf, sub(VEC_L1, "\"sha256\": \"b5", "\"sha256\": \"B5"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"sha256\": \"b5", "\"sha256\": \""),
                  "an object line is malformed")); /* 62 digits */
    CHECK(refused(buf, sub(VEC_L1, "\"sha256\": \"b5", "\"sha256\": \"b5b5"),
                  "an object line is malformed")); /* 66 digits */
    CHECK(refused(buf, sub(VEC_L1, "\"sha256\": \"b5", "\"sha256\": \"bg"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"name\": \"BOOTX64.EFI\"", "\"name\": \"BOOT\\\\X64.EFI\""),
                  "an object line is malformed")); /* an escape */
    CHECK(refused(buf, sub(VEC_L1, "\"name\": \"BOOTX64.EFI\"", "\"name\": \"BOOT\x7fX64.EFI\""),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L0, "\"link_address\": \"0x100000\"", "\"link_address\": 1048576"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"cid\": \"bafkreifv", "\"cid\": 7, \"x\": \"bafkreifv"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"cid\": \"bafkreifv", "\"cid\": 7, \"cid\": \"bafkreifv"),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "\"name\": \"BOOTX64.EFI\"", "\"name\": \"BOOT\\\"X64.EFI\""),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L0, "\"firmware\": \"UEFI\"", "\"firmware\": \"UEFI-LONG\""),
                  "an object line is malformed")); /* longer than the field */
    CHECK(refused(buf, sub(VEC_L0, "\"firmware\": \"UEFI\"", "\"firmware\": \"UEFI-ABC\""),
                  "an object line is malformed")); /* no room for the NUL */
    CHECK(parse_ok(buf, sub(VEC_L0, "\"firmware\": \"UEFI\"", "\"firmware\": \"UEFI-AB\""), &L));
    CHECK(refused(buf, sub(VEC_L1, "{\"kind\": \"boot\", ", "{"), "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "  {\"kind\": \"boot\"", " {\"kind\": \"boot\""),
                  "an object line is malformed"));
    CHECK(refused(buf, sub(VEC_L1, "4oawxu\"},", "4oawxu\"}"), "commas between objects are wrong"));
    CHECK(refused(buf, sub(VEC_L1, "d7a9db\"}\n", "d7a9db\"},\n"),
                  "commas between objects are wrong"));
    CHECK(refused(buf, sub(VEC_L1, " ]\n}\n", " ]\n}\n\n"), "bytes after the closing brace"));
    CHECK(refused(buf, sub(VEC_L1, " ]\n}\n", " ]\n}}\n"), "bytes after the closing brace"));
    CHECK(refused(buf, sub(VEC_L1, " ]\n}\n", " ]\n"), "bytes after the closing brace"));
    CHECK(refused(buf, sub(VEC_L1, "d7a9db\"}\n ]\n}\n", "d7a9db\"},\n"),
                  "the objects list does not close"));
    CHECK(refused(buf, sub(VEC_L1, " ]\n}\n", "}\n"), "commas between objects are wrong"));
    /* the size limit comes before anything else */
    {
        uint32_t n = ZXV_LAYER_BYTES_MAX + 1u;
        memcpy(buf, VEC_L1, n1);
        memset(buf + n1, ' ', n - n1);
        buf[n - 1] = '\n';
        CHECK(refused(buf, n, "over 1 MiB"));
        CHECK(refused(buf, n - 1, "does not end with a newline"));
    }
}

static uint8_t admitted[ZXV_LAYER_CID_BYTES];
static int admit_calls;
static int admit(const uint8_t parent[ZXV_LAYER_CID_BYTES], void *ctx)
{
    admit_calls++;
    return ctx == &admitted && memcmp(parent, admitted, ZXV_LAYER_CID_BYTES) == 0;
}

static int chain(const zxv_layer_t *const *L, uint32_t n, int use_admit, uint32_t *at,
                 const char **why)
{
    *at = 99;
    *why = 0;
    return zxv_layer_chain(L, n, use_admit ? admit : 0, &admitted, at, why);
}

static void test_chain(void)
{
    const zxv_layer_t *c[6] = {&V[0], &V[1], &V[2], &V[3], &V[4], 0};
    uint32_t at;
    const char *why;
    CHECK(chain(c, 5, 0, &at, &why) == 0 && why == 0);
    CHECK(chain(c, 3, 0, &at, &why) == 0);
    CHECK(chain(c, 1, 0, &at, &why) == 0);
    CHECK(chain(c, 0, 0, &at, &why) == 0);

    /* starts above L0 */
    CHECK(chain(c + 1, 2, 0, &at, &why) == -1 && at == 0 &&
          !strcmp(why, "the chain does not start at L0"));
    /* skips a layer */
    const zxv_layer_t *skip[3] = {&V[0], &V[2], &V[3]};
    CHECK(chain(skip, 3, 0, &at, &why) == -1 && at == 1 &&
          !strcmp(why, "depth does not rise by one"));
    const zxv_layer_t *jump[4] = {&V[0], &V[1], &V[2], &V[4]};
    CHECK(chain(jump, 4, 0, &at, &why) == -1 && at == 3 &&
          !strcmp(why, "depth does not rise by one"));
    /* a parent off by one bit, early in the CID */
    zxv_layer_t B = V[1];
    B.parent[5] ^= 1;
    const zxv_layer_t *flip[2] = {&V[0], &B};
    CHECK(chain(flip, 2, 0, &at, &why) == -1 && at == 1);
    /* a layer twice */
    const zxv_layer_t *twice[3] = {&V[0], &V[1], &V[1]};
    CHECK(chain(twice, 3, 0, &at, &why) == -1 && at == 2 &&
          !strcmp(why, "depth does not rise by one"));

    /* one changed byte in L0 breaks L1's anchor */
    zxv_layer_t T;
    CHECK(parse_ok(buf, sub(VEC_L0, "0x100000", "0x100001"), &T));
    const zxv_layer_t *bent[3] = {&T, &V[1], &V[2]};
    CHECK(chain(bent, 3, 1, &at, &why) == -1 && at == 1 &&
          !strcmp(why, "names a parent that is not the layer below"));

    /* L4 on L4 */
    char cid4[61], from[128], to[128];
    zxv_layer_cid_text(V[4].cid, cid4);
    snprintf(from, sizeof from, "\"%s\"", CID_L3);
    snprintf(to, sizeof to, "\"%s\"", cid4);
    zxv_layer_t O2;
    CHECK(parse_ok(buf, sub(VEC_L4, from, to), &O2));
    const zxv_layer_t *ov[6] = {&V[0], &V[1], &V[2], &V[3], &V[4], &O2};
    CHECK(chain(ov, 6, 0, &at, &why) == 0);
    const zxv_layer_t *ov3[5] = {&V[0], &V[1], &V[2], &V[3], &O2};
    CHECK(chain(ov3, 5, 0, &at, &why) == -1 && at == 4); /* O2 names L4, not L3 */

    /* evo_comm: L3 on an L2 other than the one below, if admitted */
    zxv_layer_t L2b, L3e, L2x;
    CHECK(parse_ok(buf, sub(VEC_L2, "\"group\": \"REASON\"", "\"group\": \"REASON2\""), &L2b));
    char cid2[61];
    zxv_layer_cid_text(L2b.cid, cid2);
    CHECK(strcmp(cid2, CID_L2) != 0);
    snprintf(from, sizeof from, "\"%s\"", CID_L2);
    snprintf(to, sizeof to, "\"%s\"", cid2);
    static char l3e[1024];
    CHECK(sub(VEC_L3, from, to) > 0);
    strcpy(l3e, buf);
    CHECK(parse_ok(l3e, (uint32_t) strlen(l3e), &L3e) && L3e.compat);
    const zxv_layer_t *evo[4] = {&V[0], &V[1], &V[2], &L3e};
    memset(admitted, 0, sizeof admitted);
    admit_calls = 0;
    CHECK(chain(evo, 4, 1, &at, &why) == -1 && at == 3 && admit_calls == 1 &&
          !strcmp(why, "names a parent that is neither the layer below nor one evo_comm admits"));
    CHECK(chain(evo, 4, 0, &at, &why) == -1 && at == 3);
    memcpy(admitted, L2b.cid, sizeof admitted);
    admit_calls = 0;
    CHECK(chain(evo, 4, 1, &at, &why) == 0 && admit_calls == 1);
    /* the exact parent never asks */
    admit_calls = 0;
    CHECK(chain(c, 5, 1, &at, &why) == 0 && admit_calls == 0);
    /* exact layers are never admitted, whatever the contract says */
    snprintf(from, sizeof from, "\"%s\"", CID_L1);
    CHECK(parse_ok(buf, sub(VEC_L2, from, to), &L2x)); /* L2 naming L2b */
    memcpy(admitted, L2x.parent, sizeof admitted);
    const zxv_layer_t *ex[3] = {&V[0], &V[1], &L2x};
    admit_calls = 0;
    CHECK(chain(ex, 3, 1, &at, &why) == -1 && at == 2 && admit_calls == 0 &&
          !strcmp(why, "names a parent that is not the layer below"));
}

int main(void)
{
    test_vectors();
    test_objects();
    test_refusals();
    test_chain();
    printf("test_layers: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
