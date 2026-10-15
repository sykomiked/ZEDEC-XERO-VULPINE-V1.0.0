/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* hw_models.c — index the golden disc's models at boot, hash them later.
 * See hw_models.h for the disc layout and the manifest format. */
#include "hw_models.h"

#define END_OF_CHAIN 0x0FFFFFF8u

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static uint8_t hex_val(char c)
{
    if (c >= '0' && c <= '9') return (uint8_t) (c - '0');
    if (c >= 'a' && c <= 'f') return (uint8_t) (c - 'a' + 10);
    return (uint8_t) (c - 'A' + 10);
}

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static int is_space(char c)
{
    return c == ' ' || c == '\t';
}

/* Next field of [*pos, end): sets *f and *len, moves *pos past it. 0 if none. */
static int field(const char **pos, const char *end, const char **f, uint32_t *len)
{
    const char *q = *pos;
    while (q < end && is_space(*q)) q++;
    if (q == end) return 0;
    *f = q;
    while (q < end && !is_space(*q)) q++;
    *len = (uint32_t) (q - *f);
    *pos = q;
    return 1;
}

/* An 8.3 name the FAT32 driver can match: 1-8 characters, then optionally
 * '.' and 1-3. Upper-cased into out (13 bytes). */
static int name83(const char *f, uint32_t len, char *out)
{
    uint32_t base = 0, ext = 0, dot = 0;
    if (len == 0 || len > 12) return 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = f[i];
        if (c >= 'a' && c <= 'z') c = (char) (c - 'a' + 'A');
        if (c == '.') {
            if (dot || base == 0) return 0;
            dot = 1;
        } else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            if (dot)
                ext++;
            else
                base++;
        } else {
            return 0;
        }
        out[i] = c;
    }
    out[len] = 0;
    return base >= 1 && base <= 8 && ext <= 3 && (!dot || ext >= 1);
}

/* Parse one manifest line of volume `vol` into m. Returns 0 or a reason. */
static const char *parse_line(const char *q, const char *end, hw_model_t *m, uint8_t vol)
{
    const char *f;
    uint32_t len;
    hw_memset(m, 0, sizeof *m);
    if (!field(&q, end, &f, &len) || len != 64) return "sha256 is not 64 hex digits";
    for (uint32_t i = 0; i < 64; i++) {
        if (!is_hex(f[i])) return "sha256 is not 64 hex digits";
        m->sha[i / 2] = (uint8_t) (m->sha[i / 2] << 4 | hex_val(f[i]));
    }
    if (!field(&q, end, &f, &len) || len == 0 || len > 10) return "size is not a number";
    uint64_t v = 0;
    for (uint32_t i = 0; i < len; i++) {
        if (f[i] < '0' || f[i] > '9') return "size is not a number";
        v = v * 10u + (uint64_t) (f[i] - '0');
    }
    if (v == 0 || v > 0xFFFFFFFFu) return "size is 0 or does not fit FAT32";
    m->bytes = (uint32_t) v;
    if (!field(&q, end, &f, &len) || len >= HW_MODEL_NAME) return "name missing or too long";
    hw_memcpy(m->name, f, len);
    m->name[len] = 0;
    if (!field(&q, end, &f, &len)) return "partition missing";
    int esp = len == 3 && f[0] == 'e' && f[1] == 's' && f[2] == 'p';
    int swarm = len == 5 && f[0] == 's' && f[1] == 'w' && f[2] == 'a' && f[3] == 'r' && f[4] == 'm';
    if (!esp && !swarm) return "partition is neither esp nor swarm";
    if (esp != (vol == 0))
        return vol ? "a swarm disc lists an esp model" : "the core disc lists a swarm model";
    m->vol = vol;
    if (!field(&q, end, &f, &len) || !name83(f, len, m->file)) return "file is not an 8.3 name";
    if (field(&q, end, &f, &len)) return "extra fields";
    m->verdict = HW_N;
    return 0;
}

/* Where a volume's manifest is, for the log: "sata0 /ZXV/MODELS.SHA" or
 * "ZXV_SWARM_REASON on sata1 /MODELS.SHA". */
static void where(hw_line_t *l, const hw_models_vol_t *v, uint8_t vol)
{
    if (vol) {
        hw_line_s(l, v->part.name);
        hw_line_s(l, " on ");
    }
    hw_line_s(l, v->disk->name);
    hw_line_s(l, vol ? " /MODELS.SHA" : " /ZXV/MODELS.SHA");
}

static void log_fail(const hw_plat_t *p, const hw_models_vol_t *v, uint8_t vol, uint32_t line,
                     const char *why)
{
    hw_line_t l;
    hw_line_init(&l);
    hw_line_s(&l, "  [MODELS_FAIL] ");
    where(&l, v, vol);
    if (line) {
        hw_line_s(&l, " line ");
        hw_line_u(&l, line);
    }
    hw_line_s(&l, ": ");
    hw_line_s(&l, why);
    p->log(l.b);
}

/* A bad side file on the core disc: "[MODELS_FAIL] sata0 <path> line N: why". */
static void log_note(const hw_plat_t *p, const hw_models_vol_t *v, const char *path, uint32_t line,
                     const char *why)
{
    hw_line_t l;
    hw_line_init(&l);
    hw_line_s(&l, "  [MODELS_FAIL] ");
    hw_line_s(&l, v->disk->name);
    hw_line_c(&l, ' ');
    hw_line_s(&l, path);
    if (line) {
        hw_line_s(&l, " line ");
        hw_line_u(&l, line);
    }
    hw_line_s(&l, ": ");
    hw_line_s(&l, why);
    p->log(l.b);
}

/* Parse volume `vol`'s manifest, appending to s->m. On any bad line it
 * logs, drops this volume's models and returns -1. */
static int parse(hw_models_t *s, const hw_plat_t *p, uint8_t vol, const char *t, uint32_t n)
{
    const char *q = t, *end = t + n;
    uint32_t line = 0, base = s->n;
    while (q < end) {
        const char *e = q;
        while (e < end && *e != '\n') e++;
        const char *le = e;
        if (le > q && le[-1] == '\r') le--;
        line++;
        const char *a = q;
        while (a < le && is_space(*a)) a++;
        if (a < le && *a != '#') {
            const char *why = 0;
            if (s->n == HW_MODELS_MAX) why = "more models than the kernel tracks";
            hw_model_t *m = &s->m[s->n < HW_MODELS_MAX ? s->n : 0];
            if (!why) why = parse_line(a, le, m, vol);
            for (uint32_t i = 0; !why && i < s->n; i++)
                if (str_eq(s->m[i].name, m->name) ||
                    (s->m[i].vol == vol && str_eq(s->m[i].file, m->file)))
                    why = "name or file listed twice";
            if (why) {
                log_fail(p, &s->vol[vol], vol, line, why);
                s->n = base;
                return -1;
            }
            s->n++;
        }
        q = e < end ? e + 1 : end;
    }
    return 0;
}

static int mount(hw_models_vol_t *v, hw_blk_t *b)
{
    v->disk = b;
    v->fat_sector = 0xFFFFFFFFu;
    return hw_bdev_init(&v->bd, b, v->part.first, v->part.last - v->part.first + 1) == 0 &&
           fat32_mount(&v->fs, &v->bd.bd) == 0 && fat32_read_dir(&v->fs, v->fs.root_cluster) == 0;
}

/* Read MODELS.SHA from the directory now loaded on volume `vol`, parse it,
 * then look each of its models up in the subdirectory MODELS. Returns how
 * many models the volume added. */
static uint32_t load(hw_models_t *s, const hw_plat_t *p, uint8_t vol)
{
    static char text[HW_MODELS_SHA_MAX];
    hw_models_vol_t *v = &s->vol[vol];
    fat32_file_t *f = fat32_find_file(&v->fs, "MODELS.SHA");
    uint32_t base = s->n;
    if (!f || f->is_dir) {
        log_fail(p, v, vol, 0, "missing");
        return 0;
    }
    if (f->size > sizeof text) {
        log_fail(p, v, vol, 0, "larger than the kernel reads");
        return 0;
    }
    if (fat32_read_file(&v->fs, f, (uint8_t *) text, sizeof text) != 0) {
        log_fail(p, v, vol, 0, "could not be read");
        return 0;
    }
    if (parse(s, p, vol, text, f->size) != 0) return 0;
    if (s->n > base && fat32_change_dir(&v->fs, "MODELS") == 0) {
        for (uint32_t i = base; i < s->n; i++) {
            hw_model_t *m = &s->m[i];
            fat32_file_t *e = fat32_find_file(&v->fs, m->file);
            if (!e || e->is_dir) continue;
            m->found = 1;
            m->first = e->cluster;
            m->disc_bytes = e->size;
        }
    }
    return s->n - base;
}

/* Read file `name` from the directory loaded on v into buf (cap bytes).
 * Returns its size, -1 when absent, -2 when too large or unreadable. */
static int read_small(hw_models_vol_t *v, const char *name, char *buf, uint32_t cap)
{
    fat32_file_t *f = fat32_find_file(&v->fs, name);
    if (!f || f->is_dir) return -1;
    if (f->size > cap || fat32_read_file(&v->fs, f, (uint8_t *) buf, cap) != 0) return -2;
    return (int) f->size;
}

/* Copy [f, f+len) into out (cap bytes with NUL) if every byte is in the
 * set: base32 lower case for a CIDv1 ("b..."), base36 for an IPNS name. */
static int token(const char *f, uint32_t len, char *out, uint32_t cap, char lead, int digits)
{
    if (len < 21 || len >= cap || f[0] != lead) return 0;
    for (uint32_t i = 0; i < len; i++) {
        char c = f[i];
        if (!((c >= 'a' && c <= 'z') || (digits ? (c >= '0' && c <= '9') : (c >= '2' && c <= '7'))))
            return 0;
        out[i] = c;
    }
    out[len] = 0;
    return 1;
}

/* /ZXV/UPSTREAM.CID: "cid <bafy...>" and optionally "ipns <k51...>". */
static const char *parse_upstream(hw_models_t *s, const char *t, uint32_t n, uint32_t *line)
{
    const char *q = t, *end = t + n;
    *line = 0;
    while (q < end) {
        const char *e = q, *f, *g;
        uint32_t len, glen;
        while (e < end && *e != '\n') e++;
        const char *le = e > q && e[-1] == '\r' ? e - 1 : e;
        const char *a = q;
        (*line)++;
        q = e < end ? e + 1 : end;
        if (!field(&a, le, &f, &len) || *f == '#') continue;
        if (!field(&a, le, &g, &glen)) return "a key without a value";
        const char *x;
        uint32_t xlen;
        if (field(&a, le, &x, &xlen) && *x != '#') return "extra fields";
        if (len == 3 && f[0] == 'c' && f[1] == 'i' && f[2] == 'd') {
            if (s->cid[0]) return "cid listed twice";
            if (!token(g, glen, s->cid, sizeof s->cid, 'b', 0)) return "cid is not a base32 CIDv1";
        } else if (len == 4 && f[0] == 'i' && f[1] == 'p' && f[2] == 'n' && f[3] == 's') {
            if (s->ipns[0]) return "ipns listed twice";
            if (!token(g, glen, s->ipns, sizeof s->ipns, 'k', 1))
                return "ipns is not a base36 name";
        } else {
            return "unknown key (cid|ipns)";
        }
    }
    *line = 0;
    return s->cid[0] ? 0 : "no cid line";
}

/* /ZXV/SWARM.LST: "<disc> <GROUP> <image>" per expected swarm disc. */
static const char *parse_want(hw_models_t *s, const char *t, uint32_t n, uint32_t *line)
{
    const char *q = t, *end = t + n;
    *line = 0;
    while (q < end) {
        const char *e = q, *f;
        uint32_t len;
        while (e < end && *e != '\n') e++;
        const char *le = e > q && e[-1] == '\r' ? e - 1 : e;
        const char *a = q;
        (*line)++;
        q = e < end ? e + 1 : end;
        if (!field(&a, le, &f, &len) || *f == '#') continue;
        if (s->nwant == HW_SWARM_WANT_MAX) return "more swarm discs than the kernel tracks";
        hw_swarm_want_t *w = &s->want[s->nwant];
        uint32_t d = 0;
        if (len > 3) return "disc is not a number from 2";
        for (uint32_t i = 0; i < len; i++) {
            if (f[i] < '0' || f[i] > '9') return "disc is not a number from 2";
            d = d * 10u + (uint32_t) (f[i] - '0');
        }
        if (d < 2) return "disc is not a number from 2";
        w->disc = d;
        if (!field(&a, le, &f, &len) || len == 0 || len >= sizeof w->group)
            return "group is not 1-16 of A-Z 0-9";
        for (uint32_t i = 0; i < len; i++) {
            if (!((f[i] >= 'A' && f[i] <= 'Z') || (f[i] >= '0' && f[i] <= '9')))
                return "group is not 1-16 of A-Z 0-9";
            w->group[i] = f[i];
        }
        w->group[len] = 0;
        if (!field(&a, le, &f, &len) || len == 0 || len >= sizeof w->image)
            return "image name missing or too long";
        for (uint32_t i = 0; i < len; i++) w->image[i] = f[i];
        w->image[len] = 0;
        if (field(&a, le, &f, &len)) return "extra fields";
        for (uint32_t i = 0; i < s->nwant; i++)
            if (str_eq(s->want[i].group, w->group)) return "group listed twice";
        s->nwant++;
    }
    *line = 0;
    return 0;
}

/* Read UPSTREAM.CID and SWARM.LST from the core disc's /ZXV (loaded on
 * vol[0]). Either may be absent; a bad one is logged and ignored. */
static void core_extras(hw_models_t *s, const hw_plat_t *p)
{
    static char t[2048];
    hw_models_vol_t *v = &s->vol[0];
    const char *why;
    uint32_t line;
    int n = read_small(v, "UPSTREAM.CID", t, sizeof t);
    if (n != -1) {
        why = n < 0 ? "unreadable or larger than the kernel reads"
                    : parse_upstream(s, t, (uint32_t) n, &line);
        if (why) {
            s->cid[0] = s->ipns[0] = 0;
            log_note(p, v, "/ZXV/UPSTREAM.CID", n < 0 ? 0 : line, why);
        }
    }
    n = read_small(v, "SWARM.LST", t, sizeof t);
    if (n != -1) {
        why = n < 0 ? "unreadable or larger than the kernel reads"
                    : parse_want(s, t, (uint32_t) n, &line);
        if (why) {
            s->nwant = 0;
            log_note(p, v, "/ZXV/SWARM.LST", n < 0 ? 0 : line, why);
        }
    }
}

static int swarm_name(const char *n)
{
    const char *want = "ZXV_SWARM_";
    while (*want && *n == *want) {
        n++;
        want++;
    }
    return *want == 0 && *n != 0;
}

uint32_t hw_models_index(hw_models_t *s, const hw_plat_t *p, hw_registry_t *r)
{
    static uint8_t scratch[4096];
    hw_memset(s, 0, sizeof *s);
    s->bg.idx = -1;
    s->nvol = 1;
    uint32_t on_esp = 0, on_swarm = 0, discs = 0;
    int core = 0;

    /* The core disc: the first ESP with /ZXV/MODELS.SHA. */
    for (uint32_t d = 0; d < r->n_blk && !core; d++) {
        hw_models_vol_t *v = &s->vol[0];
        hw_blk_t *b = r->blk[d];
        if (hw_gpt_find_esp(b, &v->part, scratch) != HW_GPT_OK || !mount(v, b)) continue;
        if (fat32_change_dir(&v->fs, "ZXV") != 0) continue;
        fat32_file_t *f = fat32_find_file(&v->fs, "MODELS.SHA");
        if (!f || f->is_dir) continue;
        core = 1;
        core_extras(s, p);
        on_esp = load(s, p, 0);
    }

    /* Every swarm disc: a partition of the swarm type named ZXV_SWARM_*. */
    for (uint32_t d = 0; d < r->n_blk; d++) {
        hw_blk_t *b = r->blk[d];
        hw_part_t part;
        if (hw_gpt_find_type(b, HW_GPT_ZXV_SWARM, &part, scratch) != HW_GPT_OK) continue;
        hw_line_t l;
        hw_line_init(&l);
        /* at most one swarm volume per disk, so vol[] cannot run out */
        if (!swarm_name(part.name)) {
            hw_line_s(&l, "  [SWARM_SKIPPED] ");
            hw_line_s(&l, b->name);
            hw_line_s(&l, " partition '");
            hw_line_s(&l, part.name);
            hw_line_s(&l, "' is not named ZXV_SWARM_<group>");
            p->log(l.b);
            continue;
        }
        uint8_t vol = (uint8_t) s->nvol;
        hw_models_vol_t *v = &s->vol[vol];
        v->part = part;
        if (!mount(v, b)) {
            hw_line_s(&l, "  [MODELS_FAIL] ");
            hw_line_s(&l, part.name);
            hw_line_s(&l, " on ");
            hw_line_s(&l, b->name);
            hw_line_s(&l, " is not a FAT32 volume this driver can mount");
            p->log(l.b);
            continue;
        }
        s->nvol++;
        uint32_t k = load(s, p, vol);
        if (k == 0) continue;
        on_swarm += k;
        discs++;
        hw_line_s(&l, "  [SWARM_MOUNTED] ");
        hw_line_s(&l, part.name);
        hw_line_s(&l, " on ");
        hw_line_s(&l, b->name);
        hw_line_s(&l, ": ");
        hw_line_u(&l, k);
        hw_line_s(&l, k == 1 ? " model" : " models");
        p->log(l.b);
    }

    if (s->cid[0]) {
        hw_line_t u;
        hw_line_init(&u);
        hw_line_s(&u, "  [UPSTREAM] ");
        if (s->ipns[0]) {
            hw_line_s(&u, "ipns ");
            hw_line_s(&u, s->ipns);
            hw_line_s(&u, ", fallback ");
        }
        hw_line_s(&u, "bucket ");
        hw_line_s(&u, s->cid);
        p->log(u.b);
    }
    uint32_t missing = 0;
    for (uint32_t w = 0; w < s->nwant; w++) {
        int found = 0;
        for (uint32_t k = 1; k < s->nvol && !found; k++) {
            const char *a = s->vol[k].part.name + 10, *g = s->want[w].group;
            found = str_eq(a, g);
        }
        if (found) continue;
        missing++;
        hw_line_t m;
        hw_line_init(&m);
        hw_line_s(&m, "  [SWARM_MISSING] disc ");
        hw_line_u(&m, s->want[w].disc);
        hw_line_s(&m, " ZXV_SWARM_");
        hw_line_s(&m, s->want[w].group);
        hw_line_s(&m, " (");
        hw_line_s(&m, s->want[w].image);
        hw_line_s(&m, s->cid[0] ? "): attach it, or zxvd fetches it from the upstream bucket"
                                : "): attach it");
        p->log(m.b);
    }
    if (s->nwant && !missing) {
        hw_line_t c;
        hw_line_init(&c);
        hw_line_s(&c, "  [SWARM_COMPLETE] all ");
        hw_line_u(&c, s->nwant);
        hw_line_s(&c, s->nwant == 1 ? " swarm disc the core expects is attached"
                                    : " swarm discs the core expects are attached");
        p->log(c.b);
    }

    hw_line_t l;
    hw_line_init(&l);
    if (s->n == 0) {
        p->log(core || s->nvol > 1 ? "  [MODELS_NONE] the attached discs list no usable models"
                                   : "  [MODELS_NONE] no core or swarm disc attached");
        return 0;
    }
    hw_line_s(&l, "  [MODELS_INDEXED] ");
    hw_line_u(&l, s->n);
    hw_line_s(&l, " listed (");
    hw_line_u(&l, on_esp);
    hw_line_s(&l, " on the boot partition, ");
    hw_line_u(&l, on_swarm);
    hw_line_s(&l, " on ");
    hw_line_u(&l, discs);
    hw_line_s(&l, discs == 1 ? " swarm disc)" : " swarm discs)");
    hw_line_s(&l, "; each is verified when first used");
    p->log(l.b);
    return s->n;
}

/* Next cluster of a chain, through a one-sector FAT cache (a contiguous
 * file then costs one FAT read per 128 clusters). END_OF_CHAIN on any
 * end, error or out-of-range entry. */
static uint32_t next_cluster(hw_models_vol_t *v, uint32_t c)
{
    uint64_t entries = (uint64_t) v->fs.bpb.fat_size32 * 128u;
    if (c < 2 || c >= entries) return END_OF_CHAIN;
    uint32_t sector = v->fs.fat_start_sector + c / 128u;
    if (sector != v->fat_sector) {
        if (blockdev_read(&v->bd.bd, sector, v->fat_buf) != 0) {
            v->fat_sector = 0xFFFFFFFFu;
            return END_OF_CHAIN;
        }
        v->fat_sector = sector;
    }
    const uint8_t *e = v->fat_buf + (c % 128u) * 4u;
    uint32_t n =
        ((uint32_t) e[0] | (uint32_t) e[1] << 8 | (uint32_t) e[2] << 16 | (uint32_t) e[3] << 24) &
        0x0FFFFFFFu;
    return n < 2 || n >= entries ? END_OF_CHAIN : n;
}

/* Read cluster c of v in one request of a cluster's sectors. */
static int read_cluster(hw_models_vol_t *v, uint32_t c, uint8_t *buf)
{
    uint64_t entries = (uint64_t) v->fs.bpb.fat_size32 * 128u;
    uint32_t spc = v->fs.bpb.sectors_per_cluster;
    if (c < 2 || c >= entries) return -1;
    uint64_t lba = (uint64_t) v->fs.data_start_sector + (uint64_t) (c - 2) * spc;
    if (lba + spc > v->bd.count) return -1;
    return v->bd.b->read(v->bd.b, v->bd.base + lba, spc, buf);
}

/* Data clusters the volume holds: what both the partition and the FAT reach. */
static uint32_t clusters(const hw_models_vol_t *v)
{
    uint64_t entries = (uint64_t) v->fs.bpb.fat_size32 * 128u;
    uint64_t data =
        v->bd.count > v->fs.data_start_sector ? v->bd.count - v->fs.data_start_sector : 0;
    uint64_t n = data / v->fs.bpb.sectors_per_cluster;
    if (entries < 2) return 0;
    if (n > entries - 2) n = entries - 2;
    return (uint32_t) n;
}

static void finish(hw_models_t *s, const hw_plat_t *p, hw_model_t *m, hw_verdict_t v,
                   const char *why)
{
    hw_line_t l;
    m->verdict = v;
    hw_line_init(&l);
    if (v == HW_T) {
        s->ok++;
        hw_line_s(&l, "  [MODEL_OK] ");
        hw_line_s(&l, m->name);
        hw_line_c(&l, ' ');
        hw_line_u(&l, m->bytes);
        hw_line_s(&l, " bytes, sha256 matches");
    } else {
        s->bad++;
        hw_line_s(&l, "  [MODEL_BAD] ");
        hw_line_s(&l, m->name);
        hw_line_s(&l, " (");
        hw_line_s(&l, m->vol ? s->vol[m->vol].part.name : "esp");
        hw_line_c(&l, ':');
        hw_line_s(&l, m->file);
        hw_line_s(&l, ") ");
        hw_line_s(&l, why);
    }
    p->log(l.b);
}

/* Start hashing model i into c. 0 when the model was settled without
 * reading (missing or wrong size). */
static int start(hw_models_t *s, const hw_plat_t *p, hw_model_cur_t *c, int i)
{
    hw_model_t *m = &s->m[i];
    if (!m->found) {
        finish(s, p, m, HW_F,
               m->vol ? "not found in /MODELS on its swarm disc"
                      : "not found in /ZXV/MODELS on the boot partition");
        return 0;
    }
    if (m->disc_bytes != m->bytes) {
        finish(s, p, m, HW_F, "size on disc differs from MODELS.SHA");
        return 0;
    }
    /* A file can hold at most every cluster of its volume. This also bounds
     * the walk of a chain that loops back on itself. */
    hw_models_vol_t *v = &s->vol[m->vol];
    if ((uint64_t) m->bytes > (uint64_t) clusters(v) * v->fs.bytes_per_cluster) {
        finish(s, p, m, HW_F, "is larger than its partition");
        return 0;
    }
    c->idx = i;
    c->cluster = m->first;
    c->hops = 0;
    c->done = 0;
    sha256_init(&c->ctx);
    return 1;
}

/* Hash up to `budget` clusters. Returns 1 once the model has a verdict. */
static int run(hw_models_t *s, const hw_plat_t *p, hw_model_cur_t *c, uint32_t budget)
{
    static uint8_t buf[FAT32_CLUSTER_MAX];
    hw_model_t *m = &s->m[c->idx];
    hw_models_vol_t *v = &s->vol[m->vol];
    uint32_t bpc = v->fs.bytes_per_cluster;
    uint32_t max_hops = m->bytes / bpc + 1u;
    while (budget-- && c->done < m->bytes) {
        if (c->cluster >= END_OF_CHAIN || c->hops++ >= max_hops) {
            finish(s, p, m, HW_F, "cluster chain ends before the file does");
            c->idx = -1;
            return 1;
        }
        if (read_cluster(v, c->cluster, buf) != 0) {
            finish(s, p, m, HW_F, "a cluster could not be read");
            c->idx = -1;
            return 1;
        }
        uint64_t left = m->bytes - c->done;
        uint32_t take = left < bpc ? (uint32_t) left : bpc;
        sha256_update(&c->ctx, buf, take);
        c->done += take;
        if (c->done < m->bytes) c->cluster = next_cluster(v, c->cluster);
    }
    if (c->done < m->bytes) return 0;
    uint8_t got[32];
    sha256_final(&c->ctx, got);
    int same = 1;
    for (uint32_t i = 0; i < 32; i++) same &= got[i] == m->sha[i];
    finish(s, p, m, same ? HW_T : HW_F, "sha256 differs from MODELS.SHA");
    c->idx = -1;
    return 1;
}

int hw_models_step(hw_models_t *s, const hw_plat_t *p, uint32_t clusters)
{
    if (s->n == 0) return 0;
    while (s->bg.idx < 0) {
        int next = -1;
        for (uint32_t i = 0; i < s->n && next < 0; i++)
            if (s->m[i].verdict == HW_N) next = (int) i;
        if (next < 0) {
            if (!s->reported) {
                hw_line_t l;
                hw_line_init(&l);
                if (s->bad == 0) {
                    hw_line_s(&l, "  [MODELS_OK] ");
                    hw_line_u(&l, s->n);
                    hw_line_s(&l, " verified");
                } else {
                    hw_line_s(&l, "  [MODELS_FAIL] ");
                    hw_line_u(&l, s->bad);
                    hw_line_s(&l, " of ");
                    hw_line_u(&l, s->n);
                    hw_line_s(&l, " models bad");
                }
                p->log(l.b);
                s->reported = 1;
            }
            return 0;
        }
        start(s, p, &s->bg, next);
    }
    run(s, p, &s->bg, clusters);
    return 1;
}

hw_verdict_t hw_models_require(hw_models_t *s, const hw_plat_t *p, const char *name)
{
    int i = -1;
    for (uint32_t k = 0; k < s->n && i < 0; k++)
        if (str_eq(s->m[k].name, name) || str_eq(s->m[k].file, name)) i = (int) k;
    if (i < 0) return HW_F;
    hw_model_t *m = &s->m[i];
    if (m->verdict != HW_N) return m->verdict;
    if (s->bg.idx == i) {
        while (!run(s, p, &s->bg, 0xFFFFFFFFu));
        return m->verdict;
    }
    hw_model_cur_t c;
    if (start(s, p, &c, i))
        while (!run(s, p, &c, 0xFFFFFFFFu));
    return m->verdict;
}
