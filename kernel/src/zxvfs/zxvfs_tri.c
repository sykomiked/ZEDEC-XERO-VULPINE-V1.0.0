/* zxvfs_tri.c — Tri-Space native storage over ZXVFS. See zxvfs_tri.h.
 *
 * Freestanding: integer only, no libc, no allocation. Digests via the kernel's
 * own SHA-256; the five hard requirements via trispace.c (never duplicated).
 *
 * Author: H.M. Michael-Laurence: Curzi (c)  (ZXV Tri-Space storage slice)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#include "zxvfs_tri.h"
#include "../robin_debanks/sha256.h"

/* ---- local freestanding helpers (mirroring zxvfs.c's house style) ---- */
static void tmemset(void *d, int c, unsigned long n) {
    unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c;
}
static void tmemcpy(void *d, const void *s, unsigned long n) {
    unsigned char *a = (unsigned char *)d; const unsigned char *b = (const unsigned char *)s;
    while (n--) *a++ = *b++;
}
static int tmemeq(const void *a, const void *b, unsigned long n) {
    const unsigned char *x = (const unsigned char *)a, *y = (const unsigned char *)b;
    unsigned char d = 0; while (n--) d |= (unsigned char)(*x++ ^ *y++);
    return d == 0;
}
static uint32_t tstrlen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }

/* Build "<name><suffix>" into out[ZXVFS_NAME_LEN]. Returns 0 on success. */
static int make_name(char *out, const char *name, const char *suffix) {
    uint32_t n = tstrlen(name), s = tstrlen(suffix), i;
    if (n == 0 || n > ZXVFS_TRI_NAME_MAX) return -1;
    if (n + s + 1 > ZXVFS_NAME_LEN) return -1;
    for (i = 0; i < n; i++) out[i] = name[i];
    for (i = 0; i < s; i++) out[n + i] = suffix[i];
    out[n + s] = '\0';
    return 0;
}

/* Role payload suffixes are the canonical COMPILED extensions from trispace,
 * so what is on disk matches what the packaging layer calls these artifacts. */
static const char *role_suffix(tri_role_t r) { return tri_compiled_extension(r); }
#define TRI_DESC_SUFFIX ".tri"

static uint32_t desc_checksum(const zxvfs_tri_desc_t *d) {
    /* FNV-1a over every byte except the trailing checksum field itself. */
    const uint8_t *p = (const uint8_t *)d;
    unsigned long n = sizeof(*d) - sizeof(d->checksum);
    uint32_t h = 2166136261u;
    for (unsigned long i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h ? h : 1u;   /* never 0, so a zeroed sector cannot look valid */
}

/* Populate a tri_triad_t from a spec's metadata + the supplied digests, then
 * run trispace's binder. Returns tri_bind()'s verdict; `t` carries the reason. */
static bool bind_triad(tri_triad_t *t, const zxvfs_tri_spec_t *spec,
                       uint8_t digest[3][TRI_DIGEST_LEN]) {
    tri_init(t, spec->triad_id, spec->source_graph_digest);
    t->inverse_kind = spec->inverse_kind;
    t->effect_is_irreversible = spec->effect_is_irreversible;
    for (int r = 0; r < 3; r++)
        tri_set_member(t, (tri_role_t)r, digest[r], spec->capability_set[r],
                       spec->generated[r], spec->claims_proven_inverse[r]);
    return tri_bind(t);
}

/* Rebuild the triad structure from a stored descriptor (for re-verification). */
static void triad_from_desc(tri_triad_t *t, const zxvfs_tri_desc_t *d) {
    tri_init(t, d->triad_id, d->source_graph_digest);
    t->inverse_kind = (tri_inverse_kind_t)d->inverse_kind;
    t->effect_is_irreversible = d->effect_is_irreversible ? true : false;
    for (int r = 0; r < 3; r++)
        tri_set_member(t, (tri_role_t)r, d->digest[r], d->capability_set[r],
                       d->generated[r] ? true : false,
                       d->claims_proven_inverse[r] ? true : false);
}

static int rule_err(tri_quarantine_t q) {
    return -(int)(ZXVFS_TRI_ERR_RULE_BASE + (int)q);
}

/* Read a descriptor. Returns 0 if a structurally valid one exists. */
static int load_desc(zxvfs_t *fs, const char *name, zxvfs_tri_desc_t *out) {
    char dn[ZXVFS_NAME_LEN];
    if (make_name(dn, name, TRI_DESC_SUFFIX) != 0) return -1;
    int n = zxvfs_read(fs, dn, (uint8_t *)out, (uint32_t)sizeof(*out));
    if (n != (int)sizeof(*out)) return -2;
    if (out->magic != ZXVFS_TRI_MAGIC) return -3;
    if (out->version != ZXVFS_TRI_VERSION) return -3;
    if (out->checksum != desc_checksum(out)) return -4;
    return 0;
}

static int store_desc(zxvfs_t *fs, const char *name, zxvfs_tri_desc_t *d) {
    char dn[ZXVFS_NAME_LEN];
    if (make_name(dn, name, TRI_DESC_SUFFIX) != 0) return -1;
    d->checksum = desc_checksum(d);
    return zxvfs_write(fs, dn, (const uint8_t *)d, (uint32_t)sizeof(*d));
}

/* Mark an existing triad quarantined ON DISK so it stays refused across
 * reboots. Best-effort: if the descriptor cannot be rewritten we still refuse. */
static void quarantine_on_disk(zxvfs_t *fs, const char *name,
                               zxvfs_tri_desc_t *d, tri_quarantine_t why) {
    d->state = ZXVFS_TRI_QUARANTINED;
    d->quarantine = (uint32_t)why;
    (void)store_desc(fs, name, d);
}

/* ---------------------------------------------------------------- write --- */
int zxvfs_tri_write(zxvfs_t *fs, const char *name, const zxvfs_tri_spec_t *spec) {
    char rn[ZXVFS_NAME_LEN];
    uint8_t digest[3][TRI_DIGEST_LEN];
    tri_triad_t t;

    if (!fs || !fs->mounted || !name || !spec) return -1;
    if (tstrlen(name) == 0 || tstrlen(name) > ZXVFS_TRI_NAME_MAX) return -2;

    /* Requirement 1 is checked before ANY disk contact: a triad missing a
     * member must not leave a half-written trail behind. */
    for (int r = 0; r < 3; r++) {
        if (!spec->data[r]) return rule_err(TRI_Q_MISSING_MEMBER);
        if (spec->len[r] > ZXVFS_FILE_MAX_BYTES) return -3;
    }

    for (int r = 0; r < 3; r++)
        sha256(spec->data[r], spec->len[r], digest[r]);

    /* Requirements 2-5, enforced by trispace.c — the single source of truth. */
    if (!bind_triad(&t, spec, digest))
        return rule_err(t.quarantine);       /* nothing written; FS untouched */

    /* Role payloads first. They are inert until a descriptor names them. */
    for (int r = 0; r < 3; r++) {
        if (make_name(rn, name, role_suffix((tri_role_t)r)) != 0) return -2;
        int w = zxvfs_write(fs, rn, spec->data[r], spec->len[r]);
        if (w != 0) return -4;               /* still no descriptor => no triad */
    }

    /* The descriptor LAST — this single atomic write is what makes the triad
     * observable, and is why no crash can expose an S+ without its S-. */
    zxvfs_tri_desc_t d;
    tmemset(&d, 0, sizeof(d));
    d.magic = ZXVFS_TRI_MAGIC;
    d.version = ZXVFS_TRI_VERSION;
    d.state = ZXVFS_TRI_BOUND;
    d.quarantine = (uint32_t)TRI_Q_NONE;
    d.inverse_kind = (uint32_t)spec->inverse_kind;
    d.effect_is_irreversible = spec->effect_is_irreversible ? 1u : 0u;
    tmemcpy(d.triad_id, spec->triad_id, TRI_ID_LEN);
    tmemcpy(d.source_graph_digest, spec->source_graph_digest, TRI_DIGEST_LEN);
    for (int r = 0; r < 3; r++) {
        d.size[r] = spec->len[r];
        tmemcpy(d.digest[r], digest[r], TRI_DIGEST_LEN);
        d.capability_set[r] = spec->capability_set[r];
        d.generated[r] = spec->generated[r] ? 1u : 0u;
        d.claims_proven_inverse[r] = spec->claims_proven_inverse[r] ? 1u : 0u;
    }
    tmemcpy(d.seal, t.seal, TRI_DIGEST_LEN);

    return store_desc(fs, name, &d) == 0 ? 0 : -5;
}

/* ----------------------------------------------------------------- open --- */
int zxvfs_tri_open(zxvfs_t *fs, const char *name, zxvfs_tri_desc_t *out) {
    zxvfs_tri_desc_t d;
    static uint8_t buf[ZXVFS_FILE_MAX_BYTES];
    char rn[ZXVFS_NAME_LEN];
    tri_triad_t t;

    if (!fs || !fs->mounted || !name) return -1;
    if (load_desc(fs, name, &d) != 0) return -2;          /* absent/corrupt */

    /* A triad quarantined earlier stays refused — the verdict is durable. */
    if (d.state == ZXVFS_TRI_QUARANTINED) return rule_err((tri_quarantine_t)d.quarantine);
    if (d.state != ZXVFS_TRI_BOUND) return -2;

    /* Every role must still be present, the right size, and hash to exactly
     * what was sealed. This is what catches a role edited underneath us, and
     * what catches a crashed rewrite (new bytes, old descriptor). */
    for (int r = 0; r < 3; r++) {
        if (make_name(rn, name, role_suffix((tri_role_t)r)) != 0) return -1;
        int n = zxvfs_read(fs, rn, buf, (uint32_t)sizeof(buf));
        if (n < 0) {                          /* the role is genuinely gone   */
            quarantine_on_disk(fs, name, &d, TRI_Q_MISSING_MEMBER);
            return rule_err(TRI_Q_MISSING_MEMBER);
        }
        if ((uint32_t)n != d.size[r]) {       /* present but a different size */
            quarantine_on_disk(fs, name, &d, TRI_Q_SEAL_MISMATCH);
            return rule_err(TRI_Q_SEAL_MISMATCH);
        }
        uint8_t got[TRI_DIGEST_LEN];
        sha256(buf, (uint32_t)n, got);
        if (!tmemeq(got, d.digest[r], TRI_DIGEST_LEN)) {
            quarantine_on_disk(fs, name, &d, TRI_Q_SEAL_MISMATCH);
            return rule_err(TRI_Q_SEAL_MISMATCH);
        }
    }

    /* Re-derive the seal from the descriptor's own metadata and compare, so a
     * descriptor whose fields were edited (caps, inverse kind) is rejected too. */
    triad_from_desc(&t, &d);
    tmemcpy(t.seal, d.seal, TRI_DIGEST_LEN);
    t.bound = true;
    if (!tri_verify(&t)) {
        quarantine_on_disk(fs, name, &d, TRI_Q_SEAL_MISMATCH);
        return rule_err(TRI_Q_SEAL_MISMATCH);
    }

    if (out) tmemcpy(out, &d, sizeof(d));
    return 0;
}

/* ------------------------------------------------------------ read role --- */
int zxvfs_tri_read_role(zxvfs_t *fs, const char *name, tri_role_t role,
                        uint8_t *buf, uint32_t max) {
    char rn[ZXVFS_NAME_LEN];
    if (!fs || !name || !buf) return -1;
    if ((int)role < 0 || (int)role > 2) return -1;
    int rc = zxvfs_tri_open(fs, name, 0);      /* full verification gate */
    if (rc != 0) return rc;
    if (make_name(rn, name, role_suffix(role)) != 0) return -1;
    return zxvfs_read(fs, rn, buf, max);
}

/* ---------------------------------------------------------------- state --- */
zxvfs_tri_state_t zxvfs_tri_state(zxvfs_t *fs, const char *name) {
    zxvfs_tri_desc_t d;
    if (!fs || !name) return ZXVFS_TRI_ABSENT;
    if (load_desc(fs, name, &d) != 0) return ZXVFS_TRI_ABSENT;
    if (d.state == ZXVFS_TRI_BOUND) return ZXVFS_TRI_BOUND;
    if (d.state == ZXVFS_TRI_QUARANTINED) return ZXVFS_TRI_QUARANTINED;
    return ZXVFS_TRI_ABSENT;
}

/* --------------------------------------------------------------- unlink --- */
int zxvfs_tri_unlink(zxvfs_t *fs, const char *name) {
    char n[ZXVFS_NAME_LEN];
    if (!fs || !fs->mounted || !name) return -1;

    /* Descriptor first: once it is gone the triad is no longer observable, so a
     * crash after this point leaves orphan role files (harmless, reclaimable)
     * rather than a descriptor pointing at payloads that no longer exist. */
    if (make_name(n, name, TRI_DESC_SUFFIX) != 0) return -1;
    if (zxvfs_unlink(fs, n) != 0) return -2;

    for (int r = 0; r < 3; r++) {
        if (make_name(n, name, role_suffix((tri_role_t)r)) != 0) return -1;
        (void)zxvfs_unlink(fs, n);          /* best-effort; may already be gone */
    }
    return 0;
}
