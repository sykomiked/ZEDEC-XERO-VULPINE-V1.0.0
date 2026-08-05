# ZXV ARM64 — Signed Packages + A/B Update Discipline (Status Update)

**Date:** 2026-08-04
**Path:** `05_KERNEL/`
**Scope:** ARM64. Closes audit P0-3 (unsigned update / trust chain) and the
Technical-MVP "signed install + A/B update/rollback" item, realized in the
ZXV-native "upgrade one incarnation, promote only on success, auto-rollback
on failed probation" form.

## Changes

1. **Ed25519-signed packages (ZSP).** `kernel/src/loader/zsp.c` +
   `build_system/sign_package.py`. A `.zsp` binds an ELF payload to a
   signature over its SHA-256 under an **offline root key**:
   `magic "ZSP1" | payload_len | sha256(payload) | ed25519_sig(preimage) | payload`.
   The kernel embeds the 32-byte root public key and verifies BOTH
   integrity (hash) and authenticity (signature) before executing.
   `run hello.zsp` prints `signature verified against root key (Ed25519)`;
   tamper / wrong-key / bad-magic / truncation are all rejected and refused
   execution. OpenSSL (signer) ↔ kernel `ed25519_verify` interop confirmed.

2. **A/B update + probation + auto-rollback.** `kernel/src/loader/abupdate.c`.
   Two slots (A/B). `update <pkg>` verifies the signature and stages the new
   version into the INACTIVE slot ON PROBATION — the active slot keeps
   serving. `confirm` promotes it; `rollback` reverts; probation expiry
   auto-rolls-back. A failed-signature update changes NOTHING. All state
   transitions are single journaled ZXVFS writes, so an update interrupted
   by power loss recovers to a consistent A-or-B state. This is the
   cellular-incarnation model applied to application updates.

3. **Sleeker UX (from the Cosmic blueprint / user request).** `tour` — a
   one-minute plain-language walkthrough (progressive disclosure). `pulse` —
   the kernel heartbeat with real tick / event-cycle metrics and a live bar
   (honest numbers, the timer-tick=one-event-cycle bridge made visible).
   `help` extended. No capability removed.

## Verification

- Host suites: `make verify-all` = **110 checks, ALL STAGE-1 CHECKS PASSED**,
  now including `test_zsp` (8 cases: genuine accept; tampered payload/sig/
  hash-field/wrong-key/bad-magic/truncation all reject) and `test_abupdate`
  (18 cases: init, stage→probation, probation-runs-first, confirm→promote,
  tampered-update-rejected-no-state-change, probation-expiry→auto-rollback,
  reboot-consistent journaled state).
- On target (QEMU virt + `-drive`): `run hello.zsp` verifies + executes;
  `slots`/`update`/`confirm`/`rollback` drive the A/B state machine live;
  `tour`/`pulse` work. → captured in the session evidence.
- Signing is reproducible: `build_system/build_signed_app.sh` (root key in
  `build_system/keys/`, private key NOT needed to build the kernel).

## Honest scope / what remains

- The root private key here is a dev key in `build_system/keys/`. Production
  needs an offline/HSM root, threshold release signing, and key
  rotation/revocation (audit SEC_002) — not yet done.
- A/B currently governs the APPLICATION package; extending the same slots +
  probation to the kernel image / installer is the next step (blank-disk
  installer with ESP + boot-success handshake).
- The signed-run path verifies against a single compiled-in root key; a
  multi-key / delegated-signer chain is future work.
