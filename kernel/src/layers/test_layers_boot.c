/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_layers_boot.c — the lineage check at boot over a golden disc built
 * here: a GPT disk whose ESP holds /EFI/BOOT/BOOTX64.EFI, /ZXV/MODELS.SHA,
 * /ZXV/MODELS/MDL000.GGU and /ZXV/LAYER0..2.JSN, and a ZXV_SWARM_REASON
 * partition with /MODELS.SHA and /MODELS/MDL001.GGU. Then every way the
 * layers, the boot file or a model can disagree. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../hwdrv/test/hwfake.h"
#include "layers_boot.h"

#define NBLK  2048u
#define ESP0  40u /* ESP: LBA 40..1023 */
#define ESP1  1023u
#define SWM0  1024u /* swarm: LBA 1024..2013 */
#define SWM1  2013u
#define EOC   0x0FFFFFFFu
#define A_LEN 1500u /* the core model */
#define B_LEN 2000u /* the swarm model */
#define E_LEN 3000u /* BOOTX64.EFI */

typedef struct {
    hw_blk_t b;
    uint8_t d[NBLK * 512];
} mdisk_t;

static int mread(hw_blk_t *b, uint64_t lba, uint32_t n, void *buf)
{
    mdisk_t *m = (mdisk_t *) b;
    if (lba + n > NBLK) return -1;
    memcpy(buf, m->d + lba * 512, n * 512);
    return 0;
}

static void p16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}
static void p32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}
static void p64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static const uint8_t ESP[16] = {0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
                                0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B};

/* ---- a FAT32 writer: 1 reserved sector, one FAT of 8 sectors (1024
 * entries), one sector per cluster, cluster c at sector 9 + c - 2; clusters
 * handed out in order from 2, the root ---- */
typedef struct {
    uint8_t *v;
    uint32_t next;
} fw_t;

static void fw_init(fw_t *w, mdisk_t *m, uint32_t base)
{
    w->v = m->d + base * 512;
    w->next = 2;
    p16(w->v + 11, 512);
    w->v[13] = 1;
    p16(w->v + 14, 1);
    w->v[16] = 1;
    p32(w->v + 36, 8);
    p32(w->v + 44, 2);
    p32(w->v + 512, 0x0FFFFFF8u);
    p32(w->v + 516, EOC);
}
static uint8_t *clu(fw_t *w, uint32_t c)
{
    return w->v + (9 + c - 2) * 512;
}
static void fat(fw_t *w, uint32_t c, uint32_t next)
{
    p32(w->v + 512 + c * 4, next);
}
/* n bytes in fresh contiguous clusters (one, if n is 0); the first cluster */
static uint32_t put(fw_t *w, const void *d, uint32_t n)
{
    uint32_t k = n ? (n + 511) / 512 : 1, first = w->next;
    for (uint32_t i = 0; i < k; i++) fat(w, first + i, i + 1 < k ? first + i + 1 : EOC);
    memset(clu(w, first), 0, k * 512);
    if (n) memcpy(clu(w, first), d, n);
    w->next += k;
    return first;
}
static uint32_t dir(fw_t *w)
{
    return put(w, 0, 0);
}
/* An entry in directory cluster dc ("NAME    EXT", 11 characters). */
static void ent(fw_t *w, uint32_t dc, const char *n11, uint32_t c, uint32_t size)
{
    uint8_t *d = clu(w, dc);
    while (d[0]) d += 32;
    memcpy(d, n11, 11);
    d[11] = size == 0xFFFFFFFFu ? 0x10 : 0x20;
    p16(d + 20, (uint16_t) (c >> 16));
    p16(d + 26, (uint16_t) c);
    p32(d + 28, size == 0xFFFFFFFFu ? 0 : size);
}
#define DIR 0xFFFFFFFFu

static void file(fw_t *w, uint32_t dc, const char *n11, const void *d, uint32_t n)
{
    ent(w, dc, n11, put(w, d, n), n);
}

/* ---- the content ---- */
static uint8_t A[A_LEN], B[B_LEN], E[E_LEN];

static void hexs(const uint8_t *d, uint32_t n, char out[65])
{
    uint8_t h[32];
    sha256(d, n, h);
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

/* A manifest in mk_layers.py's layout over the given object lines. */
static void render(char *out, size_t cap, int depth, const char *parent, const char *compat,
                   const char *const *objs, int n)
{
    static const char *const NM[5] = {"HARDWARE_ENVELOPE", "CORE_BOOT_ENGINE", "SWARM",
                                      "ALEXANDRIA", "COMMUNITY_OVERLAY"};
    char par[80];
    if (parent)
        snprintf(par, sizeof par, "\"%s\"", parent);
    else
        strcpy(par, "null");
    size_t k = (size_t) snprintf(out, cap,
                                 "{\n \"format\": \"zxv-layer 1\",\n \"layer_depth\": %d,\n"
                                 " \"layer_name\": \"%s\",\n \"parent_layer_cid\": %s,\n"
                                 " \"parent_compat\": \"%s\",\n \"objects\": [\n",
                                 depth, NM[depth], par, compat);
    for (int i = 0; i < n; i++)
        k += (size_t) snprintf(out + k, cap - k, "  %s%s\n", objs[i], i + 1 < n ? "," : "");
    snprintf(out + k, cap - k, " ]\n}\n");
}

/* The CID text of a manifest, as the kernel names it. */
static void cid_of(const char *t, char out[61])
{
    zxv_layer_t L;
    const char *why;
    if (zxv_layer_parse(t, (uint32_t) strlen(t), &L, &why) != 0) {
        printf("test manifest refused: %s\n%s", why, t);
        abort();
    }
    zxv_layer_cid_text(L.cid, out);
}

/* What one disc is built from; good() fills the defaults. */
typedef struct {
    char l0[4096], l1[4096], l2[4096];
    const char *layer[3]; /* the bytes of LAYER0..2.JSN, NULL for none */
    uint32_t layer_len[3];
    int no_efi, short_efi, loop_efi, no_core, layer1_dir;
    uint8_t efi_flip; /* xor into the EFI's 100th byte on disc */
} disc_t;

static char sa[65], sb[65], se[65], cid0[61], cid1[61], cid2[61];
static char o_mach[256], o_boot[256], o_core[256], o_swarm[256];

static void objects(void)
{
    hexs(A, A_LEN, sa);
    hexs(B, B_LEN, sb);
    hexs(E, E_LEN, se);
    snprintf(o_mach, sizeof o_mach,
             "{\"kind\": \"machine\", \"arch\": \"x86_64\", \"firmware\": \"UEFI\", "
             "\"link_address\": \"0x100000\", \"page_bytes\": 4096, \"sector_bytes\": 512, "
             "\"min_ram_mib\": 512}");
    snprintf(o_boot, sizeof o_boot,
             "{\"kind\": \"boot\", \"arch\": \"x86_64\", \"name\": \"BOOTX64.EFI\", \"bytes\": %u, "
             "\"sha256\": \"%s\"}",
             E_LEN, se);
    snprintf(o_core, sizeof o_core,
             "{\"kind\": \"model\", \"arch\": \"all\", \"name\": \"qwen-core\", \"bytes\": %u, "
             "\"sha256\": \"%s\"}",
             A_LEN, sa);
    snprintf(o_swarm, sizeof o_swarm,
             "{\"kind\": \"model\", \"arch\": \"all\", \"group\": \"REASON\", \"name\": "
             "\"tiamat-swarm\", \"bytes\": %u, \"sha256\": \"%s\"}",
             B_LEN, sb);
}

static void finish_layers(disc_t *d)
{
    for (int i = 0; i < 3; i++) {
        d->layer[i] = i == 0 ? d->l0 : i == 1 ? d->l1 : d->l2;
        d->layer_len[i] = (uint32_t) strlen(d->layer[i]);
    }
}

/* L0 from objs0, L1 on it from objs1, L2 on L1 from objs2. */
static void stack(disc_t *d, const char *const *o0, int n0, const char *const *o1, int n1,
                  const char *const *o2, int n2)
{
    render(d->l0, sizeof d->l0, 0, 0, "exact", o0, n0);
    cid_of(d->l0, cid0);
    render(d->l1, sizeof d->l1, 1, cid0, "exact", o1, n1);
    cid_of(d->l1, cid1);
    render(d->l2, sizeof d->l2, 2, cid1, "exact", o2, n2);
    cid_of(d->l2, cid2);
    finish_layers(d);
}

static void good(disc_t *d)
{
    memset(d, 0, sizeof *d);
    const char *o0[] = {o_mach}, *o1[] = {o_boot, o_core}, *o2[] = {o_swarm};
    stack(d, o0, 1, o1, 2, o2, 1);
}

static mdisk_t disk;

static void build(const disc_t *d)
{
    mdisk_t *m = &disk;
    char text[512];
    memset(m, 0, sizeof *m);
    strcpy(m->b.name, "gold0");
    m->b.block_size = 512;
    m->b.blocks = NBLK;
    m->b.read = mread;
    uint8_t *h = m->d + 512;
    memcpy(h, "EFI PART", 8);
    p32(h + 8, 0x00010000);
    p32(h + 12, 92);
    p64(h + 24, 1);
    p64(h + 32, NBLK - 1);
    p64(h + 40, 34);
    p64(h + 48, NBLK - 34);
    p64(h + 72, 2);
    p32(h + 80, 4);
    p32(h + 84, 128);
    uint8_t *e = m->d + 2 * 512;
    memcpy(e, ESP, 16);
    p64(e + 32, ESP0);
    p64(e + 40, ESP1);
    memcpy(e + 128, HW_GPT_ZXV_SWARM, 16);
    p64(e + 128 + 32, SWM0);
    p64(e + 128 + 40, SWM1);
    const char *sw = "ZXV_SWARM_REASON";
    for (int k = 0; sw[k]; k++) e[128 + 56 + 2 * k] = (uint8_t) sw[k];
    p32(h + 88, hw_crc32(0, m->d + 2 * 512, 4 * 128));
    p32(h + 16, hw_crc32(0, h, 92));

    fw_t w;
    fw_init(&w, m, ESP0);
    uint32_t root = dir(&w), efi = dir(&w), boot = dir(&w), zxv = dir(&w), mdl = dir(&w);
    ent(&w, root, "EFI        ", efi, DIR);
    ent(&w, efi, "BOOT       ", boot, DIR);
    ent(&w, root, "ZXV        ", zxv, DIR);
    ent(&w, zxv, "MODELS     ", mdl, DIR);
    if (!d->no_efi) {
        uint32_t c = put(&w, E, E_LEN);
        ent(&w, boot, "BOOTX64 EFI", c, E_LEN);
        clu(&w, c)[100] ^= d->efi_flip;
        if (d->short_efi) fat(&w, c + 3, EOC);
        if (d->loop_efi) fat(&w, c + 3, c + 1);
    }
    if (!d->no_core) {
        snprintf(text, sizeof text, "%s %u qwen-core esp MDL000.GGU\n", sa, A_LEN);
        file(&w, zxv, "MODELS  SHA", text, (uint32_t) strlen(text));
    }
    file(&w, mdl, "MDL000  GGU", A, A_LEN);
    static const char *const N11[3] = {"LAYER0  JSN", "LAYER1  JSN", "LAYER2  JSN"};
    for (int i = 0; i < 3; i++)
        if (i == 1 && d->layer1_dir)
            ent(&w, zxv, N11[i], dir(&w), DIR);
        else if (d->layer[i])
            file(&w, zxv, N11[i], d->layer[i], d->layer_len[i]);

    fw_init(&w, m, SWM0);
    root = dir(&w);
    mdl = dir(&w);
    ent(&w, root, "MODELS     ", mdl, DIR);
    file(&w, mdl, "MDL001  GGU", B, B_LEN);
    snprintf(text, sizeof text, "%s %u tiamat-swarm swarm MDL001.GGU\n", sb, B_LEN);
    file(&w, root, "MODELS  SHA", text, (uint32_t) strlen(text));
}

static fake_plat_t fp;
static hw_registry_t reg;
static hw_models_t s;
static zxv_layers_boot_t lb;

/* Build the disc, index it, run the check. */
static int run(const disc_t *d)
{
    build(d);
    memset(&reg, 0, sizeof reg);
    hw_reg_blk(&reg, &disk.b);
    fp.nlog = 0;
    if (hw_models_index(&s, &fp.plat, &reg) != (d->no_core ? 1u : 2u)) {
        printf("index failed\n");
        for (uint32_t i = 0; i < fp.nlog; i++) printf("  | %s\n", fp.log[i]);
        abort();
    }
    fp.nlog = 0;
    return zxv_layers_boot(&lb, &s, &fp.plat, "x86_64", "BOOTX64.EFI");
}

static int has(const char *t)
{
    if (fake_log_has(&fp, t)) return 1;
    printf("  log lacks \"%s\"; it has:\n", t);
    for (uint32_t i = 0; i < fp.nlog; i++) printf("  | %s\n", fp.log[i]);
    return 0;
}

/* The lineage failed: both models refused, no [LAYERS_OK]. */
static int all_refused(void)
{
    return lb.verdict == ZXV_LAYERS_FAIL && lb.refused == 2 && s.m[0].verdict == HW_F &&
           s.m[1].verdict == HW_F && !fake_log_has(&fp, "[LAYERS_OK]") &&
           has("[MODEL_BAD] qwen-core (esp:MDL000.GGU) its disc's layers do not check out") &&
           has("[MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) its disc's layers do not "
               "check out");
}

static disc_t d;

static void test_good(void)
{
    char line[160];
    CHECK(run(&d) == ZXV_LAYERS_OK && lb.refused == 0 && lb.n == 3);
    snprintf(line, sizeof line, "  [LAYER] L0 HARDWARE_ENVELOPE %s", cid0);
    CHECK(has(line));
    snprintf(line, sizeof line, "  [LAYER] L1 CORE_BOOT_ENGINE %s", cid1);
    CHECK(has(line));
    snprintf(line, sizeof line, "  [LAYER] L2 SWARM %s", cid2);
    CHECK(has(line));
    CHECK(has("  [LAYERS_OK] L0 -> L1 -> L2: each names the one below; BOOTX64.EFI and 2 models "
              "match their layers"));
    /* the table */
    char t[61];
    zxv_layer_cid_text(lb.node[1].layer_cid, t);
    CHECK(!strcmp(t, cid1));
    zxv_layer_cid_text(lb.node[2].parent_cid, t);
    CHECK(!strcmp(t, cid1));
    CHECK(memcmp(lb.node[1].parent_cid, lb.node[0].layer_cid, ZXV_LAYER_CID_BYTES) == 0);
    for (int i = 0; i < 3; i++)
        CHECK(lb.node[i].depth == i && lb.node[i].flags == ZXV_LAYER_VERIFIED &&
              lb.node[i].sector_start == ESP0 && lb.node[i].sector_count == ESP1 - ESP0 + 1);
    /* checked, not hashed: the models still wait for first use */
    CHECK(s.m[0].verdict == HW_N && s.m[1].verdict == HW_N);
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_T);
    CHECK(hw_models_require(&s, &fp.plat, "tiamat-swarm") == HW_T);
    zxv_layers_sealed(&lb, 0);
    CHECK(lb.node[2].flags == (ZXV_LAYER_VERIFIED | ZXV_LAYER_SEALED));
    zxv_layers_sealed(&lb, 1);
    CHECK(lb.node[0].flags == (ZXV_LAYER_VERIFIED | ZXV_LAYER_SEALED | ZXV_LAYER_AIRGAPPED));
    /* a second run over the same index agrees */
    fp.nlog = 0;
    CHECK(zxv_layers_boot(&lb, &s, &fp.plat, "x86_64", "BOOTX64.EFI") == ZXV_LAYERS_OK);
}

static void test_none(void)
{
    good(&d);
    d.layer[0] = d.layer[1] = d.layer[2] = 0;
    CHECK(run(&d) == ZXV_LAYERS_NONE && lb.n == 0 && lb.refused == 0);
    CHECK(has("  [LAYERS] none on the core disc (/ZXV/LAYER0.JSN to LAYER2.JSN)"));
    CHECK(s.m[0].verdict == HW_N && s.m[1].verdict == HW_N);

    good(&d);
    d.no_core = 1;
    CHECK(run(&d) == ZXV_LAYERS_NONE && lb.refused == 0);
    CHECK(has("  [LAYERS] none: no core disc attached"));
    CHECK(s.m[0].verdict == HW_N);
}

static void test_lineage(void)
{
    good(&d);
    d.layer[2] = 0;
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] LAYER2.JSN: missing from /ZXV"));

    /* again over the same index: the models are refused already, and stay so */
    fp.nlog = 0;
    CHECK(zxv_layers_boot(&lb, &s, &fp.plat, "x86_64", "BOOTX64.EFI") == ZXV_LAYERS_FAIL &&
          lb.refused == 0 && !fake_log_has(&fp, "[MODEL_BAD]"));

    good(&d);
    d.layer[0] = 0;
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused() && has("LAYER0.JSN: missing from /ZXV"));

    good(&d);
    d.layer1_dir = 1; /* a directory of the name is not the file */
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused() && has("LAYER1.JSN: missing from /ZXV"));

    good(&d);
    d.layer_len[1]--; /* no final newline */
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] LAYER1.JSN: does not end with a newline"));

    /* the files swapped */
    good(&d);
    d.layer[1] = d.l2;
    d.layer_len[1] = (uint32_t) strlen(d.l2);
    d.layer[2] = d.l1;
    d.layer_len[2] = (uint32_t) strlen(d.l1);
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] LAYER1.JSN: not the layer of its name"));

    /* L1 stands on another L0 */
    good(&d);
    {
        static char other[4096];
        const char *o0[] = {o_mach, o_mach};
        char c[61];
        render(other, sizeof other, 0, 0, "exact", o0, 2);
        cid_of(other, c);
        const char *o1[] = {o_boot, o_core};
        render(d.l1, sizeof d.l1, 1, c, "exact", o1, 2);
        cid_of(d.l1, cid1);
        const char *o2[] = {o_swarm};
        render(d.l2, sizeof d.l2, 2, cid1, "exact", o2, 1);
        finish_layers(&d);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] LAYER1.JSN: names a parent that is not the layer below"));

    /* L2 stands on another L1 */
    good(&d);
    {
        const char *o1[] = {o_core, o_boot};
        static char l1b[4096];
        char c[61];
        render(l1b, sizeof l1b, 1, cid0, "exact", o1, 2);
        cid_of(l1b, c);
        const char *o2[] = {o_swarm};
        render(d.l2, sizeof d.l2, 2, c, "exact", o2, 1);
        finish_layers(&d);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] LAYER2.JSN: names a parent that is not the layer below"));

    /* too large to read at boot */
    good(&d);
    {
        static char big[ZXV_LAYERS_BOOT_BYTES + 1];
        memset(big, ' ', sizeof big);
        d.layer[2] = big;
        d.layer_len[2] = sizeof big;
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] LAYER2.JSN: larger than the kernel reads"));
}

static void test_machine(void)
{
    static char m[4][256];
    const char *rep[4][2] = {
        {"x86_64", "arm64"}, {"4096", "8192"}, {"512, \"min", "4096, \"min"}, {"UEFI", "BIOS"}};
    for (int k = 0; k < 4; k++) {
        const char *at = strstr(o_mach, rep[k][0]);
        size_t a = (size_t) (at - o_mach);
        snprintf(m[k], sizeof m[k], "%.*s%s%s", (int) a, o_mach, rep[k][1], at + strlen(rep[k][0]));
        good(&d);
        const char *o0[] = {m[k]}, *o1[] = {o_boot, o_core}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 2, o2, 1);
        CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
        CHECK(has("  [LAYERS_FAIL] L0 lists no UEFI machine of 4096-byte pages and 512-byte "
                  "sectors for x86_64"));
    }
    /* another machine first, ours second */
    good(&d);
    {
        const char *o0[] = {m[0], o_mach}, *o1[] = {o_boot, o_core}, *o2[] = {o_swarm};
        stack(&d, o0, 2, o1, 2, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_OK);
}

static void test_boot_file(void)
{
    good(&d);
    d.efi_flip = 0x40;
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] BOOTX64.EFI: not the boot file L1 lists"));

    good(&d);
    d.short_efi = 1;
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] BOOTX64.EFI: could not be read to its end"));

    good(&d);
    d.loop_efi = 1;
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] BOOTX64.EFI: not the boot file L1 lists"));

    /* a model listed before the boot entry, for the same architecture */
    good(&d);
    {
        static char om[320];
        snprintf(om, sizeof om,
                 "{\"kind\": \"model\", \"arch\": \"x86_64\", \"name\": \"qwen-core\", "
                 "\"bytes\": %u, \"sha256\": \"%s\"}",
                 A_LEN, sa);
        const char *o0[] = {o_mach}, *o1[] = {om, o_boot}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 2, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_OK);

    good(&d);
    d.no_efi = 1;
    CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
    CHECK(has("  [LAYERS_FAIL] BOOTX64.EFI: not in /EFI/BOOT"));

    /* L1's boot entry is for another architecture, or another file, or
     * another size */
    static char ob[3][320];
    const char *rep[3][2] = {{"\"x86_64\"", "\"arm64\""},
                             {"BOOTX64", "BOOTAA64"},
                             {"\"bytes\": 3000", "\"bytes\": 3001"}};
    for (int k = 0; k < 3; k++) {
        const char *at = strstr(o_boot, rep[k][0]);
        size_t a = (size_t) (at - o_boot);
        snprintf(ob[k], sizeof ob[k], "%.*s%s%s", (int) a, o_boot, rep[k][1],
                 at + strlen(rep[k][0]));
        good(&d);
        const char *o0[] = {o_mach}, *o1[] = {ob[k], o_core}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 2, o2, 1);
        CHECK(run(&d) == ZXV_LAYERS_FAIL && all_refused());
        CHECK(has(k < 2 ? "  [LAYERS_FAIL] L1 lists no boot file BOOTX64.EFI"
                        : "  [LAYERS_FAIL] BOOTX64.EFI: not the boot file L1 lists"));
    }
}

static void test_models(void)
{
    static char oc[320], os[320], og[320], ghost[320];
    /* the core model's hash in L1 differs: that model alone is refused */
    strcpy(oc, o_core);
    char *h = strstr(oc, sa);
    h[63] = h[63] == '0' ? '1' : '0';
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, oc}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 2, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 1);
    CHECK(s.m[0].verdict == HW_F && s.m[1].verdict == HW_N);
    CHECK(has("  [MODEL_BAD] qwen-core (esp:MDL000.GGU) differs from the model its layer lists"));
    CHECK(has("  [LAYERS_FAIL] L0 -> L1 -> L2 lineage holds, but 1 model differs from its layer "
              "and 0 listed in L1 are missing"));
    CHECK(lb.node[0].flags == 0); /* not VERIFIED */

    /* ... or its first byte */
    strcpy(oc, o_core);
    h = strstr(oc, sa);
    h[0] = h[0] == '0' ? '1' : '0';
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, oc}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 2, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 1 && s.m[0].verdict == HW_F);

    /* the swarm model's size in L2 differs */
    strcpy(os, o_swarm);
    h = strstr(os, "\"bytes\": 2000");
    h[12] = '1';
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, o_core}, *o2[] = {os};
        stack(&d, o0, 1, o1, 2, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 1);
    CHECK(s.m[0].verdict == HW_N && s.m[1].verdict == HW_F);
    CHECK(has("  [MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) differs from the model "
              "its layer lists"));

    /* L2 lists it under another group */
    strcpy(og, o_swarm);
    memcpy(strstr(og, "REASON"), "REASOX", 6);
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, o_core}, *o2[] = {og};
        stack(&d, o0, 1, o1, 2, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 1 && s.m[1].verdict == HW_F);
    CHECK(has("  [MODEL_BAD] tiamat-swarm (ZXV_SWARM_REASON:MDL001.GGU) is not a model L2 lists "
              "for its group"));

    /* the core model is not in L1 at all, and L2 does not list core models */
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot}, *o2[] = {o_swarm, o_core};
        stack(&d, o0, 1, o1, 1, o2, 2);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 1 && s.m[0].verdict == HW_F);
    CHECK(has("  [MODEL_BAD] qwen-core (esp:MDL000.GGU) is not a model L1 lists"));

    /* the swarm model listed in L1 instead of L2 */
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, o_core, o_swarm}, *o2[] = {o_core};
        stack(&d, o0, 1, o1, 3, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 1 && s.m[1].verdict == HW_F);
    CHECK(has("  [LAYERS_FAIL] L1 lists tiamat-swarm, which the core disc does not carry"));

    /* L1 lists a core model the disc does not carry */
    snprintf(ghost, sizeof ghost,
             "{\"kind\": \"model\", \"arch\": \"all\", \"name\": \"ghost\", \"bytes\": 7, "
             "\"sha256\": \"%s\"}",
             sa);
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, o_core, ghost}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 3, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 0);
    CHECK(s.m[0].verdict == HW_N && s.m[1].verdict == HW_N);
    CHECK(has("  [LAYERS_FAIL] L1 lists ghost, which the core disc does not carry"));
    CHECK(has("  [LAYERS_FAIL] L0 -> L1 -> L2 lineage holds, but 0 models differ from its layer "
              "and 1 listed in L1 is missing"));
    /* ... listed before the one it carries: each model is found by name */
    good(&d);
    {
        const char *o0[] = {o_mach}, *o1[] = {o_boot, ghost, o_core}, *o2[] = {o_swarm};
        stack(&d, o0, 1, o1, 3, o2, 1);
    }
    CHECK(run(&d) == ZXV_LAYERS_FAIL && lb.refused == 0 && s.m[0].verdict == HW_N);
}

/* hw_models_refuse on its own */
static void test_refuse(void)
{
    good(&d);
    CHECK(run(&d) == ZXV_LAYERS_OK);
    CHECK(hw_models_require(&s, &fp.plat, "qwen-core") == HW_T && s.ok == 1);
    fp.nlog = 0;
    hw_models_refuse(&s, &fp.plat, 0, "because");
    CHECK(s.m[0].verdict == HW_F && s.ok == 0 && s.bad == 1);
    CHECK(has("  [MODEL_BAD] qwen-core (esp:MDL000.GGU) because"));
    hw_models_refuse(&s, &fp.plat, 0, "again");
    CHECK(s.bad == 1 && !fake_log_has(&fp, "again"));
    hw_models_refuse(&s, &fp.plat, 9, "nobody");
    CHECK(s.bad == 1);
    /* refusing the model the idle loop is hashing drops the cursor */
    CHECK(hw_models_step(&s, &fp.plat, 1) == 1 && s.bg.idx == 1);
    hw_models_refuse(&s, &fp.plat, 1, "mid-hash");
    CHECK(s.bg.idx == -1 && s.m[1].verdict == HW_F);
    CHECK(hw_models_step(&s, &fp.plat, 1) == 0 && has("[MODELS_FAIL] 2 of 2 models bad"));
}

int main(void)
{
    fake_plat_init(&fp);
    for (uint32_t i = 0; i < A_LEN; i++) A[i] = (uint8_t) (i * 7 + 1);
    for (uint32_t i = 0; i < B_LEN; i++) B[i] = (uint8_t) (i * 13 + 5 + i / 256);
    for (uint32_t i = 0; i < E_LEN; i++) E[i] = (uint8_t) (i * 11 + 3 + i / 512);
    objects();
    good(&d);
    test_good();
    test_none();
    test_lineage();
    test_machine();
    test_boot_file();
    test_models();
    test_refuse();
    fake_plat_free(&fp);
    printf("test_layers_boot: %d checks, %d failures\n", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
