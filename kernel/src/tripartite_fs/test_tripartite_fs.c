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

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
