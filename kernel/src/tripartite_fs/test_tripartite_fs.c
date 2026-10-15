/* test_tripartite_fs.c — Tripartite FS: CIDs must be real and verification
 * must hash the content.
 *
 * Regression for the old module, whose "CID" was file->id + i (two files
 * with different bytes but the same id got the same CID), whose wallet
 * sign/verify and self-heal set `verified = true` without looking at any
 * bytes, and whose self-audit was skipped entirely unless auto_heal was on.
 * Each check below fails on that code. The CID vector for "hello world" was
 * computed independently with python hashlib + base64.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "tripartite_fs.h"
#include "../robin_debanks/sha256.h"

static int fails = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            printf("  ok   %s\n", m);                                                              \
        else {                                                                                     \
            printf("  FAIL %s\n", m);                                                              \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static tripartite_fs_t fs;

/* A stand-in signer for the recovery framework (test_recover covers the
 * ML-DSA-87 binding): SHA-256 over a key and the digest. */
static uint8_t mac_key[32] = {9, 9, 9};
static int sign_fail;
static uint32_t mac_sign(void *ctx, const uint8_t msg[32], uint8_t *sig, uint32_t cap)
{
    (void) ctx;
    if (sign_fail || cap < 32) return 0;
    uint8_t b[64];
    memcpy(b, mac_key, 32);
    memcpy(b + 32, msg, 32);
    sha256(b, 64, sig);
    return 32;
}
static bool mac_verify(void *ctx, const uint8_t *pk, uint32_t pk_len, const uint8_t msg[32],
                       const uint8_t *sig, uint32_t sig_len)
{
    (void) ctx;
    if (pk_len != 32 || sig_len != 32) return false;
    uint8_t b[64], m[32];
    memcpy(b, pk, 32);
    memcpy(b + 32, msg, 32);
    sha256(b, 64, m);
    return memcmp(m, sig, 32) == 0;
}

static pdx_rec_t rlog_buf[64];
static lpx_qrec_t rq_buf[64];
static uint32_t riso[4];
static rcv_proof_t rproof;

static int proven(const pdx_log_t *log)
{
    return rcv_proof_verify(&rproof, mac_verify, 0, mac_key, 32) && rcv_proof_in_log(log, &rproof);
}

static void test_recovery(void)
{
    printf("=== tripartite_fs: recovery through the framework (recover.h) ===\n");
    static tripartite_fs_t rfs;
    tf_init(&rfs, NULL, NULL, NULL, NULL, NULL, NULL, NULL);
    const uint8_t body[24] = "tripartite recovery v1!";
    const uint8_t body2[24] = "another file, same size";
    int32_t f = tf_create_file(&rfs, "r.36n9", TF_TYPE_36N9, body, sizeof body, 3);
    int32_t g = tf_create_file(&rfs, "s.36n9", TF_TYPE_36N9, body2, sizeof body2, 3);
    int32_t h = tf_create_file(&rfs, "t.36n9", TF_TYPE_36N9, body2, sizeof body2, 3);
    tf_file_entry_t *fe = tf_get_file(&rfs, (uint32_t) f);
    uint8_t buf[24];
    memcpy(buf, body, sizeof buf);

    CK(tf_recover_file(&rfs, (uint32_t) f, buf, sizeof buf) == TF_ENOTSUP &&
           tf_checkpoint_file(&rfs, (uint32_t) f, buf, sizeof buf) == TF_ENOTSUP,
       "no framework attached: recovery and checkpoints fail closed");
    CK(tf_self_heal_file(&rfs, (uint32_t) f) == TF_ENOTSUP,
       "self-heal without a framework refuses");

    pdx_log_t log;
    lpx_qlog_t q;
    rcv_shadow_t sh;
    rcv_t rc;
    static uint8_t sa[24], sb[24];
    pdx_log_init(&log, rlog_buf, 64);
    lpx_qlog_init(&q, rq_buf, 64);
    rcv_shadow_init(&sh, sa, sb, sizeof sa);
    rcv_init(&rc, &log, &q, &sh, riso, 4, NULL, NULL, mac_sign, NULL);
    CK(tf_attach_recovery(&rfs, NULL, &rproof) == TF_EINVAL &&
           tf_attach_recovery(&rfs, &rc, NULL) == TF_EINVAL &&
           tf_attach_recovery(NULL, &rc, &rproof) == TF_EINVAL,
       "attach refuses NULLs");
    CK(tf_attach_recovery(&rfs, &rc, &rproof) == 0, "framework attached");

    CK(tf_self_heal_file(&rfs, (uint32_t) f) == TF_ENOTSUP && rproof.action == RCV_ACT_REFUSE &&
           rproof.component == (uint32_t) f && proven(&log) && !fe->verified,
       "self-heal: the same refusal, now a signed REFUSE in the operator log");

    uint8_t wrong[24];
    memcpy(wrong, body, sizeof wrong);
    wrong[0] ^= 1;
    uint32_t n0 = log.n;
    CK(tf_checkpoint_file(&rfs, (uint32_t) f, wrong, sizeof wrong) == TF_EMISMATCH && log.n == n0 &&
           sh.good == RCV_NO_SLOT,
       "bytes that do not match the CID are never checkpointed");
    CK(tf_checkpoint_file(&rfs, (uint32_t) f, body, sizeof body - 1) == TF_EMISMATCH,
       "a short payload is never checkpointed");
    CK(tf_checkpoint_file(&rfs, (uint32_t) f, body, sizeof body) == 0 && sh.good == 0 &&
           memcmp(sa, body, sizeof body) == 0,
       "the genuine bytes are checkpointed into the shadow");

    CK(tf_recover_file(&rfs, (uint32_t) f, buf, sizeof buf) == 0 &&
           rproof.action == RCV_ACT_REFUSE && rproof.why == RCV_WHY_SOFT &&
           memcmp(buf, body, sizeof buf) == 0 && fe->verified && proven(&log),
       "intact bytes: nothing patched (REFUSE, why SOFT); they verify");

    buf[5] ^= 0x20;
    buf[17] = 0;
    CK(tf_recover_file(&rfs, (uint32_t) f, buf, sizeof buf) == 0 &&
           rproof.action == RCV_ACT_ROLLBACK && memcmp(buf, body, sizeof buf) == 0 &&
           fe->verified && fe->lpres_state == LPRES_STATE_TRUE && proven(&log),
       "corrupted bytes: rolled back to the checkpoint exactly, verified, signed proof");
    CK(q.n == 1 && q.rec[0].submitter == (uint32_t) f, "the rollback was quarantined");

    /* File g: the shadow holds f's bytes, not g's, so nothing can restore g. */
    tf_file_entry_t *ge = tf_get_file(&rfs, (uint32_t) g);
    uint8_t gb[24];
    memcpy(gb, body2, sizeof gb);
    gb[0] = 'X';
    CK(tf_recover_file(&rfs, (uint32_t) g, gb, sizeof gb) == TF_EISOLATED &&
           rproof.action == RCV_ACT_ISOLATE && rproof.why == RCV_WHY_BROKEN && ge->isolated &&
           !ge->verified && gb[0] == 'X' && proven(&log),
       "no checkpoint that matches its CID: isolated (why BROKEN), bytes untouched");
    CK(tf_verify_file_content(&rfs, (uint32_t) g, body2, sizeof body2) == TF_EISOLATED &&
           !ge->verified,
       "isolation sticks: even the genuine bytes no longer verify");
    CK(tf_recover_file(&rfs, (uint32_t) g, gb, sizeof gb) == TF_EISOLATED &&
           tf_checkpoint_file(&rfs, (uint32_t) g, body2, sizeof body2) == TF_EISOLATED,
       "an isolated file is neither recovered nor checkpointed");
    CK(rcv_isolated(&rc, (uint32_t) g) && !rcv_isolated(&rc, (uint32_t) f), "framework agrees");

    /* A tampered shadow is never restored. */
    sa[3] ^= 1;
    memcpy(buf, body, sizeof buf);
    buf[0] = 0;
    CK(tf_recover_file(&rfs, (uint32_t) f, buf, sizeof buf) == TF_EISOLATED &&
           rproof.why == RCV_WHY_TAMPERED && buf[0] == 0 && fe->isolated,
       "a tampered checkpoint: not restored, the file isolated");

    /* A signer that fails halts the framework. */
    tf_file_entry_t *he = tf_get_file(&rfs, (uint32_t) h);
    sign_fail = 1;
    uint8_t hb[24];
    memcpy(hb, body2, sizeof hb);
    hb[1] ^= 1;
    CK(tf_recover_file(&rfs, (uint32_t) h, hb, sizeof hb) == TF_EHALTED && rc.halted &&
           he->isolated && !rcv_proof_verify(&rproof, mac_verify, 0, mac_key, 32),
       "signer failure: the isolation stands, TF_EHALTED, framework halted");
    sign_fail = 0;
    CK(tf_self_heal_file(&rfs, (uint32_t) h) == TF_EHALTED, "halted: self-heal says so");
    CK(pdx_log_verify(&log) && lpx_qlog_verify(&q), "operator and quarantine logs verify");
}

int main(void)
{
    printf("=== tripartite_fs: real CIDs, content-hashing verification ===\n");
    uint8_t cid[TF_CID_LEN];
    char s[TF_CID_STR_LEN];
    tf_compute_cid((const uint8_t *) "hello world", 11, cid);
    CK(cid[0] == 0x01 && cid[1] == 0x55 && cid[2] == 0x12 && cid[3] == 0x20,
       "CIDv1 raw sha2-256 prefix");
    CK(tf_cid_to_string(cid, s, sizeof s) == 59 &&
           strcmp(s, "bafkreifzjut3te2nhyekklss27nh3k72ysco7y32koao5eei66wof36n5e") == 0,
       "\"hello world\" CID matches the IPFS raw-leaf CID");

    int marker = 1;
    tf_init(&fs, NULL, NULL, NULL, NULL, NULL, NULL, &marker);
    const uint8_t pos[] = "positive space payload";
    const uint8_t neg[] = "negative space payload";
    const uint8_t neu[] = "neutral";
    int32_t p = tf_create_file(&fs, "a.36n9", TF_TYPE_36N9, pos, sizeof pos, 7);
    int32_t n = tf_create_file(&fs, "a.9n63", TF_TYPE_9N63, neg, sizeof neg, 7);
    int32_t z = tf_create_file(&fs, "a.0n0", TF_TYPE_0N0, neu, sizeof neu, 7);
    CK(p == 0 && n == 1 && z == 2, "create returns file ids");
    tf_file_entry_t *fp = tf_get_file(&fs, (uint32_t) p);
    tf_compute_cid(pos, sizeof pos, cid);
    CK(memcmp(fp->cid, cid, TF_CID_LEN) == 0, "stored CID is the payload's CID");
    CK(memcmp(fp->cid, tf_get_file(&fs, (uint32_t) n)->cid, TF_CID_LEN) != 0,
       "different content, different CID");
    CK(tf_get_file_by_cid(&fs, cid) == fp, "lookup by CID");
    CK(!fp->verified, "not verified at creation");

    CK(tf_wallet_sign_file(&fs, (uint32_t) p, 1) == TF_ENOTSUP &&
           tf_wallet_verify_file(&fs, (uint32_t) p) == TF_ENOTSUP && !fp->verified,
       "wallet sign/verify fail closed and do not set verified");
    CK(tf_self_heal_file(&fs, (uint32_t) p) == TF_ENOTSUP && !fp->verified,
       "self-heal cannot fake verification");
    CK(tf_self_audit_file(&fs, (uint32_t) p) == TF_EMISMATCH,
       "audit runs by default and fails an unverified file");

    uint8_t bad[sizeof pos];
    memcpy(bad, pos, sizeof pos);
    bad[3] ^= 1;
    CK(tf_verify_file_content(&fs, (uint32_t) p, bad, sizeof bad) == TF_EMISMATCH &&
           !fp->verified && fp->lpres_state == LPRES_STATE_FALSE,
       "one flipped bit fails verification");
    CK(tf_verify_file_content(&fs, (uint32_t) p, pos, sizeof pos - 1) == TF_EMISMATCH,
       "truncated content fails verification");
    CK(tf_verify_file_content(&fs, (uint32_t) p, pos, sizeof pos) == 0 && fp->verified &&
           fp->lpres_state == LPRES_STATE_TRUE,
       "genuine content verifies");
    CK(tf_self_audit_file(&fs, (uint32_t) p) == 0, "verified file audits clean");

    int32_t pr = tf_create_pair(&fs, (uint32_t) p, (uint32_t) n, (uint32_t) z);
    CK(pr == 0 && tf_validate_pair(&fs, 0) == 0, "pair created and valid");
    CK(tf_create_pair(&fs, (uint32_t) n, (uint32_t) p, TF_NO_FILE) == TF_EINVAL,
       "phase-swapped pair refused");

    uint8_t out[64];
    CK(tf_reconstruct_interference(&fs, 0, out, sizeof out, HOLO_BLEND_INTERFERENCE) == TF_EINVAL,
       "no reconstruction before a verified load");
    CK(tf_load_pair(&fs, 0, pos, sizeof pos, bad, sizeof bad) == TF_EMISMATCH,
       "load with a wrong negative payload refused");
    CK(tf_reconstruct_interference(&fs, 0, out, sizeof out, HOLO_BLEND_INTERFERENCE) == TF_EINVAL,
       "failed load leaves nothing loaded");
    CK(tf_load_pair(&fs, 0, pos, sizeof pos, neg, sizeof neg) == 0, "verified load");
    int32_t len = tf_reconstruct_interference(&fs, 0, out, sizeof out, HOLO_BLEND_INTERFERENCE);
    CK(len == (int32_t) sizeof pos && out[0] == (uint8_t) (pos[0] ^ neg[0]),
       "interference over the verified payloads");

    /* Swap the positive file's CID behind the pair's back. */
    fp->cid[10] ^= 1;
    CK(tf_validate_pair(&fs, 0) == TF_EMISMATCH, "edited member CID breaks the pair digest");
    CK(tf_self_audit_system(&fs) == TF_EMISMATCH, "system audit notices");
    fp->cid[10] ^= 1;

    CK(tf_price_file(&fs, (uint32_t) p, 50, 2) == TF_EINVAL,
       "pricing in a state-reserved form refused");
    CK(tf_price_file(&fs, (uint32_t) p, 50, 8) == 0, "pricing in Knowledge accepted");
    CK(tf_purchase_access(&fs, (uint32_t) p, 9) == TF_ENOTSUP, "purchase fails closed");
    CK(tf_bridge_file(&fs, (uint32_t) p, "eth", "0x0") == TF_ENOTSUP &&
           tf_enforce_policy(&fs, (uint32_t) p, 1) == TF_ENOTSUP,
       "bridge and policy fail closed");

    int32_t c = tf_create_container(&fs, "box", NULL);
    CK(c == 0 && tf_container_add_file(&fs, 0, (uint32_t) p) == 0 &&
           memcmp(fs.containers[0].entries[0].cid, fp->cid, TF_CID_LEN) == 0,
       "container entry carries the CID");
    CK(tf_container_add_file(&fs, 0, 999) == TF_EINVAL, "unknown file refused (no NULL deref)");

    test_recovery();

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
