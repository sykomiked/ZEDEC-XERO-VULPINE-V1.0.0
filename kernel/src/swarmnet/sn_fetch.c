/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_fetch.c — find a swarm disc in the bucket and write it onto a blank
 * drive, committed only on a SHA-256 match (swarmnet.h). */
#include "swarmnet.h"
#include "../ipfs_node/ipfsn_dir.h"

int sn_drive_blank(hw_blk_t *b, uint64_t bytes, uint8_t *sector)
{
    if (b->block_size != 512 || b->blocks < (bytes + 511u) / 512u) return 0;
    if (b->read(b, 0, 1, sector) != 0 || (sector[510] == 0x55 && sector[511] == 0xAA)) return 0;
    if (b->read(b, 1, 1, sector) != 0) return 0;
    const char *sig = "EFI PART";
    for (int i = 0; i < 8; i++)
        if (sector[i] != (uint8_t) sig[i]) return 1;
    return 0;
}

void sn_writer_init(sn_writer_t *w, hw_blk_t *b, uint64_t want)
{
    w->b = b;
    w->want = want;
    w->got = w->written = 0;
    w->fill = 0;
    w->err = 0;
    sha256_init(&w->sha);
}

static int flush(sn_writer_t *w)
{
    if (!w->fill) return 0;
    uint64_t lba = SN_HOLD_SECTORS + w->written / 512u;
    uint32_t n = (w->fill + 511u) / 512u;
    for (uint32_t i = w->fill; i < n * 512u; i++) w->stage[i] = 0;
    if (w->b->write(w->b, lba, n, w->stage) != 0) return -1;
    w->written += w->fill;
    w->fill = 0;
    return 0;
}

int sn_writer_put(void *ctx, const uint8_t *data, uint32_t len)
{
    sn_writer_t *w = (sn_writer_t *) ctx;
    if (w->err) return -1;
    if (w->got + len > w->want) {
        w->err = 1;
        return -1;
    }
    sha256_update(&w->sha, data, len);
    while (len) {
        uint32_t k;
        if (w->got < sizeof w->hold) {
            k = (uint32_t) sizeof w->hold - (uint32_t) w->got;
            if (k > len) k = len;
            hw_memcpy(w->hold + w->got, data, k);
        } else {
            k = SN_STAGE_BYTES - w->fill;
            if (k > len) k = len;
            hw_memcpy(w->stage + w->fill, data, k);
            w->fill += k;
            if (w->fill == SN_STAGE_BYTES && flush(w) != 0) {
                w->err = 1;
                return -1;
            }
        }
        w->got += k;
        data += k;
        len -= k;
    }
    if (w->progress) w->progress(w->progress_ctx, w->got, w->want);
    return 0;
}

int sn_writer_commit(sn_writer_t *w, const uint8_t sha[32], const char **why)
{
    uint8_t got[32];
    if (w->err) {
        *why = w->got > w->want - 1 ? "longer than the manifest says or a drive write failed"
                                    : "a drive write failed";
        return -1;
    }
    if (flush(w) != 0) {
        *why = "a drive write failed";
        return -1;
    }
    if (w->got != w->want) {
        *why = "size differs from the manifest";
        return -1;
    }
    sha256_final(&w->sha, got);
    for (int i = 0; i < 32; i++)
        if (got[i] != sha[i]) {
            *why = "sha256 differs from the manifest";
            return -1;
        }
    uint32_t held = w->want < sizeof w->hold ? (uint32_t) w->want : (uint32_t) sizeof w->hold;
    for (uint32_t i = held; i < (held + 511u) / 512u * 512u; i++) w->hold[i] = 0;
    if (w->b->write(w->b, 0, (held + 511u) / 512u, w->hold) != 0) {
        *why = "writing the partition table failed";
        return -1;
    }
    return 0;
}

/* ---- the fetch -------------------------------------------------------------- */

static void say(sn_fetch_t *f, const char *tag, const hw_swarm_want_t *w, const char *a,
                const char *b)
{
    hw_line_t s;
    hw_line_init(&s);
    hw_line_s(&s, tag);
    hw_line_s(&s, " disc ");
    hw_line_u(&s, w->disc);
    hw_line_s(&s, " ZXV_SWARM_");
    hw_line_s(&s, w->group);
    hw_line_s(&s, a);
    if (b) hw_line_s(&s, b);
    f->p->log(s.b);
}

static void progress(void *ctx, uint64_t got, uint64_t want)
{
    sn_fetch_t *f = (sn_fetch_t *) ctx;
    uint32_t pct = (uint32_t) (got * 10u / want) * 10u;
    if (pct <= f->pct || pct == 100) return;
    f->pct = pct;
    hw_line_t s;
    hw_line_init(&s);
    hw_line_s(&s, "  [SWARM_FETCH] disc ");
    hw_line_u(&s, f->e.disc);
    hw_line_s(&s, ": ");
    hw_line_u(&s, pct);
    hw_line_s(&s, "% (");
    hw_line_u(&s, got);
    hw_line_s(&s, " of ");
    hw_line_u(&s, want);
    hw_line_s(&s, " bytes)");
    f->p->log(s.b);
}

/* Is disk b already one of m's volumes (the core disc or a swarm disc)? */
static int in_use(const hw_models_t *m, const hw_blk_t *b)
{
    for (uint32_t i = 0; i < m->nvol; i++)
        if (m->vol[i].disk == b) return 1;
    return 0;
}

/* ipfsn_write_fn into the manifest buffer. */
typedef struct {
    char *b;
    uint32_t n, cap;
} sn_mem_t;

static int mem_put(void *ctx, const uint8_t *data, uint32_t len)
{
    sn_mem_t *o = (sn_mem_t *) ctx;
    if (len > o->cap - o->n) return -1;
    hw_memcpy(o->b + o->n, data, len);
    o->n += len;
    return 0;
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

/* ipfsn_get_fn over f->get, answering a repeat of the last block from memory. */
static int cached_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    sn_fetch_t *f = (sn_fetch_t *) ctx;
    if (f->last_len && f->last_len <= cap && ipfsn_cid_equal(cid, &f->last_cid)) {
        hw_memcpy(buf, f->last, f->last_len);
        *len = f->last_len;
        return IPFSN_OK;
    }
    int rc = f->get(f->get_ctx, cid, buf, cap, len);
    f->last_len = 0;
    if (rc == IPFSN_OK && *len && *len <= SN_CACHE_MAX) {
        hw_memcpy(f->last, buf, *len);
        f->last_cid = *cid;
        f->last_len = *len;
    }
    return rc;
}

/* Fetch disc w onto a blank drive: NULL when committed (*used = the
 * drive), else the reason, with `detail` appended when set. */
static const char *fetch_one(sn_fetch_t *f, hw_models_t *m, hw_registry_t *r,
                             const hw_swarm_want_t *w, hw_blk_t **used, const char **detail)
{
    ipfsn_cid_t root, want;
    ipfsn_dirent_t ent;
    uint32_t line;
    uint64_t size = 0;
    static uint8_t sector[512];
    *detail = 0;
    if (ipfsn_cid_parse(m->cid, slen(m->cid), &root) != IPFSN_OK)
        return ": the bucket CID does not parse";

    /* manifest.txt, through the bucket's root directory: every block verified */
    if (ipfsn_dir_resolve(&root, f->get, f->get_ctx, "manifest.txt", 12, f->dirbuf,
                          sizeof f->dirbuf, &ent) != IPFSN_OK)
        return ": the bucket has no manifest.txt this kernel can reach";
    sn_mem_t mo = {f->manifest, 0, SN_MANIFEST_MAX};
    if (ipfsn_cat(&f->walk, &ent.cid, f->get, f->get_ctx, mem_put, &mo, &size) != IPFSN_OK)
        return ": manifest.txt could not be read (unreachable, or over 16 KiB)";
    *detail = sn_manifest_find(f->manifest, mo.n, w->disc, w->group, &f->e, &line);
    if (*detail) return ": manifest.txt: ";

    /* the disc's CID through the directories must be the manifest's */
    if (ipfsn_cid_parse(f->e.cid, slen(f->e.cid), &want) != IPFSN_OK)
        return ": manifest.txt gives a CID that does not parse";
    if (ipfsn_dir_resolve(&root, f->get, f->get_ctx, f->e.path, slen(f->e.path), f->dirbuf,
                          sizeof f->dirbuf, &ent) != IPFSN_OK)
        return ": the bucket has no file at manifest.txt's path";
    if (!ipfsn_cid_equal(&ent.cid, &want))
        return ": the bucket's directory and manifest.txt disagree on its CID";

    /* a blank drive large enough, not one the system already uses */
    hw_blk_t *b = 0;
    for (uint32_t i = 0; i < r->n_blk && !b; i++)
        if (!in_use(m, r->blk[i]) && sn_drive_blank(r->blk[i], f->e.bytes, sector)) b = r->blk[i];
    if (!b) return ": no blank drive with 512-byte sectors large enough to hold it";
    say(f, "  [SWARM_FETCH]", w, " -> ", b->name);

    sn_writer_init(&f->wr, b, f->e.bytes);
    f->wr.progress = progress;
    f->wr.progress_ctx = f;
    f->pct = 0;
    int rc = ipfsn_cat(&f->walk, &want, cached_get, f, sn_writer_put, &f->wr, &size);
    if (rc != IPFSN_OK && !f->wr.err) return ": the download failed; nothing committed";
    if (sn_writer_commit(&f->wr, f->e.sha, detail) != 0) return ": nothing committed: ";
    *used = b;
    return 0;
}

uint32_t sn_fetch_missing(sn_fetch_t *f, hw_models_t *m, hw_registry_t *r)
{
    uint32_t done = 0;
    hw_blk_t *used;
    f->walk.leaf = f->leaf;
    f->walk.leaf_cap = sizeof f->leaf;
    f->last_len = 0;
    for (uint32_t i = 0; i < m->nwant; i++) {
        const hw_swarm_want_t *w = &m->want[i];
        if (w->attached) continue;
        const char *detail;
        const char *why = fetch_one(f, m, r, w, &used, &detail);
        if (why) {
            say(f, "  [SWARM_FETCH_FAIL]", w, why, detail);
            continue;
        }
        hw_line_t s;
        hw_line_init(&s);
        hw_line_s(&s, "  [SWARM_FETCHED] disc ");
        hw_line_u(&s, w->disc);
        hw_line_s(&s, " ZXV_SWARM_");
        hw_line_s(&s, w->group);
        hw_line_s(&s, " on ");
        hw_line_s(&s, used->name);
        hw_line_s(&s, ": ");
        hw_line_u(&s, f->e.bytes);
        hw_line_s(&s, " bytes, sha256 matches the manifest; committed");
        f->p->log(s.b);
        done++;
    }
    return done;
}
