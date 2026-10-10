/* update.h — decentralized, opt-in, content-addressed updates
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 *
 * WHAT THIS IS
 * -----------
 * An update system with no central server and no forced installs. Anyone — the
 * maintainers or any developer — can PUBLISH an update: a content-addressed
 * blob (its address IS the SHA-256 of its content, IPFS-style) signed by the
 * publisher's key. Updates are fetched over a peer-to-peer transport (ZXV's
 * `decent` bitswap/swarm) and the user chooses, per update, whether to install
 * it. Nothing is installed that the user did not opt into. Updates can be
 * grouped into BUNDLES that a user opts into as a set.
 *
 * THE THREE GUARANTEES
 * --------------------
 *  1. OPT-IN BY DEFAULT. The install plan is empty until the user SELECTS
 *     something. An update is never applied because it merely exists.
 *  2. SELF-CERTIFYING CONTENT. An update's address is the hash of its bytes, so
 *     fetched content that does not hash to the address it was requested by is
 *     rejected — you cannot be handed different bytes than you asked for, no
 *     matter which peer served them.
 *  3. TRUSTED AUTHORSHIP. An update carries the publisher's signature over its
 *     COMPLETE manifest: zxp_manifest_digest(ZXP_DOMAIN_UPDATE, {id, version,
 *     arch, deps, cid}) from src/provenance/zx_provenance.h (canonical,
 *     length-prefixed SHA-256). Changing the version, architecture, any
 *     dependency or the CID breaks the signature, so an old signed blob cannot
 *     be re-listed as a newer version or for another machine. Versions are
 *     strictly monotonic per id: a re-publish at a version <= the one already
 *     known is refused (UPD_ERR_ROLLBACK). The user chooses which publisher
 *     keys to trust; an
 *     update from an untrusted key, or with a bad signature, is refused.
 *     Decentralized publishing does NOT mean trusting everyone — it means the
 *     user, not a store, decides whom to trust.
 *
 * This module is the POLICY: the catalog, selection, bundle expansion,
 * dependency resolution, and the verify gate. The P2P fetch is an ops boundary
 * (decent), and the A/B staging/rollback is src/loader/abupdate — both are
 * composed, not reimplemented, and both fail closed when absent.
 *
 * Freestanding: integer only, no allocation. Digests via kernel SHA-256.
 */
#ifndef ZXV_UPDATE_H
#define ZXV_UPDATE_H

#include <stdint.h>
#include <stdbool.h>

#define UPD_MAX          64u
#define UPD_MAX_BUNDLES  16u
#define UPD_MAX_DEPS     8u
#define UPD_MAX_BUNDLE   24u
#define UPD_MAX_TRUSTED  16u
#define UPD_ID_LEN       32u
#define UPD_NAME_LEN     48u
#define UPD_CID_LEN      32u
#define UPD_KEY_LEN      32u
#define UPD_SIG_LEN      64u
#define UPD_ARCH_LEN     16u

typedef enum {
    UPD_AVAILABLE = 0,  /* published, known                                   */
    UPD_SELECTED,       /* the user opted in                                  */
    UPD_VERIFIED,       /* fetched + content-address + signature all checked  */
    UPD_INSTALLED,      /* staged/committed                                   */
    UPD_REJECTED        /* failed verification                                */
} upd_state_t;

typedef enum {
    UPD_OK = 0,
    UPD_ERR_FULL, /* catalog full                                    */
    UPD_ERR_NOT_FOUND,
    UPD_ERR_NO_TRANSPORT, /* no P2P fetch backend bound                      */
    UPD_ERR_FETCH,        /* the transport could not retrieve the content    */
    UPD_ERR_CID_MISMATCH, /* fetched bytes do not hash to the requested CID  */
    UPD_ERR_UNTRUSTED,    /* publisher key is not in the user's trust set     */
    UPD_ERR_BAD_SIG,      /* signature does not verify                        */
    UPD_ERR_MISSING_DEP,  /* a required update was not selected/available     */
    UPD_ERR_CYCLE,        /* dependency cycle                                 */
    UPD_ERR_ROLLBACK,     /* version not above the one already known/installed */
    UPD_ERR_BAD_MANIFEST  /* missing id/arch, or a field too long to sign     */
} upd_result_t;

typedef struct {
    bool     in_use;
    char     id[UPD_ID_LEN];
    char     name[UPD_NAME_LEN];
    uint32_t version;
    char arch[UPD_ARCH_LEN];                   /* "aarch64", "x86_64", "any"   */
    uint8_t  cid[UPD_CID_LEN];                 /* content address (= sha256)  */
    uint8_t  author[UPD_KEY_LEN];              /* publisher pubkey            */
    uint8_t sig[UPD_SIG_LEN];                  /* sig over upd_manifest_digest */
    uint32_t size;
    char     dep[UPD_MAX_DEPS][UPD_ID_LEN];
    uint32_t n_deps;
    bool     selected;
    bool     installed;
    upd_state_t state;
} upd_entry_t;

typedef struct {
    bool     in_use;
    char     id[UPD_ID_LEN];
    char     name[UPD_NAME_LEN];
    char     member[UPD_MAX_BUNDLE][UPD_ID_LEN];
    uint32_t n_members;
} upd_bundle_t;

/* content-addressed P2P fetch (decent bitswap/swarm). Returns 0 on success and
 * fills *out_len. With no backend, every fetch fails — content is never
 * invented. */
typedef struct {
    int (*fetch)(const uint8_t cid[UPD_CID_LEN], uint8_t *buf, uint32_t cap,
                 uint32_t *out_len, void *ctx);
    void *ctx;
} upd_transport_t;

/* Ed25519 verify hook (the kernel's ed25519_verify). */
typedef bool (*upd_verify_fn)(const uint8_t *msg, uint32_t len,
                              const uint8_t sig[64], const uint8_t pubkey[32]);

typedef struct {
    upd_entry_t  upd[UPD_MAX];       uint32_t n;
    upd_bundle_t bundle[UPD_MAX_BUNDLES]; uint32_t n_bundles;
    uint8_t      trusted[UPD_MAX_TRUSTED][UPD_KEY_LEN]; uint32_t n_trusted;
    upd_transport_t transport;
    upd_verify_fn   verify;
} upd_catalog_t;

void upd_init(upd_catalog_t *c);
void upd_set_transport(upd_catalog_t *c, const upd_transport_t *t);
void upd_set_verifier(upd_catalog_t *c, upd_verify_fn fn);

/* The user chooses whom to trust. An update from an untrusted author is never
 * applied, however valid its signature. */
bool upd_trust_author(upd_catalog_t *c, const uint8_t pubkey[UPD_KEY_LEN]);
bool upd_is_trusted(const upd_catalog_t *c, const uint8_t pubkey[UPD_KEY_LEN]);

/* Publish an available update (anyone may, decentralized). deps is an array of
 * id strings this update requires; arch names the machine it is for ("any" if
 * none). Returns UPD_OK, or an error. A re-publish of a known id must carry a
 * strictly higher version (UPD_ERR_ROLLBACK otherwise), except an identical
 * re-publish (same version, CID and author), which only refreshes the entry. The
 * id, arch and deps must fit their fields exactly (UPD_ERR_BAD_MANIFEST): a
 * silently truncated field would be signed as something else. */
upd_result_t upd_publish(upd_catalog_t *c, const char *id, const char *name, uint32_t version,
                         const char *arch, const uint8_t cid[UPD_CID_LEN],
                         const uint8_t author[UPD_KEY_LEN], const uint8_t sig[UPD_SIG_LEN],
                         uint32_t size, const char deps[][UPD_ID_LEN], uint32_t n_deps);

/* Define a bundle of update ids. */
upd_result_t upd_bundle_define(upd_catalog_t *c, const char *id, const char *name,
                               const char members[][UPD_ID_LEN], uint32_t n);

int32_t upd_find(const upd_catalog_t *c, const char *id);

/* Opt in / out. Selecting a bundle selects each of its members. */
bool upd_select(upd_catalog_t *c, const char *id);
bool upd_deselect(upd_catalog_t *c, const char *id);
bool upd_select_bundle(upd_catalog_t *c, const char *bundle_id);
bool upd_is_selected(const upd_catalog_t *c, const char *id);

/* Resolve the ordered install plan from the current selections: dependencies
 * before dependents, already-installed updates excluded, a selection's deps
 * auto-included. Writes plan indices to `plan` (cap entries). Returns UPD_OK
 * and sets *n; UPD_ERR_MISSING_DEP / UPD_ERR_CYCLE on failure. With nothing
 * selected, *n is 0 — the opt-in guarantee. */
upd_result_t upd_resolve(const upd_catalog_t *c, int32_t *plan, uint32_t cap, uint32_t *n);

/* The 32 bytes a publisher signs for an entry (and upd_fetch_verify checks):
 * zxp_manifest_digest(ZXP_DOMAIN_UPDATE, {id, version, arch, deps, cid}).
 * Returns false on a malformed entry. */
bool upd_manifest_digest(const upd_entry_t *e, uint8_t out[32]);

/* Fetch one update over the transport and VERIFY it: the fetched bytes must
 * hash to its CID (self-certifying), its author must be trusted, and its
 * signature over the complete manifest (upd_manifest_digest) must verify. On UPD_OK, `buf` holds
 * the content and the entry moves to UPD_VERIFIED. Does NOT install — staging is the caller's A/B
 * step. */
upd_result_t upd_fetch_verify(upd_catalog_t *c, const char *id,
                              uint8_t *buf, uint32_t cap, uint32_t *out_len);

/* Mark a verified update installed (after the caller's A/B stage+commit). */
bool upd_mark_installed(upd_catalog_t *c, const char *id);

const char *upd_strerror(upd_result_t r);

#endif /* ZXV_UPDATE_H */
