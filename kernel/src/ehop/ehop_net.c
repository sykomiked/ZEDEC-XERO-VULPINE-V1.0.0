/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ehop_net.c — private networks: create, invite, join, rotate, remove.
 *
 * Welcome message (EHOP_WELCOME_BYTES), signed with ML-DSA-65 under the
 * context string "ZXV-EHOP-v1/welcome":
 *   off    0  "ZW" | version | kind (1 invite, 2 re-key)
 *   off    4  net_id[16]
 *   off   20  le32 epoch
 *   off   24  le32 member_id
 *   off   28  ML-KEM-768 ciphertext[1088] to the member's ek
 *   off 1116  wrapped key[32] = key XOR SHAKE256("ZXV-EHOP-v1/wrap" || ss ||
 *                                         net_id || epoch || member_id, 32)
 *   off 1148  commit[32]  = SHA3-256("ZXV-EHOP-v1/commit" || net_id ||
 *                                    epoch || key)
 *   off 1180  ML-DSA-65 signature[3309] over bytes 0..1179
 * The joiner checks the signature, decapsulates, unwraps, and only installs
 * the key when its commitment matches, so a corrupted or substituted share
 * is never installed.
 *
 * Ratchet: key(e+1) = SHAKE256("ZXV-EHOP-v1/ratchet" || net_id || le32 e+1 ||
 * key(e)); key(e) is wiped (forward secrecy across epochs). Re-key on removal:
 * key = SHAKE256("ZXV-EHOP-v1/rekey" || net_id || le32 e+1 || entropy ||
 * key(e)); the removed member never sees the entropy, so it cannot follow.
 */
#include "ehop_internal.h"
#include "../mlkem/keccak.h"
#include "../mlkem/mlkem768.h"

#define W_MAGIC0  0x5Au /* 'Z' */
#define W_MAGIC1  0x57u /* 'W' */
#define W_OFF_NET 4u
#define W_OFF_EP  20u
#define W_OFF_MID 24u
#define W_OFF_CT  28u
#define W_OFF_WK  (W_OFF_CT + MLKEM768_CT_BYTES)
#define W_OFF_CM  (W_OFF_WK + EHOP_KEY_BYTES)
#define W_OFF_SIG (W_OFF_CM + 32u)

static const char k_sig_ctx[] = "ZXV-EHOP-v1/welcome";
#define SIG_CTX_LEN ((uint32_t) (sizeof k_sig_ctx - 1u))

typedef char ehop_welcome_layout_ok[(W_OFF_SIG == EHOP_WELCOME_BODY) ? 1 : -1];

static void wrap_pad(const uint8_t ss[MLKEM768_SS_BYTES], const uint8_t net_id[EHOP_NET_ID_BYTES],
                     uint32_t epoch, uint32_t member_id, uint8_t pad[EHOP_KEY_BYTES])
{
    uint8_t in[16 + MLKEM768_SS_BYTES + EHOP_NET_ID_BYTES + 8];
    uint32_t n = ehop_put_str(in, "ZXV-EHOP-v1/wrap");
    ehop_cpy(in + n, ss, MLKEM768_SS_BYTES);
    n += MLKEM768_SS_BYTES;
    ehop_cpy(in + n, net_id, EHOP_NET_ID_BYTES);
    n += EHOP_NET_ID_BYTES;
    ehop_le32(in + n, epoch);
    ehop_le32(in + n + 4, member_id);
    n += 8;
    shake256(in, n, pad, EHOP_KEY_BYTES);
    ehop_wipe(in, sizeof in);
}

static void commit_key(const uint8_t net_id[EHOP_NET_ID_BYTES], uint32_t epoch,
                       const uint8_t key[EHOP_KEY_BYTES], uint8_t out[32])
{
    uint8_t in[18 + EHOP_NET_ID_BYTES + 4 + EHOP_KEY_BYTES];
    uint32_t n = ehop_put_str(in, "ZXV-EHOP-v1/commit");
    ehop_cpy(in + n, net_id, EHOP_NET_ID_BYTES);
    n += EHOP_NET_ID_BYTES;
    ehop_le32(in + n, epoch);
    n += 4;
    ehop_cpy(in + n, key, EHOP_KEY_BYTES);
    n += EHOP_KEY_BYTES;
    sha3_256(in, n, out);
    ehop_wipe(in, sizeof in);
}

/* key' = SHAKE256(label || net_id || le32 epoch || extra[0..32) || key). */
static void derive_next(const char *label, const uint8_t net_id[EHOP_NET_ID_BYTES], uint32_t epoch,
                        const uint8_t *extra, uint8_t key[EHOP_KEY_BYTES])
{
    uint8_t in[20 + EHOP_NET_ID_BYTES + 4 + 32 + EHOP_KEY_BYTES];
    uint32_t n = ehop_put_str(in, label);
    ehop_cpy(in + n, net_id, EHOP_NET_ID_BYTES);
    n += EHOP_NET_ID_BYTES;
    ehop_le32(in + n, epoch);
    n += 4;
    if (extra) {
        ehop_cpy(in + n, extra, 32);
        n += 32;
    }
    ehop_cpy(in + n, key, EHOP_KEY_BYTES);
    n += EHOP_KEY_BYTES;
    shake256(in, n, key, EHOP_KEY_BYTES);
    ehop_wipe(in, sizeof in);
}

int ehop_net_create(ehop_net_t *net, const uint8_t net_id[EHOP_NET_ID_BYTES], uint32_t admin_id,
                    const uint8_t sig_seed[32], const uint8_t key_entropy[32])
{
    uint8_t in[18 + 32 + EHOP_NET_ID_BYTES];
    uint32_t n;
    if (!net || !net_id || !sig_seed || !key_entropy) return EHOP_EARG;
    ehop_wipe(net, sizeof *net);
    ehop_cpy(net->net_id, net_id, EHOP_NET_ID_BYTES);
    pq_mldsa65_keygen(sig_seed, net->admin_pk, net->admin_sk);
    n = ehop_put_str(in, "ZXV-EHOP-v1/netkey");
    ehop_cpy(in + n, key_entropy, 32);
    n += 32;
    ehop_cpy(in + n, net_id, EHOP_NET_ID_BYTES);
    n += EHOP_NET_ID_BYTES;
    shake256(in, n, net->key, EHOP_KEY_BYTES);
    ehop_wipe(in, sizeof in);
    net->epoch = 0;
    net->self_id = admin_id;
    net->is_admin = 1;
    net->has_key = 1;
    return EHOP_OK;
}

int ehop_net_member_init(ehop_net_t *net, const uint8_t net_id[EHOP_NET_ID_BYTES], uint32_t self_id,
                         const uint8_t admin_pk[EHOP_MLDSA_PK_BYTES])
{
    if (!net || !net_id || !admin_pk) return EHOP_EARG;
    ehop_wipe(net, sizeof *net);
    ehop_cpy(net->net_id, net_id, EHOP_NET_ID_BYTES);
    ehop_cpy(net->admin_pk, admin_pk, EHOP_MLDSA_PK_BYTES);
    net->self_id = self_id;
    return EHOP_OK;
}

/* Write one signed welcome carrying net->key at net->epoch. */
static void write_welcome(const ehop_net_t *net, uint32_t kind, const ehop_member_t *mem,
                          const uint8_t m[32], const uint8_t *rnd, uint8_t *w)
{
    uint8_t ss[MLKEM768_SS_BYTES], pad[EHOP_KEY_BYTES];
    uint32_t i;
    w[0] = W_MAGIC0;
    w[1] = W_MAGIC1;
    w[2] = EHOP_VERSION;
    w[3] = (uint8_t) kind;
    ehop_cpy(w + W_OFF_NET, net->net_id, EHOP_NET_ID_BYTES);
    ehop_le32(w + W_OFF_EP, net->epoch);
    ehop_le32(w + W_OFF_MID, mem->member_id);
    mlkem768_encaps(mem->ek, m, w + W_OFF_CT, ss);
    wrap_pad(ss, net->net_id, net->epoch, mem->member_id, pad);
    for (i = 0; i < EHOP_KEY_BYTES; i++) w[W_OFF_WK + i] = (uint8_t) (net->key[i] ^ pad[i]);
    commit_key(net->net_id, net->epoch, net->key, w + W_OFF_CM);
    pq_mldsa65_sign(net->admin_sk, w, EHOP_WELCOME_BODY, (const uint8_t *) k_sig_ctx, SIG_CTX_LEN,
                    rnd, w + W_OFF_SIG);
    ehop_wipe(ss, sizeof ss);
    ehop_wipe(pad, sizeof pad);
}

int ehop_net_invite(ehop_net_t *net, uint32_t member_id, const uint8_t ek[MLKEM768_EK_BYTES],
                    const uint8_t m[32], const uint8_t *rnd, uint8_t welcome[EHOP_WELCOME_BYTES])
{
    ehop_member_t *slot = NULL;
    uint32_t i;
    if (!net || !ek || !m || !welcome || !net->is_admin || !net->has_key) return EHOP_EARG;
    if (member_id == net->self_id) return EHOP_EMEMBER;
    for (i = 0; i < EHOP_NET_MAX_MEMBERS; i++) {
        ehop_member_t *e = &net->members[i];
        if (e->active && e->member_id == member_id) {
            slot = e;
            break;
        }
        if (!e->active && !slot) slot = e;
    }
    if (!slot) return EHOP_EFULL;
    slot->member_id = member_id;
    ehop_cpy(slot->ek, ek, MLKEM768_EK_BYTES);
    slot->active = 1;
    write_welcome(net, EHOP_WELCOME_INVITE, slot, m, rnd, welcome);
    return EHOP_OK;
}

int ehop_net_join(ehop_net_t *net, const uint8_t dk[MLKEM768_DK_BYTES],
                  const uint8_t welcome[EHOP_WELCOME_BYTES])
{
    uint8_t ss[MLKEM768_SS_BYTES], pad[EHOP_KEY_BYTES], key[EHOP_KEY_BYTES];
    uint8_t cm[32];
    uint32_t epoch, i;
    int ok;
    if (!net || !dk || !welcome || net->is_admin) return EHOP_EARG;
    if (welcome[0] != W_MAGIC0 || welcome[1] != W_MAGIC1 || welcome[2] != EHOP_VERSION ||
        (welcome[3] != EHOP_WELCOME_INVITE && welcome[3] != EHOP_WELCOME_REKEY))
        return EHOP_EFORMAT;
    if (!ehop_ct_eq(welcome + W_OFF_NET, net->net_id, EHOP_NET_ID_BYTES)) return EHOP_EMEMBER;
    if (ehop_rd32(welcome + W_OFF_MID) != net->self_id) return EHOP_EMEMBER;
    epoch = ehop_rd32(welcome + W_OFF_EP);
    if (net->has_key && epoch <= net->epoch)
        return EHOP_EEPOCH; /* no rollback, no replay of an old share */
    if (!pq_mldsa65_verify(net->admin_pk, welcome, EHOP_WELCOME_BODY, (const uint8_t *) k_sig_ctx,
                           SIG_CTX_LEN, welcome + W_OFF_SIG))
        return EHOP_ESIG;
    mlkem768_decaps(dk, welcome + W_OFF_CT, ss);
    wrap_pad(ss, net->net_id, epoch, net->self_id, pad);
    for (i = 0; i < EHOP_KEY_BYTES; i++) key[i] = (uint8_t) (welcome[W_OFF_WK + i] ^ pad[i]);
    commit_key(net->net_id, epoch, key, cm);
    ok = ehop_ct_eq(cm, welcome + W_OFF_CM, 32);
    if (ok) {
        ehop_cpy(net->key, key, EHOP_KEY_BYTES);
        net->epoch = epoch;
        net->has_key = 1;
    }
    ehop_wipe(ss, sizeof ss);
    ehop_wipe(pad, sizeof pad);
    ehop_wipe(key, sizeof key);
    ehop_wipe(cm, sizeof cm);
    return ok ? EHOP_OK : EHOP_EAUTH;
}

int ehop_net_rotate(ehop_net_t *net)
{
    if (!net || !net->has_key) return EHOP_EARG;
    if (net->epoch == 0xFFFFFFFFu) return EHOP_ESEQ;
    net->epoch++;
    derive_next("ZXV-EHOP-v1/ratchet", net->net_id, net->epoch, NULL, net->key);
    return EHOP_OK;
}

int ehop_net_remove(ehop_net_t *net, uint32_t member_id, const uint8_t entropy[32],
                    const uint8_t *rnd, uint8_t welcomes[][EHOP_WELCOME_BYTES], uint32_t *count)
{
    uint8_t in[17 + 32 + 8], m[32];
    uint32_t i, n, found = 0;
    if (count) *count = 0;
    if (!net || !entropy || !welcomes || !count || !net->is_admin || !net->has_key)
        return EHOP_EARG;
    if (net->epoch == 0xFFFFFFFFu) return EHOP_ESEQ;
    for (i = 0; i < EHOP_NET_MAX_MEMBERS; i++) {
        ehop_member_t *e = &net->members[i];
        if (e->active && e->member_id == member_id) {
            ehop_wipe(e, sizeof *e);
            found = 1;
        }
    }
    if (!found) return EHOP_EMEMBER;
    net->epoch++;
    derive_next("ZXV-EHOP-v1/rekey", net->net_id, net->epoch, entropy, net->key);
    for (i = 0; i < EHOP_NET_MAX_MEMBERS; i++) {
        const ehop_member_t *e = &net->members[i];
        if (!e->active) continue;
        n = ehop_put_str(in, "ZXV-EHOP-v1/kem-m");
        ehop_cpy(in + n, entropy, 32);
        n += 32;
        ehop_le32(in + n, e->member_id);
        ehop_le32(in + n + 4, net->epoch);
        n += 8;
        shake256(in, n, m, sizeof m);
        write_welcome(net, EHOP_WELCOME_REKEY, e, m, rnd, welcomes[*count]);
        (*count)++;
    }
    ehop_wipe(in, sizeof in);
    ehop_wipe(m, sizeof m);
    return EHOP_OK;
}

int ehop_net_channel(const ehop_net_t *net, uint32_t chan_id, const ehop_cfg_t *cfg,
                     uint32_t self_sender, ehop_channel_t *ch)
{
    uint8_t in[19 + EHOP_NET_ID_BYTES + EHOP_KEY_BYTES], k[EHOP_KEY_BYTES];
    uint32_t n;
    int rc;
    if (!net || !net->has_key || !cfg || !ch) return EHOP_EARG;
    n = ehop_put_str(in, "ZXV-EHOP-v1/netchan");
    ehop_cpy(in + n, net->net_id, EHOP_NET_ID_BYTES);
    n += EHOP_NET_ID_BYTES;
    ehop_cpy(in + n, net->key, EHOP_KEY_BYTES);
    n += EHOP_KEY_BYTES;
    shake256(in, n, k, sizeof k);
    rc = ehop_channel_init(ch, k, chan_id, net->epoch, cfg, self_sender);
    ehop_wipe(in, sizeof in);
    ehop_wipe(k, sizeof k);
    return rc;
}

void ehop_net_wipe(ehop_net_t *net)
{
    if (net) ehop_wipe(net, sizeof *net);
}
