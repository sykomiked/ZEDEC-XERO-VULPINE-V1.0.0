/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* layers_boot.c — the release's lineage, checked at boot (layers_boot.h). */
#include "layers_boot.h"

static const char *const FILES[ZXV_LAYERS_BOOT_N] = {"LAYER0.JSN", "LAYER1.JSN", "LAYER2.JSN"};
static const char *const NAMES[ZXV_LAYERS_BOOT_N] = {"HARDWARE_ENVELOPE", "CORE_BOOT_ENGINE",
                                                     "SWARM"};

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int same_sha(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < 32; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

static int same_cid(const uint8_t *a, const uint8_t *b)
{
    uint8_t d = 0;
    for (uint32_t i = 0; i < ZXV_LAYER_CID_BYTES; i++) d |= (uint8_t) (a[i] ^ b[i]);
    return d == 0;
}

/* Back to the ESP's root, then into each directory of path[0..n). */
static int cd(fat32_state_t *fs, const char *const *path, uint32_t n)
{
    if (fat32_read_dir(fs, fs->root_cluster) != 0) return -1;
    fs->current_dir_cluster = fs->root_cluster;
    for (uint32_t i = 0; i < n; i++)
        if (fat32_change_dir(fs, path[i]) != 0) return -1;
    return 0;
}

/* SHA-256 of the file f (its size from the directory entry), cluster by
 * cluster. 0, or -1 when the chain ends early or a cluster cannot be read
 * (fat32_read_cluster refuses any cluster outside the FAT, the end-of-chain
 * marks included). Each pass takes a whole cluster of the size, so a chain
 * that loops ends when the size is reached, with a hash that does not match. */
static int hash_file(fat32_state_t *fs, const fat32_file_t *f, uint8_t out[32])
{
    static uint8_t buf[FAT32_CLUSTER_MAX];
    sha256_ctx_t c;
    uint32_t cl = f->cluster, bpc = fs->bytes_per_cluster;
    uint64_t done = 0;
    sha256_init(&c);
    while (done < f->size) {
        if (fat32_read_cluster(fs, cl, buf) != 0) return -1;
        uint64_t left = f->size - done;
        uint32_t take = left < bpc ? (uint32_t) left : bpc;
        sha256_update(&c, buf, take);
        done += take;
        if (done < f->size) cl = fat32_next_cluster(fs, cl);
    }
    sha256_final(&c, out);
    return 0;
}

/* The object of layer L with this kind, name and (if group) group. */
static int find(const zxv_layer_t *L, const char *kind, const char *arch, const char *name,
                const char *group, zxv_layer_obj_t *o)
{
    uint32_t pos = 0;
    while (zxv_layer_next(L, &pos, o)) {
        if (!same(o->kind, kind)) continue;
        if (arch && !same(o->arch, arch)) continue;
        if (name && !same(o->name, name)) continue;
        if (group && !same(o->group, group)) continue;
        return 1;
    }
    return 0;
}

static void say(const hw_plat_t *p, const char *a, const char *b, const char *c)
{
    hw_line_t l;
    hw_line_init(&l);
    hw_line_s(&l, a);
    hw_line_s(&l, b);
    hw_line_s(&l, c);
    p->log(l.b);
}

/* Lineage or boot file failed: say why, and refuse every model. */
static int refuse_all(zxv_layers_boot_t *b, hw_models_t *s, const hw_plat_t *p, const char *what,
                      const char *why)
{
    say(p, "  [LAYERS_FAIL] ", what, why);
    for (uint32_t i = 0; i < s->n; i++)
        if (s->m[i].verdict != HW_F) {
            hw_models_refuse(s, p, i, "its disc's layers do not check out");
            b->refused++;
        }
    b->verdict = ZXV_LAYERS_FAIL;
    return b->verdict;
}

int zxv_layers_boot(zxv_layers_boot_t *b, hw_models_t *s, const hw_plat_t *p, const char *arch,
                    const char *efi, const uint8_t l0[ZXV_LAYER_CID_BYTES])
{
    static char text[ZXV_LAYERS_BOOT_N][ZXV_LAYERS_BOOT_BYTES];
    static const char *const ZXV[] = {"ZXV"}, *const BOOT[] = {"EFI", "BOOT"};
    zxv_layer_t L[ZXV_LAYERS_BOOT_N];
    const zxv_layer_t *chain[ZXV_LAYERS_BOOT_N] = {&L[0], &L[1], &L[2]};
    hw_models_vol_t *v = &s->vol[0];
    fat32_file_t *f[ZXV_LAYERS_BOOT_N];
    const char *why;
    hw_memset(b, 0, sizeof *b);

    if (!s->core) {
        p->log("  [LAYERS] none: no core disc attached");
        return b->verdict;
    }
    if (cd(&v->fs, ZXV, 1) != 0)
        return refuse_all(b, s, p, "/ZXV on the core disc", ": unreadable");
    uint32_t present = 0;
    for (uint32_t i = 0; i < ZXV_LAYERS_BOOT_N; i++) {
        f[i] = fat32_find_file(&v->fs, FILES[i]);
        if (f[i] && f[i]->is_dir) f[i] = 0;
        present += f[i] != 0;
    }
    if (!present) {
        p->log("  [LAYERS] none on the core disc (/ZXV/LAYER0.JSN to LAYER2.JSN)");
        return b->verdict;
    }
    /* f[] points into the directory just read: read every file before the
     * directory changes */
    uint32_t len[ZXV_LAYERS_BOOT_N];
    for (uint32_t i = 0; i < ZXV_LAYERS_BOOT_N; i++) {
        if (!f[i]) return refuse_all(b, s, p, FILES[i], ": missing from /ZXV");
        if (f[i]->size > ZXV_LAYERS_BOOT_BYTES)
            return refuse_all(b, s, p, FILES[i], ": larger than the kernel reads");
        len[i] = f[i]->size;
        if (fat32_read_file(&v->fs, f[i], (uint8_t *) text[i], ZXV_LAYERS_BOOT_BYTES) != 0)
            return refuse_all(b, s, p, FILES[i], ": could not be read");
    }
    for (uint32_t i = 0; i < ZXV_LAYERS_BOOT_N; i++) {
        if (zxv_layer_parse(text[i], len[i], &L[i], &why) != 0) {
            hw_line_t l;
            hw_line_init(&l);
            hw_line_s(&l, ": ");
            hw_line_s(&l, why);
            return refuse_all(b, s, p, FILES[i], l.b);
        }
        if (L[i].depth != i) return refuse_all(b, s, p, FILES[i], ": not the layer of its name");
        struct zxv_layer_node *nd = &b->node[i];
        nd->depth = L[i].depth;
        hw_memcpy(nd->layer_cid, L[i].cid, ZXV_LAYER_CID_BYTES);
        hw_memcpy(nd->parent_cid, L[i].parent, ZXV_LAYER_CID_BYTES);
        nd->sector_start = v->part.first;
        nd->sector_count = v->part.last - v->part.first + 1u;
        b->n++;
    }
    if (!same_cid(L[0].cid, l0))
        return refuse_all(b, s, p, FILES[0], ": is not the L0 this kernel's table lists");
    b->node[0].flags |= ZXV_LAYER_BAKED;
    uint32_t at = 0;
    if (zxv_layer_chain(chain, ZXV_LAYERS_BOOT_N, 0, 0, &at, &why) != 0)
        return refuse_all(b, s, p, FILES[at], ": names a parent that is not the layer below");

    zxv_layer_obj_t o;
    if (!find(&L[0], "machine", arch, 0, 0, &o) || !same(o.firmware, "UEFI") ||
        o.page_bytes != 4096u || o.sector_bytes != 512u)
        return refuse_all(b, s, p,
                          "L0 lists no UEFI machine of 4096-byte pages and 512-byte "
                          "sectors for ",
                          arch);

    if (!find(&L[1], "boot", arch, 0, 0, &o) || !same(o.name, efi) || !o.has_bytes || !o.has_sha)
        return refuse_all(b, s, p, "L1 lists no boot file ", efi);
    {
        uint8_t got[32];
        fat32_file_t *e = 0;
        if (cd(&v->fs, BOOT, 2) == 0) e = fat32_find_file(&v->fs, efi);
        if (!e || e->is_dir) return refuse_all(b, s, p, efi, ": not in /EFI/BOOT");
        if (e->size != o.bytes) return refuse_all(b, s, p, efi, ": not the boot file L1 lists");
        if (hash_file(&v->fs, e, got) != 0)
            return refuse_all(b, s, p, efi, ": could not be read to its end");
        if (!same_sha(got, o.sha)) return refuse_all(b, s, p, efi, ": not the boot file L1 lists");
    }

    /* the models, each against its layer */
    for (uint32_t i = 0; i < s->n; i++) {
        hw_model_t *m = &s->m[i];
        int ok;
        if (m->vol == 0)
            ok = find(&L[1], "model", 0, m->name, 0, &o);
        else
            ok = find(&L[2], "model", 0, m->name, s->vol[m->vol].part.name + 10, &o);
        if (ok && o.has_bytes && o.has_sha && o.bytes == m->bytes && same_sha(o.sha, m->sha))
            continue;
        hw_models_refuse(
            s, p, i,
            ok ? "differs from the model its layer lists"
               : (m->vol ? "is not a model L2 lists for its group" : "is not a model L1 lists"));
        b->refused++;
    }
    uint32_t pos = 0, absent = 0;
    while (zxv_layer_next(&L[1], &pos, &o)) {
        if (!same(o.kind, "model")) continue;
        uint32_t k = 0;
        while (k < s->n && !(s->m[k].vol == 0 && same(s->m[k].name, o.name))) k++;
        if (k == s->n) {
            say(p, "  [LAYERS_FAIL] L1 lists ", o.name, ", which the core disc does not carry");
            absent++;
        }
    }

    for (uint32_t i = 0; i < ZXV_LAYERS_BOOT_N; i++) {
        hw_line_t l;
        char cid[61];
        zxv_layer_cid_text(L[i].cid, cid);
        hw_line_init(&l);
        hw_line_s(&l, "  [LAYER] L");
        hw_line_u(&l, i);
        hw_line_c(&l, ' ');
        hw_line_s(&l, NAMES[i]);
        hw_line_c(&l, ' ');
        hw_line_s(&l, cid);
        p->log(l.b);
    }
    hw_line_t l;
    hw_line_init(&l);
    if (b->refused || absent) {
        hw_line_s(&l, "  [LAYERS_FAIL] L0 -> L1 -> L2 lineage holds, but ");
        hw_line_u(&l, b->refused);
        hw_line_s(&l, b->refused == 1 ? " model differs" : " models differ");
        hw_line_s(&l, " from its layer and ");
        hw_line_u(&l, absent);
        hw_line_s(&l, " listed in L1 ");
        hw_line_s(&l, absent == 1 ? "is" : "are");
        hw_line_s(&l, " missing");
        p->log(l.b);
        b->verdict = ZXV_LAYERS_FAIL;
        return b->verdict;
    }
    for (uint32_t i = 0; i < ZXV_LAYERS_BOOT_N; i++) b->node[i].flags |= ZXV_LAYER_VERIFIED;
    hw_line_s(&l, "  [LAYERS_OK] L0 -> L1 -> L2: each names the one below; ");
    hw_line_s(&l, efi);
    hw_line_s(&l, " and ");
    hw_line_u(&l, s->n);
    hw_line_s(&l, s->n == 1 ? " model match" : " models match");
    hw_line_s(&l, " their layers");
    p->log(l.b);
    b->verdict = ZXV_LAYERS_OK;
    return b->verdict;
}

void zxv_layers_sealed(zxv_layers_boot_t *b, int airgapped)
{
    for (uint32_t i = 0; i < b->n; i++)
        b->node[i].flags |= ZXV_LAYER_SEALED | (airgapped ? ZXV_LAYER_AIRGAPPED : 0u);
}
