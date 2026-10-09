<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# Identity, optional KYC, passkeys and account recovery

The code is in `kernel/src/ident/`. The public API and its design notes are in `ident.h`. The tests are in `test_ident.c`. This document has three parts:

- a plain explanation for the people who use ZXV;
- an explanation for a compliance team, such as a central bank's;
- the privacy properties, the cryptography, and the honest limits.

## Part 1: for users

### KYC is your choice

ZXV does not ask who you are. Most things work without any identity check.

Some institutions are legally required to check who their customers are. This is called KYC, "know your customer". A bank or a central bank may switch KYC on for its own services. If you want to use those services, you can choose to get verified. If you don't, nothing about the rest of ZXV changes.

If you choose to get verified:

1. You deal directly with a licensed verifier, such as a bank or an identity provider, and show them your documents.
2. The verifier gives your device a signed note that says, for example, "KYC level 2, United Kingdom, valid until next October". The note also has sealed entries for details like your name and birth date. A sealed entry can be checked if you open it, but nobody can read it unless you do.
3. ZXV keeps that note. It never keeps copies of your passport or ID card.

When a service asks, you show only what it needs. To prove you are over 18, you open just the "over 18" entry. The service learns that one fact and checks the verifier's signature. It does not learn your name or birth date.

### Passkeys instead of passwords

Each of your devices holds its own passkey for logging in. The passkey cannot be copied off the device. To unlock it, you use the device's own Face ID, Touch ID, fingerprint or PIN. That check happens inside your phone or computer. Your face or fingerprint is never sent anywhere when you log in.

All your devices belong to one identity. When you add a device, the identity vouches for it. Services trust the identity, so any of your current devices can log you in.

### If you lose your device

When you set up recovery, you choose three things:

- **A short recovery name.** For example, "blue heron at the lake". Pick something you will remember and others won't guess. Spaces and capital letters don't matter.
- **Your guardians.** These are people you trust, or devices you own such as a home computer. The default is five guardians, any three of whom can help you get back in.
- **A biometric scan** taken on your device.

To get back in on a new device:

1. Type your recovery name. Your new device finds your sealed recovery package and shows the guardians you picked, so you can tell it is yours.
2. Ask your guardians. Each one gets an alert: "someone is recovering this account". The guardian should check with you, for example by phone. Their device waits 24 hours before it will release anything. If you still have one of your old devices, you can cancel the recovery during that time.
3. Once three guardians have approved, scan your biometric again. Small differences from the first scan are fine.
4. Your identity is back. Your new device gets a new passkey. Every old device is cut off, including the lost one. Your accounts work again from the new device.

All three are needed. The right name and biometric are not enough without three guardians. Three guardians are not enough without your name and your biometric.

### What nobody can do

- Nobody can search the network for the account that goes with a face or fingerprint. That kind of index does not exist anywhere.
- A guardian cannot see your recovery package or your identity. A guardian holds one piece of a secret, and one piece reveals nothing.
- A single guardian cannot recover your account. Neither can two.

## Part 2: for a compliance team

### What the operator configures

An operator profile (`id_kyc_profile_t`) belongs to an institution: a central bank, a commercial bank, or any other service. It sets:

| Setting | Default | Meaning |
|---|---|---|
| `required_level` | 0 (KYC off) | Minimum KYC level a customer needs to use the service. |
| `issuers` | none | Verifiers the operator trusts. Each comes with its post-quantum public key and the highest level it may attest. |
| `jur` | any | Jurisdictions (ISO 3166-1 alpha-2) whose attestations the operator accepts. |
| `max_revlist_age_ms` | any age | How fresh a verifier's revocation list must be. |
| `tiers` | none (no limits) | Per-level limits on a single payment and a daily total, in VFV minor units. |

Levels run from 0 to 4: none, basic, standard, enhanced, institutional. What each level means is for the operator and its regulator to define. ZXV enforces the number.

### What a licensed verifier does

The verifier plugs in as an adapter (`id_kyc_adapter_t`, a callback). The adapter receives no personal data. It gets the holder's public key identifier, the level asked for, the jurisdiction and a nonce.

The verifier runs its own document and liveness checks in its own system, under its own licence. It returns a signed attestation shaped like a W3C Verifiable Credential. `id_vc_to_json()` produces the VC Data Model 2.0 JSON for it. The attestation contains:

- the issuer's DID-style identifier, `did:zxv:<base32 SHA3-256 of the issuer key>`;
- the holder's per-credential key identifier;
- the KYC level, the jurisdiction, the issue time and the expiry time;
- a status-list index for revocation;
- salted SHA3-256 commitments to each personal claim, such as `given_name`, `birth_date`, `age_over_18` and `residency`;
- the issuer's signature: ML-DSA-65, ML-DSA-87, or the ML-DSA-87 + SLH-DSA dual signature, depending on the issuer's key.

The platform stores the attestation and the holder's claim openings, on the holder's device only. Documents never enter ZXV.

### What the operator receives at verification time

The customer sends a presentation (`id_vp_t`). It contains the attestation, the claims the customer chose to open, and a signature by the holder key over the operator's nonce and audience string.

`id_vp_verify()` accepts the presentation only if all of these hold:

1. the issuer is on the operator's trust list and may attest that level;
2. the issuer's signature is valid;
3. the attestation has not expired;
4. the jurisdiction is accepted;
5. the issuer's current signed revocation list does not mark the attestation as revoked, and the list is fresh enough;
6. the holder's signature binds the presentation to this operator and this nonce, so it cannot be replayed elsewhere;
7. every opened claim matches its commitment.

If the operator requires KYC and no revocation list is supplied, verification **fails closed**.

The result is an `id_kyc_status_t`. It holds the verified level, the jurisdiction, the expiry, the issuer and the opened claims.

### Limits on payments and cards

Payments and cards call `id_kyc_check_payment(profile, status, now, rail, amount, spent_today, &need)`:

- If KYC is off and no tiers are set, every payment is allowed.
- If the customer's level is below `required_level`, the call returns `ID_ERR_LEVEL`. It also returns the level needed.
- If the amount breaks the customer's tier, the call returns `ID_ERR_LIMIT` and the lowest level whose tier would allow the payment.
- Rails are the ZXV rails: DEBIT 555, CREDIT 777, EQUITY 888. An expired attestation counts as level 0.

### Records and data minimisation

| Record | Held by | Contains personal data? |
|---|---|---|
| Identity documents | The verifier, under its own retention rules | Yes. Never in ZXV. |
| Attestation and claim openings | The holder's device | Only inside commitments, unless the holder opens them. |
| Presentation | The operator, as its own evidence of the check | Only the claims the holder opened. |
| Revocation list | The issuer, published | No. It is a bitmap of indices. |
| Recovery vault | IPFS, public | No. It is encrypted, and no field is linked to a biometric. |
| Guardian share | Each guardian's device | No. It is 32 random-looking bytes. |

An operator that must keep records of its checks keeps the presentations it verified. Each one is signed by the issuer and by the holder. To audit a check, an auditor re-runs `id_vp_verify` against the issuer's public key.

### Revoking a credential

The issuer sets the credential's bit in its revocation list (`id_revlist_revoke`). It then signs and publishes the new version. Operators that require a fresh list reject the credential from then on.

## Part 3: how it works

### Passkeys

- **Format.** Credentials follow the WebAuthn / FIDO2 layout. The authenticator data is SHA-256(rp id) || flags || counter, plus the attested credential data at registration. The public key is a COSE key `{1: 7, 3: -49, -1: pk}`, which is ML-DSA-65 in draft-ietf-cose-dilithium. The signature is pure FIPS 204 ML-DSA-65 over authenticatorData || SHA-256(clientDataJSON).
- **Checks.** Relying parties check the rp id hash, user presence, user verification when required, a strictly increasing counter, and the signature.
- **Device binding.** Passkeys are device-bound: the backup-eligible flag is never set. Each passkey records the 16-byte devmesh device id of the device it lives on. ident has no device roster of its own: the roster is devmesh's.
- **Root identity.** The root identity is a `pq_matrix` dual-signature key. It is MATRIX by default: ML-DSA-87 + SLH-DSA-SHAKE-256s, both of which must verify. The root signs:
  - a certificate for each passkey (`id_dev_cert_t`);
  - a status list of the passkeys that are valid now (`id_cred_status_t`). A list with a higher epoch replaces a lower one, and any passkey it leaves out is revoked.
- **Logins.** A relying party that knows the root key accepts a login when three things hold: the certificate chains to the root, the passkey is in the current list, and the assertion verifies (`id_rp_login`). Root-certified passkeys use the platform rp id `zxv.id`. Each ZXV relying party binds the assertion to itself through its own challenge and origin.

### Recovery

```text
recovery name ──Argon2id (64 MiB, 3 passes)──► nk (64 bytes)
      nk[0..32]  ──SHA3──► lookup key  (where the vault is announced)
      nk[32..64] ──SHAKE─► key A       (opens the helper data and guardian list)

biometric features w (host-supplied bits)
      helper P = w XOR random BCH codewords      (stored, inside A)
      fe_key   = SHAKE256(salt || w)              (never stored)

G = 32 random bytes ──Shamir 3-of-5 over GF(2^8)──► one share per guardian
      share commitments (stored, so a bad share is caught)

key B = SHAKE256(nk[32..64] || fe_key || G)
      B section = AEAD(key B, root seed || level || root DID)
```

The vault is under 1.2 KB. It is pinned to IPFS as a raw CIDv1, and its SHA2-256 digest is the vault id that the guardians know. It is announced under the lookup key.

Recovery runs in this order:

1. The name gives nk. nk gives the lookup key and the vault, and opens the helper data and the guardian list.
2. Each guardian, after the waiting period, seals its share to a fresh post-quantum KEM key on the new device.
3. With k shares, a re-scan w′ is corrected to w by the BCH decoder.
4. That gives fe_key, then key B, then the root seed.
5. `id_reenrol` certifies the new device's passkey and signs a status list that holds only that passkey. The host's `revoke_device` callback is told the devmesh id of each old device, so an admin device can call `dm_revoke`.

**The fuzzy extractor.** It uses the code-offset construction with binary BCH(255, k, t). It is integer-only, and t is configurable from 4 to 30. The default is BCH(255, 91, 25), which corrects up to 25 flipped bits in each 255-bit block (about 9.8%). The input is 1 to 8 blocks, up to 2040 bits. In the tests:

- 20 out of 20 genuine re-scans with 4% noise recover the key;
- 0 out of 20 impostors do;
- a 20% noisy scan is refused.

**Why Argon2id.** It is the standardised memory-hard password hash, RFC 9106. It has reviewed time-memory trade-off bounds and published test vectors; `test_ident.c` checks the RFC 9106 §5.3 vector. A scrypt-like construction built on SHAKE would have neither. The implementation is bounded: the caller supplies the memory and the parameters are capped. It computes lanes one after another, which gives the same output as a parallel implementation. On the development host, the default 64 MiB with 3 passes takes about 0.3 s per guess.

**Guardian protections.** These run on the guardian's device (`id_guardian_t`):

| Protection | Default |
|---|---|
| Waiting period before a share is released | 24 h, by the guardian's own clock |
| Alert to the guardian on every request | always |
| Requests per account per 30 days | 3 |
| Cancels by the owner's certified passkeys during the wait | 2 per 30 days. The limit stops a thief with an old phone from blocking recovery forever. |
| Requests in flight at once | 1 |

On the new device, three failed attempts lock the session and wipe the collected shares, so new guardian approvals are needed.

### Privacy properties

| Property | Why it holds |
|---|---|
| No biometric index | The lookup key is a function of the name alone. The helper data is encrypted under the name key. No stored value depends only on the biometric. |
| No offline biometric testing from the vault | The only check that says whether a biometric is right is the AEAD under key B, which also needs G from k guardians. |
| Guardians learn nothing | One share is uniform, and up to k−1 shares say nothing about G. Guardians never see the vault keys. |
| Wrong name, wrong face, too few guardians | Each one alone is enough to make recovery fail. The tests check each case. |
| KYC data minimisation | Only signed attestations are stored. Claims stay sealed until the holder opens them. |
| Optional KYC | `required_level` is 0 by default. The wallet sends nothing to any verifier until the person opts in (`ID_ERR_DISABLED`). |

## Honest limits

- **Fuzzy extractors leak entropy.** The helper data reveals the biometric up to a BCH codeword, which can cost up to n − k = 164 bits per 255-bit block. Real feature vectors also carry far less entropy than their length. Treat the biometric as one factor, not a secret: faces can be photographed and fingerprints lifted. The recovery name and the guardians carry most of the security.
- **Biometric quality varies.** Sensors, lighting, age and injuries all change a scan. A genuine re-scan that differs in more than t bits in any block fails. Real sensor noise is bursty, not uniform like the noise in the tests.
- **The sensor pipeline is out of scope.** This module never sees an image. The host must supply a stable binary feature vector, with liveness detection, alignment and reliable-bit selection. Garbage in means no recovery.
- **Recovery names are low entropy.** Argon2id makes each guess cost memory and time, but cannot stop a patient attacker from finding the vault behind a guessable name. The lookup salt has to be fixed network-wide, because the vault must be findable from the name alone. Finding the vault is not enough without k guardians.
- **Guardians are the main barrier.** If k of them are fooled or collude, the attacker can then try biometrics offline. The waiting period, rate limits and alerts run on guardian devices. A guardian who runs modified software can skip them.
- **The new device's lockout is advisory.** An attacker runs their own code. Only the limits on guardian devices can be enforced.
- **Some steps are not constant time.** BCH decoding uses table lookups indexed by syndromes. Argon2id's data-dependent passes access memory at addresses that depend on the password. Run both on the user's own device.
- **Presentations of one credential are linkable,** by its signature and subject key. Unlinkability needs one credential per verifier (batch issuance, which the per-credential holder keys allow) or zero-knowledge proofs, which are not implemented. Predicates such as "over 18" exist only if the issuer included them as claims.
- **Browsers don't accept these passkeys yet.** The format is WebAuthn-shaped, but browsers and FIDO servers do not accept ML-DSA today. Working with them needs a classical passkey alongside, which is out of scope here.
- **devmesh revocation after total loss.** devmesh accepts a revocation only from an active admin device. If every device is lost, the new device starts a new mesh. Any old device that survives learns of its revocation from the root-signed status list, delivered by the host. devmesh does not handle this itself yet.
- **The root key's location matters.** Where the root seed lives after enrolment is a host decision. If it is kept on a phone, its safety rests on that phone's secure hardware. Keeping it only in the vault and on a home node is safer.
- **Backing up the credential wallet is the host's job.** Holder keys can be re-derived from the root seed after a recovery. The stored attestations themselves need a backup, such as an AEAD blob under `id_root_derive(seed, "wallet-backup", 0)` pinned to IPFS.
- **No randomness source and no clock.** `host.random` must be a CSPRNG, and every call takes the time from the caller. The module is single-threaded: it keeps signature scratch buffers in static storage.

## Build and test

```sh
cd kernel && gcc -std=c11 -Wall -Werror -Wextra -O2 -DTEST_HOST -Iinclude -Isrc/pqsec -Isrc/mlkem \
  -Isrc/lpres -Isrc/surplus -Isrc/edp_risk -Isrc/event_space -Isrc/modbind -Isrc/trispace \
  src/ident/test_ident.c src/ident/id_core.c src/ident/id_argon2.c src/ident/id_fuzzy.c \
  src/ident/id_shamir.c src/ident/id_passkey.c src/ident/id_kyc.c src/ident/id_recover.c \
  $(PQM_SRCS) $(PQSIG_SRCS) src/mlkem/keccak.c src/mlkem/mlkem768.c src/mlkem/mlkem_encode.c \
  src/mlkem/mlkem_sample.c src/mlkem/mlkem_ntt.c src/mlkem/mlkem_kpe.c src/tls/x25519.c \
  src/tls/aead.c src/robin_debanks/sha256.c src/ipfs_node/ipfsn_multiformats.c src/tensor/zt.c \
  -o /tmp/test_ident && /tmp/test_ident
```

There are 127 checks. They run in about 2 s, including one MATRIX root signature.
