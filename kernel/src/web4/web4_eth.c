/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_eth.c — Ethereum transactions, addresses, EIP-55, ABI, EIP-191 and
 * JSON-RPC (see web4_web3.h). */
#include "web4_web3.h"

#define RPC_TOKS 96u

static void put_to(w4_rlp_enc_t *e, const w4_eth_tx_t *tx)
{
    if (tx->has_to)
        w4_rlp_bytes(e, tx->to, W4_ETH_ADDR_LEN);
    else
        w4_rlp_bytes(e, NULL, 0);
}

static void legacy_body(w4_rlp_enc_t *e, const w4_eth_tx_t *tx)
{
    w4_rlp_u64(e, tx->nonce);
    w4_rlp_u256(e, &tx->gas_price);
    w4_rlp_u64(e, tx->gas_limit);
    put_to(e, tx);
    w4_rlp_u256(e, &tx->value);
    w4_rlp_bytes(e, tx->data, tx->data_len);
}

int32_t w4_eth_legacy_unsigned(const w4_eth_tx_t *tx, uint8_t *out, uint32_t cap)
{
    if (!tx || tx->data_len > W4_ETH_DATA_MAX || (tx->data_len && !tx->data)) return W4_ERR_ARG;
    w4_rlp_enc_t e;
    w4_rlp_init(&e, out, cap);
    w4_rlp_list_begin(&e);
    legacy_body(&e, tx);
    if (tx->chain_id) { /* EIP-155 */
        w4_rlp_u64(&e, tx->chain_id);
        w4_rlp_u64(&e, 0);
        w4_rlp_u64(&e, 0);
    }
    w4_rlp_list_end(&e);
    return w4_rlp_finish(&e);
}

static void b1559_body(w4_rlp_enc_t *e, const w4_eth_tx_t *tx)
{
    w4_rlp_u64(e, tx->chain_id);
    w4_rlp_u64(e, tx->nonce);
    w4_rlp_u256(e, &tx->max_priority);
    w4_rlp_u256(e, &tx->max_fee);
    w4_rlp_u64(e, tx->gas_limit);
    put_to(e, tx);
    w4_rlp_u256(e, &tx->value);
    w4_rlp_bytes(e, tx->data, tx->data_len);
    w4_rlp_list_begin(e); /* accessList: empty */
    w4_rlp_list_end(e);
}

int32_t w4_eth_1559_unsigned(const w4_eth_tx_t *tx, uint8_t *out, uint32_t cap)
{
    if (!tx || !tx->chain_id || tx->data_len > W4_ETH_DATA_MAX || (tx->data_len && !tx->data) ||
        cap < 1)
        return W4_ERR_ARG;
    out[0] = 0x02;
    w4_rlp_enc_t e;
    w4_rlp_init(&e, out + 1, cap - 1);
    w4_rlp_list_begin(&e);
    b1559_body(&e, tx);
    w4_rlp_list_end(&e);
    int32_t n = w4_rlp_finish(&e);
    return n < 0 ? n : n + 1;
}

void w4_eth_signing_hash(const uint8_t *payload, uint32_t len, uint8_t hash[32])
{
    w4_keccak256(payload, len, hash);
}

static void put_rs(w4_rlp_enc_t *e, const w4_ecdsa_sig_t *sig)
{
    w4_u256 r, s;
    w4_u256_from_be(&r, sig->r, 32);
    w4_u256_from_be(&s, sig->s, 32);
    w4_rlp_u256(e, &r);
    w4_rlp_u256(e, &s);
}

int32_t w4_eth_legacy_signed(const w4_eth_tx_t *tx, const w4_ecdsa_sig_t *sig, uint8_t *out,
                             uint32_t cap)
{
    if (!tx || !sig || sig->recid > 1 || tx->data_len > W4_ETH_DATA_MAX) return W4_ERR_ARG;
    if (tx->chain_id > (UINT64_MAX - 36u) / 2u) return W4_ERR_RANGE;
    w4_rlp_enc_t e;
    w4_rlp_init(&e, out, cap);
    w4_rlp_list_begin(&e);
    legacy_body(&e, tx);
    uint64_t v = tx->chain_id ? (uint64_t) sig->recid + 35u + 2u * tx->chain_id
                              : (uint64_t) sig->recid + 27u;
    w4_rlp_u64(&e, v);
    put_rs(&e, sig);
    w4_rlp_list_end(&e);
    return w4_rlp_finish(&e);
}

int32_t w4_eth_1559_signed(const w4_eth_tx_t *tx, const w4_ecdsa_sig_t *sig, uint8_t *out,
                           uint32_t cap)
{
    if (!tx || !sig || sig->recid > 1 || !tx->chain_id || cap < 1) return W4_ERR_ARG;
    out[0] = 0x02;
    w4_rlp_enc_t e;
    w4_rlp_init(&e, out + 1, cap - 1);
    w4_rlp_list_begin(&e);
    b1559_body(&e, tx);
    w4_rlp_u64(&e, sig->recid & 1u);
    put_rs(&e, sig);
    w4_rlp_list_end(&e);
    int32_t n = w4_rlp_finish(&e);
    return n < 0 ? n : n + 1;
}

int w4_eth_1559_decode(const uint8_t *raw, uint32_t len, w4_eth_tx_t *tx, w4_ecdsa_sig_t *sig)
{
    if (!raw || len < 2 || raw[0] != 0x02 || !tx || !sig) return W4_ERR_PARSE;
    w4_memset(tx, 0, sizeof *tx);
    w4_memset(sig, 0, sizeof *sig);
    w4_rlp_item_t l, it;
    if (w4_rlp_decode(raw + 1, len - 1, &l) || !l.is_list || l.total != len - 1)
        return W4_ERR_PARSE;
    uint32_t off = 0;
    uint64_t yp;
    w4_u256 r, s;
#define NEXT()                                                                                     \
    do {                                                                                           \
        if (w4_rlp_next(&l, &off, &it)) return W4_ERR_PARSE;                                       \
    } while (0)
    NEXT();
    if (w4_rlp_as_u64(&it, &tx->chain_id) || !tx->chain_id) return W4_ERR_PARSE;
    NEXT();
    if (w4_rlp_as_u64(&it, &tx->nonce)) return W4_ERR_PARSE;
    NEXT();
    if (w4_rlp_as_u256(&it, &tx->max_priority)) return W4_ERR_PARSE;
    NEXT();
    if (w4_rlp_as_u256(&it, &tx->max_fee)) return W4_ERR_PARSE;
    NEXT();
    if (w4_rlp_as_u64(&it, &tx->gas_limit)) return W4_ERR_PARSE;
    NEXT();
    if (it.is_list || (it.len != 0 && it.len != 20)) return W4_ERR_PARSE;
    tx->has_to = it.len == 20;
    if (tx->has_to) w4_memcpy(tx->to, it.p, 20);
    NEXT();
    if (w4_rlp_as_u256(&it, &tx->value)) return W4_ERR_PARSE;
    NEXT();
    if (it.is_list || it.len > W4_ETH_DATA_MAX) return W4_ERR_PARSE;
    tx->data = it.len ? it.p : NULL;
    tx->data_len = it.len;
    NEXT();
    if (!it.is_list || it.len != 0) return W4_ERR_UNSUPP; /* access lists not supported */
    NEXT();
    if (w4_rlp_as_u64(&it, &yp) || yp > 1) return W4_ERR_PARSE;
    NEXT();
    if (w4_rlp_as_u256(&it, &r)) return W4_ERR_PARSE;
    NEXT();
    if (w4_rlp_as_u256(&it, &s)) return W4_ERR_PARSE;
    if (w4_rlp_next(&l, &off, &it) != W4_ERR_NOTFOUND) return W4_ERR_PARSE;
#undef NEXT
    sig->recid = (uint8_t) yp;
    w4_u256_to_be(&r, sig->r);
    w4_u256_to_be(&s, sig->s);
    return W4_OK;
}

void w4_eth_tx_hash(const uint8_t *raw, uint32_t len, uint8_t hash[32])
{
    w4_keccak256(raw, len, hash);
}

void w4_eth_address(const uint8_t pub64[64], uint8_t addr[W4_ETH_ADDR_LEN])
{
    uint8_t h[32];
    w4_keccak256(pub64, 64, h);
    w4_memcpy(addr, h + 12, W4_ETH_ADDR_LEN);
}

void w4_eth_checksum(const uint8_t addr[W4_ETH_ADDR_LEN], char out[43])
{
    char low[41];
    uint8_t h[32];
    w4_hex_encode(addr, W4_ETH_ADDR_LEN, low, sizeof low);
    w4_keccak256((const uint8_t *) low, 40, h);
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 40; i++) {
        uint8_t nib = (uint8_t) ((i & 1) ? (h[i >> 1] & 15) : (h[i >> 1] >> 4));
        char c = low[i];
        out[2 + i] = (c >= 'a' && nib >= 8) ? (char) (c - 32) : c;
    }
    out[42] = 0;
}

int w4_eth_address_parse(const char *s, uint32_t len, uint8_t addr[W4_ETH_ADDR_LEN], bool *checked)
{
    if (!s || len != 42 || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return W4_ERR_PARSE;
    if (w4_hex_decode(s + 2, 40, addr, W4_ETH_ADDR_LEN) != 20) return W4_ERR_PARSE;
    bool lower = false, upper = false;
    for (int i = 2; i < 42; i++) {
        if (s[i] >= 'a' && s[i] <= 'f') lower = true;
        if (s[i] >= 'A' && s[i] <= 'F') upper = true;
    }
    if (checked) *checked = lower && upper;
    if (lower && upper) {
        char want[43];
        w4_eth_checksum(addr, want);
        if (!w4_memeq(want + 2, s + 2, 40)) return W4_ERR_HASH;
    }
    return W4_OK;
}

void w4_abi_selector(const char *signature, uint8_t sel[4])
{
    uint8_t h[32];
    w4_keccak256((const uint8_t *) signature, w4_strnlen(signature, 1024), h);
    w4_memcpy(sel, h, 4);
}

static void addr_word(uint8_t *w, const uint8_t a[20])
{
    w4_memset(w, 0, 12);
    w4_memcpy(w + 12, a, 20);
}

void w4_abi_erc20_transfer(const uint8_t to[W4_ETH_ADDR_LEN], const w4_u256 *amount,
                           uint8_t out[68])
{
    static const uint8_t sel[4] = {0xa9, 0x05, 0x9c, 0xbb}; /* transfer(address,uint256) */
    w4_memcpy(out, sel, 4);
    addr_word(out + 4, to);
    w4_u256_to_be(amount, out + 36);
}

void w4_abi_erc20_balance_of(const uint8_t who[W4_ETH_ADDR_LEN], uint8_t out[36])
{
    static const uint8_t sel[4] = {0x70, 0xa0, 0x82, 0x31}; /* balanceOf(address) */
    w4_memcpy(out, sel, 4);
    addr_word(out + 4, who);
}

void w4_abi_word_u256(const uint8_t word[32], w4_u256 *v)
{
    w4_u256_from_be(v, word, 32);
}

int w4_abi_word_address(const uint8_t word[32], uint8_t addr[W4_ETH_ADDR_LEN])
{
    for (int i = 0; i < 12; i++)
        if (word[i]) return W4_ERR_PARSE;
    w4_memcpy(addr, word + 12, 20);
    return W4_OK;
}

void w4_eth_personal_hash(const uint8_t *msg, uint32_t len, uint8_t hash[32])
{
    /* Keccak-256 has no streaming API here; stage through a bounded buffer. */
    uint8_t buf[26 + 10 + 512];
    w4_w w;
    w4_w_init(&w, buf, sizeof buf);
    w4_w_str(&w, "\x19"
                 "Ethereum Signed Message:\n");
    w4_w_u64(&w, len);
    w4_w_bytes(&w, msg, len);
    if (w.err) {
        w4_memset(hash, 0, 32); /* messages over 512 bytes are refused */
        return;
    }
    w4_keccak256(buf, w.len, hash);
}

/* ===== JSON-RPC ===== */
static void rpc_head(w4_jw *j, uint64_t id, const char *method)
{
    w4_jw_obj(j);
    w4_jw_key(j, "jsonrpc");
    w4_jw_str(j, "2.0");
    w4_jw_key(j, "id");
    w4_jw_u64(j, id);
    w4_jw_key(j, "method");
    w4_jw_str(j, method);
    w4_jw_key(j, "params");
    w4_jw_arr(j);
}

static int32_t rpc_tail(w4_jw *j)
{
    w4_jw_arr_end(j);
    w4_jw_obj_end(j);
    return w4_jw_finish(j);
}

static bool block_ok(const char *b)
{
    if (w4_streq(b, "latest") || w4_streq(b, "pending") || w4_streq(b, "earliest") ||
        w4_streq(b, "safe") || w4_streq(b, "finalized"))
        return true;
    w4_u256 v;
    return w4_u256_from_qty(&v, b, (uint32_t) w4_strnlen(b, 80));
}

int32_t w4_rpc_eth_call(char *out, uint32_t cap, uint64_t id, const uint8_t to[20],
                        const uint8_t *data, uint32_t dlen, const char *block)
{
    if (!block) block = "latest";
    if (!block_ok(block) || dlen > W4_ETH_DATA_MAX) return W4_ERR_ARG;
    w4_jw j;
    w4_jw_init(&j, out, cap);
    rpc_head(&j, id, "eth_call");
    w4_jw_obj(&j);
    w4_jw_key(&j, "to");
    w4_jw_hexstr(&j, to, 20);
    w4_jw_key(&j, "data");
    w4_jw_hexstr(&j, data, dlen);
    w4_jw_obj_end(&j);
    w4_jw_str(&j, block);
    return rpc_tail(&j);
}

int32_t w4_rpc_eth_send_raw(char *out, uint32_t cap, uint64_t id, const uint8_t *raw, uint32_t len)
{
    w4_jw j;
    w4_jw_init(&j, out, cap);
    rpc_head(&j, id, "eth_sendRawTransaction");
    w4_jw_hexstr(&j, raw, len);
    return rpc_tail(&j);
}

static int32_t addr_block(char *out, uint32_t cap, uint64_t id, const char *m,
                          const uint8_t addr[20], const char *block)
{
    if (!block) block = "latest";
    if (!block_ok(block)) return W4_ERR_ARG;
    w4_jw j;
    w4_jw_init(&j, out, cap);
    rpc_head(&j, id, m);
    w4_jw_hexstr(&j, addr, 20);
    w4_jw_str(&j, block);
    return rpc_tail(&j);
}

int32_t w4_rpc_eth_get_balance(char *out, uint32_t cap, uint64_t id, const uint8_t addr[20],
                               const char *block)
{
    return addr_block(out, cap, id, "eth_getBalance", addr, block);
}

int32_t w4_rpc_eth_get_tx_count(char *out, uint32_t cap, uint64_t id, const uint8_t addr[20],
                                const char *block)
{
    return addr_block(out, cap, id, "eth_getTransactionCount", addr, block);
}

int w4_rpc_parse(const char *json, uint32_t len, w4_rpc_result_t *r)
{
    if (!json || !r) return W4_ERR_ARG;
    w4_memset(r, 0, sizeof *r);
    w4_jtok_t t[RPC_TOKS];
    int32_t n = w4_json_parse(json, len, t, RPC_TOKS, 8);
    if (n <= 0 || t[0].type != W4_J_OBJ) return W4_ERR_PARSE;
    int32_t v = w4_json_get(json, t, n, 0, "jsonrpc");
    if (v < 0 || !w4_json_str_eq(json, &t[v], "2.0")) return W4_ERR_PARSE;
    if (w4_json_get_u64(json, t, n, 0, "id", &r->id)) return W4_ERR_PARSE;
    int32_t er = w4_json_get(json, t, n, 0, "error");
    int32_t res = w4_json_get(json, t, n, 0, "result");
    if (er == W4_ERR_PARSE || res == W4_ERR_PARSE) return W4_ERR_PARSE;
    if ((er >= 0) == (res >= 0)) return W4_ERR_PARSE; /* exactly one of them */
    if (er >= 0) {
        if (t[er].type != W4_J_OBJ) return W4_ERR_PARSE;
        int32_t c = w4_json_get(json, t, n, er, "code");
        if (c < 0 || w4_json_i64(json, &t[c], &r->err_code)) return W4_ERR_PARSE;
        int m = w4_json_get_str(json, t, n, er, "message", r->err_msg, sizeof r->err_msg);
        if (m != W4_OK && m != W4_ERR_NOTFOUND && m != W4_ERR_SPACE) return W4_ERR_PARSE;
        r->is_error = true;
        return W4_OK;
    }
    if (t[res].type != W4_J_STR) return W4_ERR_UNSUPP; /* objects (receipts...) not modelled */
    int32_t sl = w4_json_str(json, &t[res], r->result_str, sizeof r->result_str);
    uint32_t L = t[res].end - t[res].start;
    const char *s = json + t[res].start;
    if (L >= 2 && s[0] == '0' && s[1] == 'x') {
        /* odd-length hex is a QUANTITY: decode with a leading zero nibble */
        uint32_t hl = L - 2;
        if ((hl + 1) / 2 > sizeof r->data) return W4_ERR_SPACE;
        uint32_t o = 0, i = 2;
        if (hl & 1u) {
            int h = w4_hexval(s[i++]);
            if (h < 0) return W4_ERR_PARSE;
            r->data[o++] = (uint8_t) h;
        }
        for (; i < L; i += 2) {
            int a = w4_hexval(s[i]), b = w4_hexval(s[i + 1]);
            if (a < 0 || b < 0) return W4_ERR_PARSE;
            r->data[o++] = (uint8_t) ((a << 4) | b);
        }
        r->data_len = o;
    } else if (sl < 0) {
        return W4_ERR_SPACE;
    }
    return W4_OK;
}

int w4_rpc_result_qty(const w4_rpc_result_t *r, w4_u256 *v)
{
    if (!r || r->is_error) return W4_ERR_STATE;
    return w4_u256_from_qty(v, r->result_str, (uint32_t) w4_strnlen(r->result_str, 160))
               ? W4_OK
               : W4_ERR_PARSE;
}
