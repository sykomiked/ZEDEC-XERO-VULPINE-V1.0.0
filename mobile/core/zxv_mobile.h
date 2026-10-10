/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxv_mobile.h — the flat C API of the mobile core, called from Kotlin
 * (through mobile/android/app/src/main/cpp/zxv_jni.c) and from Swift
 * (through the ZXVCore module map in mobile/ios).
 *
 * One process holds one device identity (a static dm_mesh_t inside the
 * library). The app supplies randomness (SecureRandom / SecRandomCopyBytes),
 * a transport (send frames, feed received frames to zxv_receive) and a
 * clock (milliseconds, passed to every call). Every function returns 0 or a
 * positive value on success and a negative dm_status_t on failure.
 *
 * Not thread-safe: call it from one thread (the app's core thread).
 */
#ifndef ZXV_MOBILE_H
#define ZXV_MOBILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZXV_ID_BYTES    16
#define ZXV_API_VERSION 1

/* events (same numbers as dm_event_kind_t) */
#define ZXV_EV_SAS             1
#define ZXV_EV_PAIRED          2
#define ZXV_EV_ROSTER          3
#define ZXV_EV_REVOKED         4
#define ZXV_EV_SELF_REVOKED    5
#define ZXV_EV_PEER_UP         6
#define ZXV_EV_PEER_DOWN       7
#define ZXV_EV_CAPS            8
#define ZXV_EV_SYNC            9
#define ZXV_EV_REQ_FALLBACK    10
#define ZXV_EV_PAIR_FAILED     11
#define ZXV_EV_MONEY_CONFIRMED 12
#define ZXV_EV_MONEY_DECLINED  13

/* roles / flags / network kinds / routes: as in kernel/src/devmesh */
#define ZXV_ROLE_HOME            1
#define ZXV_ROLE_THIN            2
#define ZXV_ROLE_STANDALONE      3
#define ZXV_FLAG_ADMIN           1
#define ZXV_FLAG_HELD            2
#define ZXV_NET_OFFLINE          0
#define ZXV_NET_LAN              1
#define ZXV_NET_UNMETERED        2
#define ZXV_NET_METERED          3
#define ZXV_ROUTE_REMOTE         1
#define ZXV_ROUTE_LOCAL          2
#define ZXV_ROUTE_LOCAL_DEGRADED 3
#define ZXV_ROUTE_NEED_CAPACITY  4
#define ZXV_ROUTE_UNAVAILABLE    5
#define ZXV_ROUTE_CONFIRM        6

typedef struct {
    void *ctx;
    void (*random)(void *ctx, uint8_t *out, uint32_t n);
    int (*send)(void *ctx, const uint8_t to[ZXV_ID_BYTES], const uint8_t *frame, uint32_t len);
    void (*event)(void *ctx, int kind, const uint8_t peer[ZXV_ID_BYTES], uint32_t sas,
                  uint64_t value);
    void (*reply)(void *ctx, uint32_t req_id, const uint8_t *data, uint32_t len, int final);
    /* a money action to show the user; answer with zxv_money_answer */
    void (*money)(void *ctx, uint64_t amount, uint32_t rail, const char *memo);
} zxv_host_t;

int zxv_api_version(void);

/* seed: 64 bytes kept in Android Keystore / iOS Keychain. level: 2 = HIGH
 * (default), 3 = MATRIX. */
int zxv_start(const zxv_host_t *host, const uint8_t seed[64], const char *name, int role, int flags,
              int level);
void zxv_stop(void);
int zxv_self_id(uint8_t out[ZXV_ID_BYTES]);

/* phone-only use: a mesh of one */
int zxv_create_mesh(uint64_t now_ms);

/* pairing; qr_text receives base32 text for a QR code */
int zxv_invite(int role, int flags, uint64_t now_ms, char *qr_text, uint32_t qr_cap, char code[11]);
int zxv_join_qr_text(const char *qr_text, uint64_t now_ms);
int zxv_join_code(const char *code, uint64_t now_ms);
int zxv_confirm_sas(const uint8_t peer[ZXV_ID_BYTES], int match, uint64_t now_ms);

/* transport and time */
int zxv_receive(const uint8_t *frame, uint32_t len, uint64_t now_ms);
void zxv_tick(uint64_t now_ms);
int zxv_connect(const uint8_t peer[ZXV_ID_BYTES], uint64_t now_ms);

/* devices */
int zxv_device_count(void);
/* name: 32 bytes; returns role | flags << 8 | status << 16 */
int zxv_device(int i, uint8_t id[ZXV_ID_BYTES], char name[32]);
int zxv_rename(const uint8_t id[ZXV_ID_BYTES], const char *name, uint64_t now_ms);
int zxv_revoke(const uint8_t id[ZXV_ID_BYTES], uint64_t now_ms);

/* capabilities: fills the model list from the catalogue */
int zxv_set_caps(uint32_t ram_mb, uint32_t compute, int battery_pct, int charging, int net,
                 uint32_t features, uint64_t now_ms);
/* name of local model i (or NULL) */
const char *zxv_local_model(int i);

/* what runs where; target receives the device id */
int zxv_route(int kind, int model_class, uint64_t now_ms, uint8_t target[ZXV_ID_BYTES]);
int zxv_prompt(const uint8_t target[ZXV_ID_BYTES], const char *text, uint64_t now_ms,
               uint32_t *req_id);

/* settings sync */
int zxv_set_setting(uint16_t key, const char *value, uint64_t now_ms);
int zxv_get_setting(uint16_t key, char *out, uint32_t cap);

/* money: answer the last action shown through host.money */
int zxv_money_answer(int approve, uint64_t now_ms);
/* the exact 0.08889% assurance fee of an amount (no carry), for display */
uint64_t zxv_assure_fee(uint64_t amount);

#ifdef __cplusplus
}
#endif

#endif /* ZXV_MOBILE_H */
