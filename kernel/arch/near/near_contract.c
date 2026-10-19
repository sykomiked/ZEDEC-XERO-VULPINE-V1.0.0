/* near_contract.c — the ZXV kernel's economic core as a NEAR smart contract.
 *
 * This is not a re-implementation: it is the same triple_ledger and vino
 * sources the fleet kernels link, compiled for the NEAR host ABI. The
 * contract holds one state blob — the two ledgers plus a holder registry —
 * which round-trips through env.storage_* on every call, because NEAR gives
 * every transaction fresh memory.
 *
 * Exported methods (the public surface of the floating-voucher ledger):
 *   zxv_boot()                       — initialize ledgers, register $YHVH
 *   voucher_issue(holder,amount,purpose) — owner-only; floats a voucher
 *   voucher_transfer(id,to_holder)   — current-holder-only; re-points it
 *   voucher_redeem(id)               — current-holder-only; closes it
 *   ledger_state()                   — view: accounts, vouchers, chain head
 *   chain_verify()                   — view: vino chain integrity code
 *   scp144_bind(init,resp)           — view: pure SCP-144 lattice binding
 *
 * Authorization is the chain's own: signer_account_id must equal the owner
 * (issue) or the current holder (transfer/redeem). Replay protection is
 * the ledger itself — a redeemed voucher stays redeemed, a transferred
 * voucher's holder has moved — layered on the protocol's per-transaction
 * nonce. All money arithmetic is Q32.32 fixed-point (surplus_real_t), no
 * floats.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 */
#include "near_env.h"
#include "triple_ledger.h"
#include "vino.h"
#include "scp144.h"
#include "sha256.h"
#include "lpres.h"
#include "tribute.h"

/* Bounded tables for the on-chain build: the fleet defaults (512 accounts
 * x 256 entries, 65,536 vino txns) serialize to ~48 MB — far beyond the
 * practical storage/gas budget of one contract. These bounds keep the same
 * code and the same Q32.32 arithmetic at ~660 KB of contract state. */
#ifndef ZXV_TL_MAX_ACCOUNTS
#define ZXV_TL_MAX_ACCOUNTS 64u
#endif
#ifndef ZXV_TL_MAX_ENTRIES
#define ZXV_TL_MAX_ENTRIES 16u
#endif
#ifndef ZXV_TL_MAX_VOUCHERS
#define ZXV_TL_MAX_VOUCHERS 8u
#endif
#ifndef VINO_MAX_ACCOUNTS
#define VINO_MAX_ACCOUNTS 128u
#endif
#ifndef VINO_MAX_TXNS
#define VINO_MAX_TXNS 1024u
#endif
#ifndef VINO_MAX_ASSETS
#define VINO_MAX_ASSETS 16u
#endif
#ifndef VINO_MAX_PEERS
#define VINO_MAX_PEERS 8u
#endif
#ifndef VINO_MAX_MESSAGES
#define VINO_MAX_MESSAGES 8u
#endif

#define ZXV_NEAR_MAGIC 0x5A58564E /* "ZXVN" */
#define ZXV_NEAR_VERSION 1u
#define ZXV_NEAR_STATE_KEY "zxv:v1"
#define ZXV_NEAR_HOLDER_MAX 63u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t booted;
    char owner[65];
    uint64_t ops;
    triple_ledger_t tl;
    vino_ledger_t vino;
    char holders[ZXV_TL_MAX_ACCOUNTS][65];
    uint32_t num_holders;
} zxv_near_state_t;

static zxv_near_state_t g_st;

static void wr_u32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) (v >> 24);
    p[1] = (uint8_t) (v >> 16);
    p[2] = (uint8_t) (v >> 8);
    p[3] = (uint8_t) v;
}

static void wr_u64be(uint8_t *p, uint64_t v)
{
    uint32_t hi = (uint32_t) (v >> 32), lo = (uint32_t) v;
    wr_u32be(p, hi);
    wr_u32be(p + 4, lo);
}

static uint32_t rd_u32be(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) |
           ((uint32_t) p[2] << 8) | (uint32_t) p[3];
}

static uint64_t rd_u64be(const uint8_t *p)
{
    return ((uint64_t) rd_u32be(p) << 32) | rd_u32be(p + 4);
}

/* ---- signer plumbing -------------------------------------------------- */

static uint8_t g_scratch[4096];

static const char *fetch_signer(void)
{
    static char s[65];
    uint64_t n;
    env_signer_account_id(1);
    n = env_register_len(1);
    if (n == NEAR_REG_EMPTY || n >= 65) {
        s[0] = 0;
        return s;
    }
    env_read_register(1, (uint64_t) (uintptr_t) s);
    s[n] = 0;
    return s;
}

static const char *current_account(void)
{
    static char s[65];
    uint64_t n;
    env_current_account_id(2);
    n = env_register_len(2);
    if (n == NEAR_REG_EMPTY || n >= 65) {
        s[0] = 0;
        return s;
    }
    env_read_register(2, (uint64_t) (uintptr_t) s);
    s[n] = 0;
    return s;
}

static void fail(const char *msg)
{
    uint64_t len = 0;
    while (msg[len]) len++;
    env_panic_utf8(len, (uint64_t) (uintptr_t) msg);
}

/* ---- state round-trip ------------------------------------------------- */

static void state_load(void)
{
    static const uint8_t key[] = ZXV_NEAR_STATE_KEY;
    uint64_t n;
    if (!env_storage_has_key(sizeof key - 1, (uint64_t) (uintptr_t) key)) {
        fs_memset(&g_st, 0, sizeof g_st);
        return;
    }
    /* storage_read returns presence (1 = found, u64::MAX = absent), not the
     * length; the value lands in the register and its size comes from
     * register_len. (Verified on mainnet: return 1, register 898,312.) */
    n = env_storage_read(sizeof key - 1, (uint64_t) (uintptr_t) key, 0);
    if (n == NEAR_REG_EMPTY)
        fail("zxv: state missing");
    n = env_register_len(0);
    if (n != sizeof g_st)
        fail("zxv: state size mismatch");
    env_read_register(0, (uint64_t) (uintptr_t) &g_st);
    if (g_st.magic != ZXV_NEAR_MAGIC || g_st.version != ZXV_NEAR_VERSION)
        fail("zxv: state magic");
}

static void state_store(void)
{
    static const uint8_t key[] = ZXV_NEAR_STATE_KEY;
    g_st.ops++;
    (void) env_storage_write(sizeof key - 1, (uint64_t) (uintptr_t) key,
                             sizeof g_st, (uint64_t) (uintptr_t) &g_st, 0);
}

/* ---- holder registry -------------------------------------------------- */

static int32_t holder_find(const char *name)
{
    uint32_t i;
    for (i = 0; i < g_st.num_holders; i++)
        if (fs_strcmp(g_st.holders[i], name) == 0) return (int32_t) i;
    return -1;
}

/* The vino ledger's address field is 64 bytes (63 chars + NUL); a 64-char
 * NEAR account id is truncated on create, so lookups must truncate the same
 * way or they miss. */
static const char *vino_addr(const char *name)
{
    static char buf[VINO_ADDR_LEN];
    uint32_t i = 0;
    while (name[i] && i + 1u < VINO_ADDR_LEN) {
        buf[i] = name[i];
        i++;
    }
    buf[i] = 0;
    return buf;
}

static int32_t holder_register(const char *name)
{
    int32_t i = holder_find(name);
    uint64_t nl = 0;
    if (i >= 0) return i;
    if (g_st.num_holders >= ZXV_TL_MAX_ACCOUNTS) return -2;
    while (name[nl]) nl++;
    if (nl == 0 || nl >= 65) return -3;
    i = (int32_t) g_st.num_holders;
    fs_memset(g_st.holders[i], 0, 64);
    fs_memcpy(g_st.holders[i], name, nl);
    g_st.num_holders++;
    if (triple_ledger_create_account(&g_st.tl, (uint32_t) i, CAP_FINANCIAL, name) >=
        ZXV_TL_MAX_ACCOUNTS)
        fail("zxv: account table full");
    (void) vino_create_account(&g_st.vino, vino_addr(name), "NEAR holder");
    return i;
}

/* ---- argument decoding ------------------------------------------------ */

/* Binary arg formats (all lengths as u8, big-endian integers):
 *   issue:    [hl u8][holder hl][amount u64][pl u8][purpose pl]
 *   transfer: [id u64][hl u8][holder hl]
 *   redeem:   [id u64]
 *   scp144:   [init 32][resp 32]
 */
static const uint8_t *args_bytes(uint64_t *out_len)
{
    uint64_t n;
    env_input(0);
    n = env_register_len(0);
    if (n == NEAR_REG_EMPTY) {
        *out_len = 0;
        return 0;
    }
    if (n > sizeof g_scratch) fail("zxv: args too large");
    env_read_register(0, (uint64_t) (uintptr_t) g_scratch);
    /* NEAR tooling sends the empty JSON object "{}" when no args are given;
     * treat it as no args so every exported method works from near-cli. */
    if (n == 2 && g_scratch[0] == '{' && g_scratch[1] == '}') n = 0;
    *out_len = n;
    return g_scratch;
}

/* ---- exported methods -------------------------------------------------- */

__attribute__((export_name("zxv_boot")))
void zxv_boot(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    (void) a;
    if (alen != 0) fail("zxv: boot takes no args");
    state_load();
    if (g_st.booted) fail("zxv: already booted");
    fs_memset(&g_st.tl, 0, sizeof g_st.tl);
    fs_memset(&g_st.vino, 0, sizeof g_st.vino);
    triple_ledger_init(&g_st.tl);
    vino_init(&g_st.vino, 1);
    vino_register_asset(&g_st.vino, "$YHVH", "YHVH floating voucher",
                        ASSET_TOKEN, 0, 0xFFFFFFFFu);
    {
        const char *owner = current_account();
        uint64_t nl = 0;
        while (owner[nl]) nl++;
        if (nl == 0 || nl >= 65) fail("zxv: boot from unknown account");
        fs_memset(g_st.owner, 0, 64);
        fs_memcpy(g_st.owner, owner, nl);
    }
    g_st.magic = ZXV_NEAR_MAGIC;
    g_st.version = ZXV_NEAR_VERSION;
    g_st.booted = 1;
    g_st.num_holders = 0;
    (void) holder_register(g_st.owner);
    state_store();
}

__attribute__((export_name("voucher_issue")))
void voucher_issue(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    const char *signer = fetch_signer();
    char holder[65], purpose[65];
    uint64_t amount;
    uint32_t hl, pl, i;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    if (fs_strcmp(signer, g_st.owner) != 0) fail("zxv: only owner may issue");
    if (alen < 1 + 1 + 8 + 1) fail("zxv: issue args");
    hl = a[0];
    if (alen != 1 + hl + 8 + 1 + a[1 + hl + 8]) fail("zxv: issue args");
    if (hl == 0 || hl >= 65) fail("zxv: holder name");
    fs_memcpy(holder, a + 1, hl);
    holder[hl] = 0;
    pl = a[1 + hl + 8];
    if (pl >= 65) fail("zxv: purpose");
    fs_memcpy(purpose, a + 1 + hl + 8 + 1, pl);
    purpose[pl] = 0;
    amount = rd_u64be(a + 1 + hl);
    if (amount == 0) fail("zxv: zero amount");
    i = (uint32_t) holder_register(holder);
    if (i >= ZXV_TL_MAX_ACCOUNTS) fail("zxv: holder table full");
    {
        surplus_real_t merit = SR_FROM_INT(amount);
        surplus_real_t cover = SR_DIV(SR_FROM_INT(18), SR_FROM_INT(10));
        uint64_t vid = triple_ledger_issue_voucher(&g_st.tl, 0, i, CAP_FINANCIAL,
                                                   merit, cover, purpose);
        if (vid == 0) fail("zxv: voucher table full");
        if (vino_issue(&g_st.vino, vino_addr(holder), ASSET_TOKEN, amount,
                       "voucher issue") < 0)
            fail("zxv: vino issue");
        state_store();
        {
            uint8_t out[9];
            out[0] = 0;
            wr_u64be(out + 1, vid);
            env_value_return(sizeof out, (uint64_t) (uintptr_t) out);
        }
        return;
    }
}

__attribute__((export_name("voucher_transfer")))
void voucher_transfer(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    const char *signer = fetch_signer();
    char to[65];
    uint64_t vid;
    uint32_t hl, from, i;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    if (alen < 8 + 1 + 1) fail("zxv: transfer args");
    vid = rd_u64be(a);
    hl = a[8];
    if (alen != 8 + 1 + hl) fail("zxv: transfer args");
    if (hl == 0 || hl >= 65) fail("zxv: holder name");
    fs_memcpy(to, a + 9, hl);
    to[hl] = 0;
    /* the current holder must be the signer: the voucher's holder_id is
     * authoritative (transfer_voucher re-points it in place, the physical
     * account that holds the array entry never moves). holder_id equals the
     * registry index, so it names g_st.holders[holder_id]. */
    {
        uint32_t acc;
        const floating_voucher_t *fv = 0;
        for (acc = 0; acc < g_st.tl.num_accounts && !fv; acc++) {
            uint32_t v;
            for (v = 0; v < g_st.tl.accounts[acc].num_vouchers; v++)
                if (g_st.tl.accounts[acc].vouchers[v].voucher_id == vid) {
                    fv = &g_st.tl.accounts[acc].vouchers[v];
                    break;
                }
        }
        if (!fv) fail("zxv: no such voucher");
        from = (uint32_t) fv->holder_id;
    }
    if (from >= g_st.num_holders) fail("zxv: bad holder");
    if (fs_strcmp(signer, g_st.holders[from]) != 0)
        fail("zxv: only the holder may transfer");
    i = (uint32_t) holder_register(to);
    if (i >= ZXV_TL_MAX_ACCOUNTS) fail("zxv: holder table full");
    if (triple_ledger_transfer_voucher(&g_st.tl, vid, i) < 0)
        fail("zxv: transfer rejected");
    {
        uint32_t acc;
        const floating_voucher_t *v = 0;
        for (acc = 0; acc < g_st.tl.num_accounts; acc++) {
            uint32_t vv;
            for (vv = 0; vv < g_st.tl.accounts[acc].num_vouchers; vv++)
                if (g_st.tl.accounts[acc].vouchers[vv].voucher_id == vid) {
                    v = &g_st.tl.accounts[acc].vouchers[vv];
                    break;
                }
            if (v) break;
        }
        if (v && !v->redeemed) {
            uint64_t amount = (uint64_t) (v->merit_value / SR_ONE);
            (void) vino_transfer(&g_st.vino, vino_addr(g_st.holders[from]),
                                 vino_addr(to), amount,
                                 CAP_FINANCIAL, RAIL_VINO_NATIVE,
                                 "voucher transfer");
        }
    }
    state_store();
    {
        uint8_t out[9];
        out[0] = 0;
        wr_u64be(out + 1, vid);
        env_value_return(sizeof out, (uint64_t) (uintptr_t) out);
    }
}

__attribute__((export_name("voucher_redeem")))
void voucher_redeem(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    const char *signer = fetch_signer();
    uint64_t vid;
    uint32_t holder = 0;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    if (alen != 8) fail("zxv: redeem args");
    vid = rd_u64be(a);
    {
        uint32_t acc;
        const floating_voucher_t *fv = 0;
        for (acc = 0; acc < g_st.tl.num_accounts && !fv; acc++) {
            uint32_t v;
            for (v = 0; v < g_st.tl.accounts[acc].num_vouchers; v++)
                if (g_st.tl.accounts[acc].vouchers[v].voucher_id == vid) {
                    fv = &g_st.tl.accounts[acc].vouchers[v];
                    break;
                }
        }
        if (!fv) fail("zxv: no such voucher");
        holder = (uint32_t) fv->holder_id;
    }
    if (holder >= g_st.num_holders) fail("zxv: bad holder");
    if (fs_strcmp(signer, g_st.holders[holder]) != 0)
        fail("zxv: only the holder may redeem");
    if (triple_ledger_redeem_voucher(&g_st.tl, vid, holder) < 0)
        fail("zxv: redeem rejected");
    state_store();
    {
        uint8_t out[9];
        out[0] = 0;
        wr_u64be(out + 1, vid);
        env_value_return(sizeof out, (uint64_t) (uintptr_t) out);
    }
}

/* view: textual summary — accounts, vouchers, chain head, op count */
static uint32_t fmt_u64(char *out, uint32_t cap, uint64_t v)
{
    char tmp[24];
    uint32_t n = 0, i;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        tmp[n++] = (char) ('0' + (v % 10));
        v /= 10;
    }
    if (n >= cap) return 0;
    for (i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    return n;
}

static uint32_t fmt_str(char *out, uint32_t cap, const char *s)
{
    uint32_t n = 0;
    while (s[n] && n + 1 < cap) {
        out[n] = s[n];
        n++;
    }
    return n;
}

__attribute__((export_name("ledger_state")))
void ledger_state(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    static char out[2048];
    uint32_t n = 0, acc, v;
    (void) a;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    n += fmt_str(out + n, sizeof out - n, "booted=1 owner=");
    n += fmt_str(out + n, sizeof out - n, g_st.owner);
    n += fmt_str(out + n, sizeof out - n, " ops=");
    n += fmt_u64(out + n, sizeof out - n, g_st.ops);
    n += fmt_str(out + n, sizeof out - n, " accounts=");
    n += fmt_u64(out + n, sizeof out - n, g_st.tl.num_accounts);
    n += fmt_str(out + n, sizeof out - n, " txns=");
    n += fmt_u64(out + n, sizeof out - n, g_st.vino.num_txns);
    out[n++] = '\n';
    for (acc = 0; acc < g_st.tl.num_accounts && n < sizeof out - 200; acc++) {
        n += fmt_str(out + n, sizeof out - n, " ");
        n += fmt_str(out + n, sizeof out - n, g_st.holders[acc]);
        n += fmt_str(out + n, sizeof out - n, ":");
        for (v = 0; v < g_st.tl.accounts[acc].num_vouchers; v++) {
            const floating_voucher_t *fv = &g_st.tl.accounts[acc].vouchers[v];
            n += fmt_str(out + n, sizeof out - n, " v");
            n += fmt_u64(out + n, sizeof out - n, fv->voucher_id);
            n += fmt_str(out + n, sizeof out - n, "=");
            n += fmt_u64(out + n, sizeof out - n,
                         (uint64_t) (fv->merit_value / SR_ONE));
            if (fv->redeemed) n += fmt_str(out + n, sizeof out - n, "R");
        }
        out[n++] = '\n';
    }
    if (n >= sizeof out) n = sizeof out - 1;
    out[n] = 0;
    env_value_return(n, (uint64_t) (uintptr_t) out);
}

/* view: the vino chain integrity code (0 = OK) */
__attribute__((export_name("chain_verify")))
void chain_verify(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    uint8_t out[5];
    uint32_t bad = 0;
    int32_t code;
    (void) a;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    code = vino_chain_verify(&g_st.vino, &bad);
    out[0] = 0;
    wr_u32be(out + 1, (uint32_t) code);
    env_value_return(sizeof out, (uint64_t) (uintptr_t) out);
}

/* view: pure SCP-144 lattice binding of two 32-byte identities */
__attribute__((export_name("scp144_bind")))
void scp144_bind(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    uint8_t out[32];
    if (alen != 64) fail("zxv: scp144 args (init 32 + resp 32)");
    scp144_lattice_bind(a, a + 32, out);
    env_value_return(sizeof out, (uint64_t) (uintptr_t) out);
}

/* view: the raw on-chain state facts — key presence, stored length, the
 * first bytes read back. A forensic boot report: what the chain actually
 * holds for zxv:v1, compared against what this binary expects. */
__attribute__((export_name("state_diag")))
void state_diag(void)
{
    static const uint8_t key[] = ZXV_NEAR_STATE_KEY;
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    static char out[256];
    uint64_t n = 0, stored;
    (void) a;
    n += fmt_u64(out + n, sizeof out - n,
                 env_storage_has_key(sizeof key - 1,
                                     (uint64_t) (uintptr_t) key) ? 1 : 0);
    n += fmt_str(out + n, sizeof out - n, ":");
    stored = env_storage_read(sizeof key - 1, (uint64_t) (uintptr_t) key, 0);
    if (stored == NEAR_REG_EMPTY) {
        n += fmt_str(out + n, sizeof out - n, "none");
    } else {
        n += fmt_u64(out + n, sizeof out - n, stored);
        n += fmt_str(out + n, sizeof out - n, ":expect:");
        n += fmt_u64(out + n, sizeof out - n, sizeof g_st);
        n += fmt_str(out + n, sizeof out - n, ":head:");
        /* read the first 12 bytes of the stored value and hex them */
        {
            uint8_t head[12];
            static const char hex[] = "0123456789abcdef";
            uint32_t j;
            env_read_register(0, (uint64_t) (uintptr_t) head);
            for (j = 0; j < 12 && j < stored; j++) {
                out[n++] = hex[head[j] >> 4];
                out[n++] = hex[head[j] & 15];
            }
        }
    }
    out[n] = 0;
    env_value_return(n, (uint64_t) (uintptr_t) out);
}

/* view+event: the public boot announcement. Emits a NEP-297 standard event
 * ("EVENT_JSON:" log) that every NEAR indexer, dashboard and watchdog
 * parses, carrying the root IPFS CID of the ZXV release, the contract's
 * code hash and the SCP-144 lattice binding. Anyone can call it — it is
 * the advertisement surface. Also returns the same facts as text. */
__attribute__((export_name("zxv_announce")))
void zxv_announce(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    static char log[512];
    static char out[384];
    uint64_t n = 0;
    uint32_t i;
    (void) a;
    {
        static const char prefix[] =
            "EVENT_JSON:{\"standard\":\"nep297\",\"version\":\"1.0.0\","
            "\"event\":\"zxv_boot\",\"data\":[{\"root_cid\":"
            "\"QmSskhPoGK3oJG5ZQtRK26yRCDjxcB8R6uZQggmsUffChB\","
            "\"contract\":\"6ca890f766fdaeccd6c934da325f5f06ebc63ca9be4f5f1b44d0686af93c64a8\","
            "\"code_hash\":\"EMyZ2XWmsV364xCQKUF4YcUx2g1ZiHB7xBUzBXDKLjmf\","
            "\"scp144\":\"5b7fea6e5370d78ddfd5a1a34ff5258ff5a4067ba39ad95c1d8ef067e3c7eab5\"}]}";
        uint32_t p = 0;
        for (p = 0; p < sizeof prefix - 1; p++) log[p] = prefix[p];
        env_log_utf8(sizeof prefix - 1, (uint64_t) (uintptr_t) log);
    }
    n += fmt_str(out + n, sizeof out - n,
                 "boot:root_cid=QmSskhPoGK3oJG5ZQtRK26yRCDjxcB8R6uZQggmsUffChB "
                 "contract=6ca890f766fdaeccd6c934da325f5f06ebc63ca9be4f5f1b44d0686af93c64a8 "
                 "code=EMyZ2XWmsV364xCQKUF4YcUx2g1ZiHB7xBUzBXDKLjmf scp144=");
    {
        static const uint8_t bind32[32] = {
            0x5b, 0x7f, 0xea, 0x6e, 0x53, 0x70, 0xd7, 0x8d,
            0xdf, 0xd5, 0xa1, 0xa3, 0x4f, 0xf5, 0x25, 0x8f,
            0xf5, 0xa4, 0x06, 0x7b, 0xa3, 0x9a, 0xd9, 0x5c,
            0x1d, 0x8e, 0xf0, 0x67, 0xe3, 0xc7, 0xea, 0xb5};
        static const char hex[] = "0123456789abcdef";
        for (i = 0; i < 32; i++) {
            out[n++] = hex[bind32[i] >> 4];
            out[n++] = hex[bind32[i] & 15];
        }
    }
    out[n] = 0;
    env_value_return(n, (uint64_t) (uintptr_t) out);
}

/* view: the definitive state probe. storage_read into register 3 (never
 * touched by args/signer), then report the returned length AND the
 * register length, so the on-chain read-back bug is pinned exactly. */
__attribute__((export_name("read_probe")))
void read_probe(void)
{
    static const uint8_t key[] = ZXV_NEAR_STATE_KEY;
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    static char out[256];
    uint64_t n = 0, stored, rl;
    (void) a;
    n += fmt_u64(out + n, sizeof out - n,
                 env_storage_has_key(sizeof key - 1,
                                     (uint64_t) (uintptr_t) key) ? 1 : 0);
    n += fmt_str(out + n, sizeof out - n, ":read:");
    stored = env_storage_read(sizeof key - 1, (uint64_t) (uintptr_t) key, 3);
    if (stored == NEAR_REG_EMPTY) {
        n += fmt_str(out + n, sizeof out - n, "none");
    } else {
        n += fmt_u64(out + n, sizeof out - n, stored);
    }
    n += fmt_str(out + n, sizeof out - n, ":reg3:");
    rl = env_register_len(3);
    if (rl == NEAR_REG_EMPTY) {
        n += fmt_str(out + n, sizeof out - n, "empty");
    } else {
        n += fmt_u64(out + n, sizeof out - n, rl);
    }
    out[n] = 0;
    env_value_return(n, (uint64_t) (uintptr_t) out);
}

/* ---- the paraconsistent gift ------------------------------------------- */

/* view: pure four-valued logic, public and gas-free. Args: [op u8][a u8]
 * [b u8]; op 0=conjoin (meet), 1=disjoin (join), 2=kjoin (knowledge).
 * Returns one byte: the verdict state. Any contract or agent on NEAR can
 * ask "what do these two observations say" and get the honest four-valued
 * answer — NEITHER/TRUE/FALSE/BOTH — never a forced true/false. */
__attribute__((export_name("lpres_join")))
void lpres_join(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    uint8_t out[1];
    uint8_t op, x, y;
    lpres_state_t r = LPRES_STATE_NEITHER;
    if (alen != 3) fail("zxv: lpres_join args (op,a,b)");
    op = a[0];
    x = a[1];
    y = a[2];
    if (x >= LPRES_STATE_COUNT || y >= LPRES_STATE_COUNT)
        fail("zxv: lpres state out of range");
    switch (op) {
    case 0: r = lpres_conjoin((lpres_state_t) x, (lpres_state_t) y); break;
    case 1: r = lpres_disjoin((lpres_state_t) x, (lpres_state_t) y); break;
    case 2: r = lpres_kjoin((lpres_state_t) x, (lpres_state_t) y); break;
    default: fail("zxv: lpres op");
    }
    out[0] = (uint8_t) r;
    env_value_return(1, (uint64_t) (uintptr_t) out);
}

/* view: the ledger's own evidence joined — every on-chain voucher's
 * existence state (TRUE = confirmed issue, FALSE = rejected, NEITHER =
 * absent) kjoined into one verdict about the ledger's health. Public,
 * gas-free, deterministic. */
__attribute__((export_name("lpres_verdicts")))
void lpres_verdicts(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    static char out[256];
    uint32_t n = 0, i;
    lpres_state_t v = LPRES_STATE_NEITHER;
    (void) a;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    for (i = 0; i < g_st.tl.num_accounts; i++) {
        uint32_t j;
        for (j = 0; j < g_st.tl.accounts[i].num_vouchers; j++) {
            const floating_voucher_t *fv = &g_st.tl.accounts[i].vouchers[j];
            lpres_state_t s = fv->redeemed ? LPRES_STATE_FALSE
                                           : LPRES_STATE_TRUE;
            v = lpres_kjoin(v, s);
        }
    }
    n += fmt_str(out + n, sizeof out - n, "ledger=");
    out[n++] = (char) ('0' + (uint32_t) v);
    n += fmt_str(out + n, sizeof out - n, " vouchers=");
    n += fmt_u64(out + n, sizeof out - n, g_st.tl.next_voucher_id - 1);
    n += fmt_str(out + n, sizeof out - n, " chain=");
    n += fmt_u64(out + n, sizeof out - n, (uint64_t) vino_chain_verify(&g_st.vino, 0));
    out[n] = 0;
    env_value_return(n, (uint64_t) (uintptr_t) out);
}

/* ---- the information-economy tokenomics --------------------------------- */

/* The tribute addresses (public bearer identities, never secrets) and the
 * card accounts, exactly as the fleet kernels use them. The economy on
 * the L2 mirrors the fleet's: earned float splits 50/50 into the two
 * Black Swan cards, 11% tributes flow to Crown/Ministry/Fountain, and
 * the fountain pays its own 11% outflow to the African Union. The
 * unallocated remainder stays with the protocol — never a tribute rail. */
static const char *const ECON_CARDS[2] = {
    "BSC-0-1870892d", "BSC-1-8b58e9d4",
};
static const char *const ECON_TRIBUTE[TRIBUTE_N] = {
    "TRIBUTE-CROWN", "TRIBUTE-MINISTRY", "TRIBUTE-FOUNTAIN",
    "TRIBUTE-AFRICAN-UNION",
};

/* view+issue: information-economy-gated issuance. Args (all u64be):
 *   [earned u64][records_served u64][wants u64]
 * The paraconsistent rule is the governance: only TRUE authorizes. A
 * claim of earned float is TRUE only when the ledger confirms positive
 * records served AND the network shows demand (wants) — otherwise the
 * evidence kjoins to NEITHER or BOTH and issuance is refused. When it
 * issues, the split mirrors the kernel exactly (50/50 cards, 11%
 * tributes, fountain outflow). Owner-only, like the fleet's budget gate. */
__attribute__((export_name("econ_issue")))
void econ_issue(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    const char *signer = fetch_signer();
    uint64_t earned, served, wants;
    lpres_state_t v_served, v_wants, verdict;
    uint32_t i;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    if (fs_strcmp(signer, g_st.owner) != 0) fail("zxv: only owner may issue");
    if (alen != 24) fail("zxv: econ args (earned,served,wants)");
    earned = rd_u64be(a);
    served = rd_u64be(a + 8);
    wants = rd_u64be(a + 16);
    if (earned == 0) fail("zxv: no earned float");
    /* the evidence verdict: served>0 is TRUE, none is NEITHER; wants>0 is
     * TRUE, none is NEITHER. A claimed earning with no served records and
     * no demand kjoins NEITHER — never authority. */
    v_served = served > 0 ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    v_wants = wants > 0 ? LPRES_STATE_TRUE : LPRES_STATE_NEITHER;
    verdict = lpres_kjoin(v_served, v_wants);
    if (verdict != LPRES_STATE_TRUE)
        fail("zxv: evidence not TRUE (only TRUE authorizes)");
    /* mirror the kernel's split of the earned float */
    for (i = 0; i < 2; i++) {
        uint64_t part = i == 0 ? earned / 2 + (earned & 1u) : earned / 2;
        uint32_t h = (uint32_t) holder_register(ECON_CARDS[i]);
        if (h >= ZXV_TL_MAX_ACCOUNTS) fail("zxv: holder table full");
        if (vino_issue(&g_st.vino, vino_addr(ECON_CARDS[i]), ASSET_TOKEN,
                       part, "$YHVH earned float") < 0)
            fail("zxv: card issue");
    }
    {
        uint64_t tr[TRIBUTE_N];
        protocol_tribute_split(earned, tr);
        for (i = 0; i < 3; i++) {
            if (!tr[i]) continue;
            uint32_t h = (uint32_t) holder_register(ECON_TRIBUTE[i]);
            if (h >= ZXV_TL_MAX_ACCOUNTS) fail("zxv: holder table full");
            if (vino_issue(&g_st.vino, vino_addr(ECON_TRIBUTE[i]), ASSET_TOKEN,
                           tr[i], "11% protocol tribute") < 0)
                fail("zxv: tribute issue");
        }
        {
            const vino_account_t *fa = vino_get_account(
                &g_st.vino, vino_addr(ECON_TRIBUTE[TRIBUTE_FOUNTAIN]));
            uint64_t fbal = fa ? fa->asset_balances[ASSET_TOKEN] : 0;
            uint64_t out = tribute_fountain_outflow(fbal);
            if (out) {
                uint32_t h = (uint32_t) holder_register(
                    ECON_TRIBUTE[TRIBUTE_AFRICAN_UNION]);
                if (h >= ZXV_TL_MAX_ACCOUNTS) fail("zxv: holder table full");
                if (vino_issue(&g_st.vino,
                               vino_addr(ECON_TRIBUTE[TRIBUTE_AFRICAN_UNION]),
                               ASSET_TOKEN, out,
                               "the fountain's 11% to the Union") < 0)
                    fail("zxv: union issue");
            }
        }
    }
    state_store();
    {
        uint8_t out[9];
        out[0] = 0;
        wr_u64be(out + 1, g_st.tl.next_voucher_id);
        env_value_return(sizeof out, (uint64_t) (uintptr_t) out);
    }
}

/* view: the economy's state — every account's $YHVH balance. Public,
 * gas-free; the information economy's ledger readable by anyone. */
__attribute__((export_name("econ_state")))
void econ_state(void)
{
    uint64_t alen;
    const uint8_t *a = args_bytes(&alen);
    static char out[1024];
    uint32_t n = 0, i;
    (void) a;
    state_load();
    if (!g_st.booted) fail("zxv: not booted");
    for (i = 0; i < g_st.vino.num_accounts && n < sizeof out - 96; i++) {
        const vino_account_t *ac = &g_st.vino.balances[i];
        uint32_t j;
        n += fmt_str(out + n, sizeof out - n, ac->address);
        n += fmt_str(out + n, sizeof out - n, "=");
        n += fmt_u64(out + n, sizeof out - n, ac->asset_balances[ASSET_TOKEN]);
        n += fmt_str(out + n, sizeof out - n, " ");
        (void) j;
    }
    out[n] = 0;
    env_value_return(n, (uint64_t) (uintptr_t) out);
}
