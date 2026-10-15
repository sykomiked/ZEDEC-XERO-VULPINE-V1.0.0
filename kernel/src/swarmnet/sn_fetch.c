/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sn_fetch.c — find a swarm disc in the bucket and write it onto a blank
 * drive, committed only on a SHA-256 match; in steps, resumable and patient
 * (swarmnet.h, ELASTIC). */
#include "swarmnet.h"
#include "../ipfs_node/ipfsn_dir.h"

#define HOLD_BYTES (SN_HOLD_SECTORS * 512u)

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

/* ---- the resume record ------------------------------------------------------
 * The drive's last sector: "ZXVHYDR1", the image's bytes and how many of them
 * are safely written (both little-endian u64), the image's CID (120 chars at
 * most, NUL-padded), and at 480 the SHA-256 of bytes 0..479, so a torn or
 * stray sector is not taken for one. The held head is in the 34 sectors
 * before it. */

static const char JMAGIC[8] = {'Z', 'X', 'V', 'H', 'Y', 'D', 'R', '1'};

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = v << 8 | p[i];
    return v;
}

static int jwrite(sn_writer_t *w, uint64_t durable)
{
    uint8_t *s = w->jsec;
    hw_memset(s, 0, 512);
    hw_memcpy(s, JMAGIC, 8);
    put64(s + 8, w->want);
    put64(s + 16, durable);
    for (uint32_t i = 0; w->jcid[i] && i < 120; i++) s[24 + i] = (uint8_t) w->jcid[i];
    sha256(s, 480, s + 480);
    return w->b->write(w->b, w->jlba, 1, s);
}

int sn_journal_read(hw_blk_t *b, uint8_t *s, char cid[121], uint64_t *want, uint64_t *durable)
{
    uint8_t sum[32];
    if (b->block_size != 512 || b->blocks < SN_JOURNAL_SECTORS + 1u) return 0;
    if (b->read(b, b->blocks - 1u, 1, s) != 0) return 0;
    for (int i = 0; i < 8; i++)
        if (s[i] != (uint8_t) JMAGIC[i]) return 0;
    sha256(s, 480, sum);
    for (int i = 0; i < 32; i++)
        if (sum[i] != s[480 + i]) return 0;
    *want = get64(s + 8);
    *durable = get64(s + 16);
    for (uint32_t i = 0; i < 120; i++) cid[i] = (char) s[24 + i];
    cid[120] = 0;
    return 1;
}

/* ---- the writer ------------------------------------------------------------- */

void sn_writer_init(sn_writer_t *w, hw_blk_t *b, uint64_t want)
{
    w->b = b;
    w->want = want;
    w->got = w->written = 0;
    w->fill = 0;
    w->err = 0;
    w->jlba = 0;
    w->jcid = 0;
    sha256_init(&w->sha);
}

int sn_writer_journal(sn_writer_t *w, const char *cid)
{
    uint64_t sectors = (w->want + 511u) / 512u;
    if (w->want <= HOLD_BYTES || w->b->blocks < sectors + SN_JOURNAL_SECTORS) return 0;
    w->jlba = w->b->blocks - 1u;
    w->jcid = cid;
    return 1;
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
            /* the head is complete: keep a copy by the record */
            if (w->jlba && w->got + k == sizeof w->hold &&
                (w->b->write(w->b, w->jlba - SN_HOLD_SECTORS, SN_HOLD_SECTORS, w->hold) != 0 ||
                 jwrite(w, HOLD_BYTES) != 0)) {
                w->err = 1;
                return -1;
            }
        } else {
            k = SN_STAGE_BYTES - w->fill;
            if (k > len) k = len;
            hw_memcpy(w->stage + w->fill, data, k);
            w->fill += k;
            if (w->fill == SN_STAGE_BYTES &&
                (flush(w) != 0 || (w->jlba && w->written % SN_JOURNAL_EVERY == 0 &&
                                   jwrite(w, HOLD_BYTES + w->written) != 0))) {
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

int sn_writer_reread(sn_writer_t *w, uint64_t durable, uint32_t max)
{
    if (!w->jlba || durable > w->want || durable < HOLD_BYTES ||
        (durable - HOLD_BYTES) % SN_JOURNAL_EVERY != 0)
        return -1;
    if (w->got == 0) {
        if (w->b->read(w->b, w->jlba - SN_HOLD_SECTORS, SN_HOLD_SECTORS, w->hold) != 0) return -1;
        sha256_update(&w->sha, w->hold, HOLD_BYTES);
        w->got = HOLD_BYTES;
    }
    uint32_t done = 0;
    while (w->got < durable && done < max) {
        uint32_t n =
            durable - w->got < SN_STAGE_BYTES ? (uint32_t) (durable - w->got) : SN_STAGE_BYTES;
        if (w->b->read(w->b, SN_HOLD_SECTORS + w->written / 512u, n / 512u, w->stage) != 0)
            return -1;
        sha256_update(&w->sha, w->stage, n);
        w->written += n;
        w->got += n;
        done += n;
    }
    return w->got == durable;
}

int sn_writer_commit(sn_writer_t *w, const uint8_t sha[32], const char **why)
{
    uint8_t got[32];
    const char *bad = 0;
    if (w->err)
        bad = w->got > w->want - 1 ? "longer than the table says or a drive write failed"
                                   : "a drive write failed";
    else if (flush(w) != 0)
        bad = "a drive write failed";
    else if (w->got != w->want)
        bad = "size differs from the table";
    if (!bad) {
        sha256_final(&w->sha, got);
        for (int i = 0; i < 32; i++)
            if (got[i] != sha[i]) bad = "sha256 differs from the table";
    }
    if (w->jlba) { /* resume nothing from here: what failed is not kept */
        hw_memset(w->jsec, 0, 512);
        (void) w->b->write(w->b, w->jlba, 1, w->jsec);
    }
    if (bad) {
        *why = bad;
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

static uint64_t now(sn_fetch_t *f)
{
    return f->clk.ms(f->clk.ctx);
}

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

/* "  [SWARM_FETCH] disc 2 [#####-----] 50% (550000 of 1100000 bytes, 812 KB/s)" */
static void progress(void *ctx, uint64_t got, uint64_t want)
{
    sn_fetch_t *f = (sn_fetch_t *) ctx;
    uint32_t dec = (uint32_t) (got * 10u / want);
    if (dec * 10u <= f->pct || dec == 10) return;
    f->pct = dec * 10u;
    hw_line_t s;
    hw_line_init(&s);
    hw_line_s(&s, "  [SWARM_FETCH] disc ");
    hw_line_u(&s, f->e.disc);
    hw_line_s(&s, " [");
    for (uint32_t i = 0; i < 10; i++) hw_line_c(&s, i < dec ? '#' : '-');
    hw_line_s(&s, "] ");
    hw_line_u(&s, f->pct);
    hw_line_s(&s, "% (");
    hw_line_u(&s, got);
    hw_line_s(&s, " of ");
    hw_line_u(&s, want);
    hw_line_s(&s, " bytes");
    uint64_t ms = now(f) - f->t0;
    if (ms) {
        hw_line_s(&s, ", ");
        hw_line_u(&s, (got - f->got0) / ms); /* bytes per ms = KB/s */
        hw_line_s(&s, " KB/s");
    }
    hw_line_c(&s, ')');
    f->p->log(s.b);
}

/* Is disk b already one of m's volumes (the core disc or a swarm disc)? */
static int in_use(const hw_models_t *m, const hw_blk_t *b)
{
    for (uint32_t i = 0; i < m->nvol; i++)
        if (m->vol[i].disk == b) return 1;
    return 0;
}

static int same(const char *a, const char *b)
{
    while (*a && *a == *b) a++, b++;
    return *a == *b;
}

/* ipfsn_get_fn over f->get: dag-pb nodes from a small cache (a resumed walk
 * goes down the same path every step), and a repeat of the last block from
 * memory. The walker hashes every block it is handed, cached or not. */
static int cached_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    sn_fetch_t *f = (sn_fetch_t *) ctx;
    if (f->last_len && f->last_len <= cap && ipfsn_cid_equal(cid, &f->last_cid)) {
        hw_memcpy(buf, f->last, f->last_len);
        *len = f->last_len;
        return IPFSN_OK;
    }
    for (uint32_t i = 0; i < SN_NODE_CACHE; i++)
        if (f->node_len[i] && f->node_len[i] <= cap && ipfsn_cid_equal(cid, &f->node_cid[i])) {
            hw_memcpy(buf, f->node[i], f->node_len[i]);
            *len = f->node_len[i];
            return IPFSN_OK;
        }
    int rc = f->get(f->get_ctx, cid, buf, cap, len);
    if (rc != IPFSN_OK || !*len) return rc;
    if (cid->codec == IPFSN_MC_DAG_PB && *len <= IPFSN_NODE_MAX) {
        uint32_t i = f->node_next++ % SN_NODE_CACHE;
        hw_memcpy(f->node[i], buf, *len);
        f->node_cid[i] = *cid;
        f->node_len[i] = *len;
    } else if (*len <= SN_CACHE_MAX) {
        hw_memcpy(f->last, buf, *len);
        f->last_cid = *cid;
        f->last_len = *len;
    }
    return rc;
}

/* The writer, within the step's budget: past it the walk is paused, and the
 * next step resumes at the writer's offset. */
static int step_put(void *ctx, const uint8_t *data, uint32_t len)
{
    sn_fetch_t *f = (sn_fetch_t *) ctx;
    if (!f->budget) {
        f->paused = 1;
        return -1;
    }
    f->budget = len < f->budget ? f->budget - len : 0;
    return sn_writer_put(&f->wr, data, len);
}

/* The drive for disc f->e: 1. one whose resume record is for this very image
 * (*resume = 1), 2. a blank one with no record, 3. any blank one. Never one
 * the system already uses. */
static hw_blk_t *pick(sn_fetch_t *f, const hw_models_t *m, hw_registry_t *r, int *resume)
{
    static uint8_t sector[512];
    char cid[121];
    uint64_t want, durable;
    *resume = 0;
    for (int pass = 1; pass <= 3; pass++)
        for (uint32_t i = 0; i < r->n_blk; i++) {
            hw_blk_t *b = r->blk[i];
            if (in_use(m, b) || !sn_drive_blank(b, f->e.bytes, sector)) continue;
            int rec = sn_journal_read(b, sector, cid, &want, &durable);
            if (pass == 1 && rec && want == f->e.bytes && same(cid, f->e.cid)) {
                f->durable = durable;
                *resume = 1;
                return b;
            }
            if (pass == 2 ? !rec : pass == 3) return b;
        }
    return 0;
}

/* A failure no wait will cure: say it, and go on to the next disc. */
static int fail(sn_fetch_t *f, const hw_swarm_want_t *w, const char *why, const char *detail)
{
    say(f, "  [SWARM_FETCH_FAIL]", w, why, detail);
    f->done[f->cur] = 1;
    f->phase = 0;
    return 1;
}

/* A failure time may cure (no gateway answered): wait, longer each time. */
static int wait(sn_fetch_t *f, const hw_swarm_want_t *w)
{
    uint32_t ms = SN_BACKOFF_MS;
    for (uint32_t i = 0; i < f->tries && ms < SN_BACKOFF_MAX_MS; i++) ms *= 2u;
    if (ms > SN_BACKOFF_MAX_MS) ms = SN_BACKOFF_MAX_MS;
    f->tries++;
    f->next_try = now(f) + ms;
    hw_line_t s;
    hw_line_init(&s);
    hw_line_s(&s, "  [SWARM_FETCH] disc ");
    hw_line_u(&s, w->disc);
    hw_line_s(&s, ": waiting for peers (no gateway served the next block); retry ");
    hw_line_u(&s, f->tries);
    hw_line_s(&s, " in ");
    hw_line_u(&s, ms / 1000u);
    hw_line_s(&s, " s; ");
    hw_line_u(&s, f->phase == 2 ? f->wr.got : 0);
    hw_line_s(&s, " bytes so far");
    f->p->log(s.b);
    return 1;
}

/* Phase 0: the disc's row in the baked table, a drive. Then on to the
 * re-read (a resume) or the stream. */
static int find(sn_fetch_t *f, hw_models_t *m, hw_registry_t *r, const hw_swarm_want_t *w)
{
    const sn_baked_t *t = sn_baked_find(f->table, f->ntable, w->disc, w->group);
    if (!t)
        return fail(f, w, ": not in the table this kernel was built with; attach it by hand", 0);
    if (ipfsn_cid_decode_exact(t->cid, sizeof t->cid, &f->disc) != IPFSN_OK ||
        ipfsn_cid_to_string(&f->disc, f->e.cid, sizeof f->e.cid) <= 0)
        return fail(f, w, ": its CID in this kernel's table does not decode", 0);
    f->e.disc = t->disc;
    f->e.bytes = t->bytes;
    hw_memcpy(f->e.sha, t->sha, sizeof f->e.sha);

    /* a blank drive large enough, not one the system already uses */
    int resume;
    hw_blk_t *b = pick(f, m, r, &resume);
    if (!b) return fail(f, w, ": no blank drive with 512-byte sectors large enough to hold it", 0);
    if (f->restarted) resume = 0;
    hw_memcpy(f->jcid, f->e.cid, sizeof f->jcid);
    sn_writer_init(&f->wr, b, f->e.bytes);
    int kept = sn_writer_journal(&f->wr, f->jcid);
    f->wr.progress = progress;
    f->wr.progress_ctx = f;
    f->tries = 0;
    if (resume && kept) {
        hw_line_t s;
        hw_line_init(&s);
        hw_line_s(&s, ": resuming on ");
        hw_line_s(&s, b->name);
        hw_line_s(&s, " at ");
        hw_line_u(&s, f->durable);
        hw_line_s(&s, " of ");
        hw_line_u(&s, f->e.bytes);
        hw_line_s(&s, " bytes; re-reading them first");
        say(f, "  [SWARM_FETCH]", w, s.b, 0);
        f->phase = 1;
        return 1;
    }
    say(f, "  [SWARM_FETCH]", w, " -> ", b->name);
    if (!kept)
        say(f, "  [SWARM_FETCH]", w, ": no room on the drive for a resume record; ",
            "an interrupted download starts again");
    f->pct = 0;
    f->t0 = now(f);
    f->got0 = 0;
    f->phase = 2;
    return 1;
}

/* Phase 1: hash what the drive already holds. */
static int reread(sn_fetch_t *f, const hw_swarm_want_t *w)
{
    int rc = sn_writer_reread(&f->wr, f->durable, SN_REREAD_BYTES);
    if (rc < 0) {
        /* an unreadable drive or an odd record: start this disc afresh */
        say(f, "  [SWARM_FETCH]", w, ": the resume record or the drive could not be read back; ",
            "starting from the beginning");
        sn_writer_init(&f->wr, f->wr.b, f->e.bytes);
        (void) sn_writer_journal(&f->wr, f->jcid);
        f->wr.progress = progress;
        f->wr.progress_ctx = f;
        f->durable = 0;
    } else if (!rc) {
        return 1;
    } else {
        hw_line_t s;
        hw_line_init(&s);
        hw_line_s(&s, ": ");
        hw_line_u(&s, f->durable);
        hw_line_s(&s, " bytes re-read from ");
        hw_line_s(&s, f->wr.b->name);
        hw_line_s(&s, " and hashed; continuing from there");
        say(f, "  [SWARM_FETCH]", w, s.b, 0);
    }
    f->pct = (uint32_t) (f->wr.got * 10u / f->e.bytes) * 10u;
    f->t0 = now(f);
    f->got0 = f->wr.got;
    f->phase = 2;
    return 1;
}

/* Phase 2: up to SN_STEP_BYTES more of the image; commit at the end. */
static int stream(sn_fetch_t *f, const hw_swarm_want_t *w)
{
    uint64_t size = 0, before = f->wr.got;
    const char *detail = 0;
    f->budget = SN_STEP_BYTES;
    f->paused = 0;
    int rc = ipfsn_cat_from(&f->walk, &f->disc, f->wr.got, cached_get, f, step_put, f, &size);
    if (f->wr.got > before) f->tries = 0;
    if (f->paused && !f->wr.err) return 1;
    if (f->wr.err) {
        (void) sn_writer_commit(&f->wr, f->e.sha, &detail);
        return fail(f, w, ": nothing committed: ", detail);
    }
    if (rc == IPFSN_ERR_TRANSPORT) return wait(f, w);
    if (rc == IPFSN_ERR_HASH) {
        (void) sn_writer_commit(&f->wr, f->e.sha, &detail);
        return fail(f, w, ": every gateway sent a block that does not match its CID; ",
                    "nothing committed");
    }
    if (rc != IPFSN_OK) {
        (void) sn_writer_commit(&f->wr, f->e.sha, &detail);
        return fail(f, w, ": the download failed; nothing committed", 0);
    }
    int resumed = f->got0 > 0;
    if (sn_writer_commit(&f->wr, f->e.sha, &detail) != 0) {
        if (resumed && !f->restarted) { /* the drive's earlier part was not the image */
            say(f, "  [SWARM_FETCH]", w, ": ", detail);
            say(f, "  [SWARM_FETCH]", w, ": the resumed part was not the image; ",
                "downloading it again from the start");
            f->restarted = 1;
            f->phase = 0;
            return 1;
        }
        return fail(f, w, ": nothing committed: ", detail);
    }
    hw_line_t s;
    hw_line_init(&s);
    hw_line_s(&s, "  [SWARM_FETCHED] disc ");
    hw_line_u(&s, w->disc);
    hw_line_s(&s, " ZXV_SWARM_");
    hw_line_s(&s, w->group);
    hw_line_s(&s, " on ");
    hw_line_s(&s, f->wr.b->name);
    hw_line_s(&s, ": ");
    hw_line_u(&s, f->e.bytes);
    hw_line_s(&s, " bytes, sha256 matches the table; committed");
    f->p->log(s.b);
    f->done[f->cur] = 1;
    f->committed++;
    f->fresh = 1;
    f->phase = 0;
    return 1;
}

void sn_fetch_begin(sn_fetch_t *f)
{
    f->walk.leaf = f->leaf;
    f->walk.leaf_cap = sizeof f->leaf;
    f->last_len = 0;
    for (uint32_t i = 0; i < SN_NODE_CACHE; i++) f->node_len[i] = 0;
    for (uint32_t i = 0; i < HW_SWARM_WANT_MAX; i++) f->done[i] = 0;
    f->cur = 0;
    f->phase = 0;
    f->restarted = f->stopped = f->fresh = 0;
    f->committed = 0;
    f->tries = 0;
    f->next_try = 0;
    f->g0 = 0;
}

int sn_fetch_step(sn_fetch_t *f, hw_models_t *m, hw_registry_t *r)
{
    f->fresh = 0;
    if (f->stopped) return 0;
    if (f->phase == 0) { /* the next disc still missing */
        while (f->cur < m->nwant && (m->want[f->cur].attached || f->done[f->cur])) {
            f->cur++;
            f->restarted = 0;
        }
        if (f->cur >= m->nwant) return 0;
    }
    if (now(f) < f->next_try) return 1;
    const hw_swarm_want_t *w = &m->want[f->cur];
    if (f->phase == 0) return find(f, m, r, w);
    if (f->phase == 1) return reread(f, w);
    return stream(f, w);
}

void sn_fetch_stop(sn_fetch_t *f, const hw_models_t *m)
{
    hw_line_t s;
    f->stopped = 1;
    for (uint32_t i = 0; i < m->nwant; i++) {
        if (m->want[i].attached || f->done[i]) continue;
        hw_line_init(&s);
        hw_line_s(&s, "[SWARM] Hydration stopped at your request: Attach Disc ");
        hw_line_u(&s, m->want[i].disc);
        hw_line_s(&s, " USB / drive.");
        f->p->log(s.b);
        break;
    }
    if (f->phase == 2 && f->wr.jlba) {
        hw_line_init(&s);
        hw_line_s(&s, "  [SWARM] ");
        hw_line_u(&s, f->wr.got < HOLD_BYTES
                          ? 0
                          : HOLD_BYTES + f->wr.written - f->wr.written % SN_JOURNAL_EVERY);
        hw_line_s(&s, " bytes of disc ");
        hw_line_u(&s, f->e.disc);
        hw_line_s(&s, " stay on ");
        hw_line_s(&s, f->wr.b->name);
        hw_line_s(&s, " and resume on a later boot with a network");
        f->p->log(s.b);
    }
}
