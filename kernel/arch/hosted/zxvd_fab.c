/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvd_fab.c — fab.*: send, receive and stream fabricated files over zxvd's
 * socket (zxvd_fab.h B1-B12) on kernel/src/fab. Integers only.
 */
#if !defined(_WIN32)
#    define _POSIX_C_SOURCE 200809L
#    include <fcntl.h>
#    include <sys/stat.h>
#    include <unistd.h>
#else
#    include <windows.h>
#    include <direct.h>
#    include <errno.h>
#    include <io.h>
#    include <fcntl.h>
#    include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxvd_fab.h"
#include "zxvd_groups.h"
#include "zxv_http_guard.h" /* zxv_os_random */
#include "fab.h"

#if defined(_WIN32)
static int open_new(const char *p)
{
    return _open(p, _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
}
static int sync_fd(FILE *f)
{
    return _commit(_fileno(f));
}
static bool replace(const char *tmp, const char *path)
{
    return MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
static int make_dir(const char *p)
{
    return _mkdir(p) == 0 || errno == EEXIST ? 0 : -1;
}
#    define fdopen _fdopen
#    define close  _close
#    define unlink _unlink
#else
static int open_new(const char *p)
{
    return open(p, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
}
static int sync_fd(FILE *f)
{
    return fsync(fileno(f));
}
static bool replace(const char *tmp, const char *path)
{
    return rename(tmp, path) == 0;
}
static int make_dir(const char *p)
{
    struct stat st;
    if (mkdir(p, S_IRWXU) == 0) return 0;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode) ? 0 : -1;
}
#endif

typedef struct {
    uint32_t handle; /* 0: free */
    bool send;
    fab_stream_t st;
    uint8_t *prev; /* the last frame (B10, B9): the PREV source */
    uint32_t prev_len;
    ipfsn_cid_t *cids; /* the sender's blocks (B9) */
    uint32_t n_cids;
    uint32_t window;
} stream_t;

static struct {
    bool open;
    char dir[ZXVD_FAB_PATH];
    char blocks[ZXVD_FAB_PATH + 8];
    uint8_t spk[FAB_SIG_PK], ssk[FAB_SIG_SK];
    uint8_t ek[FAB_KEM_EK], dk[FAB_KEM_DK];
    uint8_t sid[FAB_ID], kid[FAB_ID];
    lpres_state_t (*seal)(void *ctx);
    void *seal_ctx;
    stream_t s[ZXVD_FAB_STREAMS];
    uint32_t next_handle;
} G;

static fab_work_t W;
static int (*RANDOM)(uint8_t *p, size_t n) = zxv_os_random;

void zxvd_fab_set_random(int (*fn)(uint8_t *p, size_t n))
{
    RANDOM = fn ? fn : zxv_os_random;
}

void zxvd_fab_set_seal(lpres_state_t (*fn)(void *ctx), void *ctx)
{
    G.seal = fn;
    G.seal_ctx = fn ? ctx : NULL;
}

/* ---- files (B3) ------------------------------------------------------------ */

static bool write_atomic(const char *path, const uint8_t *data, uint32_t len)
{
    char tmp[ZXVD_FAB_PATH + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    unlink(tmp);
    int fd = open_new(tmp);
    FILE *f = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!f) {
        if (fd >= 0) close(fd);
        return false;
    }
    bool ok = fwrite(data, 1, len, f) == len && fflush(f) == 0 && sync_fd(f) == 0;
    ok = fclose(f) == 0 && ok && replace(tmp, path);
    if (!ok) unlink(tmp);
    return ok;
}

/* The whole file, at most max bytes: a malloc'd buffer (len 0 still gets
 * one byte), or NULL with *too_big set when it is longer than max. */
static uint8_t *read_all(const char *path, uint32_t max, uint32_t *len, bool *too_big)
{
    *too_big = false;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *b = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long n = ftell(f);
        if (n > (long) max) *too_big = true;
        if (n >= 0 && n <= (long) max && fseek(f, 0, SEEK_SET) == 0) {
            b = malloc(n ? (size_t) n : 1u);
            if (b && fread(b, 1, (size_t) n, f) != (size_t) n) {
                free(b);
                b = NULL;
            }
            *len = (uint32_t) n;
        }
    }
    fclose(f);
    return b;
}

static bool read_exact(const char *path, uint8_t *out, uint32_t n)
{
    uint32_t len = 0;
    bool big;
    uint8_t *b = read_all(path, n, &len, &big);
    bool ok = b && len == n;
    if (ok) memcpy(out, b, n);
    if (b) {
        memset(b, 0, len);
        free(b);
    }
    return ok;
}

/* the store's file for a CID (B2) */
static bool block_path(const ipfsn_cid_t *cid, char *out, size_t cap)
{
    char name[IPFSN_CID_STR_MAX];
    if (ipfsn_cid_to_string(cid, name, sizeof name) < 0) return false;
    return snprintf(out, cap, "%s/%s", G.blocks, name) < (int) cap;
}

static int store_get(void *ctx, const ipfsn_cid_t *cid, uint8_t *buf, uint32_t cap, uint32_t *len)
{
    (void) ctx;
    char path[ZXVD_FAB_PATH + IPFSN_CID_STR_MAX + 16];
    if (!block_path(cid, path, sizeof path)) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    int more = fgetc(f) != EOF;
    fclose(f);
    if (more) return -1;
    *len = (uint32_t) n;
    return 0;
}

/* ---- keys (B2) ------------------------------------------------------------- */

static bool seed_file(const char *name, uint8_t *seed, uint32_t n)
{
    char path[ZXVD_FAB_PATH + 16];
    snprintf(path, sizeof path, "%s/%s", G.dir, name);
    if (read_exact(path, seed, n)) return true;
    FILE *f = fopen(path, "rb");
    if (f) { /* there, but not n bytes: never replaced silently */
        fclose(f);
        return false;
    }
    return RANDOM(seed, n) == 0 && write_atomic(path, seed, n);
}

int zxvd_fab_open(const char *dir)
{
    zxvd_fab_close();
    if (!dir || !dir[0] || strlen(dir) + 16 > ZXVD_FAB_PATH) return -1;
    snprintf(G.dir, sizeof G.dir, "%s", dir);
    snprintf(G.blocks, sizeof G.blocks, "%s/blocks", dir);
    if (make_dir(G.dir) != 0 || make_dir(G.blocks) != 0) return -1;
    uint8_t s1[32], s2[64];
    bool ok = seed_file("sign.seed", s1, sizeof s1) && seed_file("kem.seed", s2, sizeof s2);
    if (ok) {
        fab_sig_keypair(s1, G.spk, G.ssk);
        fab_kem_keypair(s2, s2 + 32, G.ek, G.dk);
        fab_key_id(G.spk, FAB_SIG_PK, G.sid);
        fab_key_id(G.ek, FAB_KEM_EK, G.kid);
    }
    memset(s1, 0, sizeof s1);
    memset(s2, 0, sizeof s2);
    W.loaded = -1;
    G.open = ok;
    return ok ? 0 : -1;
}

static void stream_free(stream_t *s)
{
    if (s->prev) {
        memset(s->prev, 0, s->prev_len);
        free(s->prev);
    }
    free(s->cids);
    memset(s, 0, sizeof *s);
}

void zxvd_fab_close(void)
{
    for (uint32_t i = 0; i < ZXVD_FAB_STREAMS; i++) stream_free(&G.s[i]);
    memset(G.ssk, 0, sizeof G.ssk);
    memset(G.dk, 0, sizeof G.dk);
    G.open = false;
}

/* ---- helpers ---------------------------------------------------------------- */

static const char *rc_name(int rc)
{
    static const char *const N[] = {"OK",   "ARG",    "FORM",   "TRUST", "SIGN",  "KEY",
                                    "AUTH", "PLAN",   "SPACE",  "FETCH", "BLOCK", "TARGET",
                                    "SEAL", "REPLAY", "RESYNC", "STREAM"};
    return rc <= 0 && -rc < (int) (sizeof N / sizeof N[0]) ? N[-rc] : "ERROR";
}

static const char *rc_why(int rc)
{
    switch (rc) {
    case FAB_ERR_FORM:
        return "not a well-formed recipe, frame or checkpoint";
    case FAB_ERR_TRUST:
        return "the sender is not in the trust list";
    case FAB_ERR_SIGN:
        return "the signature does not verify";
    case FAB_ERR_KEY:
        return "it is not sealed to this zxvd's key";
    case FAB_ERR_AUTH:
        return "decryption failed: tampered, or the wrong key";
    case FAB_ERR_PLAN:
        return "the plan breaks a rule";
    case FAB_ERR_SPACE:
        return "too large for this zxvd";
    case FAB_ERR_FETCH:
        return "a block is not in the store";
    case FAB_ERR_BLOCK:
        return "a block does not match its CID";
    case FAB_ERR_TARGET:
        return "the rebuilt bytes do not match the target CID";
    case FAB_ERR_SEAL:
        return "the air-gap seal is not in place";
    case FAB_ERR_REPLAY:
        return "the frame is not newer than the last one";
    case FAB_ERR_RESYNC:
        return "the frame needs a base this end does not hold";
    case FAB_ERR_STREAM:
        return "not this stream";
    default:
        return "refused";
    }
}

static bool fab_ok(zxvd_call_t *c, int rc)
{
    if (rc == FAB_OK) return true;
    zxvd_fail(c, ZXVD_E_FAB, "%s: %s", rc_name(rc), rc_why(rc));
    return false;
}

static bool need_open(zxvd_call_t *c)
{
    if (G.open) return true;
    zxvd_fail(c, ZXVD_E_FAB, "FILE: fabrication is not open in this zxvd");
    return false;
}

static bool absolute(const char *p)
{
#if defined(_WIN32)
    if (((p[0] | 0x20) >= 'a' && (p[0] | 0x20) <= 'z') && p[1] == ':' &&
        (p[2] == '\\' || p[2] == '/'))
        return true;
    if (p[0] == '\\' && p[1] == '\\') return true;
#endif
    return p[0] == '/';
}

/* B3 */
static bool p_path(zxvd_call_t *c, const char *key, char out[ZXVD_FAB_PATH])
{
    if (!zxvd_p_str(c, key, out, ZXVD_FAB_PATH, true)) return false;
    if (absolute(out)) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must be an absolute path", key);
    return false;
}

static uint8_t *p_file(zxvd_call_t *c, const char *path, uint32_t max, uint32_t *len)
{
    bool big;
    uint8_t *b = read_all(path, max, len, &big);
    if (!b) zxvd_fail(c, ZXVD_E_FAB, "FILE: %s %s", big ? "too large:" : "cannot read", path);
    return b;
}

static bool p_write(zxvd_call_t *c, const char *path, const uint8_t *b, uint32_t len)
{
    if (write_atomic(path, b, len)) return true;
    zxvd_fail(c, ZXVD_E_FAB, "FILE: cannot write %s", path);
    return false;
}

static void put_cid(zxvd_buf_t *b, const ipfsn_cid_t *cid)
{
    char s[IPFSN_CID_STR_MAX];
    if (ipfsn_cid_to_string(cid, s, sizeof s) < 0) s[0] = 0;
    zxvd_buf_json_str(b, s);
}

/* blocks: [{cid}] (B7, B9): their CIDs; *n = 0 when absent */
static ipfsn_cid_t *p_blocks(zxvd_call_t *c, uint32_t *n)
{
    *n = 0;
    int32_t k = zxvd_p_len(c, "blocks");
    if (k < -1) return NULL;
    if (k > (int32_t) FAB_MAX_BLOCKS) {
        zxvd_fail(c, ZXVD_E_PARAMS, "at most %u blocks", (unsigned) FAB_MAX_BLOCKS);
        return NULL;
    }
    ipfsn_cid_t *v = calloc(k > 0 ? (size_t) k : 1u, sizeof *v);
    if (!v) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        return NULL;
    }
    for (int32_t i = 0; i < k; i++) {
        int32_t saved;
        char s[IPFSN_CID_STR_MAX];
        if (!zxvd_p_enter(c, "blocks", (uint32_t) i, &saved)) break;
        bool ok = zxvd_p_str(c, "cid", s, sizeof s, true);
        zxvd_p_leave(c, saved);
        if (!ok) break;
        if (ipfsn_cid_parse(s, (uint32_t) strlen(s), &v[i]) != IPFSN_OK) {
            zxvd_fail(c, ZXVD_E_PARAMS, "\"blocks\"[%d].cid is not a CID", (int) i);
            break;
        }
        for (int32_t j = 0; j < i; j++)
            if (ipfsn_cid_equal(&v[j], &v[i])) {
                zxvd_fail(c, ZXVD_E_PARAMS, "\"blocks\"[%d] repeats a block", (int) i);
                break;
            }
        if (zxvd_failed(c)) break;
    }
    if (zxvd_failed(c)) {
        free(v);
        return NULL;
    }
    *n = (uint32_t) (k > 0 ? k : 0);
    return v;
}

/* the blocks' bytes from the store, checked against their CIDs */
static bool load_blocks(zxvd_call_t *c, const ipfsn_cid_t *cids, uint32_t n, fab_src_t *src)
{
    for (uint32_t i = 0; i < n; i++) {
        char path[ZXVD_FAB_PATH + IPFSN_CID_STR_MAX + 16];
        uint32_t len = 0;
        bool big;
        uint8_t *b = block_path(&cids[i], path, sizeof path)
                         ? read_all(path, FAB_BLOCK_MAX, &len, &big)
                         : NULL;
        if (!b) return fab_ok(c, FAB_ERR_FETCH);
        src[i].data = b;
        src[i].len = len;
        src[i].cid = cids[i];
        src[i].prev = false;
        if (!len || ipfsn_cid_verify(&cids[i], b, len) != IPFSN_OK) return fab_ok(c, FAB_ERR_BLOCK);
    }
    return true;
}

static void free_srcs(fab_src_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (!src[i].prev) free((void *) src[i].data);
    free(src);
}

/* trust: [{id}] (B8, B10): NULL and *n = 0 when absent (anyone) */
static uint8_t (*p_trust(zxvd_call_t *c, uint32_t *n, bool *bad))[FAB_ID]
{
    *n = 0;
    *bad = false;
    int32_t k = zxvd_p_len(c, "trust");
    if (k < -1 || k > 64) {
        if (k > 64) zxvd_fail(c, ZXVD_E_PARAMS, "at most 64 trusted senders");
        *bad = true;
        return NULL;
    }
    if (k < 0) return NULL;
    uint8_t(*ids)[FAB_ID] = calloc(k ? (size_t) k : 1u, FAB_ID);
    if (!ids) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        *bad = true;
        return NULL;
    }
    for (int32_t i = 0; i < k; i++) {
        int32_t saved;
        uint32_t got = 0;
        if (!zxvd_p_enter(c, "trust", (uint32_t) i, &saved)) break;
        bool ok = zxvd_p_hex(c, "id", ids[i], FAB_ID, &got, true);
        zxvd_p_leave(c, saved);
        if (ok && got != FAB_ID) {
            zxvd_fail(c, ZXVD_E_PARAMS, "\"trust\"[%d].id must be %u bytes of hex", (int) i,
                      (unsigned) FAB_ID);
            ok = false;
        }
        if (!ok) {
            free(ids);
            *bad = true;
            return NULL;
        }
    }
    *n = (uint32_t) k;
    return ids;
}

static stream_t *p_stream(zxvd_call_t *c, bool send)
{
    uint32_t h;
    if (!zxvd_p_u32(c, "stream", &h)) return NULL;
    for (uint32_t i = 0; i < ZXVD_FAB_STREAMS; i++)
        if (h && G.s[i].handle == h && G.s[i].send == send) return &G.s[i];
    zxvd_fail(c, ZXVD_E_FAB, "STREAM: no %s stream %u", send ? "sending" : "receiving",
              (unsigned) h);
    return NULL;
}

static stream_t *new_stream(zxvd_call_t *c)
{
    for (uint32_t i = 0; i < ZXVD_FAB_STREAMS; i++)
        if (!G.s[i].handle) return &G.s[i];
    zxvd_fail(c, ZXVD_E_FAB, "STREAM: %u streams are open; close one first",
              (unsigned) ZXVD_FAB_STREAMS);
    return NULL;
}

/* ---- building (B7, B9) --------------------------------------------------------- */

typedef struct {
    uint8_t *plan, *diff;
    uint32_t plan_len, diff_len, n_ops, used;
    ipfsn_cid_t cid;
} built_t;

static void built_free(built_t *b)
{
    free(b->plan);
    if (b->diff) free(b->diff);
    memset(b, 0, sizeof *b);
}

/* fab_build over src, then the plan bytes */
static bool build(zxvd_call_t *c, const fab_src_t *src, uint32_t nsrc, const uint8_t *t,
                  uint32_t tlen, uint32_t window, uint32_t slot_us, built_t *out)
{
    memset(out, 0, sizeof *out);
    if (window < FAB_WINDOW_MIN) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"window\" must be at least %u", (unsigned) FAB_WINDOW_MIN);
        return false;
    }
    const uint32_t bits = 20;
    uint32_t ops_cap = tlen / 8u + 16u;
    if (ops_cap > FAB_MAX_OPS) ops_cap = FAB_MAX_OPS;
    fab_ient_t *table = malloc(sizeof *table << bits);
    uint8_t *ops = malloc((size_t) ops_cap * FAB_OP_REC);
    out->diff = malloc(tlen ? tlen : 1u);
    bool ok = false;
    if (!table || !ops || !out->diff) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!fab_ok(c, fab_build(src, nsrc, t, tlen, window, table, bits, ops, ops_cap, &out->n_ops,
                             out->diff, tlen, &out->diff_len)) ||
        !fab_ok(c, fab_target_cid(t, tlen, &W, &out->cid)))
        goto done;
    uint32_t nb = 0;
    for (uint32_t i = 0; i < nsrc; i++) nb += src[i].prev ? 0u : 1u;
    uint32_t cap = FAB_PLAN_HDR + nb * FAB_BLOCK_REC + out->n_ops * FAB_OP_REC;
    out->plan = malloc(cap);
    if (!out->plan) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!fab_ok(c, fab_plan_write(&out->cid, tlen, slot_us, src, nsrc, ops, out->n_ops, out->plan,
                                  cap, &out->plan_len)))
        goto done;
    for (uint32_t b = 0; b < nb; b++)
        for (uint32_t k = 0; k < out->n_ops; k++) {
            const uint8_t *o = ops + (size_t) k * FAB_OP_REC;
            if (o[0] == FAB_OP_SLICE && (uint32_t) ((o[2] << 8) | o[3]) == b) {
                out->used++;
                break;
            }
        }
    ok = true;
done:
    free(table);
    free(ops);
    if (!ok) built_free(out);
    return ok;
}

/* the signed recipe for a built plan */
static uint8_t *seal_recipe(zxvd_call_t *c, uint8_t kind, const uint8_t *ek, const built_t *b,
                            fab_stream_t *s, uint32_t *len)
{
    uint8_t coins[32], rnd[32];
    if (RANDOM(coins, sizeof coins) != 0 || RANDOM(rnd, sizeof rnd) != 0) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "no random source");
        return NULL;
    }
    fab_seal_args_t a = {
        kind, ek ? FAB_MODE_SEALED : FAB_MODE_PUBLIC, G.spk, G.ssk, ek, ek ? coins : NULL, rnd};
    uint32_t cap =
        b->plan_len + b->diff_len + FAB_HDR + FAB_KEM_CT + 2u * FAB_TAG + FAB_SIG_PK + FAB_SIG;
    uint8_t *rec = malloc(cap);
    if (!rec) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        return NULL;
    }
    int rc = fab_recipe_seal(&a, b->plan, b->plan_len, b->diff, b->diff_len, s, rec, cap, len);
    memset(coins, 0, sizeof coins);
    if (!fab_ok(c, rc)) {
        free(rec);
        return NULL;
    }
    return rec;
}

/* ---- receiving (B8, B10) --------------------------------------------------------- */

typedef struct {
    uint8_t *rec, *plan;
    fab_recipe_t r;
    fab_plan_t p;
    fab_io_t io;
    uint32_t got;
} opened_t;

static void opened_free(opened_t *o)
{
    free(o->rec);
    free(o->plan);
    if (o->io.diff_buf) {
        memset(o->io.diff_buf, 0, o->io.diff_cap);
        free(o->io.diff_buf);
    }
    if (o->io.out) {
        memset(o->io.out, 0, o->io.out_cap);
        free(o->io.out);
    }
    memset(o, 0, sizeof *o);
}

/* Read, check and open a recipe of `kind`, hydrate its blocks and assemble
 * it (into o->io.out) after the seal when airgap is asked (B4). */
static bool receive(zxvd_call_t *c, uint8_t kind, fab_stream_t *s, opened_t *o, uint32_t *olen)
{
    char path[ZXVD_FAB_PATH];
    bool airgap, bad;
    uint32_t len = 0, nt = 0;
    memset(o, 0, sizeof *o);
    if (!p_path(c, "recipe", path) || !zxvd_p_bool(c, "airgap", false, &airgap)) return false;
    uint8_t(*trust)[FAB_ID] = p_trust(c, &nt, &bad);
    if (bad) return false;
    bool ok = false;
    if (airgap && !G.seal) {
        zxvd_fail(c, ZXVD_E_FAB, "SEAL: no air-gap seal is registered in this zxvd");
        goto done;
    }
    if (!(o->rec = p_file(c, path, ZXVD_FAB_MAX_FILE, &len)) ||
        !fab_ok(c, fab_recipe_parse(o->rec, len, &o->r)) ||
        !fab_ok(c, fab_recipe_check(&o->r, (const uint8_t(*)[FAB_ID]) trust, nt)))
        goto done;
    if (o->r.kind != kind) {
        zxvd_fail(c, ZXVD_E_FAB, "FORM: a %s recipe; use %s",
                  o->r.kind == FAB_KIND_STREAM ? "stream" : "file",
                  o->r.kind == FAB_KIND_STREAM ? "fab.stream.open" : "fab.receive");
        goto done;
    }
    o->plan = malloc(o->r.plan_len);
    if (!o->plan) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!fab_ok(c, fab_open_plan(&o->r, G.dk, o->plan, o->r.plan_len, &o->p))) goto done;
    if (o->p.target_len > ZXVD_FAB_MAX_FILE) {
        fab_ok(c, FAB_ERR_SPACE);
        goto done;
    }
    o->io.get = store_get;
    o->io.work = &W;
    o->io.diff_cap = o->r.diff_len;
    o->io.diff_buf = malloc(o->r.diff_len ? o->r.diff_len : 1u);
    o->io.out_cap = (uint32_t) o->p.target_len;
    o->io.out = malloc(o->p.target_len ? (size_t) o->p.target_len : 1u);
    if (!o->io.diff_buf || !o->io.out) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!fab_ok(c, fab_hydrate(&o->p, &o->io, &o->got))) goto done;
    if (airgap) {
        o->io.seal = G.seal;
        o->io.seal_ctx = G.seal_ctx;
    }
    ok = fab_ok(c, fab_assemble(&o->r, &o->p, G.dk, &o->io, s, olen));
done:
    free(trust);
    return ok;
}

/* ---- methods ---------------------------------------------------------------------- */

/* B5 */
static void m_status(zxvd_call_t *c)
{
    zxvd_buf_t *b = zxvd_res(c);
    zxvd_buf_printf(b, "{\"open\":%s,\"sign_id\":", G.open ? "true" : "false");
    if (G.open)
        zxvd_buf_hex(b, G.sid, FAB_ID);
    else
        zxvd_buf_raw(b, "null", 4);
    zxvd_buf_raw(b, ",\"kem_id\":", 10);
    if (G.open)
        zxvd_buf_hex(b, G.kid, FAB_ID);
    else
        zxvd_buf_raw(b, "null", 4);
    zxvd_buf_printf(b, ",\"seal\":%s,\"streams\":[", G.seal ? "true" : "false");
    bool first = true;
    for (uint32_t i = 0; i < ZXVD_FAB_STREAMS; i++) {
        const stream_t *s = &G.s[i];
        if (!s->handle) continue;
        zxvd_buf_printf(b,
                        "%s{\"stream\":%u,\"role\":\"%s\",\"seq\":\"%llu\",\"checked\":\"%llu\"}",
                        first ? "" : ",", (unsigned) s->handle, s->send ? "send" : "receive",
                        (unsigned long long) s->st.seq, (unsigned long long) s->st.checked_seq);
        first = false;
    }
    zxvd_buf_raw(b, "]}", 2);
}

/* B5 */
static void m_keys(zxvd_call_t *c)
{
    if (!need_open(c)) return;
    zxvd_buf_t *b = zxvd_res(c);
    zxvd_buf_raw(b, "{\"sign_id\":", 11);
    zxvd_buf_hex(b, G.sid, FAB_ID);
    zxvd_buf_raw(b, ",\"sign_pk\":", 11);
    zxvd_buf_hex(b, G.spk, FAB_SIG_PK);
    zxvd_buf_raw(b, ",\"kem_id\":", 10);
    zxvd_buf_hex(b, G.kid, FAB_ID);
    zxvd_buf_raw(b, ",\"kem_ek\":", 10);
    zxvd_buf_hex(b, G.ek, FAB_KEM_EK);
    zxvd_buf_raw(b, "}", 1);
}

/* B6 */
static void m_put(zxvd_call_t *c)
{
    char path[ZXVD_FAB_PATH], dst[ZXVD_FAB_PATH + IPFSN_CID_STR_MAX + 16];
    uint32_t len = 0;
    if (!need_open(c) || !p_path(c, "path", path)) return;
    uint8_t *b = p_file(c, path, FAB_BLOCK_MAX, &len);
    if (!b) return;
    ipfsn_cid_t cid;
    if (!len)
        zxvd_fail(c, ZXVD_E_PARAMS, "a block cannot be empty");
    else if (ipfsn_cid_sha256(IPFSN_MC_RAW, b, len, &cid) != IPFSN_OK ||
             !block_path(&cid, dst, sizeof dst))
        zxvd_fail(c, ZXVD_E_INTERNAL, "cannot name the block");
    else if (p_write(c, dst, b, len)) {
        zxvd_buf_t *o = zxvd_res(c);
        zxvd_buf_raw(o, "{\"cid\":", 7);
        put_cid(o, &cid);
        zxvd_buf_printf(o, ",\"len\":%u}", (unsigned) len);
    }
    free(b);
}

/* "to": a recipient's ML-KEM-1024 key (B7, B9) */
static bool p_to(zxvd_call_t *c, bool required, uint8_t ek[FAB_KEM_EK], bool *have)
{
    uint32_t n = 0;
    if (!zxvd_p_hex(c, "to", ek, FAB_KEM_EK, &n, required)) return false;
    *have = n > 0;
    if (!*have || n == FAB_KEM_EK) return true;
    zxvd_fail(c, ZXVD_E_PARAMS, "\"to\" must be an ML-KEM-1024 key of %u bytes",
              (unsigned) FAB_KEM_EK);
    return false;
}

/* B7 */
static void m_send(zxvd_call_t *c)
{
    char tpath[ZXVD_FAB_PATH], opath[ZXVD_FAB_PATH];
    static uint8_t ek[FAB_KEM_EK];
    uint32_t window = 32, nb = 0, tlen = 0, rl = 0;
    bool sealed;
    if (!need_open(c) || !p_path(c, "target", tpath) || !p_path(c, "out", opath) ||
        !p_to(c, false, ek, &sealed) ||
        (zxvd_p_has(c, "window") && !zxvd_p_u32(c, "window", &window)))
        return;
    ipfsn_cid_t *cids = p_blocks(c, &nb);
    if (!cids) return;
    fab_src_t *src = calloc(nb ? nb : 1u, sizeof *src);
    uint8_t *t = NULL, *rec = NULL;
    built_t b = {0};
    if (!src) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!load_blocks(c, cids, nb, src) || !(t = p_file(c, tpath, ZXVD_FAB_MAX_FILE, &tlen)) ||
        !build(c, src, nb, t, tlen, window, 0, &b) ||
        !(rec = seal_recipe(c, FAB_KIND_FILE, sealed ? ek : NULL, &b, NULL, &rl)) ||
        !p_write(c, opath, rec, rl))
        goto done;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_raw(o, "{\"cid\":", 7);
    put_cid(o, &b.cid);
    zxvd_buf_printf(o,
                    ",\"len\":%u,\"mode\":\"%s\",\"ops\":%u,\"diff\":%u,\"used\":%u,\"decoys\":%u,"
                    "\"recipe\":%u}",
                    (unsigned) tlen, sealed ? "sealed" : "public", (unsigned) b.n_ops,
                    (unsigned) b.diff_len, (unsigned) b.used, (unsigned) (nb - b.used),
                    (unsigned) rl);
done:
    if (src) free_srcs(src, nb);
    free(cids);
    if (t) {
        memset(t, 0, tlen);
        free(t);
    }
    free(rec);
    built_free(&b);
}

/* B8 */
static void m_receive(zxvd_call_t *c)
{
    char opath[ZXVD_FAB_PATH];
    opened_t o;
    uint32_t olen = 0;
    if (!need_open(c) || !p_path(c, "out", opath)) return;
    if (receive(c, FAB_KIND_FILE, NULL, &o, &olen) && p_write(c, opath, o.io.out, olen)) {
        zxvd_buf_t *b = zxvd_res(c);
        zxvd_buf_raw(b, "{\"cid\":", 7);
        put_cid(b, &o.p.target);
        zxvd_buf_printf(b, ",\"len\":%u,\"mode\":\"%s\",\"sender\":", (unsigned) olen,
                        o.r.mode == FAB_MODE_SEALED ? "sealed" : "public");
        zxvd_buf_hex(b, o.r.sender_id, FAB_ID);
        zxvd_buf_printf(b, ",\"blocks\":%u}", (unsigned) o.got);
    }
    opened_free(&o);
}

/* B9 */
static void m_stream_start(zxvd_call_t *c)
{
    char tpath[ZXVD_FAB_PATH], opath[ZXVD_FAB_PATH];
    static uint8_t ek[FAB_KEM_EK];
    uint32_t window = 32, slot_us, nb = 0, tlen = 0, rl = 0;
    bool have;
    stream_t *s;
    if (!need_open(c) || !p_path(c, "target", tpath) || !p_path(c, "out", opath) ||
        !p_to(c, true, ek, &have) || !zxvd_p_u32(c, "slot_us", &slot_us) ||
        (zxvd_p_has(c, "window") && !zxvd_p_u32(c, "window", &window)))
        return;
    if (!slot_us) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"slot_us\" must be at least 1");
        return;
    }
    if (!(s = new_stream(c))) return;
    ipfsn_cid_t *cids = p_blocks(c, &nb);
    if (!cids) return;
    fab_src_t *src = calloc(nb ? nb : 1u, sizeof *src);
    uint8_t *t = NULL, *rec = NULL;
    built_t b = {0};
    if (!src) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!load_blocks(c, cids, nb, src) || !(t = p_file(c, tpath, ZXVD_FAB_MAX_FILE, &tlen)) ||
        !build(c, src, nb, t, tlen, window, slot_us, &b) ||
        !(rec = seal_recipe(c, FAB_KIND_STREAM, ek, &b, &s->st, &rl)) ||
        !p_write(c, opath, rec, rl)) {
        memset(&s->st, 0, sizeof s->st);
        goto done;
    }
    s->handle = ++G.next_handle;
    s->send = true;
    s->prev = t;
    s->prev_len = tlen;
    s->cids = cids;
    s->n_cids = nb;
    s->window = window;
    t = NULL;
    cids = NULL;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_printf(o, "{\"stream\":%u,\"cid\":", (unsigned) s->handle);
    put_cid(o, &b.cid);
    zxvd_buf_printf(o, ",\"len\":%u,\"recipe\":%u}", (unsigned) tlen, (unsigned) rl);
done:
    if (src) free_srcs(src, nb);
    free(cids);
    if (t) {
        memset(t, 0, tlen);
        free(t);
    }
    free(rec);
    built_free(&b);
}

/* B9 */
static void m_stream_send(zxvd_call_t *c)
{
    char tpath[ZXVD_FAB_PATH], opath[ZXVD_FAB_PATH];
    uint32_t tlen = 0, fl = 0;
    stream_t *s;
    if (!need_open(c) || !(s = p_stream(c, true)) || !p_path(c, "target", tpath) ||
        !p_path(c, "out", opath))
        return;
    fab_src_t *src = calloc(s->n_cids + 1u, sizeof *src);
    uint8_t *t = NULL, *fr = NULL;
    built_t b = {0};
    if (!src) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    if (!load_blocks(c, s->cids, s->n_cids, src)) goto done;
    src[s->n_cids].data = s->prev;
    src[s->n_cids].len = s->prev_len;
    src[s->n_cids].prev = true;
    if (!(t = p_file(c, tpath, ZXVD_FAB_MAX_FILE, &tlen)) ||
        !build(c, src, s->n_cids + 1u, t, tlen, s->window, 0, &b))
        goto done;
    uint32_t cap = FAB_FHDR + b.plan_len + b.diff_len + 2u * FAB_TAG;
    if (!(fr = malloc(cap))) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
        goto done;
    }
    uint64_t base = s->st.seq;
    if (!fab_ok(c, fab_frame_seal(&s->st, base, base + 1u, b.plan, b.plan_len, b.diff, b.diff_len,
                                  fr, cap, &fl)))
        goto done;
    if (!p_write(c, opath, fr, fl)) goto done; /* the frame is sealed: the next seq moves on */
    memset(s->prev, 0, s->prev_len);
    free(s->prev);
    s->prev = t;
    s->prev_len = tlen;
    t = NULL;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_printf(o, "{\"seq\":\"%llu\",\"cid\":", (unsigned long long) s->st.seq);
    put_cid(o, &b.cid);
    zxvd_buf_printf(o, ",\"len\":%u,\"frame\":%u,\"diff\":%u}", (unsigned) tlen, (unsigned) fl,
                    (unsigned) b.diff_len);
done:
    if (src) free_srcs(src, s->n_cids + 1u);
    if (t) {
        memset(t, 0, tlen);
        free(t);
    }
    free(fr);
    built_free(&b);
}

static bool p_seq(zxvd_call_t *c, const char *key, uint64_t *v)
{
    int64_t x;
    if (!zxvd_p_i64(c, key, &x)) return false;
    if (x < 0) {
        zxvd_fail(c, ZXVD_E_PARAMS, "\"%s\" must not be negative", key);
        return false;
    }
    *v = (uint64_t) x;
    return true;
}

/* B9 */
static void m_stream_sign(zxvd_call_t *c)
{
    stream_t *s;
    uint64_t from, to;
    if (!need_open(c) || !(s = p_stream(c, true)) || !p_seq(c, "from", &from) ||
        !p_seq(c, "to", &to))
        return;
    static uint8_t cp[FAB_CP_LEN];
    uint8_t rnd[32];
    if (RANDOM(rnd, sizeof rnd) != 0) {
        zxvd_fail(c, ZXVD_E_INTERNAL, "no random source");
        return;
    }
    if (!fab_ok(c, fab_checkpoint_sign(&s->st, from, to, G.ssk, rnd, cp))) return;
    zxvd_buf_t *o = zxvd_res(c);
    zxvd_buf_raw(o, "{\"checkpoint\":", 14);
    zxvd_buf_hex(o, cp, FAB_CP_LEN);
    zxvd_buf_raw(o, "}", 1);
}

/* B10 */
static void m_stream_open(zxvd_call_t *c)
{
    char opath[ZXVD_FAB_PATH];
    opened_t o;
    uint32_t olen = 0;
    stream_t *s;
    if (!need_open(c) || !p_path(c, "out", opath) || !(s = new_stream(c))) return;
    if (receive(c, FAB_KIND_STREAM, &s->st, &o, &olen) && p_write(c, opath, o.io.out, olen)) {
        s->handle = ++G.next_handle;
        s->send = false;
        s->prev = o.io.out;
        s->prev_len = olen;
        o.io.out = NULL;
        zxvd_buf_t *b = zxvd_res(c);
        zxvd_buf_printf(b, "{\"stream\":%u,\"cid\":", (unsigned) s->handle);
        put_cid(b, &o.p.target);
        zxvd_buf_printf(b, ",\"len\":%u,\"slot_us\":%u}", (unsigned) olen, (unsigned) o.p.slot_us);
    } else {
        memset(&s->st, 0, sizeof s->st);
    }
    opened_free(&o);
}

/* B10: the output buffer starts at the last frame's size and doubles on
 * SPACE (a refused frame changes nothing in the stream) */
static void m_stream_frame(zxvd_call_t *c)
{
    char fpath[ZXVD_FAB_PATH], opath[ZXVD_FAB_PATH];
    uint32_t fl = 0, olen = 0;
    stream_t *s;
    if (!need_open(c) || !(s = p_stream(c, false)) || !p_path(c, "frame", fpath) ||
        !p_path(c, "out", opath))
        return;
    uint8_t *fr = p_file(c, fpath, ZXVD_FAB_MAX_FILE, &fl), *plan = NULL;
    if (!fr) return;
    /* a frame's plan and its diff are each shorter than the frame */
    fab_io_t io = {.get = store_get, .work = &W, .diff_cap = fl};
    plan = malloc(fl ? fl : 1u);
    io.diff_buf = malloc(fl ? fl : 1u);
    uint32_t cap = s->prev_len < 4096u ? 4096u : s->prev_len;
    int rc = FAB_ERR_SPACE;
    while (plan && io.diff_buf && rc == FAB_ERR_SPACE) {
        free(io.out);
        io.out = malloc(cap);
        io.out_cap = cap;
        if (!io.out) break;
        rc = fab_frame_apply(&s->st, fr, fl, s->prev, s->prev_len, plan, fl, &io, &olen);
        if (rc != FAB_ERR_SPACE || cap >= ZXVD_FAB_MAX_FILE) break;
        cap = cap > ZXVD_FAB_MAX_FILE / 2u ? ZXVD_FAB_MAX_FILE : cap * 2u;
    }
    if (!plan || !io.diff_buf || !io.out)
        zxvd_fail(c, ZXVD_E_INTERNAL, "out of memory");
    else if (fab_ok(c, rc) && p_write(c, opath, io.out, olen)) {
        memset(s->prev, 0, s->prev_len);
        free(s->prev);
        s->prev = io.out;
        s->prev_len = olen;
        io.out = NULL;
        zxvd_buf_t *b = zxvd_res(c);
        zxvd_buf_printf(b, "{\"seq\":\"%llu\",\"cid\":", (unsigned long long) s->st.seq);
        uint32_t k = (uint32_t) (s->st.seq % FAB_CHAIN_RING);
        ipfsn_cid_t cid;
        if (ipfsn_cid_decode_exact(s->st.ring_cid[k], FAB_CID, &cid) == IPFSN_OK)
            put_cid(b, &cid);
        else
            zxvd_buf_raw(b, "null", 4);
        zxvd_buf_printf(b, ",\"len\":%u}", (unsigned) olen);
    }
    if (io.out) {
        memset(io.out, 0, io.out_cap);
        free(io.out);
    }
    if (io.diff_buf) {
        memset(io.diff_buf, 0, fl);
        free(io.diff_buf);
    }
    if (plan) {
        memset(plan, 0, fl);
        free(plan);
    }
    free(fr);
}

static uint64_t get64(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 0; i < 8; i++) v = v << 8 | p[i];
    return v;
}

/* B10 */
static void m_stream_checkpoint(zxvd_call_t *c)
{
    static uint8_t cp[FAB_CP_LEN];
    uint32_t n = 0;
    stream_t *s;
    if (!need_open(c) || !(s = p_stream(c, false)) ||
        !zxvd_p_hex(c, "checkpoint", cp, FAB_CP_LEN, &n, true))
        return;
    if (!fab_ok(c, fab_checkpoint_verify(&s->st, cp, n))) return;
    zxvd_buf_printf(zxvd_res(c), "{\"from\":\"%llu\",\"to\":\"%llu\"}",
                    (unsigned long long) get64(cp + 24), (unsigned long long) get64(cp + 32));
}

/* B11 */
static void m_stream_close(zxvd_call_t *c)
{
    uint32_t h;
    if (!need_open(c) || !zxvd_p_u32(c, "stream", &h)) return;
    for (uint32_t i = 0; i < ZXVD_FAB_STREAMS; i++)
        if (h && G.s[i].handle == h) {
            stream_free(&G.s[i]);
            zxvd_buf_printf(zxvd_res(c), "{\"ok\":true}");
            return;
        }
    zxvd_fail(c, ZXVD_E_FAB, "STREAM: no stream %u", (unsigned) h);
}

static const zxvd_group_method_t METHODS[] = {
    {"fab.status", m_status, true},
    {"fab.keys", m_keys, true},
    {"fab.put", m_put, false},
    {"fab.send", m_send, false},
    {"fab.receive", m_receive, false},
    {"fab.stream.start", m_stream_start, false},
    {"fab.stream.send", m_stream_send, false},
    {"fab.stream.sign", m_stream_sign, false},
    {"fab.stream.open", m_stream_open, false},
    {"fab.stream.frame", m_stream_frame, false},
    {"fab.stream.checkpoint", m_stream_checkpoint, false},
    {"fab.stream.close", m_stream_close, false},
};

static const zxvd_group_t GROUP = {"fab",
                                   false,
                                   "file fabrication: send, receive and stream files built from "
                                   "public blocks plus a sealed diff",
                                   METHODS,
                                   sizeof METHODS / sizeof METHODS[0],
                                   zxvd_fab_close};

const zxvd_group_t *zxvd_fab_group(void)
{
    return &GROUP;
}
