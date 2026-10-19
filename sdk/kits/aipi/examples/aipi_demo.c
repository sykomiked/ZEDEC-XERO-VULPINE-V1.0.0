/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* aipi_demo.c — AIPI in one run: a function published by CID, called over
 * a (simulated) mesh with a bounty, a forged answer refused by the
 * function's own inverse, the honest answer paid, then the same function
 * run offline from the local cache. */
#include "aipi_mods.h"

static const char *tagname(lpx_t t, char *buf, size_t cap)
{
    snprintf(buf, cap, "%s/%s/%s -> %s", lpx_base_name(lpx_base(t)), lpx_pol_name(lpx_pol(t)),
             lpx_phase_name(lpx_phase(t)), lpx_action_name(lpx_action(t)));
    return buf;
}

int main(void)
{
    zbudget_t b;
    tnode_t *alice, *bob;
    wb m;
    ipfsn_cid_t mod, con, f, call;
    aipi_fn_t fn;
    char txt[AIPI_TEXT_MAX], tb[96];
    uint8_t in[8], out[16], claim[AIPI_CLAIM_ENC_MAX], forged[8], v = 0, key[32];
    uint32_t job, len;
    int cl;
    int64_t wrong = 1234;

    zbudget_init(&b);
    alice = tn_new("alice", 1, &b); /* wants an answer   */
    bob = tn_new("bob", 2, &b);     /* hosts the function */

    /* Bob publishes half(n) with its inverse half_ok and a contract. */
    mod_halves(&m);
    aipi_put_blob(&bob->a, m.b, (uint32_t) m.n, IPFSN_VIS_PUBLIC, &mod);
    aipi_put_blob(&bob->a, (const uint8_t *) HALVES_ZC, sizeof HALVES_ZC - 1, IPFSN_VIS_PUBLIC, &con);
    memset(&fn, 0, sizeof fn);
    fn.intent = AIPI_INTENT_FN;
    fn.verify = AIPI_VERIFY_CHECKER;
    fn.determinism = AIPI_DET_DETERMINISTIC;
    fn.fuel_max = 100000;
    fn.module = mod;
    fn.has_contract = true;
    fn.contract = con;
    strcpy(fn.entry, "half");
    strcpy(fn.checker, "half_ok");
    aipi_publish_fn(&bob->a, &fn, &f);
    aipi_cid_to_text(&f, txt, sizeof txt);
    aipi_provider_key(&f, key);
    printf("function CID  %s\n", txt);
    printf("  codec 0x%X (zxv-fn, private-use range), sha2-256 of the descriptor\n", f.codec);
    printf("  Vinea provider key %02x%02x%02x%02x...\n", key[0], key[1], key[2], key[3]);

    /* Alice knows only the CID. */
    alice->peer = bob;
    alice->balance = 100;
    aipi_execute(&alice->a, &f, i64in(2026, in), 8, false, 10, 0, 100, &job);
    printf("\nalice: AIPI_EXECUTE(half, 2026, bounty 10): route %s, escrow %lld\n",
           aipi_job(&alice->a, job)->route == AIPI_ROUTE_MESH ? "mesh" : "other",
           (long long) alice->escrow);
    printf("  fetched by CID and verified: descriptor, module (%u bytes), contract\n",
           (unsigned) m.n);
    printf("  tag %s\n", tagname(aipi_job(&alice->a, job)->tag, tb, sizeof tb));

    aipi_call_cid(&f, in, 8, &call);
    aipi_zw_pack(&wrong, 1, forged, 8, &len);
    cl = aipi_claim_encode(&call, (const uint8_t *) "MALLORYMALLORYMALLORYMALLORYMALL", forged, 8,
                           0, 0, claim, sizeof claim);
    aipi_submit(&alice->a, job, claim, (uint32_t) cl, &v);
    printf("mallory claims 1234: %s, paid 0\n", v == AIPI_V_REJECTED ? "REJECTED by half_ok" : "?");
    printf("  tag %s\n", tagname(aipi_job(&alice->a, job)->tag, tb, sizeof tb));

    cl = aipi_serve(&bob->a, alice->call, alice->call_len, claim, sizeof claim);
    aipi_submit(&alice->a, job, claim, (uint32_t) cl, &v);
    aipi_result(&alice->a, job, out, sizeof out, &len);
    printf("bob runs the Wasm and claims %lld: %s, paid %lld\n", (long long) i64out(out),
           v == AIPI_V_ACCEPTED ? "ACCEPTED" : "?", (long long) tn_paid_total);
    printf("  tag %s\n", tagname(aipi_job(&alice->a, job)->tag, tb, sizeof tb));
    aipi_release(&alice->a, job);

    /* Bob disappears and the network drops. */
    alice->peer = NULL;
    alice->a.online = false;
    aipi_execute(&alice->a, &f, i64in(99, in), 8, false, 10, 0, 100, &job);
    aipi_result(&alice->a, job, out, sizeof out, &len);
    printf("\noffline: half(99) = %lld, run locally from the cached module (route %s)\n",
           (long long) i64out(out), aipi_job(&alice->a, job)->route == AIPI_ROUTE_LOCAL ? "local" : "?");
    aipi_release(&alice->a, job);
    aipi_execute(&alice->a, &f, i64in(2026, in), 8, false, 10, 0, 100, &job);
    printf("offline: half(2026) answered from the memo of verified results (route %s)\n",
           aipi_job(&alice->a, job)->route == AIPI_ROUTE_CACHE ? "cache" : "?");
    aipi_release(&alice->a, job);
    printf("alice's balance: %lld (10 paid to bob, nothing to mallory)\n", (long long) alice->balance);

    tn_free(alice);
    tn_free(bob);
    zbudget_fini(&b);
    return 0;
}
