/* ipfsn_fidx.c — the OS file index: path -> CID, and CID -> every path.
 *
 * The OS names files by VFS path (src/vfs, at most VFS_PATH_LEN = 256 bytes
 * with the NUL; zxvfs's flat names sit under their mount point). The index
 * gives every file a content identity on top of that name: two paths with the
 * same bytes share one CID (and one copy in the blockstore), and a CID can be
 * asked "where in the system does this live?".
 *
 * Paths are accepted only in canonical form, so one file cannot hide behind
 * two spellings ("/a//b", "/a/./b", "/a/b/").
 *
 * The CID stored is the file's handle: the plaintext root CID, or for a file
 * added in private-CID mode, the private CID, so the index never writes down
 * an identifier that would match a public copy.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "ipfs_node.h"
#include "ipfsn_util.h"

static int path_check(const char *p, uint32_t *out_len)
{
    uint32_t n;
    if (!p || p[0] != '/') return IPFSN_ERR_ARG;
    n = ipfsn__strlen(p, IPFSN_PATH_MAX);
    if (n >= IPFSN_PATH_MAX) return IPFSN_ERR_ARG;
    if (n > 1 && p[n - 1] == '/') return IPFSN_ERR_ARG;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] != '/') continue;
        uint32_t j = i + 1, c = 0;
        while (j < n && p[j] != '/') j++;
        c = j - i - 1;
        if (c == 0 && n > 1) return IPFSN_ERR_ARG; /* "//" */
        if (c == 1 && p[i + 1] == '.') return IPFSN_ERR_ARG;
        if (c == 2 && p[i + 1] == '.' && p[i + 2] == '.') return IPFSN_ERR_ARG;
    }
    *out_len = n;
    return IPFSN_OK;
}

static int find_path(const ipfsn_fidx_t *x, const char *p, uint32_t n)
{
    for (uint32_t i = 0; i < x->cap; i++)
        if (x->e[i].used && x->e[i].path_len == n && ipfsn__eq(x->e[i].path, p, n)) return (int) i;
    return -1;
}

void ipfsn_fidx_init(ipfsn_fidx_t *x, ipfsn_fidx_entry_t *entries, uint32_t cap)
{
    if (!x) return;
    x->e = entries;
    x->cap = entries ? cap : 0;
    x->count = 0;
    for (uint32_t i = 0; i < x->cap; i++) entries[i].used = 0;
}

int ipfsn_fidx_set(ipfsn_fidx_t *x, const char *path, const ipfsn_cid_t *cid, uint64_t size,
                   uint32_t vis)
{
    uint32_t n;
    int i;
    if (!x || !cid) return IPFSN_ERR_ARG;
    if (vis != IPFSN_VIS_PUBLIC && vis != IPFSN_VIS_PRIVATE) return IPFSN_ERR_ARG;
    if (path_check(path, &n)) return IPFSN_ERR_ARG;
    if ((i = find_path(x, path, n)) < 0) {
        for (uint32_t k = 0; k < x->cap && i < 0; k++)
            if (!x->e[k].used) i = (int) k;
        if (i < 0) return IPFSN_ERR_FULL;
        x->count++;
    }
    ipfsn_fidx_entry_t *e = &x->e[i];
    ipfsn__cpy(e->path, path, n);
    e->path[n] = 0;
    e->path_len = n;
    ipfsn_cid_to_v1(cid, &e->cid);
    e->size = size;
    e->vis = (uint8_t) vis;
    e->used = 1;
    return IPFSN_OK;
}

int ipfsn_fidx_lookup(const ipfsn_fidx_t *x, const char *path, ipfsn_cid_t *cid, uint64_t *size,
                      uint32_t *vis)
{
    uint32_t n;
    int i;
    if (!x || path_check(path, &n)) return IPFSN_ERR_ARG;
    if ((i = find_path(x, path, n)) < 0) return IPFSN_ERR_NOTFOUND;
    if (cid) ipfsn__cid_copy(cid, &x->e[i].cid);
    if (size) *size = x->e[i].size;
    if (vis) *vis = x->e[i].vis;
    return IPFSN_OK;
}

int ipfsn_fidx_remove(ipfsn_fidx_t *x, const char *path)
{
    uint32_t n;
    int i;
    if (!x || path_check(path, &n)) return IPFSN_ERR_ARG;
    if ((i = find_path(x, path, n)) < 0) return IPFSN_ERR_NOTFOUND;
    x->e[i].used = 0;
    x->count--;
    return IPFSN_OK;
}

int ipfsn_fidx_next_path(const ipfsn_fidx_t *x, const ipfsn_cid_t *cid, uint32_t *iter,
                         const char **path)
{
    ipfsn_cid_t k;
    if (!x || !cid || !iter || !path) return IPFSN_ERR_ARG;
    ipfsn_cid_to_v1(cid, &k);
    while (*iter < x->cap) {
        const ipfsn_fidx_entry_t *e = &x->e[(*iter)++];
        if (e->used && ipfsn_cid_equal(&e->cid, &k)) {
            *path = e->path;
            return IPFSN_OK;
        }
    }
    return IPFSN_ERR_NOTFOUND;
}

uint32_t ipfsn_fidx_refs(const ipfsn_fidx_t *x, const ipfsn_cid_t *cid)
{
    uint32_t it = 0, n = 0;
    const char *p;
    while (ipfsn_fidx_next_path(x, cid, &it, &p) == IPFSN_OK) n++;
    return n;
}

uint32_t ipfsn_fidx_unique(const ipfsn_fidx_t *x)
{
    uint32_t n = 0;
    if (!x) return 0;
    for (uint32_t i = 0; i < x->cap; i++) {
        if (!x->e[i].used) continue;
        bool seen = false;
        for (uint32_t j = 0; j < i && !seen; j++)
            if (x->e[j].used && ipfsn_cid_equal(&x->e[j].cid, &x->e[i].cid)) seen = true;
        if (!seen) n++;
    }
    return n;
}
