/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* ztp.h — zero-touch provisioning with a ZHP-1 provisioning profile (R1.5).
 *
 * WHAT IT IS
 * ----------
 * How a device running the ZXV image gets an owner without a cloud account:
 *
 *   1. FIRST BOOT. The device makes its own ML-DSA-87 key pair and a 32-byte
 *      one-time CLAIM SECRET (ztp_first_boot, or ztp_device_init with a key
 *      from a hardware key store). It is now UNCLAIMED.
 *   2. MAKER. The maker signs a ZHP-1 PROVISIONING PROFILE for it: the hash
 *      of the device public key, model, serial, the id of the golden boot
 *      manifest the device ships with (ba.h), the maker's key hash, the
 *      issue time, an optional ZHP-1 hardware-profile digest (hwemu/hwp.h)
 *      and a COMMITMENT to the claim secret. It is the companion of the
 *      hardware profile (hwp.h): that one says what the machine is, this one
 *      says which individual unit the maker built and what it should boot.
 *   3. QR CODE. The device shows (or the box carries) a QR code with the
 *      text "ZTP1:" + base32(device id || claim secret), 82 characters,
 *      a version-5 level-M symbol (qr.h). The owner's app checks the QR
 *      against the maker's profile: same device id, and the secret opens the
 *      commitment. A sticker swapped onto another box fails here.
 *   4. CLAIM. The owner's app sends a CLAIM message that carries the owner's
 *      public key, is MAC'd with the claim secret (HMAC-SHA256) and signed by
 *      the owner. The device accepts the FIRST valid claim, wipes the secret
 *      and becomes OWNED; every later claim is refused.
 *   5. AFTER. Ownership TRANSFER needs the current owner's signature.
 *      FACTORY RESET needs the current owner's signature, or the physical-
 *      presence path if the maker compiled it in (rule Z7).
 *
 * RULES
 *   Z1 device id = the first 16 bytes of SHA3-256(device public key).
 *   Z2 claim commitment = SHA3-256("ZXV-ZTP1-COMMIT" || device id || secret).
 *   Z3 Every message names the device id and the device's current
 *      ownership EPOCH (0 before the first claim, +1 on every claim,
 *      transfer and reset). A message for another device or another epoch
 *      is refused, so an old transfer or reset can never be replayed.
 *   Z4 Claim: device UNCLAIMED, MAC under the claim secret valid, signed by
 *      the new owner's key carried in the message. Transfer and reset:
 *      device OWNED, signed by the stored owner key, MAC field all zero.
 *      Reset carries no new owner (all-zero key field).
 *   Z5 Checks run in this order, each with its own error: format, device
 *      state, device id, claimed-already, epoch, MAC, signature.
 *   Z6 A claim wipes the claim secret. A reset installs a fresh secret
 *      supplied by the caller (from the device's entropy source), so the
 *      device can be claimed again from a new QR code, which no longer
 *      matches the maker's commitment (the owner app should say so).
 *   Z7 Physical-presence reset (ztp_reset_physical) is OFF by default: it
 *      returns ZTP_E_DISABLED unless the image is built with
 *      -DZTP_PHYSICAL_RESET=1. Turning it on lets anyone holding the device
 *      (for example pressing a reset button for 10 seconds during boot)
 *      wipe ownership, which is the usual consumer trade-off against a
 *      stolen QR code or a lost owner key. The platform code must call it
 *      only from a path a remote attacker cannot reach.
 *   Z8 Signatures are ML-DSA-87 with FIPS 204 contexts "ZXV-ZHP1-PROFILE"
 *      and "ZXV-ZTP1-MSG". Signing goes through ba_sign_fn, so a hardware
 *      key store can sign. No allocation, no libc, integer only.
 *
 * WIRE FORMATS (integers little-endian)
 *   profile  "ZPR1" | u8 version=1 | u8 model_len | u8 serial_len | u8 0 |
 *            device_key_hash[32] | maker_key_hash[32] | manifest_id[36] |
 *            claim_commit[32] | hw_profile_digest[32] (zero = none) |
 *            u64 issued_at | model | serial | signature[4627]
 *            model and serial: 1..32 printable ASCII bytes.
 *   message  "ZTM1" | u8 version=1 | u8 kind (1 claim, 2 transfer, 3 reset) |
 *            u16 0 | device_id[16] | u32 epoch | u32 0 | u64 issued_at |
 *            new_owner_pk[2592] | mac[32] | signature[4627]   (7291 bytes)
 *            mac = HMAC-SHA256(claim secret, everything before mac)
 *            signature covers everything before it.
 *   QR text  "ZTP1:" + lower-case base32, no padding, of id || secret.
 *
 * HONEST LIMITS
 *   - Firmware-level ZTP only works on devices that run our image (or whose
 *     maker builds these steps into theirs). Consumer phones and payment
 *     (POS) terminals only get this as an app, with the app's key in the
 *     phone's keystore, unless the maker cooperates: their boot chain and
 *     secure element belong to the maker.
 *   - Whoever holds the QR code first can claim the device. Keep it inside
 *     the box or on the device's screen, not on the outside of the package.
 *   - The device state (ztp_device_t) must be stored by the platform in
 *     storage an attacker cannot rewrite (sealed by a TPM or secure element
 *     where present). With plain flash, someone with the device on a bench
 *     can roll the state back. Not tested on real hardware.
 *   - issued_at is signed but informational: nothing here reads a clock.
 *   - The device sends no signed receipt for a claim yet; the owner learns
 *     the result from the device's next attestation or session.
 */
#ifndef ZXV_ZTP_H
#define ZXV_ZTP_H

#include "../bootattest/ba.h"

#define ZTP_ID_LEN       16u
#define ZTP_SECRET_LEN   32u
#define ZTP_TEXT_MAX     32u
#define ZTP_QR_TEXT_LEN  82u                /* "ZTP1:" + 77 base32 chars */
#define ZTP_PROFILE_HDR  (8u + 32u + 32u + BA_ID_LEN + 32u + 32u + 8u)
#define ZTP_PROFILE_MAX  (ZTP_PROFILE_HDR + 2u * ZTP_TEXT_MAX + BA_SIG_LEN)
#define ZTP_MSG_MAC_OFF  (8u + ZTP_ID_LEN + 8u + 8u + BA_PK_LEN)
#define ZTP_MSG_LEN      (ZTP_MSG_MAC_OFF + 32u + BA_SIG_LEN)

#ifndef ZTP_PHYSICAL_RESET
#define ZTP_PHYSICAL_RESET 0
#endif

typedef enum {
    ZTP_OK = 0,
    ZTP_E_ARG,
    ZTP_E_STATE,
    ZTP_E_FORMAT,
    ZTP_E_DEVICE,
    ZTP_E_ALREADY_CLAIMED,
    ZTP_E_EPOCH,
    ZTP_E_MAC,
    ZTP_E_SIG,
    ZTP_E_SAME_OWNER,
    ZTP_E_DISABLED,
    ZTP_E_SIGNER,
    ZTP_E_SPACE,
    ZTP_E_PROFILE_FORMAT,
    ZTP_E_PROFILE_KEY,
    ZTP_E_PROFILE_SIG,
    ZTP_E_QR_FORMAT,
    ZTP_E_QR_MISMATCH,
    ZTP_E_COUNT_
} ztp_err_t;

const char *ztp_strerror(ztp_err_t e);

typedef enum { ZTP_FACTORY = 0, ZTP_UNCLAIMED = 1, ZTP_OWNED = 2 } ztp_state_t;
typedef enum { ZTP_MSG_CLAIM = 1, ZTP_MSG_TRANSFER = 2, ZTP_MSG_RESET = 3 } ztp_kind_t;

/* Everything the device keeps. The platform persists it (see limits). */
typedef struct {
    uint8_t  state;                          /* ztp_state_t */
    uint32_t epoch;
    uint8_t  device_key_hash[BA_KEYHASH_LEN];
    uint8_t  claim_secret[ZTP_SECRET_LEN];   /* zero while OWNED */
    uint8_t  owner_pk[BA_PK_LEN];            /* zero unless OWNED */
    uint8_t  owner_key_hash[BA_KEYHASH_LEN];
} ztp_device_t;

/* ---- device side ----------------------------------------------------- */

/* A zeroed ztp_device_t is FACTORY. Install a key made elsewhere (for
 * example a hardware key store) and a fresh claim secret: FACTORY -> UNCLAIMED. */
ztp_err_t ztp_device_init(ztp_device_t *d, const uint8_t device_pk[BA_PK_LEN],
                          const uint8_t secret[ZTP_SECRET_LEN]);
/* Software first boot: key from entropy[0..32), secret = entropy[32..64). */
ztp_err_t ztp_first_boot(ztp_device_t *d, const uint8_t entropy[64], uint8_t pk[BA_PK_LEN],
                         uint8_t sk[BA_SK_LEN]);
void ztp_device_id(const ztp_device_t *d, uint8_t id[ZTP_ID_LEN]);
/* The QR text, only while UNCLAIMED. out needs ZTP_QR_TEXT_LEN + 1. */
ztp_err_t ztp_qr_text(const ztp_device_t *d, char out[ZTP_QR_TEXT_LEN + 1]);
/* Apply a claim, transfer or reset (rules Z3-Z6). new_secret is required
 * for a reset and ignored otherwise. */
ztp_err_t ztp_device_accept(ztp_device_t *d, const uint8_t *msg, size_t len,
                            const uint8_t new_secret[ZTP_SECRET_LEN]);
/* Rule Z7. */
ztp_err_t ztp_reset_physical(ztp_device_t *d, const uint8_t new_secret[ZTP_SECRET_LEN]);

/* ---- maker side ------------------------------------------------------ */

typedef struct {
    uint8_t  device_key_hash[BA_KEYHASH_LEN];
    uint8_t  maker_key_hash[BA_KEYHASH_LEN];
    uint8_t  manifest_id[BA_ID_LEN];
    uint8_t  claim_commit[32];
    uint8_t  hw_profile_digest[32];
    uint64_t issued_at;
    uint8_t  model_len, serial_len;
    char     model[ZTP_TEXT_MAX + 1];
    char     serial[ZTP_TEXT_MAX + 1];
} ztp_profile_t;

void ztp_commit(const uint8_t id[ZTP_ID_LEN], const uint8_t secret[ZTP_SECRET_LEN],
                uint8_t out[32]);
ztp_err_t ztp_profile_build(const ztp_profile_t *p, ba_sign_fn sign, void *user, uint8_t *out,
                            size_t cap, size_t *out_len);
ztp_err_t ztp_profile_verify(const uint8_t *rec, size_t len, const uint8_t maker_pk[BA_PK_LEN],
                             ztp_profile_t *p);

/* ---- owner side ------------------------------------------------------ */

ztp_err_t ztp_qr_parse(const char *text, size_t len, uint8_t id[ZTP_ID_LEN],
                       uint8_t secret[ZTP_SECRET_LEN]);
/* QR belongs to the profiled device: same id and the commitment opens. */
ztp_err_t ztp_qr_check(const ztp_profile_t *p, const uint8_t id[ZTP_ID_LEN],
                       const uint8_t secret[ZTP_SECRET_LEN]);
/* Build a message. new_owner_pk: required for claim and transfer, NULL for
 * reset. secret: required for claim, NULL otherwise. sign signs as the new
 * owner (claim) or the current owner (transfer, reset). out: ZTP_MSG_LEN. */
ztp_err_t ztp_msg_build(ztp_kind_t kind, const uint8_t id[ZTP_ID_LEN], uint32_t epoch,
                        uint64_t issued_at, const uint8_t *new_owner_pk, const uint8_t *secret,
                        ba_sign_fn sign, void *user, uint8_t out[ZTP_MSG_LEN]);

#endif /* ZXV_ZTP_H */
