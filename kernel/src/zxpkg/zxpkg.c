/* zxpkg.c — the on-disk native package. See zxpkg.h. */
#include "zxpkg.h"
#include "../robin_debanks/sha256.h"
#include "../loader/zsp.h"

static void put32le(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint32_t get32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) { for (uint32_t i=0;i<n;i++) d[i]=s[i]; }
static bool eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i=0;i<n;i++) if (a[i]!=b[i]) return false;
    return true;
}

const char *zxpkg_extension(tri_role_t role) { return tri_compiled_extension(role); }

/* Build a tri_triad_t from already-computed content digests, bind it, and
 * return (quarantine, seal). Shared by seal-from-payloads and verify. */
static tri_quarantine_t bind_from_digests(
        const uint8_t triad_id[TRI_ID_LEN],
        const uint8_t sgd[TRI_DIGEST_LEN],
        tri_inverse_kind_t inverse_kind, bool irreversible,
        const uint8_t digest[3][TRI_DIGEST_LEN],
        const uint32_t capability[3],
        const bool generated[3], const bool proven[3],
        uint8_t seal_out[TRI_DIGEST_LEN]) {
    tri_triad_t t;
    tri_init(&t, triad_id, sgd);
    t.inverse_kind = inverse_kind;
    t.effect_is_irreversible = irreversible;
    for (int r = 0; r < 3; r++)
        tri_set_member(&t, (tri_role_t)r, digest[r], capability[r], generated[r], proven[r]);
    bool ok = tri_bind(&t);
    if (ok) { if (seal_out) cpy(seal_out, t.seal, TRI_DIGEST_LEN); return TRI_Q_NONE; }
    if (seal_out) for (uint32_t i=0;i<TRI_DIGEST_LEN;i++) seal_out[i]=0;
    return t.quarantine;
}

tri_quarantine_t zxpkg_seal(const zxpkg_spec_t *spec, uint8_t seal_out[TRI_DIGEST_LEN]) {
    if (!spec) return TRI_Q_MISSING_MEMBER;
    uint8_t digest[3][TRI_DIGEST_LEN];
    for (int r = 0; r < 3; r++) {
        if (!spec->payload[r] && spec->payload_len[r] != 0) return TRI_Q_MISSING_MEMBER;
        sha256(spec->payload[r] ? spec->payload[r] : (const uint8_t*)"",
               spec->payload_len[r], digest[r]);
    }
    return bind_from_digests(spec->triad_id, spec->source_graph_digest,
                             spec->inverse_kind, spec->irreversible,
                             (const uint8_t (*)[TRI_DIGEST_LEN])digest,
                             spec->capability, spec->generated,
                             spec->claims_proven_inverse, seal_out);
}

uint32_t zxpkg_write(const zxpkg_spec_t *spec, tri_role_t role,
                     const uint8_t seal[TRI_DIGEST_LEN], uint8_t *out, uint32_t cap) {
    if (!spec || (uint32_t)role > 2 || !seal || !out) return 0;
    uint32_t plen = spec->payload_len[role];
    if (cap < ZXPKG_HDR_LEN + plen) return 0;

    uint8_t flags = 0;
    if (spec->generated[role])              flags |= ZXPKG_FLAG_GENERATED;
    if (spec->claims_proven_inverse[role])  flags |= ZXPKG_FLAG_PROVEN_INV;
    if (spec->irreversible)                 flags |= ZXPKG_FLAG_IRREVERSIBLE;

    uint32_t at = 0;
    out[at++]=ZXPKG_MAGIC0; out[at++]=ZXPKG_MAGIC1; out[at++]=ZXPKG_MAGIC2; out[at++]=ZXPKG_MAGIC3;
    out[at++]=(uint8_t)ZXPKG_SCHEMA; out[at++]=(uint8_t)(ZXPKG_SCHEMA>>8);
    out[at++]=(uint8_t)role;
    out[at++]=flags;
    out[at++]=(uint8_t)spec->inverse_kind;
    out[at++]=0; out[at++]=0; out[at++]=0;          /* pad */
    put32le(out+at, spec->capability[role]); at+=4;
    put32le(out+at, plen); at+=4;
    cpy(out+at, spec->triad_id, TRI_ID_LEN); at+=TRI_ID_LEN;
    cpy(out+at, spec->source_graph_digest, TRI_DIGEST_LEN); at+=TRI_DIGEST_LEN;
    sha256(spec->payload[role] ? spec->payload[role] : (const uint8_t*)"", plen, out+at);
    at+=TRI_DIGEST_LEN;                              /* content_digest */
    cpy(out+at, seal, TRI_DIGEST_LEN); at+=TRI_DIGEST_LEN;
    /* at == ZXPKG_HDR_LEN */
    if (spec->payload[role] && plen) cpy(out+at, spec->payload[role], plen);
    at += plen;
    return at;
}

bool zxpkg_read(const uint8_t *buf, uint32_t len, zxpkg_member_t *out) {
    if (!buf || !out || len < ZXPKG_HDR_LEN) return false;
    if (buf[0]!=ZXPKG_MAGIC0||buf[1]!=ZXPKG_MAGIC1||buf[2]!=ZXPKG_MAGIC2||buf[3]!=ZXPKG_MAGIC3)
        return false;
    uint32_t at = 4;
    out->schema = (uint32_t)buf[at] | ((uint32_t)buf[at+1]<<8); at+=2;
    uint8_t role = buf[at++];
    if (role > 2) return false;
    out->role = (tri_role_t)role;
    uint8_t flags = buf[at++];
    out->generated             = (flags & ZXPKG_FLAG_GENERATED) != 0;
    out->claims_proven_inverse = (flags & ZXPKG_FLAG_PROVEN_INV) != 0;
    out->irreversible          = (flags & ZXPKG_FLAG_IRREVERSIBLE) != 0;
    out->inverse_kind = (tri_inverse_kind_t)buf[at++];
    at += 3;                                         /* pad */
    out->capability_set = get32le(buf+at); at+=4;
    out->payload_len = get32le(buf+at); at+=4;
    cpy(out->triad_id, buf+at, TRI_ID_LEN); at+=TRI_ID_LEN;
    cpy(out->source_graph_digest, buf+at, TRI_DIGEST_LEN); at+=TRI_DIGEST_LEN;
    cpy(out->content_digest, buf+at, TRI_DIGEST_LEN); at+=TRI_DIGEST_LEN;
    cpy(out->seal, buf+at, TRI_DIGEST_LEN); at+=TRI_DIGEST_LEN;

    if (len < ZXPKG_HDR_LEN + out->payload_len) return false;   /* truncated */
    out->payload = buf + ZXPKG_HDR_LEN;

    /* verify the payload against its own content digest — detects tampering */
    uint8_t h[TRI_DIGEST_LEN];
    sha256(out->payload_len ? out->payload : (const uint8_t*)"", out->payload_len, h);
    if (!eq(h, out->content_digest, TRI_DIGEST_LEN)) return false;
    return true;
}

tri_quarantine_t zxpkg_verify_triad(const uint8_t *pos, uint32_t pos_len,
                                    const uint8_t *neg, uint32_t neg_len,
                                    const uint8_t *neu, uint32_t neu_len) {
    zxpkg_member_t m[3];
    const uint8_t *bufs[3] = { pos, neg, neu };
    uint32_t lens[3] = { pos_len, neg_len, neu_len };

    /* parse + payload-digest check each member */
    for (int i = 0; i < 3; i++)
        if (!zxpkg_read(bufs[i], lens[i], &m[i])) return TRI_Q_SEAL_MISMATCH;

    /* each file must carry the role its header claims, in the expected slot */
    for (int i = 0; i < 3; i++) if ((int)m[i].role != i) return TRI_Q_MISSING_MEMBER;

    /* all three must agree on triad identity and source graph, or a member was
     * lifted from a different build */
    for (int i = 1; i < 3; i++) {
        if (!eq(m[i].triad_id, m[0].triad_id, TRI_ID_LEN)) return TRI_Q_SEAL_MISMATCH;
        if (!eq(m[i].source_graph_digest, m[0].source_graph_digest, TRI_DIGEST_LEN))
            return TRI_Q_SOURCE_MISMATCH;
        if (!eq(m[i].seal, m[0].seal, TRI_DIGEST_LEN)) return TRI_Q_SEAL_MISMATCH;
        if (m[i].inverse_kind != m[0].inverse_kind) return TRI_Q_SEAL_MISMATCH;
        if (m[i].irreversible != m[0].irreversible) return TRI_Q_SEAL_MISMATCH;
    }

    /* rebuild the seal from the three members and compare — this is what makes
     * swapping a single member (a stale S-) detectable */
    uint8_t digest[3][TRI_DIGEST_LEN];
    uint32_t cap[3]; bool gen[3], prov[3];
    for (int i = 0; i < 3; i++) {
        cpy(digest[i], m[i].content_digest, TRI_DIGEST_LEN);
        cap[i] = m[i].capability_set; gen[i] = m[i].generated; prov[i] = m[i].claims_proven_inverse;
    }
    uint8_t seal[TRI_DIGEST_LEN];
    tri_quarantine_t q = bind_from_digests(m[0].triad_id, m[0].source_graph_digest,
                                           m[0].inverse_kind, m[0].irreversible,
                                           (const uint8_t (*)[TRI_DIGEST_LEN])digest,
                                           cap, gen, prov, seal);
    if (q != TRI_Q_NONE) return q;                  /* a requirement now fails  */
    if (!eq(seal, m[0].seal, TRI_DIGEST_LEN)) return TRI_Q_SEAL_MISMATCH;
    return TRI_Q_NONE;                              /* intact and releasable    */
}

const char *zxrel_strerror(zxrel_t r) {
    switch (r) {
    case ZXREL_OK:            return "signed release verified";
    case ZXREL_TRIAD_BAD:     return "triad does not verify";
    case ZXREL_UNSIGNED:      return "no valid signature envelope";
    case ZXREL_BAD_SIG:       return "signature not from the root key";
    case ZXREL_SEAL_MISMATCH: return "signed over a different triad's seal";
    }
    return "unknown";
}

zxrel_t zxpkg_verify_release(const uint8_t *pos, uint32_t pos_len,
                             const uint8_t *neg, uint32_t neg_len,
                             const uint8_t *neu, uint32_t neu_len,
                             const uint8_t *zsp, uint32_t zsp_len,
                             const uint8_t root_pubkey[32]) {
    /* 1. the triad must be intact and releasable */
    if (zxpkg_verify_triad(pos, pos_len, neg, neg_len, neu, neu_len) != TRI_Q_NONE)
        return ZXREL_TRIAD_BAD;

    /* recover the seal every member carries (they agree; verify_triad checked) */
    zxpkg_member_t m;
    if (!zxpkg_read(pos, pos_len, &m)) return ZXREL_TRIAD_BAD;

    /* 2. the ZSP envelope must verify against the root key */
    const uint8_t *payload = 0; uint32_t plen = 0;
    zsp_result_t zr = zsp_verify(zsp, zsp_len, root_pubkey, &payload, &plen);
    if (zr == ZSP_ERR_SIG) return ZXREL_BAD_SIG;
    if (zr != ZSP_OK)      return ZXREL_UNSIGNED;

    /* 3. and it must be a signature over THIS triad's seal, not another's */
    if (plen != TRI_DIGEST_LEN || !eq(payload, m.seal, TRI_DIGEST_LEN))
        return ZXREL_SEAL_MISMATCH;

    return ZXREL_OK;
}

/* ---- DECLARATION -----------------------------------------------------------

 * The package format. All three requirements are measured from zxpkg.o's
 * `nm -u` = {sha256, tri_bind, tri_compiled_extension, tri_init,
 * tri_set_member, zsp_verify}: a digest (sha256_ready), the S+/S0/S- triad
 * binder (trispace_ready, kernel/src/trispace/trispace.c), and the signed
 * package verifier (zsp_verify_ready, kernel/src/loader/zsp.c). Three modules,
 * three directories, none of them called zxpkg.
 *
 * The bring-up checks that the role->extension map discriminates. A packaging
 * layer that gives every role the same extension is the tri-space
 * "packaging-only binding" hole in a different costume.
 */
#include "zxv_decl.h"
static int zxvd_zxpkg_bringup(void) {
    const char *a = zxpkg_extension(TRI_POSITIVE);
    const char *b = zxpkg_extension(TRI_NEGATIVE);
    if (!a || !b) return -1;
    if (a[0] == b[0] && a[1] == b[1]) return -1;   /* roles must differ */
    if (zxrel_strerror(ZXREL_OK) == 0) return -1;
    return 0;
}

ZXV_DECLARE(zxpkg,
    ZXV_PROVIDES(zxpkg_ready),
    ZXV_REQUIRES(sha256_ready, trispace_ready, zsp_verify_ready),
    ZXV_BRINGUP(zxvd_zxpkg_bringup));
