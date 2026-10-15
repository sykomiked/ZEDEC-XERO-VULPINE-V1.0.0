<!--
Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
SPDX-License-Identifier: Apache-2.0
-->

# The passport: log in from any device with no server

The code is in `kernel/src/passport/`. The public API and its design notes are in `passport.h`. The tests are in `test_passport.c` (`make -C kernel test-passport`). The passport is built on ident (`docs/IDENTITY_AND_RECOVERY.md`): it adds no new signature scheme and no new key type.

## Part 1: for users

### One secret, every device

Your identity is a 32-byte secret, the seed. Any device that has it rebuilds exactly the same identity, the same keys and the same place to look for your data. There is no account server to ask, and nothing to sync before you can start.

You can carry the seed two ways:

- **As text.** 55 characters in groups of five, like `7K3QM-…`. Capital letters, small letters, `O` for zero and `I` or `L` for one are all read the same. A built-in check catches a mistyped character.
- **As a passphrase and a handle.** The handle is a name you will remember, such as your first name; it keeps two people with the same passphrase apart. The passphrase must be at least 16 characters. Your device uses 64 MiB of memory and three passes over it to turn them into a seed, which makes each guess expensive for an attacker. A random seed is still far stronger than any passphrase.

### Your balance on every device

Your devices do not send each other a balance. They send each other receipts: one for each thing you earned, one for each thing you spent. When two devices meet, each keeps every receipt the other has, and both work out the balance from the same receipts. So if your home server earned 50 and your phone earned 5 while they were apart, you have 55 afterwards, not 50.

If two of your devices spent the same money while they could not see each other, both spends stay on record. One of them is paid, and the other is marked as contested. Every device and every peer reaches the same verdict about which one, without asking anyone.

### Where your data lives

Your devices pack the receipts into a sealed capsule and hand it to the swarm. Nodes that keep it see only scrambled bytes. They cannot tell whose it is, and they cannot change it without your device noticing. A new device finds your capsule by a lookup name that only your seed can compute, and that name changes every day.

### Café mode: a computer that is not yours

Never type your seed on a computer you do not own. Instead:

1. The café computer shows a QR code and a six-digit number.
2. You scan the code with your phone. Your phone shows the same six digits and what the café computer is asking for: to see your balance, to spend, to run swarm queries.
3. You approve, and choose a spending limit. The session lasts at most 12 hours.
4. When you are done, close the session. The café computer's keys for it are wiped.

The café computer can only do what you approved, up to the limit, until the session ends. It can see whatever is on its screen while you use it. If the computer is compromised, assume whoever controls it can spend up to the limit before the session expires, so keep limits small.

## Part 2: how it works

### Keys from the seed

| Key | How | Used for |
|---|---|---|
| Root identity | `id_root_from_seed(seed)`: ML-DSA-87 + SLH-DSA-SHAKE-256s at MATRIX | your DID; certifies devices |
| Vault key | `id_root_derive(seed, "passport-vault")` | capsule keys, café report keys |
| Rendezvous key | `id_root_derive(seed, "passport-rendezvous")` | lookup topics |
| Topic for an epoch | SHAKE256("zxv.passport.topic.v1" ‖ rendezvous key ‖ epoch) | where capsules are announced |
| Passphrase seed | Argon2id(passphrase, SHA3-256("zxv.passport.v1/salt" ‖ handle)), 64 MiB, 3 passes, 1 lane | the seed, when there is no typed seed |

Every label is length-prefixed, as in ident's own derivations: the hash input is one byte holding the label's length, the label, then the parts. Labels longer than 64 bytes are cut to 64.

Both signature schemes are stateless. Two devices can sign at the same moment with no risk of reusing a one-time key, so devices never coordinate before signing. SHAKE256 and SHA3-256 are the key derivation functions, as everywhere else in the project.

The topic is keyed by a secret rather than computed from the public key. A node that knows your public key still cannot tell which topic is yours. The epoch length is the host's choice; a day is the default.

### Devices

Each device has its own ident passkey (ML-DSA-65), which never leaves it. The root certifies each passkey (`id_dev_cert_t`). The set of devices is part of the personal state, so it merges like everything else. `pp_status_rebuild` signs ident's status list from that set, leaving out revoked devices. Two devices adding themselves at the same time therefore cannot overwrite each other, which a list replaced by a higher epoch would allow.

A spend that leaves a device is a voucher: the spend's 96 bytes after the account's DID, signed by the device's passkey with the context `zxv.passport.spend.v1`. A peer accepts it only if the status list is signed by the root and lists the device, the root certified the device's key, and the voucher verifies (`pp_verify_device_spend`).

### The state and the merge

The state is five sets: earn receipts, spends, café grants, revocations and devices. Each set is kept sorted by its encoding, so the encoding is canonical and two devices holding the same sets have the same SHA3-256 digest. Merging is set union. A revocation keeps the lower of two last sequences, which is also order-independent. A merge is checked in full before anything is written, and refused whole when the store is too small.

`pp_state_replay` recomputes everything from the sets, with the paraconsistent ledger's rules (`docs/PARACONSISTENT_LEDGER.md`):

| Case | State | lpres | Effect on the balance |
|---|---|---|---|
| A receipt that counts | COUNTED | TRUE | earned |
| One receipt hash with two contents | CONTESTED | BOTH | neither counts |
| Two spends on one slot (signer, sequence): a cloned or restored device | HELD | BOTH | the larger is kept back, neither is paid |
| A spend whose signer is not known yet | UNKNOWN | NEITHER | kept back until it is known |
| A spend outside its authority: a revoked key past its last sequence; a café spend past its grant's cap, outside its time or without the spend right | REFUSED | FALSE | not paid |
| A spend the earnings cannot cover | QUARANTINED | BOTH | not paid |
| Everything else | SETTLED | TRUE | paid |

Spends are funded in a fixed order: Lamport time, then signer, then sequence. A device stamps each new spend with one more than the highest Lamport time it has seen. Two devices that spend the same balance offline therefore produce one settled and one quarantined spend, and everyone computes the same split. Kept-back amounts come off before funding; if they exceed the earnings, the balance is 0 and the shortfall is reported as the deficit, never as a negative number.

Amounts are integers in Vino minor units. Vino is store credit: peers issue receipts for work done, this module counts them, and nothing is minted.

### The capsule

```
"ZPC1" | 1 | 0 0 0 | salt (32) | length (u32) | ChaCha20-Poly1305 ciphertext | tag (16)
key = SHAKE256("zxv.passport.capsule.v1" || vault key || salt), nonce 0, AAD = the 44-byte header
```

Every capsule has a fresh random salt and therefore its own key, so the fixed nonce is never reused. A capsule is named by its raw CIDv1 and announced under the epoch's topic through ident's `publish` and `fetch` host callbacks; the host connects them to IPFS, AIRC or Vinea. `pp_capsule_fetch` merges every capsule under a topic that opens and skips the rest.

The capsule is authenticated by its AEAD tag, not by a signature. Only holders of the seed have the vault key, so the tag already proves one of your devices made it. A MATRIX signature would cost about 1.5 s per sync, and if it sat outside the encryption it would let anyone with your public key link the capsule to you.

### Café mode

1. **Request.** `pp_cafe_begin` makes an ML-DSA-65 key and a pq_matrix STANDARD KEM key (ML-KEM-768 + X25519) from 32 fresh random bytes, and writes a request of about 3.2 KB: the rights, cap and duration asked for, and both public keys. Its fingerprint is SHA3-256 of the whole request. The QR code carries the fingerprint; the six-digit code is the fingerprint's first four bytes modulo one million. The full request travels over the network, because 3.2 KB does not fit in one QR code.
2. **Approval.** `pp_request_parse` refuses a request whose fingerprint differs from the scanned one. `pp_grant_issue` lets the phone give at most what was asked, caps the duration at 12 hours, and signs the grant with the phone's passkey. The grant names the account, the approving device, the session key's hash, the request's fingerprint, the rights, the cap, the validity window and the phone's Lamport time. It is recorded in the state.
3. **Envelope.** `pp_envelope_seal` encapsulates to the session's KEM key and seals the report key, plus a view of the state when the grant has the view right. Only the café session can open it.
4. **Spending.** `pp_cafe_spend` checks the right, the café's clock against the window and the remaining cap, then signs the spend with the session key. A peer runs `pp_verify_cafe_spend`: root, status list, device certificate, grant signature, session key hash, right, window, cap and voucher.
5. **Report.** `pp_cafe_report` seals the café's spends under the report key, which the phone can recompute from its vault key. `pp_report_open` merges them; the replay applies the cap, window and revocations again.
6. **End.** `pp_cafe_end` wipes the session's keys and grant.

To end a session early, add a revocation for the grant or the session key. Spends after its last sequence are refused wherever the revocation has reached.

## Honest limits

- **A compromised café machine** sees everything shown during the session and keeps the session key until the grant expires. It can spend up to the cap. A peer refuses a single voucher over the cap, but spends split across peers who do not talk to each other are only caught when the phone merges them, after the fact.
- **Zero residue is the host's job.** `pp_cafe_end` zeroes what this module holds. It cannot reach copies the browser or operating system made: swap, screenshots, a keylogger.
- **An early revocation** works only where it has reached. Until then, only the expiry protects.
- **A passphrase seed is only as strong as the passphrase.** The public key lets anyone test guesses offline. Argon2id makes each guess cost memory and time; the 16-character minimum enforces length, not entropy.
- **There is no mnemonic word list.** The seed is typed as checksummed base32.
- **The topic hides who, not that.** Someone watching the network sees one topic being polled. Topics change every epoch.
- **Quarantined spends already left.** The merge cannot take them back. It tells both sides the voucher is contested.
- **Not wired in yet.** zxvd methods, the login screen (seats and zui) and keeping the seed in hwkey's sealed storage on the user's own devices are the next steps.

## Build and test

```sh
make -C kernel test-passport
```

The test has 168 checks and runs in under a second, including one MATRIX root. The target also builds `pp_core.c`, `pp_state.c` and `pp_cafe.c` freestanding for aarch64 and i386 and checks that they call nothing outside ident, pqsec, keccak, aead and ipfs_node.
