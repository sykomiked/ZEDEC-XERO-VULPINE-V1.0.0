/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_crypto.c — address checks and transfer intents. See pay_crypto.h. */
#include "pay_crypto.h"
#include "sha256.h"

/* ===== Base58 ===== */

static int b58val(char c)
{
    static const char *a = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";
    for (int i = 0; i < 58; i++)
        if (a[i] == c) return i;
    return -1;
}

int32_t pay_base58_decode(const char *s, uint8_t *out, uint32_t cap)
{
    uint8_t buf[64];
    uint32_t len = 0, zeros = 0;
    size_t n = pay_strnlen(s, 128);
    if (!s || n == 0 || n > 100) return -1;
    while (zeros < n && s[zeros] == '1') zeros++;
    for (size_t i = 0; i < n; i++) {
        int v = b58val(s[i]);
        if (v < 0) return -1;
        uint32_t carry = (uint32_t) v;
        for (uint32_t j = 0; j < len; j++) {
            carry += (uint32_t) buf[j] * 58u;
            buf[j] = (uint8_t) carry;
            carry >>= 8;
        }
        while (carry) {
            if (len >= sizeof buf) return -1;
            buf[len++] = (uint8_t) carry;
            carry >>= 8;
        }
    }
    /* buf is little-endian; leading '1's are leading zero bytes */
    if (zeros + len > cap) return -1;
    for (uint32_t i = 0; i < zeros; i++) out[i] = 0;
    for (uint32_t i = 0; i < len; i++) out[zeros + i] = buf[len - 1 - i];
    return (int32_t) (zeros + len);
}

/* ===== Bech32 / bech32m (BIP-173, BIP-350) ===== */

static uint32_t polymod_step(uint32_t chk, uint8_t v)
{
    static const uint32_t g[5] = {0x3b6a57b2u, 0x26508e6du, 0x1ea119fau, 0x3d4233ddu, 0x2a1462b3u};
    uint32_t b = chk >> 25;
    chk = ((chk & 0x1ffffffu) << 5) ^ v;
    for (int i = 0; i < 5; i++)
        if ((b >> i) & 1u) chk ^= g[i];
    return chk;
}

static int b32val(char c)
{
    static const char *a = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";
    for (int i = 0; i < 32; i++)
        if (a[i] == c) return i;
    return -1;
}

bool pay_segwit_decode(const char *addr, const char *hrp, uint8_t *version, uint8_t *prog,
                       uint32_t *prog_len)
{
    char lo[91];
    uint8_t data[90];
    size_t n = pay_strnlen(addr, 91), hl = pay_strnlen(hrp, 8), sep = 0, dl = 0;
    bool has_lower = false, has_upper = false;
    if (!addr || n < 8 || n > 90) return false;
    for (size_t i = 0; i < n; i++) {
        char c = addr[i];
        if (c < 33 || c > 126) return false;
        if (pay_is_lower(c)) has_lower = true;
        if (pay_is_upper(c)) has_upper = true;
        lo[i] = pay_to_lower(c);
        if (lo[i] == '1') sep = i;
    }
    lo[n] = '\0';
    if (has_lower && has_upper) return false; /* mixed case */
    if (sep != hl || sep + 7 > n) return false;
    for (size_t i = 0; i < hl; i++)
        if (lo[i] != hrp[i]) return false;
    uint32_t chk = 1;
    for (size_t i = 0; i < hl; i++) chk = polymod_step(chk, (uint8_t) (lo[i] >> 5));
    chk = polymod_step(chk, 0);
    for (size_t i = 0; i < hl; i++) chk = polymod_step(chk, (uint8_t) (lo[i] & 31));
    for (size_t i = sep + 1; i < n; i++) {
        int v = b32val(lo[i]);
        if (v < 0) return false;
        chk = polymod_step(chk, (uint8_t) v);
        data[dl++] = (uint8_t) v;
    }
    if (dl < 7) return false;
    uint8_t ver = data[0];
    if (ver > 16) return false;
    uint32_t want = ver == 0 ? 1u : 0x2bc830a3u; /* bech32 / bech32m */
    if (chk != want) return false;
    /* convert 5-bit groups (excluding version and checksum) to bytes */
    uint32_t acc = 0, bits = 0, pl = 0;
    for (size_t i = 1; i < dl - 6; i++) {
        acc = (acc << 5) | data[i];
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (pl >= 40) return false;
            prog[pl++] = (uint8_t) (acc >> bits);
        }
        acc &= (1u << bits) - 1u;
    }
    if (bits >= 5 || acc != 0) return false; /* non-zero padding */
    if (pl < 2 || pl > 40) return false;
    if (ver == 0 && pl != 20 && pl != 32) return false;
    *version = ver;
    *prog_len = pl;
    return true;
}

pay_addr_kind_t pay_btc_addr_check(const char *addr, bool testnet)
{
    uint8_t prog[40], ver, raw[40], h[32];
    uint32_t pl;
    if (!addr) return PAY_ADDR_INVALID;
    const char *hrp = testnet ? "tb" : "bc";
    if (pay_to_lower(addr[0]) == hrp[0] && pay_to_lower(addr[1]) == hrp[1] && addr[2] == '1') {
        if (!pay_segwit_decode(addr, hrp, &ver, prog, &pl)) return PAY_ADDR_INVALID;
        if (ver == 0) return pl == 20 ? PAY_ADDR_BTC_P2WPKH : PAY_ADDR_BTC_P2WSH;
        if (ver == 1 && pl == 32) return PAY_ADDR_BTC_P2TR;
        return PAY_ADDR_BTC_SEGWIT;
    }
    int32_t n = pay_base58_decode(addr, raw, sizeof raw);
    if (n != 25) return PAY_ADDR_INVALID;
    sha256(raw, 21, h);
    sha256(h, 32, h);
    if (!pay_memeq(h, raw + 21, 4)) return PAY_ADDR_INVALID;
    if (raw[0] == (testnet ? 0x6f : 0x00)) return PAY_ADDR_BTC_P2PKH;
    if (raw[0] == (testnet ? 0xc4 : 0x05)) return PAY_ADDR_BTC_P2SH;
    return PAY_ADDR_INVALID;
}

/* ===== EIP-55 ===== */

static int hexval(char c)
{
    if (pay_is_digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

void pay_eth_checksum(const uint8_t addr20[20], char out[43])
{
    static const char hx[] = "0123456789abcdef";
    char lo[40];
    uint8_t h[32];
    for (int i = 0; i < 20; i++) {
        lo[2 * i] = hx[addr20[i] >> 4];
        lo[2 * i + 1] = hx[addr20[i] & 15];
    }
    pay_keccak256((const uint8_t *) lo, 40, h);
    out[0] = '0';
    out[1] = 'x';
    for (int i = 0; i < 40; i++) {
        uint8_t nib = (uint8_t) ((i & 1) ? (h[i >> 1] & 15) : (h[i >> 1] >> 4));
        char c = lo[i];
        out[2 + i] = (c >= 'a' && nib >= 8) ? (char) (c - 'a' + 'A') : c;
    }
    out[42] = '\0';
}

pay_addr_kind_t pay_eth_addr_check(const char *addr)
{
    uint8_t b[20];
    char want[43];
    bool lower = false, upper = false;
    if (!addr || pay_strnlen(addr, 43) != 42 || addr[0] != '0' || addr[1] != 'x')
        return PAY_ADDR_INVALID;
    for (int i = 0; i < 40; i++) {
        int v = hexval(addr[2 + i]);
        if (v < 0) return PAY_ADDR_INVALID;
        if (addr[2 + i] >= 'a' && addr[2 + i] <= 'f') lower = true;
        if (addr[2 + i] >= 'A' && addr[2 + i] <= 'F') upper = true;
        if (i & 1)
            b[i >> 1] = (uint8_t) (b[i >> 1] | v);
        else
            b[i >> 1] = (uint8_t) (v << 4);
    }
    if (!(lower && upper)) return PAY_ADDR_ETH_NO_CHECKSUM;
    pay_eth_checksum(b, want);
    return pay_streq(want, addr) ? PAY_ADDR_ETH_CHECKSUMMED : PAY_ADDR_INVALID;
}

/* ===== Intents ===== */

pay_status_t pay_crypto_intent_init(pay_crypto_intent_t *it, const pay_ledger_t *L, uint16_t asset,
                                    pay_chain_t chain, uint64_t chain_id, const char *from,
                                    const char *to, uint64_t amount, uint64_t fee,
                                    uint32_t required_confirmations, const char *uetr)
{
    const pay_asset_t *a = pay_ledger_asset(L, asset);
    if (!it) return PAY_ERR_ARG;
    pay_memset(it, 0, sizeof *it);
    if (!a || a->kind != PAY_ASSET_CRYPTO) return PAY_ERR_NO_ASSET;
    it->asset = asset;
    pay_strlcpy(it->asset_code, a->code, sizeof it->asset_code);
    pay_strlcpy(it->dti, a->dti, sizeof it->dti);
    it->minor = a->minor;
    it->chain = chain;
    it->chain_id = chain_id;
    if (!pay_strlcpy(it->from, from, sizeof it->from) || !pay_strlcpy(it->to, to, sizeof it->to))
        return PAY_ERR_ARG;
    it->amount = amount;
    it->fee = fee;
    it->required_confirmations = required_confirmations;
    pay_strlcpy(it->uetr, uetr, sizeof it->uetr);
    it->state = PAY_INTENT_DRAFT;
    return PAY_OK;
}

static bool addr_ok(pay_chain_t c, const char *a)
{
    switch (c) {
    case PAY_CHAIN_BITCOIN:
        return pay_btc_addr_check(a, false) != PAY_ADDR_INVALID;
    case PAY_CHAIN_BITCOIN_TESTNET:
        return pay_btc_addr_check(a, true) != PAY_ADDR_INVALID;
    case PAY_CHAIN_EVM:
        return pay_eth_addr_check(a) == PAY_ADDR_ETH_CHECKSUMMED;
    default:
        return false;
    }
}

pay_status_t pay_crypto_intent_validate(pay_crypto_intent_t *it)
{
    if (!it || it->state != PAY_INTENT_DRAFT) return PAY_ERR_STATE;
    if (it->amount == 0 || !pay_uetr_valid(it->uetr)) return PAY_ERR_ARG;
    if (it->chain == PAY_CHAIN_EVM && it->chain_id == 0) return PAY_ERR_ARG;
    if (!addr_ok(it->chain, it->from) || !addr_ok(it->chain, it->to)) return PAY_ERR_ARG;
    it->state = PAY_INTENT_VALIDATED;
    return PAY_OK;
}

int32_t pay_crypto_intent_render(const pay_crypto_intent_t *it, char *out, uint32_t cap)
{
    pay_w w;
    if (!it || !out) return -1;
    pay_w_init(&w, out, cap);
    pay_w_s(&w, "ZXV-CRYPTO-INTENT-v1\nuetr=");
    pay_w_s(&w, it->uetr);
    pay_w_s(&w, "\nasset=");
    pay_w_s(&w, it->asset_code);
    pay_w_s(&w, "\ndti=");
    pay_w_s(&w, it->dti);
    pay_w_s(&w, "\nchain=");
    pay_w_u64(&w, (uint64_t) it->chain);
    pay_w_s(&w, "\nchain_id=");
    pay_w_u64(&w, it->chain_id);
    pay_w_s(&w, "\nfrom=");
    pay_w_s(&w, it->from);
    pay_w_s(&w, "\nto=");
    pay_w_s(&w, it->to);
    pay_w_s(&w, "\namount=");
    pay_w_amount(&w, it->amount, it->minor);
    pay_w_s(&w, "\nfee=");
    pay_w_amount(&w, it->fee, it->minor);
    pay_w_s(&w, "\nconfirmations_required=");
    pay_w_u64(&w, it->required_confirmations);
    if (it->travel.present) {
        pay_w_s(&w, "\ntravel.originator=");
        pay_w_s(&w, it->travel.originator.name);
        pay_w_s(&w, "\ntravel.originator_account=");
        pay_w_s(&w, it->travel.originator.account);
        pay_w_s(&w, "\ntravel.beneficiary=");
        pay_w_s(&w, it->travel.beneficiary.name);
        pay_w_s(&w, "\ntravel.beneficiary_account=");
        pay_w_s(&w, it->travel.beneficiary.account);
    }
    pay_w_c(&w, '\n');
    return pay_w_finish(&w);
}

pay_status_t pay_crypto_intent_handoff(pay_crypto_intent_t *it, pay_role_cfg_t *role,
                                       uint8_t kyc_tier, uint64_t tick)
{
    static char doc[2048];
    pay_screen_t v;
    if (!it || !role || it->state != PAY_INTENT_VALIDATED) return PAY_ERR_STATE;
    pay_screen_req_t rq = {
        role->id, it->travel.beneficiary.name, "", it->amount, it->asset_code, true, &it->travel};
    pay_status_t st = pay_role_authorize(role, &rq, kyc_tier, tick, &v);
    if (st != PAY_OK) return st;
    int32_t n = pay_crypto_intent_render(it, doc, sizeof doc);
    if (n < 0) return PAY_ERR_OVERFLOW;
    pay_gw_msg_t m = {PAY_GW_CHAIN, "zxv.crypto.intent.v1", 0, doc, (uint32_t) n, it->uetr};
    st = pay_role_dispatch(role, &m);
    if (st != PAY_OK) return st;
    it->state = PAY_INTENT_HANDED_TO_WALLET;
    return PAY_OK;
}

pay_status_t pay_crypto_record_hash(pay_crypto_intent_t *it, const uint8_t hash[32])
{
    if (!it || !hash || it->state != PAY_INTENT_HANDED_TO_WALLET) return PAY_ERR_STATE;
    pay_memcpy(it->tx_hash, hash, 32);
    it->has_hash = true;
    it->state = PAY_INTENT_BROADCAST;
    return PAY_OK;
}

pay_status_t pay_crypto_record_confirmations(pay_crypto_intent_t *it, uint32_t conf)
{
    if (!it || it->state != PAY_INTENT_BROADCAST || conf < it->confirmations) return PAY_ERR_STATE;
    it->confirmations = conf;
    if (conf >= it->required_confirmations) it->state = PAY_INTENT_CONFIRMED;
    return PAY_OK;
}

pay_status_t pay_crypto_record_failure(pay_crypto_intent_t *it)
{
    if (!it || it->state == PAY_INTENT_CONFIRMED) return PAY_ERR_STATE;
    it->state = PAY_INTENT_FAILED;
    return PAY_OK;
}
