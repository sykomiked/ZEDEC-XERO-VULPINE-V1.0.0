/* trispace.c — the Tri-Space artifact triad. See trispace.h. */
#include "trispace.h"
#include "../robin_debanks/sha256.h"

static void cpy(uint8_t *d, const uint8_t *s, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static bool eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

void tri_init(tri_triad_t *t, const uint8_t triad_id[TRI_ID_LEN],
              const uint8_t source_graph_digest[TRI_DIGEST_LEN]) {
    if (!t) return;
    for (uint32_t i = 0; i < TRI_ID_LEN; i++)
        t->triad_id[i] = triad_id ? triad_id[i] : 0;
    for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) {
        t->source_graph_digest[i] = source_graph_digest ? source_graph_digest[i] : 0;
        t->seal[i] = 0;
    }
    t->schema_version = TRI_SCHEMA_VER;
    t->inverse_kind = TRI_INV_CONSTRAINING;
    t->effect_is_irreversible = false;
    for (uint32_t r = 0; r < 3; r++) {
        t->member[r].role = (tri_role_t)r;
        t->member[r].present = false;
        t->member[r].capability_set = 0;
        t->member[r].generated = false;
        t->member[r].claims_proven_inverse = false;
        for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) t->member[r].content_digest[i] = 0;
    }
    t->bound = false;
    t->quarantine = TRI_Q_NONE;
}

bool tri_set_member(tri_triad_t *t, tri_role_t role,
                    const uint8_t content_digest[TRI_DIGEST_LEN],
                    uint32_t capability_set, bool generated,
                    bool claims_proven_inverse) {
    if (!t || (uint32_t)role > 2 || !content_digest) return false;
    tri_member_t *m = &t->member[role];
    m->role = role;
    m->present = true;
    cpy(m->content_digest, content_digest, TRI_DIGEST_LEN);
    m->capability_set = capability_set;
    m->generated = generated;
    m->claims_proven_inverse = claims_proven_inverse;
    /* any change invalidates a previous binding */
    t->bound = false;
    return true;
}

/* seal = SHA256( schema || triad_id || source_graph || inverse_kind ||
 *                for each role: role || content_digest || capability_set )
 * Because every member's digest AND capability set is inside the seal, a
 * member from a different build changes the seal — substitution is detected. */
static void compute_seal(const tri_triad_t *t, uint8_t out[TRI_DIGEST_LEN]) {
    uint8_t buf[4 + TRI_ID_LEN + TRI_DIGEST_LEN + 1 + 3 * (1 + TRI_DIGEST_LEN + 4)];
    uint32_t at = 0;
    for (int i = 0; i < 4; i++) buf[at++] = (uint8_t)(t->schema_version >> (8 * i));
    for (uint32_t i = 0; i < TRI_ID_LEN; i++) buf[at++] = t->triad_id[i];
    for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) buf[at++] = t->source_graph_digest[i];
    buf[at++] = (uint8_t)t->inverse_kind;
    for (uint32_t r = 0; r < 3; r++) {
        const tri_member_t *m = &t->member[r];
        buf[at++] = (uint8_t)r;
        for (uint32_t i = 0; i < TRI_DIGEST_LEN; i++) buf[at++] = m->content_digest[i];
        for (int i = 0; i < 4; i++) buf[at++] = (uint8_t)(m->capability_set >> (8 * i));
    }
    sha256(buf, at, out);
}

bool tri_bind(tri_triad_t *t) {
    if (!t) return false;
    t->bound = false;
    t->quarantine = TRI_Q_NONE;

    /* (1) all three members present, or S0 quarantine — never release a
     * partial triad as "just the positive half" */
    for (uint32_t r = 0; r < 3; r++) {
        if (!t->member[r].present) { t->quarantine = TRI_Q_MISSING_MEMBER; return false; }
    }

    const tri_member_t *pos = &t->member[TRI_POSITIVE];
    const tri_member_t *neg = &t->member[TRI_NEGATIVE];
    const tri_member_t *neu = &t->member[TRI_NEUTRAL];

    /* (2) S- must not hold capabilities S+ never had. The undo path is not a
     * back door: every bit of neg must be a subset of pos. */
    if ((neg->capability_set & ~pos->capability_set) != 0) {
        t->quarantine = TRI_Q_NEG_OVER_CAPABLE; return false;
    }

    /* (3) S0 has NO production-effect capability until a signed policy
     * resolves it — the unresolved remainder cannot act on the world. */
    if (neu->capability_set != 0) {
        t->quarantine = TRI_Q_NEUTRAL_HAS_EFFECT; return false;
    }

    /* (4) an irreversible effect cannot claim an exact or restoring inverse */
    if (t->effect_is_irreversible &&
        (t->inverse_kind == TRI_INV_EXACT || t->inverse_kind == TRI_INV_RESTORING)) {
        t->quarantine = TRI_Q_BAD_INVERSE_CLAIM; return false;
    }

    /* (5) generated S- material is a draft, not a proof */
    if (neg->generated && neg->claims_proven_inverse) {
        t->quarantine = TRI_Q_UNPROVEN_INVERSE; return false;
    }

    compute_seal(t, t->seal);
    t->bound = true;
    return true;
}

bool tri_verify(const tri_triad_t *t) {
    if (!t || !t->bound) return false;
    uint8_t expect[TRI_DIGEST_LEN];
    compute_seal(t, expect);
    return eq(expect, t->seal, TRI_DIGEST_LEN);
}

bool tri_may_release(const tri_triad_t *t) {
    return t && t->bound && t->quarantine == TRI_Q_NONE && tri_verify(t);
}

const char *tri_source_extension(tri_role_t role) {
    switch (role) {
    case TRI_POSITIVE: return ".n9n63";
    case TRI_NEGATIVE: return ".9n63";
    case TRI_NEUTRAL:  return ".0n0";
    default:           return "";
    }
}
const char *tri_compiled_extension(tri_role_t role) {
    switch (role) {
    case TRI_POSITIVE: return ".zxvc";
    case TRI_NEGATIVE: return ".cedez";
    case TRI_NEUTRAL:  return ".cedec";
    default:           return "";
    }
}
const char *tri_role_name(tri_role_t role) {
    switch (role) {
    case TRI_POSITIVE: return "S+ positive";
    case TRI_NEGATIVE: return "S- negative";
    case TRI_NEUTRAL:  return "S0 neutral";
    default:           return "?";
    }
}
const char *tri_quarantine_reason(tri_quarantine_t q) {
    switch (q) {
    case TRI_Q_NONE:              return "none";
    case TRI_Q_MISSING_MEMBER:    return "a triad member is missing";
    case TRI_Q_SOURCE_MISMATCH:   return "members come from different source graphs";
    case TRI_Q_NEG_OVER_CAPABLE:  return "S- claims capabilities S+ never had";
    case TRI_Q_NEUTRAL_HAS_EFFECT:return "S0 holds production-effect capability";
    case TRI_Q_BAD_INVERSE_CLAIM: return "irreversible effect claims an exact inverse";
    case TRI_Q_UNPROVEN_INVERSE:  return "generated S- presented as a proven inverse";
    case TRI_Q_SEAL_MISMATCH:     return "a member was substituted or altered";
    case TRI_Q_CAP_MISDECLARED:   return "code carries capabilities it did not declare";
    default:                      return "?";
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * The S+/S0/S- triad binder. zxpkg.o names tri_init / tri_bind /
 * tri_set_member / tri_compiled_extension; they are defined here.
 * REQUIRES sha256_ready from measurement, not from taste: trispace.o's `nm -u`
 * is exactly one symbol, sha256 -- the triad's binding digest.
 */
#include "zxv_decl.h"
ZXV_DECLARE(trispace,
    ZXV_PROVIDES(trispace_ready),
    ZXV_REQUIRES(sha256_ready),
    ZXV_NO_BRINGUP);
