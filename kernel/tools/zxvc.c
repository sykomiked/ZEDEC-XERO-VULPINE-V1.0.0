/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvc.c — the model converter: GGUF and Hugging Face safetensors files to a
 * .zxvc package of exact integer weights (roadmap 6.6c, M.1 and M.4), with a
 * bit-exact check against the source.
 *
 *   zxvc convert OUT.zxvc SRC... [--license ID] [--license-override TEXT]
 *                [--card FILE] [--tokenizer FILE] [--config FILE]
 *                [--sign SK --pub PK]
 *   zxvc check   PKG.zxvc SRC...   decode every tensor and compare it with
 *                                  the source bytes, byte for byte
 *   zxvc verify  PKG.zxvc [--pub PK]  re-hash every part; check the signature
 *   zxvc export  PKG.zxvc OUT.gguf    rebuild the source GGUF file exactly
 *   zxvc info    PKG.zxvc
 *   zxvc keygen  OUT.pk OUT.sk        an ML-DSA-87 key pair for signing
 *
 * SRC is one GGUF file, or one or more safetensors files (the shards of one
 * model). F32, F16 and BF16 tensors become exact integer blocks (zt_zxvc.h);
 * GGUF quantised tensors are kept byte for byte. A conversion that cannot be
 * exact stops; nothing is rounded. Safetensors shapes are stored in GGUF
 * order (row length first).
 *
 * LICENCE CHECK. The licence comes from the GGUF's general.license, the
 * safetensors __metadata__ "license", or --license. Only licences on the
 * allow-list (commercial use allowed without extra terms) convert; anything
 * else needs --license-override with the owner's reason, which is recorded
 * in the package and flagged. This is a gate on what the files declare, not
 * legal advice: a model card can carry terms its metadata does not.
 *
 * Host tool (POSIX: mmap, /dev/urandom). The format code is the kernel's own
 * freestanding zt_zxvc.c, so the engine reads exactly what this writes.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "zt_gguf.h"
#include "zt_zxvc.h"
#include "../robin_debanks/sha256.h"
#include "../pqsec/pq_matrix_algs.h"

#define MAX_SRC     64
#define SIG_BYTES   (ZXVC_HASH + PQM_MLDSA87_SIG_BYTES)
#define MAX_TENSORS 65536

static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "zxvc: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

typedef struct {
    const char *path;
    uint8_t *buf;
    uint64_t size;
} mapped_t;

static mapped_t map_file(const char *path)
{
    mapped_t m = {path, NULL, 0};
    int fd = open(path, O_RDONLY);
    if (fd < 0) die("%s: %s", path, strerror(errno));
    struct stat st;
    if (fstat(fd, &st) != 0) die("%s: %s", path, strerror(errno));
    m.size = (uint64_t) st.st_size;
    if (m.size) {
        void *p = mmap(NULL, (size_t) m.size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (p == MAP_FAILED) die("%s: mmap: %s", path, strerror(errno));
        m.buf = p;
    }
    close(fd);
    return m;
}

static uint8_t *read_small(const char *path, uint64_t *len)
{
    mapped_t m = map_file(path);
    uint8_t *b = malloc(m.size ? m.size : 1);
    if (!b) die("out of memory");
    memcpy(b, m.buf, m.size);
    if (m.buf) munmap(m.buf, m.size);
    *len = m.size;
    return b;
}

/* Copy a string, truncating to cap - 1 bytes. */
static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n >= cap) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static void hex(const uint8_t *h, char *out)
{
    for (int i = 0; i < 32; i++) sprintf(out + 2 * i, "%02x", h[i]);
}

/* ---- a small JSON reader for the safetensors header ---- */

typedef struct {
    const char *p, *end;
} js_t;

static void js_ws(js_t *j)
{
    while (j->p < j->end && (*j->p == ' ' || *j->p == '\n' || *j->p == '\r' || *j->p == '\t'))
        j->p++;
}

static bool js_lit(js_t *j, char c)
{
    js_ws(j);
    if (j->p < j->end && *j->p == c) {
        j->p++;
        return true;
    }
    return false;
}

/* A string into out (cap bytes incl. NUL). Only ASCII \u escapes. */
static bool js_str(js_t *j, char *out, size_t cap)
{
    if (!js_lit(j, '"')) return false;
    size_t n = 0;
    while (j->p < j->end && *j->p != '"') {
        char c = *j->p++;
        if (c == '\\') {
            if (j->p >= j->end) return false;
            char e = *j->p++;
            switch (e) {
            case '"':
            case '\\':
            case '/':
                c = e;
                break;
            case 'n':
                c = '\n';
                break;
            case 't':
                c = '\t';
                break;
            case 'r':
                c = '\r';
                break;
            case 'b':
                c = '\b';
                break;
            case 'f':
                c = '\f';
                break;
            case 'u': {
                if (j->end - j->p < 4) return false;
                unsigned v = 0;
                for (int i = 0; i < 4; i++) {
                    char h = *j->p++;
                    v = v * 16u + (unsigned) (h >= '0' && h <= '9'   ? h - '0'
                                              : h >= 'a' && h <= 'f' ? h - 'a' + 10
                                              : h >= 'A' && h <= 'F' ? h - 'A' + 10
                                                                     : 99);
                }
                if (v > 0x7F) return false;
                c = (char) v;
                break;
            }
            default:
                return false;
            }
        }
        if (out) {
            if (n + 1 >= cap) return false;
            out[n] = c;
        }
        n++;
    }
    if (out) out[n] = 0;
    return js_lit(j, '"');
}

static bool js_u64(js_t *j, uint64_t *v)
{
    js_ws(j);
    uint64_t x = 0;
    const char *s = j->p;
    while (j->p < j->end && *j->p >= '0' && *j->p <= '9') {
        if (x > (UINT64_MAX - 9) / 10) return false;
        x = x * 10 + (uint64_t) (*j->p++ - '0');
    }
    *v = x;
    return j->p > s;
}

static bool js_skip(js_t *j)
{
    js_ws(j);
    if (j->p >= j->end) return false;
    char c = *j->p;
    if (c == '"') return js_str(j, NULL, 0);
    if (c == '{' || c == '[') {
        char close = c == '{' ? '}' : ']';
        j->p++;
        if (js_lit(j, close)) return true;
        do {
            if (c == '{' && (!js_str(j, NULL, 0) || !js_lit(j, ':'))) return false;
            if (!js_skip(j)) return false;
        } while (js_lit(j, ','));
        return js_lit(j, close);
    }
    while (j->p < j->end && strchr("-+.eE0123456789truefalsn", *j->p)) j->p++;
    return true;
}

/* ---- the source tensors ---- */

typedef struct {
    char name[ZXVC_TENSOR_NAME + 1];
    uint32_t type, n_dims, file;
    uint64_t dims[ZXVC_MAX_DIMS], n_elems;
    uint64_t off, len; /* in the source file */
} stensor_t;

typedef struct {
    mapped_t f[MAX_SRC];
    uint32_t n_files;
    bool gguf;
    zt_gguf_t g;
    stensor_t *t;
    uint32_t n;
    char license[128], name[128];
} source_t;

static uint32_t st_dtype(const char *s, bool *ok)
{
    *ok = true;
    if (!strcmp(s, "F32")) return ZT_GGML_F32;
    if (!strcmp(s, "F16")) return ZT_GGML_F16;
    if (!strcmp(s, "BF16")) return ZT_GGML_BF16;
    *ok = false;
    return 0;
}

static void load_safetensors(source_t *s, uint32_t fi)
{
    mapped_t *m = &s->f[fi];
    if (m->size < 10) die("%s: too short for safetensors", m->path);
    uint64_t hl = 0;
    for (int i = 0; i < 8; i++) hl |= (uint64_t) m->buf[i] << (8 * i);
    if (hl > m->size - 8 || hl > (100u << 20)) die("%s: bad safetensors header length", m->path);
    uint64_t base = 8 + hl;
    js_t j = {(const char *) m->buf + 8, (const char *) m->buf + 8 + hl};
    if (!js_lit(&j, '{')) die("%s: header is not a JSON object", m->path);
    if (js_lit(&j, '}')) return;
    do {
        char key[512];
        if (!js_str(&j, key, sizeof key) || !js_lit(&j, ':')) die("%s: bad header key", m->path);
        if (!strcmp(key, "__metadata__")) {
            if (!js_lit(&j, '{')) die("%s: bad __metadata__", m->path);
            if (!js_lit(&j, '}')) {
                do {
                    char k[128], v[1024];
                    if (!js_str(&j, k, sizeof k) || !js_lit(&j, ':')) die("%s: metadata", m->path);
                    js_t save = j;
                    if (js_str(&j, v, sizeof v)) {
                        if (!strcasecmp(k, "license") && !s->license[0])
                            copy_str(s->license, sizeof s->license, v);
                    } else {
                        j = save;
                        if (!js_skip(&j)) die("%s: metadata value", m->path);
                    }
                } while (js_lit(&j, ','));
                if (!js_lit(&j, '}')) die("%s: metadata end", m->path);
            }
            continue;
        }
        if (strlen(key) > ZXVC_TENSOR_NAME) die("%s: tensor name too long: %s", m->path, key);
        if (s->n >= MAX_TENSORS) die("too many tensors");
        stensor_t *t = &s->t[s->n];
        memset(t, 0, sizeof *t);
        copy_str(t->name, sizeof t->name, key);
        t->file = fi;
        char dtype[32] = "";
        uint64_t shape[8], nshape = 0, off[2], noff = 0;
        if (!js_lit(&j, '{')) die("%s: %s: expected an object", m->path, key);
        do {
            char f[64];
            if (!js_str(&j, f, sizeof f) || !js_lit(&j, ':')) die("%s: %s: field", m->path, key);
            if (!strcmp(f, "dtype")) {
                if (!js_str(&j, dtype, sizeof dtype)) die("%s: %s: dtype", m->path, key);
            } else if (!strcmp(f, "shape") || !strcmp(f, "data_offsets")) {
                bool sh = f[0] == 's';
                if (!js_lit(&j, '[')) die("%s: %s: %s", m->path, key, f);
                if (!js_lit(&j, ']')) {
                    do {
                        uint64_t v;
                        if (!js_u64(&j, &v)) die("%s: %s: %s value", m->path, key, f);
                        if (sh) {
                            if (nshape >= 8) die("%s: %s: too many dims", m->path, key);
                            shape[nshape++] = v;
                        } else {
                            if (noff >= 2) die("%s: %s: data_offsets", m->path, key);
                            off[noff++] = v;
                        }
                    } while (js_lit(&j, ','));
                    if (!js_lit(&j, ']')) die("%s: %s: %s end", m->path, key, f);
                }
            } else if (!js_skip(&j)) {
                die("%s: %s: bad value", m->path, key);
            }
        } while (js_lit(&j, ','));
        if (!js_lit(&j, '}')) die("%s: %s: object end", m->path, key);
        bool ok;
        t->type = st_dtype(dtype, &ok);
        if (!ok)
            die("%s: %s: dtype %s is not supported (F32, F16 and BF16 convert exactly)", m->path,
                key, dtype);
        if (noff != 2 || off[1] < off[0]) die("%s: %s: data_offsets", m->path, key);
        if (nshape > ZXVC_MAX_DIMS) die("%s: %s: more than 4 dims", m->path, key);
        t->n_dims = nshape ? (uint32_t) nshape : 1u;
        t->n_elems = 1;
        for (uint32_t d = 0; d < t->n_dims; d++) {
            /* safetensors is row-major, outermost first; GGUF order is row length first */
            t->dims[d] = nshape ? shape[nshape - 1 - d] : 1u;
            if (!t->dims[d])
                die("%s: %s: zero-sized dim (empty tensors are not supported)", m->path, key);
            t->n_elems *= t->dims[d];
        }
        t->off = base + off[0];
        t->len = off[1] - off[0];
        if (t->len != t->n_elems * zxvc_src_size(t->type))
            die("%s: %s: size disagrees with shape", m->path, key);
        if (t->off > m->size || t->len > m->size - t->off)
            die("%s: %s: past the end", m->path, key);
        s->n++;
    } while (js_lit(&j, ','));
    if (!js_lit(&j, '}')) die("%s: header end", m->path);
}

static void load_gguf(source_t *s)
{
    mapped_t *m = &s->f[0];
    int32_t rc = zt_gguf_open(&s->g, m->buf, m->size);
    if (rc) die("%s: not a readable GGUF file (%d)", m->path, rc);
    zt_gguf_val_t v;
    if (zt_gguf_find(&s->g, "general.license", &v) == 0 && v.type == ZT_GGUF_STRING)
        snprintf(s->license, sizeof s->license, "%.*s", (int) v.str.len, (const char *) v.str.p);
    if (zt_gguf_find(&s->g, "general.name", &v) == 0 && v.type == ZT_GGUF_STRING)
        snprintf(s->name, sizeof s->name, "%.*s", (int) v.str.len, (const char *) v.str.p);
    if (s->g.n_tensors > MAX_TENSORS) die("too many tensors");
    for (uint64_t i = 0; i < s->g.n_tensors; i++) {
        zt_gguf_tensor_t gt;
        if (zt_gguf_tensor(&s->g, i, &gt))
            die("%s: tensor %llu unreadable", m->path, (unsigned long long) i);
        stensor_t *t = &s->t[s->n++];
        memset(t, 0, sizeof *t);
        if (gt.name.len > ZXVC_TENSOR_NAME) die("%s: tensor name too long", m->path);
        memcpy(t->name, gt.name.p, gt.name.len);
        t->type = gt.type;
        t->n_dims = gt.n_dims;
        for (uint32_t d = 0; d < gt.n_dims; d++) t->dims[d] = gt.dims[d];
        t->n_elems = gt.n_elems;
        t->off = (uint64_t) (gt.data - m->buf);
        t->len = gt.n_bytes;
    }
}

static void load_sources(source_t *s, char **paths, int n)
{
    memset(s, 0, sizeof *s);
    if (n < 1 || n > MAX_SRC) die("give 1 to %d source files", MAX_SRC);
    s->t = calloc(MAX_TENSORS, sizeof *s->t);
    if (!s->t) die("out of memory");
    for (int i = 0; i < n; i++) s->f[i] = map_file(paths[i]);
    s->n_files = (uint32_t) n;
    s->gguf = s->f[0].size >= 4 && !memcmp(s->f[0].buf, "GGUF", 4);
    if (s->gguf) {
        if (n != 1) die("a GGUF model is one file");
        load_gguf(s);
    } else {
        for (int i = 0; i < n; i++) load_safetensors(s, (uint32_t) i);
    }
}

/* ---- licence ---- */

static const char *const allow[] = {
    "apache-2.0", "mit",       "bsd-2-clause", "bsd-3-clause", "isc", "cc0-1.0",
    "cc-by-4.0",  "unlicense", "zlib",         "mpl-2.0",      NULL};

static bool licence_ok(const char *id)
{
    for (int i = 0; allow[i]; i++)
        if (!strcasecmp(id, allow[i])) return true;
    return false;
}

/* ---- writing ---- */

typedef struct {
    FILE *f;
    uint64_t pos;
} out_t;

static void out_write(out_t *o, const void *p, uint64_t n)
{
    if (n && fwrite(p, 1, (size_t) n, o->f) != n) die("write failed: %s", strerror(errno));
    o->pos += n;
}

static void out_align(out_t *o)
{
    static const uint8_t z[ZXVC_ALIGN];
    uint64_t pad = (ZXVC_ALIGN - (o->pos % ZXVC_ALIGN)) % ZXVC_ALIGN;
    out_write(o, z, pad);
}

static void add_part(out_t *o, zxvc_part_t *p, const char *name, const uint8_t *data, uint64_t len)
{
    out_align(o);
    memset(p, 0, sizeof *p);
    copy_str(p->name, sizeof p->name, name);
    p->off = o->pos;
    p->len = len;
    sha256(data, (size_t) len, p->sha);
    out_write(o, data, len);
}

static int cmd_convert(int argc, char **argv)
{
    const char *outp = NULL, *lic = NULL, *override = NULL, *card = NULL, *tok = NULL, *cfg = NULL,
               *skp = NULL, *pkp = NULL;
    char *srcs[MAX_SRC];
    int ns = 0;
    for (int i = 0; i < argc; i++) {
        const char *a = argv[i];
        const char **slot = !strcmp(a, "--license")            ? &lic
                            : !strcmp(a, "--license-override") ? &override
                            : !strcmp(a, "--card")             ? &card
                            : !strcmp(a, "--tokenizer")        ? &tok
                            : !strcmp(a, "--config")           ? &cfg
                            : !strcmp(a, "--sign")             ? &skp
                            : !strcmp(a, "--pub")              ? &pkp
                                                               : NULL;
        if (slot) {
            if (++i >= argc) die("%s needs a value", a);
            *slot = argv[i];
        } else if (a[0] == '-') {
            die("unknown option %s", a);
        } else if (!outp) {
            outp = a;
        } else {
            if (ns >= MAX_SRC) die("too many sources");
            srcs[ns++] = argv[i];
        }
    }
    if (!outp || !ns) die("usage: zxvc convert OUT.zxvc SRC... [options]");
    if (!skp != !pkp) die("--sign and --pub go together");

    source_t s;
    load_sources(&s, srcs, ns);
    char licid[128];
    copy_str(licid, sizeof licid, lic ? lic : s.license);
    if (!licid[0] && !override)
        die("no licence in the source metadata: pass --license ID (allow-list: apache-2.0, mit, "
            "bsd-2-clause, bsd-3-clause, isc, cc0-1.0, cc-by-4.0, unlicense, zlib, mpl-2.0)");
    if (lic && s.license[0] && strcasecmp(lic, s.license))
        die("--license %s disagrees with the source metadata (%s)", lic, s.license);
    bool lic_ok = licid[0] && licence_ok(licid);
    if (!lic_ok && !override)
        die("licence \"%s\" is not on the commercial-use allow-list; the owner may convert it "
            "anyway with --license-override \"reason\" (recorded in the package)",
            licid[0] ? licid : "(none)");

    uint8_t *sk = NULL, *pk = NULL;
    if (skp) {
        uint64_t skl, pkl;
        sk = read_small(skp, &skl);
        pk = read_small(pkp, &pkl);
        if (skl != PQM_MLDSA87_SK_BYTES || pkl != PQM_MLDSA87_PK_BYTES)
            die("keys must be raw ML-DSA-87 (sk %u bytes, pk %u bytes)", PQM_MLDSA87_SK_BYTES,
                PQM_MLDSA87_PK_BYTES);
    }

    FILE *f = fopen(outp, "wb");
    if (!f) die("%s: %s", outp, strerror(errno));
    out_t o = {f, 0};

    zxvc_part_t parts[8];
    uint32_t np = 0;
    zxvc_t z = {0};
    z.n_tensors = s.n;
    z.flags = (s.gguf ? ZXVC_FLAG_FROM_GGUF : ZXVC_FLAG_FROM_SAFETENSORS) |
              (lic_ok ? 0u : ZXVC_FLAG_LICENCE_OVERRIDE);
    /* Parts are counted first so the tables have a fixed place. */
    uint32_t want = 2u + (s.gguf ? 1u : 0u) + !!card + !!tok + !!cfg;
    z.n_parts = want;
    z.parts_off = ZXVC_HEADER_BYTES;
    z.tensors_off = z.parts_off + (uint64_t) want * ZXVC_PART_BYTES;
    z.data_off = (z.tensors_off + (uint64_t) s.n * ZXVC_TENSOR_BYTES + ZXVC_ALIGN - 1) &
                 ~(uint64_t) (ZXVC_ALIGN - 1);
    uint8_t *tables = calloc(1, (size_t) z.data_off);
    if (!tables) die("out of memory");
    out_write(&o, tables, z.data_off);

    /* source: one line per file */
    char *srcdesc = malloc(512u * s.n_files + 64);
    size_t sl = 0;
    for (uint32_t i = 0; i < s.n_files; i++) {
        uint8_t h[32];
        char hx[65];
        sha256(s.f[i].buf, (size_t) s.f[i].size, h);
        hex(h, hx);
        const char *base = strrchr(s.f[i].path, '/');
        sl += (size_t) snprintf(srcdesc + sl, 512, "%s %llu %s %s\n", hx,
                                (unsigned long long) s.f[i].size, s.gguf ? "gguf" : "safetensors",
                                base ? base + 1 : s.f[i].path);
    }
    add_part(&o, &parts[np++], "source", (const uint8_t *) srcdesc, sl);
    char licdesc[1024];
    int ll = snprintf(licdesc, sizeof licdesc, "licence: %s\nfrom: %s\nallow-list: %s\n%s%s%s",
                      licid[0] ? licid : "(none)",
                      lic ? "--license" : (s.license[0] ? "source metadata" : "none"),
                      lic_ok ? "yes" : "no", override ? "owner override: " : "",
                      override ? override : "", override ? "\n" : "");
    add_part(&o, &parts[np++], "licence", (const uint8_t *) licdesc, (uint64_t) ll);
    if (s.gguf) add_part(&o, &parts[np++], "gguf-prefix", s.f[0].buf, s.g.data_off);
    const char *extra_path[3] = {card, tok, cfg};
    const char *extra_name[3] = {"card", "tokenizer", "config"};
    for (int i = 0; i < 3; i++) {
        if (!extra_path[i]) continue;
        uint64_t len;
        uint8_t *d = read_small(extra_path[i], &len);
        add_part(&o, &parts[np++], extra_name[i], d, len);
        free(d);
    }

    uint64_t n_exact = 0, n_native = 0, src_bytes = 0, out_bytes = 0;
    uint64_t width_hist[5] = {0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < s.n; i++) {
        stensor_t *st = &s.t[i];
        const uint8_t *src = s.f[st->file].buf + st->off;
        zxvc_tensor_t t;
        memset(&t, 0, sizeof t);
        copy_str(t.name, sizeof t.name, st->name);
        t.src_type = st->type;
        t.n_dims = st->n_dims;
        t.src_file = st->file;
        memcpy(t.dims, st->dims, sizeof t.dims);
        t.n_elems = st->n_elems;
        t.src_off = st->off;
        t.src_len = st->len;
        sha256(src, (size_t) st->len, t.src_sha);
        out_align(&o);
        t.off = o.pos;
        uint32_t sz = zxvc_src_size(st->type);
        if (sz) {
            t.enc = ZXVC_ENC_EXACT;
            sha256_ctx_t c;
            sha256_init(&c);
            uint8_t blk[ZXVC_BLK_MAX_BYTES * 64];
            size_t bl = 0;
            for (uint64_t e = 0; e < st->n_elems; e += ZXVC_BLK) {
                uint32_t n = st->n_elems - e < ZXVC_BLK ? (uint32_t) (st->n_elems - e) : ZXVC_BLK;
                int32_t k = zxvc_block_encode(st->type, src + e * sz, n, blk + bl);
                if (k < 0)
                    die("%s: block at element %llu: %s", st->name, (unsigned long long) e,
                        zxvc_err_name(k));
                width_hist[blk[bl] & 7]++;
                bl += (size_t) k;
                if (bl > sizeof blk - ZXVC_BLK_MAX_BYTES) {
                    sha256_update(&c, blk, bl);
                    out_write(&o, blk, bl);
                    bl = 0;
                }
            }
            sha256_update(&c, blk, bl);
            out_write(&o, blk, bl);
            sha256_final(&c, t.sha);
            n_exact++;
        } else {
            t.enc = ZXVC_ENC_NATIVE;
            memcpy(t.sha, t.src_sha, ZXVC_HASH);
            out_write(&o, src, st->len);
            n_native++;
        }
        t.len = o.pos - t.off;
        src_bytes += t.src_len;
        out_bytes += t.len;
        zxvc_put_tensor(tables + z.tensors_off + (uint64_t) i * ZXVC_TENSOR_BYTES, &t);
    }
    for (uint32_t i = 0; i < np; i++)
        zxvc_put_part(tables + z.parts_off + (uint64_t) i * ZXVC_PART_BYTES, &parts[i]);

    z.sig_off = o.pos;
    z.sig_len = sk ? SIG_BYTES : 0;
    z.file_size = o.pos + z.sig_len;
    zxvc_put_header(tables, &z);
    zxvc_t dz = z;
    dz.buf = tables;
    uint8_t dig[32];
    zxvc_digest(&dz, dig);
    if (sk) {
        uint8_t sig[SIG_BYTES], rnd[32];
        sha256(pk, PQM_MLDSA87_PK_BYTES, sig);
        FILE *r = fopen("/dev/urandom", "rb");
        if (!r || fread(rnd, 1, 32, r) != 32) die("no randomness for signing");
        fclose(r);
        pqm_mldsa87_sign(sk, dig, 32, (const uint8_t *) ZXVC_SIG_CTX, strlen(ZXVC_SIG_CTX), rnd,
                         sig + ZXVC_HASH);
        out_write(&o, sig, SIG_BYTES);
        memset(sk, 0, PQM_MLDSA87_SK_BYTES);
    }
    if (fseek(f, 0, SEEK_SET) != 0) die("seek failed");
    if (fwrite(tables, 1, (size_t) z.data_off, f) != z.data_off) die("write failed");
    if (fclose(f) != 0) die("%s: %s", outp, strerror(errno));

    char dx[65];
    hex(dig, dx);
    printf("converted %u tensors (%llu exact, %llu native) from %s\n", s.n,
           (unsigned long long) n_exact, (unsigned long long) n_native,
           s.gguf ? "GGUF" : "safetensors");
    printf("blocks by integer width: 8-bit %llu, 16-bit %llu, 24-bit %llu, 32-bit %llu, "
           "64-bit %llu\n",
           (unsigned long long) width_hist[0], (unsigned long long) width_hist[1],
           (unsigned long long) width_hist[2], (unsigned long long) width_hist[3],
           (unsigned long long) width_hist[4]);
    printf("tensor bytes: source %llu, package %llu (%.3fx)\n", (unsigned long long) src_bytes,
           (unsigned long long) out_bytes,
           src_bytes ? (double) out_bytes / (double) src_bytes : 1.0);
    printf("licence: %s%s\n", licid[0] ? licid : "(none)", lic_ok ? "" : " (owner override)");
    printf("manifest digest: %s%s\n", dx, sk ? " (signed, ML-DSA-87)" : " (unsigned)");
    return 0;
}

static void open_pkg(const char *path, mapped_t *m, zxvc_t *z)
{
    *m = map_file(path);
    int32_t rc = zxvc_open(z, m->buf, m->size);
    if (rc) die("%s: %s", path, zxvc_err_name(rc));
}

static int cmd_check(int argc, char **argv)
{
    if (argc < 2) die("usage: zxvc check PKG.zxvc SRC...");
    mapped_t m;
    zxvc_t z;
    open_pkg(argv[0], &m, &z);
    source_t s;
    memset(&s, 0, sizeof s);
    if (argc - 1 > MAX_SRC) die("too many sources");
    for (int i = 1; i < argc; i++) s.f[i - 1] = map_file(argv[i]);
    s.n_files = (uint32_t) argc - 1;
    uint64_t bytes = 0, maxlen = 0, n_exact = 0, n_native = 0;
    for (uint32_t i = 0; i < z.n_tensors; i++) {
        zxvc_tensor_t t;
        if (zxvc_tensor(&z, i, &t)) die("tensor %u unreadable", i);
        if (t.src_len > maxlen) maxlen = t.src_len;
    }
    uint8_t *buf = malloc(maxlen ? maxlen : 1);
    if (!buf) die("out of memory");
    uint32_t bad = 0;
    for (uint32_t i = 0; i < z.n_tensors; i++) {
        zxvc_tensor_t t;
        zxvc_tensor(&z, i, &t);
        if (t.src_file >= s.n_files) die("%s: needs source file %u", t.name, t.src_file + 1);
        mapped_t *f = &s.f[t.src_file];
        if (t.src_off > f->size || t.src_len > f->size - t.src_off) {
            printf("MISMATCH %s: outside %s\n", t.name, f->path);
            bad++;
            continue;
        }
        int32_t rc = zxvc_tensor_source(&t, buf, maxlen);
        if (rc) {
            printf("MISMATCH %s: %s\n", t.name, zxvc_err_name(rc));
            bad++;
            continue;
        }
        if (memcmp(buf, f->buf + t.src_off, (size_t) t.src_len)) {
            uint64_t k = 0;
            while (buf[k] == f->buf[t.src_off + k]) k++;
            printf("MISMATCH %s: first differing byte %llu\n", t.name, (unsigned long long) k);
            bad++;
            continue;
        }
        bytes += t.src_len;
        if (t.enc == ZXVC_ENC_EXACT)
            n_exact++;
        else
            n_native++;
    }
    if (bad) {
        printf("FAILED: %u of %u tensors differ from the source\n", bad, z.n_tensors);
        return 1;
    }
    printf("bit-exact: %u tensors (%llu exact, %llu native), %llu bytes identical to the source\n",
           z.n_tensors, (unsigned long long) n_exact, (unsigned long long) n_native,
           (unsigned long long) bytes);
    return 0;
}

static int cmd_verify(int argc, char **argv)
{
    if (argc != 1 && !(argc == 3 && !strcmp(argv[1], "--pub")))
        die("usage: zxvc verify PKG.zxvc [--pub PK]");
    mapped_t m;
    zxvc_t z;
    open_pkg(argv[0], &m, &z);
    uint32_t bad;
    int32_t rc = zxvc_verify_hashes(&z, &bad);
    if (rc) {
        if (bad < z.n_parts) {
            zxvc_part_t p;
            zxvc_part(&z, bad, &p);
            die("part %s: %s", p.name, zxvc_err_name(rc));
        }
        zxvc_tensor_t t;
        zxvc_tensor(&z, bad - z.n_parts, &t);
        die("tensor %s: %s", t.name, zxvc_err_name(rc));
    }
    uint8_t dig[32];
    char dx[65];
    zxvc_digest(&z, dig);
    hex(dig, dx);
    printf("hashes OK: %u parts, %u tensors\nmanifest digest: %s\n", z.n_parts, z.n_tensors, dx);
    if (z.flags & ZXVC_FLAG_LICENCE_OVERRIDE) printf("note: licence converted by owner override\n");
    if (argc == 3) {
        uint64_t pkl;
        uint8_t *pk = read_small(argv[2], &pkl);
        if (pkl != PQM_MLDSA87_PK_BYTES) die("public key must be raw ML-DSA-87");
        if (z.sig_len != SIG_BYTES) die("package is not signed");
        uint8_t h[32];
        sha256(pk, pkl, h);
        const uint8_t *sig = m.buf + z.sig_off;
        if (memcmp(h, sig, 32)) die("signed by a different key");
        if (!pqm_mldsa87_verify(pk, dig, 32, (const uint8_t *) ZXVC_SIG_CTX, strlen(ZXVC_SIG_CTX),
                                sig + ZXVC_HASH))
            die("signature does not verify");
        printf("signature OK (ML-DSA-87)\n");
    } else if (z.sig_len) {
        printf("signed; pass --pub to check the signature\n");
    }
    return 0;
}

static int cmd_export(int argc, char **argv)
{
    if (argc != 2) die("usage: zxvc export PKG.zxvc OUT.gguf");
    mapped_t m;
    zxvc_t z;
    open_pkg(argv[0], &m, &z);
    if (!(z.flags & ZXVC_FLAG_FROM_GGUF)) die("only packages made from GGUF rebuild a GGUF file");
    zxvc_part_t pre, src;
    if (zxvc_find_part(&z, "gguf-prefix", &pre) || zxvc_find_part(&z, "source", &src))
        die("package lacks its gguf-prefix or source part");
    char want[65] = "";
    unsigned long long size = 0;
    if (sscanf((const char *) src.data, "%64s %llu", want, &size) != 2) die("bad source part");
    uint8_t *out = calloc(1, size ? size : 1);
    if (!out) die("out of memory");
    if (pre.len > size) die("bad gguf-prefix");
    memcpy(out, pre.data, pre.len);
    for (uint32_t i = 0; i < z.n_tensors; i++) {
        zxvc_tensor_t t;
        if (zxvc_tensor(&z, i, &t)) die("tensor %u unreadable", i);
        if (t.src_off > size || t.src_len > size - t.src_off) die("%s: outside the file", t.name);
        int32_t rc = zxvc_tensor_source(&t, out + t.src_off, size - t.src_off);
        if (rc) die("%s: %s", t.name, zxvc_err_name(rc));
    }
    uint8_t h[32];
    char hx[65];
    sha256(out, size, h);
    hex(h, hx);
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(out, 1, size, f) != size || fclose(f)) die("%s: write failed", argv[1]);
    bool same = !strcmp(hx, want);
    printf("wrote %s: %llu bytes, SHA-256 %s\n%s\n", argv[1], size, hx,
           same ? "identical to the source file" : "DIFFERS from the source file");
    return same ? 0 : 1;
}

static int cmd_info(int argc, char **argv)
{
    if (argc != 1) die("usage: zxvc info PKG.zxvc");
    mapped_t m;
    zxvc_t z;
    open_pkg(argv[0], &m, &z);
    printf("version %u, flags 0x%x, %u parts, %u tensors, %llu bytes%s\n", z.version, z.flags,
           z.n_parts, z.n_tensors, (unsigned long long) z.size, z.sig_len ? ", signed" : "");
    for (uint32_t i = 0; i < z.n_parts; i++) {
        zxvc_part_t p;
        if (zxvc_part(&z, i, &p)) die("part %u unreadable", i);
        printf("  part %-12s %10llu bytes\n", p.name, (unsigned long long) p.len);
        if (!strcmp(p.name, "licence") || !strcmp(p.name, "source"))
            printf("    %.*s", (int) (p.len > 600 ? 600 : p.len), (const char *) p.data);
    }
    uint64_t ex = 0, na = 0;
    for (uint32_t i = 0; i < z.n_tensors; i++) {
        zxvc_tensor_t t;
        if (zxvc_tensor(&z, i, &t)) die("tensor %u unreadable", i);
        if (t.enc == ZXVC_ENC_EXACT)
            ex++;
        else
            na++;
    }
    printf("  tensors: %llu exact, %llu native\n", (unsigned long long) ex,
           (unsigned long long) na);
    return 0;
}

static int cmd_keygen(int argc, char **argv)
{
    if (argc != 2) die("usage: zxvc keygen OUT.pk OUT.sk");
    uint8_t seed[32];
    static uint8_t pk[PQM_MLDSA87_PK_BYTES], sk[PQM_MLDSA87_SK_BYTES];
    FILE *r = fopen("/dev/urandom", "rb");
    if (!r || fread(seed, 1, 32, r) != 32) die("no randomness");
    fclose(r);
    pqm_mldsa87_keygen(seed, pk, sk);
    FILE *f = fopen(argv[0], "wb");
    if (!f || fwrite(pk, 1, sizeof pk, f) != sizeof pk || fclose(f)) die("%s: write", argv[0]);
    int fd = open(argv[1], O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0 || write(fd, sk, sizeof sk) != (ssize_t) sizeof sk || close(fd))
        die("%s: write", argv[1]);
    memset(sk, 0, sizeof sk);
    memset(seed, 0, sizeof seed);
    printf("wrote %s (public) and %s (secret, mode 0600)\n", argv[0], argv[1]);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: zxvc convert|check|verify|export|info|keygen ...\n");
        return 2;
    }
    const char *c = argv[1];
    if (!strcmp(c, "convert")) return cmd_convert(argc - 2, argv + 2);
    if (!strcmp(c, "check")) return cmd_check(argc - 2, argv + 2);
    if (!strcmp(c, "verify")) return cmd_verify(argc - 2, argv + 2);
    if (!strcmp(c, "export")) return cmd_export(argc - 2, argv + 2);
    if (!strcmp(c, "info")) return cmd_info(argc - 2, argv + 2);
    if (!strcmp(c, "keygen")) return cmd_keygen(argc - 2, argv + 2);
    fprintf(stderr, "zxvc: unknown command %s\n", c);
    return 2;
}
