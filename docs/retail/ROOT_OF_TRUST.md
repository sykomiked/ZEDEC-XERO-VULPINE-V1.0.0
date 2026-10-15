# Root of trust: boot attestation and zero-touch provisioning

Roadmap items R1.4 and R1.5 (section 6.6a). Code: `kernel/src/bootattest/`
(`ba.h`), `kernel/src/ztp/` (`ztp.h`), `kernel/src/qr/` (`qr.h`). Tests:
`cd kernel && make -f Makefile -f mk/rot.mk test-rot`.

## What it does

**Boot attestation (R1.4).** As the device boots, each stage (boot stub,
kernel, initrd, config, ...) is hashed with SHA-256. The hash goes into an
event log (stage name + hash) and is folded into a running "chain" value,
the same idea as a TPM PCR. When a peer asks, the device signs a *quote*
(the log, the chain, and a random nonce the peer sent) with its ML-DSA-87
node key. The peer checks the quote against a *golden manifest*: the list of
expected stage hashes for a release, signed by the release key. The manifest
has a content-addressed id, so it can be published in the DHT or on IPFS and
fetched by id. No cloud service is involved.

**Zero-touch provisioning (R1.5).** On first boot the device makes its own
key pair and a one-time claim secret. The maker signs a *ZHP-1 provisioning
profile* for the unit (key hash, model, serial, golden-manifest id, maker,
issue time, and a commitment to the claim secret). The owner scans a QR code
from the device or the box, the owner app checks it against the maker's
profile, and sends a *claim* signed by the owner and MAC'd with the claim
secret. The device accepts the first valid claim and refuses all later ones.
Transfer to a new owner and factory reset need the current owner's
signature.

## Formats

All integers are little-endian. Signatures are ML-DSA-87 (FIPS 204, 4627
bytes) with a context string per message type, so a signature made for one
kind of message never verifies as another. Key hash = SHA3-256 of the
ML-DSA-87 public key.

| Record | Layout | Signed by, context |
|---|---|---|
| Chain | `c0 = 32 zero bytes`; `c(i+1) = SHA-256(c(i) ‖ u8 len ‖ name ‖ digest)` | — |
| Event | `u8 name_len ‖ name (1-31 of a-z 0-9 . _ -) ‖ digest[32]` | — |
| Quote | `"ZBQ1" ‖ 1 ‖ u8 n ‖ 0 0 ‖ nonce[32] ‖ manifest_id[36] ‖ device_key_hash[32] ‖ chain[32] ‖ n events ‖ sig` | device key, `ZXV-BA1-QUOTE` |
| Golden manifest | `"ZBM1" ‖ 1 ‖ u8 n ‖ 0 0 ‖ u32 image_version ‖ u32 0 ‖ u64 issued_at ‖ release_key_hash[32] ‖ n stages ‖ sig` | release key, `ZXV-BA1-MANIFEST` |
| Manifest id | CIDv1, raw codec, sha2-256 of the whole record: `01 55 12 20 ‖ SHA-256(record)`; text form `b` + base32 (`bafkrei...`) | — |
| ZHP-1 provisioning profile | `"ZPR1" ‖ 1 ‖ u8 model_len ‖ u8 serial_len ‖ 0 ‖ device_key_hash[32] ‖ maker_key_hash[32] ‖ manifest_id[36] ‖ claim_commit[32] ‖ hw_profile_digest[32] ‖ u64 issued_at ‖ model ‖ serial ‖ sig` | maker key, `ZXV-ZHP1-PROFILE` |
| Claim commitment | `SHA3-256("ZXV-ZTP1-COMMIT" ‖ device_id ‖ secret)` | — |
| QR text | `ZTP1:` + base32(device_id[16] ‖ secret[32]), 82 characters, a 37x37 QR (version 5, level M) | — |
| Message (claim / transfer / reset) | `"ZTM1" ‖ 1 ‖ kind ‖ 0 0 ‖ device_id[16] ‖ u32 epoch ‖ u32 0 ‖ u64 issued_at ‖ new_owner_pk[2592] ‖ mac[32] ‖ sig`, 7291 bytes | new owner (claim) or current owner, `ZXV-ZTP1-MSG` |

`device_id` is the first 16 bytes of the device key hash. The MAC is
HMAC-SHA256 under the claim secret over everything before it (claims only;
zero otherwise). `hw_profile_digest` can carry the digest of the unit's
ZHP-1 hardware profile (`sdk/docs/HW_PROFILE.md`), or zeros. Manifests should
be signed deterministically so the same manifest always has the same id.

## How to verify

**A boot quote** (`ba_verify_quote`). The verifier sends a fresh 32-byte
nonce and remembers it (`ba_verifier_issue`). The quote passes only if all
of these hold, checked in this order; each failure has its own error:

1. the quote and the manifest are well formed;
2. the manifest names the release key you trust and its signature is good;
3. the manifest's image version is not below your minimum (rollback);
4. the quote names this manifest's id;
5. the quote names this device's key and its signature is good (a tampered
   log, nonce or chain fails here);
6. the nonce was issued by you and not used before (a replay fails);
7. replaying the event log gives the chain in the quote;
8. every stage is in the manifest, in the manifest's order, with the
   manifest's hash, and no stage is missing.

A passing quote uses up its nonce.

**A new device** (owner app). Verify the maker's profile with the maker's
public key (`ztp_profile_verify`), parse the QR (`ztp_qr_parse`) and check
that it opens the profile's commitment (`ztp_qr_check`). Optionally ask the
device for a boot quote against the manifest the profile names. Then build
and send the claim (`ztp_msg_build`).

**The device** (`ztp_device_accept`) checks format, its own state, the
device id, "already claimed", the ownership epoch, the MAC and the
signature, in that order. Every claim, transfer and reset moves the epoch
forward by one, so an old message can never be replayed.

## Turning on physical-presence reset

Factory reset without the owner's key is off. Build with
`-DZTP_PHYSICAL_RESET=1` to enable `ztp_reset_physical`, and call it only
from a path a remote attacker cannot reach (for example a button held during
boot). This is the usual trade-off: it rescues a device whose owner lost
their key, and it also lets a thief reset a stolen device.

## Limits

- **Consumer phones and payment terminals.** Firmware-level provisioning and
  measured boot only work on devices that run our image, or whose maker
  builds these steps into their own firmware. On ordinary phones and POS
  terminals the boot chain and secure element belong to the maker; there we
  can offer this only as an app (key in the phone's keystore), which proves
  what the app is, not what the phone booted.
- **Not wired into the boot path yet.** The measurement hook in the arm64 and
  x86_64 UEFI images is the next step: the EFI stub (`kernel/arch/*/efi_stub.c`)
  should hash the embedded kernel blob before handoff and pass the event log
  to the kernel through `zxv_bootinfo`, and the kernel should extend later
  stages with `ba_extend`. The existing `boot_evidence` hash is a log digest,
  not a measured chain. Today the format, signer and verifier are tested on
  the host only. Nothing here has been tested on real hardware.
- **No hardware root yet.** Without a TPM, secure element or boot ROM that
  measures the first stage, a compromised first stage can lie about all later
  stages, and the software signer (`ba_soft_signer`, flagged not
  hardware-backed) keeps the key in ordinary memory. The signer is a callback
  so a hardware key store (R1.1-R1.3) can be plugged in without changing the
  formats. TPMs do not sign with ML-DSA yet; there the TPM would seal the
  ML-DSA key rather than sign.
- **The QR code is the claim.** Whoever scans it first can claim the device.
  Keep it inside the box or on the device's screen.
- **Device state must be stored where it cannot be rolled back** (sealed
  storage). With plain flash, someone with the device on a bench could
  restore an older state.
- **Freshness only, no clock.** `issued_at` fields are signed but nothing
  checks them against a clock. The verifier's nonce memory holds 16 nonces.
- **QR encoder scope.** Byte mode, level M, versions 1-10 only. It matches
  python-qrcode and Project Nayuki's qrcodegen module for module, and
  zxing-cpp reads every reference symbol.
