/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
#include "quest_badge.h"
#include "keccak.h"

static const char badge_domain[] = "ZXV-QUEST-BADGE-v1";
static const char revoke_domain[] = "ZXV-QUEST-REVOKE-v1";
static const char evidence_domain[] = "ZXV-QUEST-EVIDENCE-v1";

static uint32_t put32(uint8_t *o, uint32_t at, uint32_t v)
{
    for (uint32_t i = 0; i < 4; i++) o[at + i] = (uint8_t) (v >> (8 * i));
    return at + 4;
}

static uint32_t get32(const uint8_t *o, uint32_t at)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < 4; i++) v |= (uint32_t) o[at + i] << (8 * i);
    return v;
}

static uint32_t min_verifiers(uint32_t level)
{
    return level < QST_VERIFIER_SET ? level : QST_VERIFIER_SET;
}

bool qst_badge_eligible(const qst_world_t *w, uint32_t subject, qst_skill_t skill, uint32_t level)
{
    const qst_profile_t *p = qst_profile_c(w, subject);
    if (!p || p->opted_out || skill >= QST_SKILL_COUNT) return false;
    if (level == 0 || level >= QST_LEVELS) return false;
    if (qst_level_of(p->points[skill]) < level) return false;
    return p->n_verifiers[skill] >= min_verifiers(level);
}

void qst_evidence_root(const qst_world_t *w, uint32_t subject, qst_skill_t skill, uint8_t out[32])
{
    uint8_t buf[32 + 1 + 32];
    sha3_256((const uint8_t *) evidence_domain, sizeof evidence_domain - 1, out);
    if (!w) return;
    for (uint32_t i = 0; i < w->n_records; i++) {
        const qst_record_t *r = &w->record[i];
        if (!r->used || r->rejected || r->subject != subject || r->skill != skill || r->points == 0)
            continue;
        qst__copy(buf, out, 32);
        buf[32] = r->kind;
        qst__copy(buf + 33, r->evidence, 32);
        sha3_256(buf, sizeof buf, out);
    }
}

void qst_badge_encode(const qst_badge_t *b, uint8_t o[QST_BADGE_ENC_LEN])
{
    uint32_t at = 0;
    o[at++] = b->version;
    o[at++] = b->skill;
    o[at++] = b->level;
    o[at++] = b->is_public ? 1u : 0u;
    at = put32(o, at, b->holder_subject);
    qst__copy(o + at, b->holder_key, 32);
    at += 32;
    qst__copy(o + at, b->issuer_key, 32);
    at += 32;
    qst__copy(o + at, b->evidence_root, 32);
    at += 32;
    at = put32(o, at, b->distinct_verifiers);
    at = put32(o, at, b->season);
    at = put32(o, at, b->issued_day);
    at = put32(o, at, (uint32_t) b->serial);
    put32(o, at, (uint32_t) (b->serial >> 32));
}

bool qst_badge_decode(const uint8_t *in, uint32_t len, qst_badge_t *b)
{
    if (!in || !b || len != QST_BADGE_ENC_LEN) return false;
    if (in[0] != QST_BADGE_VERSION || in[1] >= QST_SKILL_COUNT || in[2] == 0 ||
        in[2] >= QST_LEVELS || in[3] > 1)
        return false;
    uint32_t at = 0;
    b->version = in[at++];
    b->skill = in[at++];
    b->level = in[at++];
    b->is_public = in[at++];
    b->holder_subject = get32(in, at);
    at += 4;
    qst__copy(b->holder_key, in + at, 32);
    at += 32;
    qst__copy(b->issuer_key, in + at, 32);
    at += 32;
    qst__copy(b->evidence_root, in + at, 32);
    at += 32;
    b->distinct_verifiers = get32(in, at);
    b->season = get32(in, at + 4);
    b->issued_day = get32(in, at + 8);
    b->serial = (uint64_t) get32(in, at + 12) | ((uint64_t) get32(in, at + 16) << 32);
    return true;
}

void qst_badge_digest(const qst_badge_t *b, uint8_t out[32])
{
    uint8_t buf[sizeof badge_domain - 1 + QST_BADGE_ENC_LEN];
    qst__copy(buf, badge_domain, sizeof badge_domain - 1);
    qst_badge_encode(b, buf + sizeof badge_domain - 1);
    sha3_256(buf, sizeof buf, out);
}

qst_status_t qst_badge_issue(const qst_world_t *w, uint32_t subject, qst_skill_t skill,
                             uint32_t level, const uint8_t holder_key[32],
                             const uint8_t issuer_key[32], uint32_t day, uint64_t serial,
                             qst_sign_fn sign, void *sign_ctx, qst_badge_t *out, uint8_t *sig,
                             uint32_t sig_cap, uint32_t *sig_len)
{
    if (!w || !holder_key || !issuer_key || !sign || !out || !sig || !sig_len) return QST_ERR_ARG;
    if (!qst_badge_eligible(w, subject, skill, level)) return QST_ERR_NOT_ELIGIBLE;
    const qst_profile_t *p = qst_profile_c(w, subject);
    qst__zero(out, (uint32_t) sizeof *out);
    out->version = QST_BADGE_VERSION;
    out->skill = (uint8_t) skill;
    out->level = (uint8_t) level;
    out->is_public = p->policy.public_badges ? 1u : 0u;
    out->holder_subject = subject;
    qst__copy(out->holder_key, holder_key, 32);
    qst__copy(out->issuer_key, issuer_key, 32);
    qst_evidence_root(w, subject, skill, out->evidence_root);
    out->distinct_verifiers = p->n_verifiers[skill];
    out->season = w->season;
    out->issued_day = day;
    out->serial = serial;
    uint8_t d[32];
    qst_badge_digest(out, d);
    if (!sign(sign_ctx, d, sig, sig_cap, sig_len)) return QST_ERR_STATE;
    return QST_OK;
}

void qst_revocation_digest(const qst_revocation_t *r, uint8_t out[32])
{
    uint8_t buf[sizeof revoke_domain - 1 + QST_REVOKE_ENC_LEN];
    uint32_t at = sizeof revoke_domain - 1;
    qst__copy(buf, revoke_domain, at);
    qst__copy(buf + at, r->badge_digest, 32);
    qst__copy(buf + at + 32, r->issuer_key, 32);
    buf[at + 64] = r->reason;
    put32(buf, at + 65, r->day);
    sha3_256(buf, sizeof buf, out);
}

void qst_revlist_init(qst_revlist_t *l)
{
    if (l) qst__zero(l, (uint32_t) sizeof *l);
}

static bool on_list(const qst_revlist_t *l, const uint8_t digest[32], const uint8_t issuer[32])
{
    if (!l) return false;
    for (uint32_t i = 0; i < l->n; i++)
        if (qst__eq(l->r[i].badge_digest, digest, 32) && qst__eq(l->r[i].issuer_key, issuer, 32))
            return true;
    return false;
}

bool qst_revlist_add(qst_revlist_t *l, const qst_revocation_t *r, const uint8_t *sig,
                     uint32_t sig_len, qst_verify_fn verify, void *verify_ctx)
{
    if (!l || !r || !sig || !verify) return false;
    if (r->reason < QST_REVOKE_FRAUD || r->reason > QST_REVOKE_WITHDRAWN) return false;
    uint8_t d[32];
    qst_revocation_digest(r, d);
    if (!verify(verify_ctx, r->issuer_key, d, sig, sig_len)) return false;
    if (on_list(l, r->badge_digest, r->issuer_key)) return true;
    if (l->n >= QST_MAX_REVOKED) return false;
    qst__copy(&l->r[l->n++], r, (uint32_t) sizeof *r);
    return true;
}

qst_badge_verdict_t qst_badge_check(const qst_badge_t *b, const uint8_t *sig, uint32_t sig_len,
                                    qst_verify_fn verify, void *verify_ctx,
                                    const qst_revlist_t *revoked)
{
    if (!b || !sig || !verify) return QST_BADGE_MALFORMED;
    uint8_t enc[QST_BADGE_ENC_LEN];
    qst_badge_t tmp;
    qst_badge_encode(b, enc);
    if (!qst_badge_decode(enc, sizeof enc, &tmp)) return QST_BADGE_MALFORMED;
    uint8_t d[32];
    qst_badge_digest(b, d);
    if (!verify(verify_ctx, b->issuer_key, d, sig, sig_len)) return QST_BADGE_BAD_SIG;
    if (on_list(revoked, d, b->issuer_key)) return QST_BADGE_REVOKED;
    return QST_BADGE_VALID;
}

bool qst_badge_still_backed(const qst_world_t *w, const qst_badge_t *b)
{
    if (!w || !b || b->skill >= QST_SKILL_COUNT) return false;
    const qst_profile_t *p = qst_profile_c(w, b->holder_subject);
    if (!p) return false;
    /* opting out never un-backs a badge: check the evidence, not the switch */
    return qst_level_of(p->points[b->skill]) >= b->level &&
           p->n_verifiers[b->skill] >= min_verifiers(b->level);
}

int32_t qst_badge_to_reputation(rep_state_t *rep, const qst_badge_t *b, const uint8_t *sig,
                                uint32_t sig_len, qst_verify_fn verify, void *verify_ctx,
                                const qst_revlist_t *revoked)
{
    if (!rep || !b || !b->is_public) return -1;
    if (qst_badge_check(b, sig, sig_len, verify, verify_ctx, revoked) != QST_BADGE_VALID) return -1;
    return badge_award(rep, b->holder_subject, QST_REP_BADGE_BASE + b->skill, b->level);
}
