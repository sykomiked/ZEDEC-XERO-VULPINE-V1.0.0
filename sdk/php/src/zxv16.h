/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* The ABI 1.6 declarations of zxv_api.h (vino: Vino Embed) that PHP's FFI
 * parses: no preprocessor, enums passed as int32_t, array sizes as numbers.
 * Native::load appends this to zxv.h and zxv15.h when the library is ABI
 * 1.6 or later. Keep in sync with sdk/libzxv/include/zxv_api.h. */
typedef struct zxv_vino_play zxv_vino_play;
typedef struct zxv_vino_meter zxv_vino_meter;
int32_t zxv_vino_play_create(const zxv_cap *cap, zxv_vino_play **out);
void zxv_vino_play_destroy(zxv_vino_play *p);
int32_t zxv_vino_play_open(const zxv_cap *cap, zxv_vino_play *p, uint32_t owner, int32_t is_issuer, uint32_t *out_account);
int32_t zxv_vino_play_back(const zxv_cap *cap, zxv_vino_play *p, uint32_t issuer, uint64_t amount);
int32_t zxv_vino_play_unback(const zxv_cap *cap, zxv_vino_play *p, uint32_t issuer, uint64_t amount);
int32_t zxv_vino_issue(const zxv_cap *cap, zxv_vino_play *p, uint32_t issuer, uint32_t holder, uint64_t face, int32_t rail, uint64_t *out_id);
int32_t zxv_vino_transfer(const zxv_cap *cap, zxv_vino_play *p, uint64_t voucher, uint32_t from, uint32_t to, int32_t rail);
int32_t zxv_vino_set_rail(const zxv_cap *cap, zxv_vino_play *p, uint64_t voucher, uint32_t holder, int32_t rail);
int32_t zxv_vino_redeem(const zxv_cap *cap, zxv_vino_play *p, uint64_t voucher, uint32_t holder);
int32_t zxv_vino_voucher_info(const zxv_cap *cap, zxv_vino_play *p, uint64_t voucher, uint64_t *out_face, uint32_t *out_issuer, uint32_t *out_holder, int32_t *out_rail, int32_t *out_state);
int32_t zxv_vino_account_info(const zxv_cap *cap, zxv_vino_play *p, uint32_t account, uint32_t *out_owner, int32_t *out_is_issuer, uint64_t *out_backing, uint64_t *out_faces, uint32_t *out_count);
int32_t zxv_vino_vouchers(const zxv_cap *cap, zxv_vino_play *p, uint32_t account, uint64_t *ids, size_t cap_len, size_t *out_count);
int32_t zxv_vino_play_check(const zxv_cap *cap, zxv_vino_play *p);
int32_t zxv_vino_request_encode(int32_t lane, uint32_t flags, const uint8_t merchant[32], const uint8_t nonce[16], uint64_t amount, uint64_t created_at, uint64_t expires_at, const char *memo, uint8_t out[160], uint8_t out_id[32]);
int32_t zxv_vino_request_decode(const uint8_t in[160], int32_t *out_lane, uint32_t *out_flags, uint8_t out_merchant[32], uint8_t out_nonce[16], uint64_t *out_amount, uint64_t *out_created_at, uint64_t *out_expires_at, char memo[65], uint8_t out_id[32]);
int32_t zxv_vino_receipt_msg(const uint8_t request_id[32], const uint8_t wallet_pk[2592], uint64_t approved_at, uint8_t out[88]);
int32_t zxv_vino_receipt_sign(const zxv_cap *cap, const uint8_t request[160], const uint8_t wallet_sk[4896], const uint8_t wallet_pk[2592], uint64_t approved_at, uint8_t sig[4627]);
int32_t zxv_vino_receipt_verify(const uint8_t request[160], const uint8_t wallet_pk[2592], uint64_t approved_at, const uint8_t sig[4627], uint64_t now);
int32_t zxv_vino_meter_create(const zxv_cap *cap, uint64_t daily_cap, zxv_vino_meter **out);
void zxv_vino_meter_destroy(zxv_vino_meter *m);
int32_t zxv_vino_meter_admit(const zxv_cap *cap, zxv_vino_meter *m, uint64_t day, uint64_t amount, uint64_t *out_left);
