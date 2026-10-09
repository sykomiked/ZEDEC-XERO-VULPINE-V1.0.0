/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cn_mobile.h — generic mobile-money adapter model for the card networks.
 *
 * The data model and state machines a host-side adapter drives to connect
 * Dragon / Phoenix / Thunderbird cards to a phone-number wallet of the kind
 * common in East Africa and elsewhere:
 *
 *   wallet      an MSISDN (E.164 digits, no '+') linked to one card PAN,
 *               with a salted SHA3-256 PIN verifier and a lockout counter;
 *   push        a merchant-initiated payment request sent to the phone
 *               ("push payment"), PENDING until a callback ACCEPTS or
 *               REJECTS it, or it EXPIRES / is CANCELLED; callbacks are
 *               idempotent (a repeat of the same outcome is accepted, a
 *               conflicting one is refused);
 *   USSD        a menu state machine for feature phones (Pay merchant,
 *               Statement balance, Freeze card, Replace card), producing
 *               "CON ..." / "END ..." screens of at most 182 characters and
 *               an action record the host executes against the issuer.
 *
 * Mapping to cards: an ACCEPTED push (or a confirmed USSD payment) becomes a
 * cn_auth_req_t via cn_mm_push_to_auth / cn_mm_action_to_auth, which the host
 * signs and passes to cn_authorize().
 *
 * HONEST LIMITS. No network calls: this module never talks to an operator,
 * an SMS/USSD gateway or a phone. It is not affiliated with, endorsed by or
 * integrated with any mobile network operator or mobile-money service, and
 * implements no operator's API. A feature phone cannot hold an ML-DSA key
 * or compute a signature, so for USSD and push flows the holder signature
 * must be produced by a CUSTODIAL signer the host operates after the PIN or
 * operator confirmation: that is weaker than a holder-held key, and the
 * operator must disclose it. A 4-6 digit PIN is a low-entropy secret; the
 * salted hash only protects it at rest against casual reading, not against
 * an offline guess of all 10^6 values. Operating mobile money requires
 * e-money / payment-service licences in every jurisdiction that has them.
 */
#ifndef ZXV_CN_MOBILE_H
#define ZXV_CN_MOBILE_H

#include <stdint.h>
#include <stdbool.h>
#include "cardnet.h"

#define CN_MSISDN_MIN     8u
#define CN_MSISDN_MAX     15u
#define CN_MM_MAX_WALLETS 8u
#define CN_MM_MAX_PUSH    16u
#define CN_MM_REF_MAX     20u
#define CN_MM_RCPT_MAX    32u
#define CN_MM_PIN_TRIES   3u
#define CN_USSD_MAX_TEXT  182u
#define CN_USSD_TIMEOUT   180u /* seconds of inactivity */
#define CN_TILL_MIN       4u
#define CN_TILL_MAX       10u

typedef enum {
    CN_MM_OK = 0,
    CN_MM_ERR_ARG = -1,
    CN_MM_ERR_FULL = -2,
    CN_MM_ERR_NOT_FOUND = -3,
    CN_MM_ERR_STATE = -4,
    CN_MM_ERR_PIN = -5,
    CN_MM_ERR_LOCKED = -6,
    CN_MM_ERR_DUP = -7,     /* MSISDN already registered */
    CN_MM_ERR_CONFLICT = -8 /* callback contradicts an earlier one */
} cn_mm_rc_t;

typedef struct {
    bool used;
    char msisdn[CN_MSISDN_MAX + 1];
    char pan[CN_PAN_LEN + 1];
    uint8_t pin_salt[16];
    uint8_t pin_hash[32];
    uint32_t pin_fails;
    bool locked;
} cn_mm_wallet_t;

typedef enum {
    CN_PUSH_PENDING = 1,
    CN_PUSH_ACCEPTED = 2,
    CN_PUSH_REJECTED = 3,
    CN_PUSH_EXPIRED = 4,
    CN_PUSH_CANCELLED = 5,
    CN_PUSH_AUTHORIZED = 6 /* turned into a card authorization request */
} cn_push_state_t;

typedef struct {
    bool used;
    uint32_t id;
    uint32_t wallet;
    uint64_t amount_minor;
    char merchant_id[CN_MID_LEN + 1];
    char terminal_id[CN_TID_LEN + 1];
    char reference[CN_MM_REF_MAX + 1];
    cn_time_t created, expires;
    uint32_t state;  /* cn_push_state_t */
    uint32_t result; /* callback result code, 0 = accepted */
    char op_receipt[CN_MM_RCPT_MAX + 1];
} cn_mm_push_t;

/* What the host adapter reports back after the phone side finished. */
typedef struct {
    uint32_t request_id;
    uint32_t result; /* 0 accepted; anything else rejected (host-defined) */
    char op_receipt[CN_MM_RCPT_MAX + 1];
} cn_mm_callback_t;

typedef struct {
    cn_mm_wallet_t wallets[CN_MM_MAX_WALLETS];
    cn_mm_push_t push[CN_MM_MAX_PUSH];
    uint32_t n_push;
    uint32_t push_timeout; /* seconds */
} cn_mm_hub_t;

cn_mm_rc_t cn_mm_init(cn_mm_hub_t *hub, uint32_t push_timeout_secs);
/* pin: 4..6 digits; salt: 16 caller-supplied random bytes. */
cn_mm_rc_t cn_mm_register(cn_mm_hub_t *hub, const char *msisdn, const char *pan, const char *pin,
                          const uint8_t salt[16], uint32_t *out_wallet);
cn_mm_rc_t cn_mm_find(const cn_mm_hub_t *hub, const char *msisdn, uint32_t *out_wallet);
/* Point the wallet at a replacement card (after cn_reissue_card). */
cn_mm_rc_t cn_mm_relink(cn_mm_hub_t *hub, uint32_t wallet, const char *new_pan);
/* CN_MM_OK, CN_MM_ERR_PIN (and counts a failure), or CN_MM_ERR_LOCKED. */
cn_mm_rc_t cn_mm_verify_pin(cn_mm_hub_t *hub, uint32_t wallet, const char *pin);
cn_mm_rc_t cn_mm_unlock(cn_mm_hub_t *hub, uint32_t wallet); /* operator action */

cn_mm_rc_t cn_mm_push_create(cn_mm_hub_t *hub, const char *msisdn, uint64_t amount_minor,
                             const char *merchant_id, const char *terminal_id,
                             const char *reference, cn_time_t now, uint32_t *out_id);
cn_mm_rc_t cn_mm_push_callback(cn_mm_hub_t *hub, const cn_mm_callback_t *cb, cn_time_t now);
cn_mm_rc_t cn_mm_push_cancel(cn_mm_hub_t *hub, uint32_t id);
/* Expire every PENDING push whose deadline has passed. */
void cn_mm_expire(cn_mm_hub_t *hub, cn_time_t now);
/* ACCEPTED push -> authorization request for the linked card; the push
 * becomes AUTHORIZED so it cannot be turned into a second request. */
cn_mm_rc_t cn_mm_push_to_auth(cn_mm_hub_t *hub, uint32_t id, uint32_t form, uint32_t atc,
                              const uint8_t un[4], uint32_t stan, cn_time_t now,
                              cn_auth_req_t *req);

/* ---- USSD ---- */
typedef enum {
    CN_USSD_MAIN = 1,
    CN_USSD_PAY_TILL,
    CN_USSD_PAY_AMOUNT,
    CN_USSD_PAY_PIN,
    CN_USSD_PAY_CONFIRM,
    CN_USSD_BAL_PIN,
    CN_USSD_FREEZE_PIN,
    CN_USSD_REPLACE_PIN,
    CN_USSD_ENDED
} cn_ussd_state_t;

typedef enum {
    CN_MM_ACT_NONE = 0,
    CN_MM_ACT_PAY = 1,     /* host builds an auth request (cn_mm_action_to_auth) */
    CN_MM_ACT_BALANCE = 2, /* informational; the screen already shows it */
    CN_MM_ACT_FREEZE = 3,  /* host calls cn_set_frozen(card, true) */
    CN_MM_ACT_REPLACE = 4  /* host calls cn_reissue_card, then cn_mm_relink */
} cn_mm_act_kind_t;

typedef struct {
    uint32_t kind; /* cn_mm_act_kind_t */
    uint32_t wallet;
    char merchant_id[CN_MID_LEN + 1];
    uint64_t amount_minor;
} cn_mm_action_t;

typedef struct {
    uint32_t state; /* cn_ussd_state_t */
    uint32_t wallet;
    cn_time_t last;
    char merchant_id[CN_MID_LEN + 1];
    uint64_t amount_minor;
} cn_ussd_t;

/* Start a session for an MSISDN; writes the main menu ("CON ..."). An
 * unknown MSISDN or a locked wallet gets an "END ..." screen. */
cn_mm_rc_t cn_ussd_begin(cn_mm_hub_t *hub, cn_ussd_t *s, const char *msisdn, cn_time_t now,
                         char *text, uint32_t cap);
/* Feed one line of user input. iss may be NULL (then the balance screen
 * says it is unavailable). *act is set when the session ends with an
 * action; otherwise act->kind == CN_MM_ACT_NONE. */
cn_mm_rc_t cn_ussd_input(cn_mm_hub_t *hub, cn_ussd_t *s, const cn_issuer_t *iss, const char *input,
                         cn_time_t now, char *text, uint32_t cap, cn_mm_action_t *act);

cn_mm_rc_t cn_mm_action_to_auth(const cn_mm_hub_t *hub, const cn_mm_action_t *act,
                                const char *terminal_id, uint32_t form, uint32_t atc,
                                const uint8_t un[4], uint32_t stan, cn_time_t now,
                                cn_auth_req_t *req);

/* "12.50" or "12" (at most 2 decimals, at most 10 integer digits) -> 1250. */
bool cn_mm_parse_amount(const char *s, uint64_t *minor);
/* 1250 -> "12.50". Returns length written (excluding NUL), 0 on no room. */
uint32_t cn_mm_format_amount(uint64_t minor, char *out, uint32_t cap);

#endif /* ZXV_CN_MOBILE_H */
