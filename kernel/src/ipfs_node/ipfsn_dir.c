/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* ipfsn_dir.c — strict UnixFS directory reader (plain and HAMT-sharded).
 * See ipfsn_dir.h for the rules. The HAMT layout follows boxo
 * ipld/unixfs/hamt (go-bitfield bit order, "%0NX" link prefixes, murmur3
 * h1 big-endian) as read from boxo v0.24.3 and checked against Kubo 0.32.1.
 */
#include "ipfsn_dir.h"
#include "ipfsn_util.h"

/* ---- protobuf reader (local copies; the ones in ipfsn_unixfs.c are static) -- */

static int rd_varint(const uint8_t *p, uint32_t len, uint32_t *pos, uint64_t *v)
{
    int k = ipfsn_varint_get(p + *pos, len - *pos, v);
    if (k < 0) return IPFSN_ERR_MALFORMED;
    *pos += (uint32_t) k;
    return IPFSN_OK;
}

static int rd_tag(const uint8_t *p, uint32_t len, uint32_t *pos, uint32_t *field, uint32_t *wt)
{
    uint64_t t;
    if (rd_varint(p, len, pos, &t)) return IPFSN_ERR_MALFORMED;
    if (t > 0xFFFFFFFFu || (t >> 3) == 0) return IPFSN_ERR_MALFORMED;
    *field = (uint32_t) (t >> 3);
    *wt = (uint32_t) (t & 7u);
    return IPFSN_OK;
}

static int rd_bytes(const uint8_t *p, uint32_t len, uint32_t *pos, const uint8_t **b,
                    uint32_t *blen)
{
    uint64_t l;
    if (rd_varint(p, len, pos, &l)) return IPFSN_ERR_MALFORMED;
    if (l > len - *pos) return IPFSN_ERR_MALFORMED;
    *b = p + *pos;
    *blen = (uint32_t) l;
    *pos += (uint32_t) l;
    return IPFSN_OK;
}

/* ---- murmur3 ---------------------------------------------------------------- */

static uint64_t rotl64(uint64_t x, uint32_t r)
{
    return (x << r) | (x >> (64u - r));
}

static uint64_t fmix64(uint64_t k)
{
    k ^= k >> 33;
    k *= 0xff51afd7ed558ccdull;
    k ^= k >> 33;
    k *= 0xc4ceb9fe1a85ec53ull;
    k ^= k >> 33;
    return k;
}

static uint64_t rd64le(const uint8_t *p)
{
    uint64_t v = 0;
    for (uint32_t i = 8; i-- > 0;) v = (v << 8) | p[i];
    return v;
}

uint64_t ipfsn_murmur3_64(const uint8_t *data, uint32_t len)
{
    const uint64_t c1 = 0x87c37b91114253d5ull, c2 = 0x4cf5ad432745937full;
    uint64_t h1 = 0, h2 = 0, k1, k2;
    uint32_t nblocks = len >> 4, i;
    const uint8_t *tail;
    for (i = 0; i < nblocks; i++) {
        k1 = rd64le(data + i * 16u);
        k2 = rd64le(data + i * 16u + 8u);
        k1 *= c1;
        k1 = rotl64(k1, 31);
        k1 *= c2;
        h1 ^= k1;
        h1 = rotl64(h1, 27);
        h1 += h2;
        h1 = h1 * 5u + 0x52dce729u;
        k2 *= c2;
        k2 = rotl64(k2, 33);
        k2 *= c1;
        h2 ^= k2;
        h2 = rotl64(h2, 31);
        h2 += h1;
        h2 = h2 * 5u + 0x38495ab5u;
    }
    tail = data + nblocks * 16u;
    k1 = 0;
    k2 = 0;
    switch (len & 15u) {
    case 15:
        k2 ^= (uint64_t) tail[14] << 48; /* fall through */
    case 14:
        k2 ^= (uint64_t) tail[13] << 40; /* fall through */
    case 13:
        k2 ^= (uint64_t) tail[12] << 32; /* fall through */
    case 12:
        k2 ^= (uint64_t) tail[11] << 24; /* fall through */
    case 11:
        k2 ^= (uint64_t) tail[10] << 16; /* fall through */
    case 10:
        k2 ^= (uint64_t) tail[9] << 8; /* fall through */
    case 9:
        k2 ^= (uint64_t) tail[8];
        k2 *= c2;
        k2 = rotl64(k2, 33);
        k2 *= c1;
        h2 ^= k2;
        /* fall through */
    case 8:
        k1 ^= (uint64_t) tail[7] << 56; /* fall through */
    case 7:
        k1 ^= (uint64_t) tail[6] << 48; /* fall through */
    case 6:
        k1 ^= (uint64_t) tail[5] << 40; /* fall through */
    case 5:
        k1 ^= (uint64_t) tail[4] << 32; /* fall through */
    case 4:
        k1 ^= (uint64_t) tail[3] << 24; /* fall through */
    case 3:
        k1 ^= (uint64_t) tail[2] << 16; /* fall through */
    case 2:
        k1 ^= (uint64_t) tail[1] << 8; /* fall through */
    case 1:
        k1 ^= (uint64_t) tail[0];
        k1 *= c1;
        k1 = rotl64(k1, 31);
        k1 *= c2;
        h1 ^= k1;
        break;
    default:
        break;
    }
    h1 ^= len;
    h2 ^= len;
    h1 += h2;
    h2 += h1;
    h1 = fmix64(h1);
    h2 = fmix64(h2);
    h1 += h2;
    return h1;
}

/* Slot of `name` at `depth` for a table of 2^bits: bit group `depth`, MSB first. */
static int hamt_slot(uint64_t h, uint32_t bits, uint32_t depth, uint32_t *slot)
{
    uint32_t used = depth * bits;
    if (used + bits > 64u) return IPFSN_ERR_DEPTH; /* "sharded directory too deep" */
    *slot = (uint32_t) ((h << used) >> (64u - bits));
    return IPFSN_OK;
}

/* ---- names ------------------------------------------------------------------- */

bool ipfsn_dir_name_ok(const uint8_t *name, uint32_t len)
{
    if (!name || len == 0 || len > IPFSN_DIR_NAME_MAX) return false;
    if (len == 1 && name[0] == '.') return false;
    if (len == 2 && name[0] == '.' && name[1] == '.') return false;
    for (uint32_t i = 0; i < len; i++)
        if (name[i] == '/' || name[i] == 0) return false;
    return true;
}

/* -1, 0, 1 by bytes, shorter first on a common prefix (Go bytes.Compare). */
static int name_cmp(const uint8_t *a, uint32_t al, const uint8_t *b, uint32_t bl)
{
    uint32_t n = al < bl ? al : bl;
    for (uint32_t i = 0; i < n; i++)
        if (a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    return al == bl ? 0 : (al < bl ? -1 : 1);
}

static int hexval(uint8_t c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1; /* lowercase is not what Kubo writes: refused */
}

static int prefix_slot(const uint8_t *name, uint32_t padlen, uint32_t *slot)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < padlen; i++) {
        int h = hexval(name[i]);
        if (h < 0) return IPFSN_ERR_MALFORMED;
        v = (v << 4) | (uint32_t) h;
    }
    *slot = v;
    return IPFSN_OK;
}

/* ---- UnixFS Data for directories --------------------------------------------- */

typedef struct {
    uint32_t type;
    const uint8_t *data;
    uint32_t data_len;
    bool has_data, has_hash, has_fanout, has_filefields;
    uint64_t hash_type, fanout;
} ufs_dir_t;

static int parse_ufs(const uint8_t *msg, uint32_t len, ufs_dir_t *u)
{
    uint32_t pos = 0, last = 0, f, wt, blen;
    uint64_t v;
    const uint8_t *b;
    bool have_type = false;
    ipfsn__set(u, 0, (uint32_t) sizeof(*u));
    while (pos < len) {
        if (rd_tag(msg, len, &pos, &f, &wt)) return IPFSN_ERR_MALFORMED;
        if (f < last || (f == last && f != 4)) return IPFSN_ERR_MALFORMED; /* order, dups */
        last = f;
        if (wt == 0 && (f == 1 || f == 3 || f == 4 || f == 5 || f == 6 || f == 7)) {
            if (rd_varint(msg, len, &pos, &v)) return IPFSN_ERR_MALFORMED;
            if (f == 1) {
                if (v > 5) return IPFSN_ERR_MALFORMED;
                u->type = (uint32_t) v;
                have_type = true;
            } else if (f == 3 || f == 4) {
                u->has_filefields = true; /* filesize / blocksizes: never in a directory */
            } else if (f == 5) {
                u->hash_type = v;
                u->has_hash = true;
            } else if (f == 6) {
                u->fanout = v;
                u->has_fanout = true;
            } else if (v > 0xFFFFFFFFu) {
                return IPFSN_ERR_MALFORMED; /* mode is a uint32 */
            }
        } else if (wt == 2 && (f == 2 || f == 8)) {
            if (rd_bytes(msg, len, &pos, &b, &blen)) return IPFSN_ERR_MALFORMED;
            if (f == 2) {
                u->data = b;
                u->data_len = blen;
                u->has_data = true;
            }
        } else {
            return IPFSN_ERR_MALFORMED;
        }
    }
    return have_type ? IPFSN_OK : IPFSN_ERR_MALFORMED;
}

/* go-bitfield: bit i lives in byte (n-1-i/8), bit (i%8), the bytes right-aligned. */
static bool bf_bit(const uint8_t *bf, uint32_t bflen, uint32_t nbytes, uint32_t i)
{
    uint32_t idx = nbytes - 1u - (i >> 3);
    uint32_t start = nbytes - bflen; /* missing leading bytes are zero */
    if (idx < start) return false;
    return ((bf[idx - start] >> (i & 7u)) & 1u) != 0;
}

int ipfsn_dir_parse(const uint8_t *block, uint32_t len, ipfsn_dirnode_t *d)
{
    ufs_dir_t u;
    ipfsn_pblink_t l, prev;
    uint32_t pos = 0, n = 0;
    int r;
    if (!block || !d) return IPFSN_ERR_ARG;
    ipfsn__set(d, 0, (uint32_t) sizeof(*d));
    if (ipfsn_dagpb_parse(block, len, &d->pb)) return IPFSN_ERR_MALFORMED;
    if (!d->pb.data) return IPFSN_ERR_MALFORMED; /* dag-pb but not UnixFS */
    if (parse_ufs(d->pb.data, d->pb.data_len, &u)) return IPFSN_ERR_MALFORMED;
    if (u.type != IPFSN_UFS_DIR && u.type != IPFSN_UFS_HAMT) return IPFSN_ERR_UNSUPP;
    if (u.has_filefields) return IPFSN_ERR_MALFORMED;
    d->kind = u.type;

    if (u.type == IPFSN_UFS_DIR) {
        if (u.has_data || u.has_hash || u.has_fanout) return IPFSN_ERR_MALFORMED;
        ipfsn__set(&prev, 0, (uint32_t) sizeof(prev));
        while ((r = ipfsn_dagpb_next_link(&d->pb, &pos, &l)) == IPFSN_OK) {
            if (!ipfsn_dir_name_ok(l.name, l.name_len) || !l.has_tsize) return IPFSN_ERR_MALFORMED;
            if (n && name_cmp(prev.name, prev.name_len, l.name, l.name_len) >= 0)
                return IPFSN_ERR_MALFORMED; /* unsorted or duplicate */
            ipfsn__cpy(&prev, &l, (uint32_t) sizeof(l));
            n++;
        }
        if (r != IPFSN_ERR_NOTFOUND) return IPFSN_ERR_MALFORMED;
        d->entries = n;
        return IPFSN_OK;
    }

    /* HAMT shard */
    if (!u.has_hash || u.hash_type != IPFSN_HAMT_MURMUR3) return IPFSN_ERR_UNSUPP;
    if (!u.has_fanout || u.fanout < IPFSN_HAMT_FANOUT_MIN || u.fanout > IPFSN_HAMT_FANOUT_MAX ||
        (u.fanout & (u.fanout - 1u)) != 0)
        return IPFSN_ERR_MALFORMED;
    d->fanout = (uint32_t) u.fanout;
    while ((1u << d->bits) < d->fanout) d->bits++;
    {
        uint32_t m = d->fanout - 1u;
        d->padlen = 0;
        while (m) {
            d->padlen++;
            m >>= 4;
        }
    }
    {
        uint32_t nbytes = d->fanout >> 3, slot_i = 0, ones = 0;
        const uint8_t *bf = u.data;
        uint32_t bflen = u.has_data ? u.data_len : 0;
        if (bflen > nbytes) return IPFSN_ERR_MALFORMED;
        for (uint32_t i = 0; i < d->fanout; i++) ones += bf_bit(bf, bflen, nbytes, i) ? 1u : 0u;
        if (ones != d->pb.nlinks || ones == 0) return IPFSN_ERR_MALFORMED;
        while ((r = ipfsn_dagpb_next_link(&d->pb, &pos, &l)) == IPFSN_OK) {
            uint32_t slot;
            while (slot_i < d->fanout && !bf_bit(bf, bflen, nbytes, slot_i)) slot_i++;
            if (slot_i >= d->fanout) return IPFSN_ERR_MALFORMED;
            if (l.name_len < d->padlen || !l.has_tsize) return IPFSN_ERR_MALFORMED;
            if (prefix_slot(l.name, d->padlen, &slot) || slot != slot_i) return IPFSN_ERR_MALFORMED;
            if (l.name_len == d->padlen) {
                if (l.cid.codec != IPFSN_MC_DAG_PB) return IPFSN_ERR_MALFORMED;
                d->shards++;
            } else {
                if (!ipfsn_dir_name_ok(l.name + d->padlen, l.name_len - d->padlen))
                    return IPFSN_ERR_MALFORMED;
                d->entries++;
            }
            slot_i++;
        }
        if (r != IPFSN_ERR_NOTFOUND) return IPFSN_ERR_MALFORMED;
    }
    return IPFSN_OK;
}

/* ---- fetching --------------------------------------------------------------- */

static int fetch(const ipfsn_cid_t *cid, ipfsn_get_fn get, void *ctx, uint8_t *buf, uint32_t cap,
                 uint32_t *len)
{
    int r;
    if (cid->codec != IPFSN_MC_DAG_PB) return IPFSN_ERR_UNSUPP; /* raw is a file, not a dir */
    if (cid->mh_code == IPFSN_MH_IDENTITY) {
        if (cid->digest_len > cap) return IPFSN_ERR_SPACE;
        ipfsn__cpy(buf, cid->digest, cid->digest_len);
        *len = cid->digest_len;
        return IPFSN_OK;
    }
    *len = 0;
    if ((r = get(ctx, cid, buf, cap, len)) != 0) return r < 0 ? r : IPFSN_ERR_NOTFOUND;
    if (*len > cap) return IPFSN_ERR_SPACE;
    return ipfsn_cid_verify(cid, buf, *len);
}

/* ---- list -------------------------------------------------------------------- */

static bool path_matches(const ipfsn_dir_walk_t *w, uint32_t depth, uint32_t bits,
                         const uint8_t *name, uint32_t nlen, uint32_t slot)
{
    uint64_t h = ipfsn_murmur3_64(name, nlen);
    uint32_t s;
    for (uint32_t d = 1; d <= depth; d++) {
        if (hamt_slot(h, bits, d - 1u, &s) || s != w->f[d].slot) return false;
    }
    if (hamt_slot(h, bits, depth, &s) || s != slot) return false;
    return true;
}

int ipfsn_dir_list(ipfsn_dir_walk_t *w, const ipfsn_cid_t *root, ipfsn_get_fn get, void *gctx,
                   uint8_t *scratch, uint32_t cap, ipfsn_dirent_fn cb, void *cbctx, uint32_t *count)
{
    uint32_t used = 0, depth = 0, len, emitted = 0;
    ipfsn_pblink_t l;
    ipfsn_dirent_t e;
    int r;
    if (!w || !root || !get || !scratch || !cb) return IPFSN_ERR_ARG;
    if (count) *count = 0;
    if ((r = fetch(root, get, gctx, scratch, cap, &len)) != 0) return r;
    if ((r = ipfsn_dir_parse(scratch, len, &w->f[0].node)) != 0) return r;
    w->f[0].block = scratch;
    w->f[0].len = len;
    w->f[0].pos = 0;
    w->f[0].slot = 0;
    used = len;
    for (;;) {
        ipfsn_dir_frame_t *F = &w->f[depth];
        r = ipfsn_dagpb_next_link(&F->node.pb, &F->pos, &l);
        if (r == IPFSN_ERR_NOTFOUND) {
            if (depth == 0) break;
            used -= F->len; /* pop: this level's block is done */
            depth--;
            continue;
        }
        if (r) return IPFSN_ERR_MALFORMED;
        if (F->node.kind == IPFSN_UFS_DIR) {
            ipfsn__cid_copy(&e.cid, &l.cid);
            e.name = l.name;
            e.name_len = l.name_len;
            e.tsize = l.tsize;
        } else {
            uint32_t slot, p = F->node.padlen;
            if (prefix_slot(l.name, p, &slot)) return IPFSN_ERR_MALFORMED;
            if (l.name_len == p) { /* sub-shard: push */
                ipfsn_dir_frame_t *C;
                if (depth + 1u >= IPFSN_HAMT_MAX_LEVELS || (depth + 2u) * F->node.bits > 64u)
                    return IPFSN_ERR_DEPTH;
                C = &w->f[depth + 1u];
                if ((r = fetch(&l.cid, get, gctx, scratch + used, cap - used, &len)) != 0) return r;
                if ((r = ipfsn_dir_parse(scratch + used, len, &C->node)) != 0)
                    return r == IPFSN_ERR_UNSUPP ? IPFSN_ERR_MALFORMED : r;
                if (C->node.kind != IPFSN_UFS_HAMT || C->node.fanout != F->node.fanout)
                    return IPFSN_ERR_MALFORMED;
                C->block = scratch + used;
                C->len = len;
                C->pos = 0;
                C->slot = slot;
                used += len;
                depth++;
                continue;
            }
            if (!path_matches(w, depth, F->node.bits, l.name + p, l.name_len - p, slot))
                return IPFSN_ERR_MALFORMED; /* filed under a slot its hash does not give */
            ipfsn__cid_copy(&e.cid, &l.cid);
            e.name = l.name + p;
            e.name_len = l.name_len - p;
            e.tsize = l.tsize;
        }
        emitted++;
        if (count) *count = emitted;
        if ((r = cb(cbctx, &e)) != 0) return r;
    }
    if (count) *count = emitted;
    return IPFSN_OK;
}

/* ---- lookup ------------------------------------------------------------------- */

int ipfsn_dir_lookup(const ipfsn_cid_t *root, ipfsn_get_fn get, void *gctx, const uint8_t *name,
                     uint32_t name_len, uint8_t *buf, uint32_t cap, ipfsn_dirent_t *out)
{
    ipfsn_dirnode_t d;
    ipfsn_pblink_t l;
    ipfsn_cid_t cur;
    uint32_t len, pos, depth = 0, fanout = 0;
    uint64_t h = 0;
    int r;
    if (!root || !get || !name || !buf || !out) return IPFSN_ERR_ARG;
    if (!ipfsn_dir_name_ok(name, name_len)) return IPFSN_ERR_NOTFOUND; /* cannot exist */
    ipfsn__cid_copy(&cur, root);
    for (;;) {
        bool descended = false;
        if ((r = fetch(&cur, get, gctx, buf, cap, &len)) != 0) return r;
        if ((r = ipfsn_dir_parse(buf, len, &d)) != 0)
            return (r == IPFSN_ERR_UNSUPP && depth) ? IPFSN_ERR_MALFORMED : r;
        pos = 0;
        if (d.kind == IPFSN_UFS_DIR) {
            if (depth) return IPFSN_ERR_MALFORMED; /* a plain dir inside a HAMT */
            while ((r = ipfsn_dagpb_next_link(&d.pb, &pos, &l)) == IPFSN_OK) {
                int c = name_cmp(l.name, l.name_len, name, name_len);
                if (c == 0) {
                    ipfsn__cid_copy(&out->cid, &l.cid);
                    out->name = name;
                    out->name_len = name_len;
                    out->tsize = l.tsize;
                    return IPFSN_OK;
                }
                if (c > 0) break; /* sorted: it is not here */
            }
            return r == IPFSN_OK || r == IPFSN_ERR_NOTFOUND ? IPFSN_ERR_NOTFOUND : r;
        }
        if (depth == 0) {
            fanout = d.fanout;
            h = ipfsn_murmur3_64(name, name_len);
        } else if (d.fanout != fanout) {
            return IPFSN_ERR_MALFORMED;
        }
        {
            uint32_t want, slot;
            if ((r = hamt_slot(h, d.bits, depth, &want)) != 0) return r;
            while ((r = ipfsn_dagpb_next_link(&d.pb, &pos, &l)) == IPFSN_OK) {
                if (prefix_slot(l.name, d.padlen, &slot)) return IPFSN_ERR_MALFORMED;
                if (slot < want) continue;
                if (slot > want) break;
                if (l.name_len == d.padlen) { /* sub-shard on our path */
                    ipfsn__cid_copy(&cur, &l.cid);
                    descended = true;
                    break;
                }
                if (name_cmp(l.name + d.padlen, l.name_len - d.padlen, name, name_len) == 0) {
                    ipfsn__cid_copy(&out->cid, &l.cid);
                    out->name = name;
                    out->name_len = name_len;
                    out->tsize = l.tsize;
                    return IPFSN_OK;
                }
                return IPFSN_ERR_NOTFOUND; /* another name holds our slot */
            }
            if (r != IPFSN_OK && r != IPFSN_ERR_NOTFOUND) return r;
        }
        if (!descended) return IPFSN_ERR_NOTFOUND;
        depth++;
        if (depth >= IPFSN_HAMT_MAX_LEVELS) return IPFSN_ERR_DEPTH;
    }
}

int ipfsn_dir_resolve(const ipfsn_cid_t *root, ipfsn_get_fn get, void *gctx, const char *path,
                      uint32_t path_len, uint8_t *buf, uint32_t cap, ipfsn_dirent_t *out)
{
    ipfsn_cid_t cur;
    uint32_t i = 0;
    int r;
    if (!root || !get || !path || !buf || !out || path_len == 0) return IPFSN_ERR_ARG;
    ipfsn__cid_copy(&cur, root);
    while (i < path_len) {
        uint32_t j = i;
        while (j < path_len && path[j] != '/') j++;
        if (j == i) return IPFSN_ERR_ARG;
        if (!ipfsn_dir_name_ok((const uint8_t *) path + i, j - i)) return IPFSN_ERR_ARG;
        if (j < path_len && j + 1u == path_len) return IPFSN_ERR_ARG; /* trailing '/' */
        r = ipfsn_dir_lookup(&cur, get, gctx, (const uint8_t *) path + i, j - i, buf, cap, out);
        if (r) return r;
        ipfsn__cid_copy(&cur, &out->cid);
        i = j + 1u;
    }
    out->name = (const uint8_t *) path;
    out->name_len = path_len;
    return IPFSN_OK;
}
