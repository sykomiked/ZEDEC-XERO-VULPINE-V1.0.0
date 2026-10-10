/* vino.c — Vino Decentralized Bank Node Implementation
 * Author: H.M. Michael-Laurence: Curzi (c)
 */
#include "vino.h"
#include "m5_types.h"
#include "rmag_core.h"
#include "lpres_core.h"
#ifndef TEST_HOST
#include "../include/freestanding.h"
#else
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#define fs_memset memset
#define fs_memcpy memcpy
#define fs_strlen strlen
#define fs_strcpy strcpy
#endif

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int str_cmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
/* Bounded copy: every destination is a fixed array, so str_copy() passes its
 * size and an over-long caller string is truncated, never written past it. */
static void str_ncopy(char *d, const char *s, uint32_t cap)
{
    uint32_t i = 0;
    if (cap == 0) return;
    if (s)
        while (s[i] && i + 1u < cap) {
            d[i] = s[i];
            i++;
        }
    d[i] = 0;
}
#define str_copy(d, s) str_ncopy((d), (s), (uint32_t) sizeof(d))

static const char *cap_names[CAP_MAX] = {
    "FINANCIAL","MATERIAL","INTELLECTUAL","SOCIAL","CULTURAL",
    "SPIRITUAL","NATURAL","TEMPORAL","RELATIONAL"
};
static const char *asset_names[ASSET_MAX] = {
    "Currency","Crypto","Equity","Bond","Commodity","Option",
    "Future","Forex","Token","NFT","CBDC","Stablecoin"
};
static const char *rail_names[RAIL_MAX] = {
    "Vino-Native","SWIFT","CIPS","SPFS","ISO20022","Visa","MasterCard",
    "Hormung","EVC","Blockchain","SEPA","Fedwire","CHIPS","RTGS","ACH",
    "UPI","Pix","FAST","NPP","Interac"
};
static const char *msg_names[MSG_MAX] = {
    "Vino-Native","ISO20022","CAMT.053","CAMT.054","PACS.008","PACS.009",
    "PAIN.001","MT103","MT202","MX-Head","CIPS.001","SPFS"
};

const char *vino_capital_name(capital_type_t c) { return c<CAP_MAX?cap_names[c]:"UNKNOWN"; }
const char *vino_asset_class_name(asset_class_t a) { return a<ASSET_MAX?asset_names[a]:"?"; }
const char *vino_rail_name(payment_rail_t r) { return r<RAIL_MAX?rail_names[r]:"?"; }
const char *vino_msg_standard_name(msg_standard_t m) { return m<MSG_MAX?msg_names[m]:"?"; }

void vino_hash(const void *data, uint32_t len, uint8_t out[VINO_HASH_LEN]) {
    /* FNV-1a 32-bit stretched to 32 bytes. NOT cryptographic: only 32 bits
     * of state, trivially forgeable. */
    const uint8_t *d = data;
    uint32_t h = 2166136261u;
    for (uint32_t i = 0; i < len; i++) { h ^= d[i]; h *= 16777619u; }
    for (int i = 0; i < VINO_HASH_LEN; i++) {
        h ^= h >> 13; h *= 16777619u;
        out[i] = (uint8_t)(h & 0xFF);
    }
}

void vino_init(vino_ledger_t *v, uint32_t node_id) {
    fs_memset(v, 0, sizeof(*v));
    v->node_id = node_id;
    v->consensus_threshold = 67; /* 67% */
    v->is_validator = false;

    /* M5: initialize RMAG and LPRES if not already done */
    rmag_init(256);
    lpres_init();
}

int32_t vino_create_account(vino_ledger_t *v, const char *addr, const char *name) {
    if (v->num_accounts >= VINO_MAX_ACCOUNTS) return -1;
    if (!addr || !addr[0]) return -1;
    /* one account per address: a duplicate would be unreachable (lookups
     * return the first) while still being counted */
    if (vino_get_account(v, addr)) return -1;
    vino_account_t *a = &v->balances[v->num_accounts];
    str_copy(a->address, addr);
    str_copy(a->name, name);
    a->active = true;
    a->nonce = 0;
    return (int32_t)v->num_accounts++;
}

vino_account_t *vino_get_account(vino_ledger_t *v, const char *addr) {
    for (uint32_t i = 0; i < v->num_accounts; i++)
        if (str_cmp(v->balances[i].address, addr) == 0) return &v->balances[i];
    return 0;
}

/* Would rmag_add_quotas (sub = false) or rmag_sub_quotas (sub = true) of a
 * and b overflow int64? Those helpers compute a.num*b.den +/- b.num*a.den
 * over a.den*b.den with plain int64 arithmetic, so a quota shadow that has
 * absorbed more than 2^63 of flow overflowed (signed overflow, undefined
 * behaviour; found by fuzz_econ_vino). A result of INT64_MIN is refused
 * too: rational_normalize would negate it. */
static bool quota_overflows(rational_t a, rational_t b, bool sub)
{
    int64_t x, y, r;
    if (__builtin_mul_overflow(a.num, b.den, &x) || __builtin_mul_overflow(b.num, a.den, &y) ||
        __builtin_mul_overflow(a.den, b.den, &r))
        return true;
    if (sub ? __builtin_sub_overflow(x, y, &r) : __builtin_add_overflow(x, y, &r)) return true;
    /* rational_normalize negates (via m5_gcd): INT64_MIN has no negation */
    return r == INT64_MIN;
}

int32_t vino_transfer(vino_ledger_t *v, const char *from, const char *to,
                       uint64_t amount, capital_type_t cap,
                       payment_rail_t rail, const char *memo) {
    if (v->num_txns >= VINO_MAX_TXNS) return -1;
    vino_account_t *fa = vino_get_account(v, from);
    vino_account_t *ta = vino_get_account(v, to);
    if (!fa || !ta) return -1;
    if ((uint32_t) cap >= CAP_MAX) return -1;     /* balance[cap] is CAP_MAX wide */
    if (amount > (uint64_t) INT64_MAX) return -1; /* the RMAG rational is signed  */
    if (fa->balance[cap] < amount) return -1;
    /* a credit that wraps would destroy value instead of moving it */
    if (fa != ta && ta->balance[cap] > UINT64_MAX - amount) return -1;

    /* M5 LPRES: attestation — both accounts must have logical presence */
    ordinal_t from_ord = (ordinal_t)(v->balances[0].address[0] ? fa - v->balances + 1 : 0);
    ordinal_t to_ord = (ordinal_t)(ta - v->balances + 1);
    /* the exact-rational quota shadow (RMAG, below) must be able to hold the
     * result; refuse before any state changes if it cannot */
    {
        rational_t amt_q = {(int64_t) amount, 1};
        if (quota_overflows(rmag_get_quota(from_ord), amt_q, true) ||
            quota_overflows(rmag_get_quota(to_ord), amt_q, false))
            return -1;
    }
    trit_t from_presence = lpres_get_presence(from_ord);
    trit_t to_presence = lpres_get_presence(to_ord);
    /* Auto-attest new accounts with TRUE presence on first transfer */
    if (from_presence == TRIT_FALSE) {
        lpres_set_presence(from_ord, TRIT_TRUE);
        from_presence = TRIT_TRUE;
    }
    if (to_presence == TRIT_FALSE) {
        lpres_set_presence(to_ord, TRIT_TRUE);
        to_presence = TRIT_TRUE;
    }
    if (from_presence == TRIT_FALSE || to_presence == TRIT_FALSE) return -1;
    /* GLUT state: allow but flag for audit */
    bool audit_flag = (from_presence == TRIT_GLUT || to_presence == TRIT_GLUT);
    (void)audit_flag;

    /* M5 RMAG: exact rational accounting — no float drift in balances */
    rational_t txn_amount = { (int64_t)amount, 1 };
    rational_t from_quota = rmag_get_quota(from_ord);
    rational_t to_quota = rmag_get_quota(to_ord);
    rational_t new_from = rmag_sub_quotas(from_quota, txn_amount);
    rational_t new_to = rmag_add_quotas(to_quota, txn_amount);
    rmag_set_quota(from_ord, new_from);
    rmag_set_quota(to_ord, new_to);

    fa->balance[cap] -= amount;
    ta->balance[cap] += amount;
    fa->nonce++;

    vino_transaction_t *t = &v->primary[v->num_txns];
    t->id = v->num_txns;
    t->type = TXN_TRANSFER;
    t->capital = cap;
    t->amount = amount;
    t->rail = rail;
    str_copy(t->from_addr, from);
    str_copy(t->to_addr, to);
    if (memo) str_copy(t->memo, memo);
    t->confirmed = true;
    t->block_height = v->block_height;
    fs_memcpy(t->prev_hash, v->chain_head_hash, VINO_HASH_LEN);
    vino_hash(t, sizeof(*t), t->hash);
    fs_memcpy(v->chain_head_hash, t->hash, VINO_HASH_LEN);

    /* Audit ledger copy */
    fs_memcpy(&v->audit[v->num_txns], t, sizeof(*t));
    v->num_audit++;
    v->num_txns++;
    v->txn_count++;
    /* a running statistic: saturate rather than wrap back toward zero (the
     * wrap was found by fuzz_econ_vino under -fsanitize=integer) */
    if (v->total_volume[cap] > UINT64_MAX - amount)
        v->total_volume[cap] = UINT64_MAX;
    else
        v->total_volume[cap] += amount;
    return t->id;
}

int32_t vino_trade(vino_ledger_t *v, const char *from, const char *to,
                    asset_class_t asset, uint64_t amount, uint64_t price,
                    const char *memo) {
    (void)price; (void)asset;
    return vino_transfer(v, from, to, amount, CAP_FINANCIAL, RAIL_VINO_NATIVE, memo);
}

int32_t vino_issue(vino_ledger_t *v, const char *to, asset_class_t asset,
                    uint64_t amount, const char *memo) {
    vino_account_t *a = vino_get_account(v, to);
    if (!a) return -1;
    if ((uint32_t) asset >= ASSET_MAX) return -1; /* asset_balances is ASSET_MAX wide */
    /* the count is 32-bit: refuse an issue that would truncate or wrap it */
    if (amount > (uint64_t) (UINT32_MAX - a->asset_balances[asset])) return -1;
    /* check capacity BEFORE crediting, so a full ledger cannot mint unrecorded */
    if (v->num_txns >= VINO_MAX_TXNS) return -1;
    a->asset_balances[asset] += (uint32_t) amount;
    vino_transaction_t *t = &v->primary[v->num_txns];
    t->id = v->num_txns;
    t->type = TXN_ISSUE;
    t->asset = asset;
    t->amount = amount;
    str_copy(t->to_addr, to);
    if (memo) str_copy(t->memo, memo);
    t->confirmed = true;
    t->block_height = v->block_height;
    /* An issue is a ledger event like any other: chain it, audit it and count
     * it. (It used to stop here, so the next transfer overwrote the slot and
     * the minting left no trace in the primary, audit or hash-chain ledgers.) */
    fs_memcpy(t->prev_hash, v->chain_head_hash, VINO_HASH_LEN);
    vino_hash(t, sizeof(*t), t->hash);
    fs_memcpy(v->chain_head_hash, t->hash, VINO_HASH_LEN);
    fs_memcpy(&v->audit[v->num_txns], t, sizeof(*t));
    v->num_audit++;
    v->num_txns++;
    v->txn_count++;
    return t->id;
}

int32_t vino_bridge(vino_ledger_t *v, const char *from, const char *to,
                     uint64_t amount, payment_rail_t src, payment_rail_t dst,
                     const char *memo) {
    (void)src; (void)dst;
    return vino_transfer(v, from, to, amount, CAP_FINANCIAL, RAIL_BLOCKCHAIN, memo);
}

int32_t vino_get_balance(vino_ledger_t *v, const char *addr,
                          capital_type_t cap, uint64_t *out) {
    vino_account_t *a = vino_get_account(v, addr);
    if (!a) return -1;
    /* balance[] is CAP_MAX wide: an out-of-range capital read past it
     * (found by fuzz_econ_vino) */
    if ((uint32_t) cap >= CAP_MAX || !out) return -1;
    *out = a->balance[cap];
    return 0;
}

int32_t vino_register_asset(vino_ledger_t *v, const char *sym, const char *name,
                             asset_class_t cls, uint32_t prec, uint64_t supply) {
    if (v->num_assets >= VINO_MAX_ASSETS) return -1;
    vino_asset_t *a = &v->assets[v->num_assets];
    str_copy(a->symbol, sym);
    str_copy(a->name, name);
    a->class = cls; a->precision = prec; a->total_supply = supply; a->active = true;
    return (int32_t)v->num_assets++;
}

vino_asset_t *vino_get_asset(vino_ledger_t *v, const char *sym) {
    for (uint32_t i = 0; i < v->num_assets; i++)
        if (str_cmp(v->assets[i].symbol, sym) == 0) return &v->assets[i];
    return 0;
}

int32_t vino_add_peer(vino_ledger_t *v, const char *addr, const char *ep) {
    if (v->num_peers >= VINO_MAX_PEERS) return -1;
    str_copy(v->peers[v->num_peers].address, addr);
    str_copy(v->peers[v->num_peers].endpoint, ep);
    v->peers[v->num_peers].connected = true;
    v->peers[v->num_peers].trust_score = 50;
    return (int32_t)v->num_peers++;
}

int32_t vino_remove_peer(vino_ledger_t *v, const char *addr) {
    for (uint32_t i = 0; i < v->num_peers; i++)
        if (str_cmp(v->peers[i].address, addr) == 0) { v->peers[i].connected = false; return 0; }
    return -1;
}

void vino_set_validator(vino_ledger_t *v, bool is_val, uint32_t stake) {
    v->is_validator = is_val;
    v->validator_stake = stake;
}

int32_t vino_propose_block(vino_ledger_t *v) {
    if (!v->is_validator) return -1;
    v->block_height++;
    return (int32_t)v->block_height;
}

int32_t vino_vote_block(vino_ledger_t *v, uint32_t height, bool approve) {
    (void)v; (void)height; (void)approve;
    return VINO_ENOTIMPL; /* no consensus exists */
}

int32_t vino_sync_peers(vino_ledger_t *v) {
    (void)v;
    return VINO_ENOTIMPL; /* no networking exists */
}

/* === Messaging adapters === */
int32_t vino_msg_to_iso20022(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void)txn;
    if (max_out < 128) return -1;
    /* Opening fragment only: not a conformant ISO 20022 message */
    int j = 0;
    j += fs_strlen(fs_strcpy(out+j, "<Doc:Document>"));
    j += fs_strlen(fs_strcpy(out+j, "<PmtInf>"));
    j += fs_strlen(fs_strcpy(out+j, "<Amt>"));
    /* amount */
    return j;
}

int32_t vino_msg_from_iso20022(const char *xml, vino_transaction_t *txn) {
    (void) xml;
    (void) txn;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_camt053(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void)txn;
    if (max_out < 128) return -1;
    int j = 0;
    j += fs_strlen(fs_strcpy(out+j, "<BkToCstmrStmt>"));
    j += fs_strlen(fs_strcpy(out+j, "<Stmt><Ntry>"));
    return j;
}

int32_t vino_msg_from_camt053(const char *xml, vino_transaction_t *txn) {
    (void) xml;
    (void) txn;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_mt103(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void)txn;
    if (max_out < 64) return -1;
    fs_strcpy(out, "{1:F01}{2:I103BANKDEFFXXXXN}{3:{108:VINO}}{4:");
    return (int32_t)str_len(out);
}

int32_t vino_msg_from_mt103(const char *msg, vino_transaction_t *txn) {
    (void) msg;
    (void) txn;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_pacs008(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void)txn;
    if (max_out < 64) return -1;
    fs_strcpy(out, "<pacs.008><CdtTrfTxInf>");
    return (int32_t)str_len(out);
}

int32_t vino_msg_to_cips(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void) txn;
    (void) out;
    (void) max_out;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_spfs(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void) txn;
    (void) out;
    (void) max_out;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_visa(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void) txn;
    (void) out;
    (void) max_out;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_mastercard(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void) txn;
    (void) out;
    (void) max_out;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_btc(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void) txn;
    (void) out;
    (void) max_out;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_to_eth(const vino_transaction_t *txn, char *out, uint32_t max_out) {
    (void) txn;
    (void) out;
    (void) max_out;
    return VINO_ENOTIMPL;
}

int32_t vino_msg_from_btc(const char *raw, vino_transaction_t *txn)
{
    (void) raw;
    (void) txn;
    return VINO_ENOTIMPL;
}
int32_t vino_msg_from_eth(const char *raw, vino_transaction_t *txn)
{
    (void) raw;
    (void) txn;
    return VINO_ENOTIMPL;
}

/* ---- DECLARATION -----------------------------------------------------------

 * The ledger three sutra files reach for (sutra_capital.o: vino_get_account,
 * vino_capital_name; sutra_rails.o: vino_msg_to_*; sutra_runtime.o:
 * vino_transfer). Its own `nm -u` is {lpres_*, rmag_*} and nothing else, so
 * both requirements below are edges the linker can see, not architecture
 * opinion: an account balance is an rmag quota, and an account's standing is
 * an lpres presence.
 */
#include "zxv_decl.h"
ZXV_DECLARE(vino,
    ZXV_PROVIDES(vino_ledger_ready),
    ZXV_REQUIRES(rmag_ready, lpres_ready),
    ZXV_NO_BRINGUP);
