# Audit status — October 2026, repair order

The independent audit (October 16, 2026) found 22 items. This page is the
living repair ledger. Fixed items are struck; open items carry their
repair order.

## Fixed

1. **Critical — the MAC-derived signing key.** The node's ML-DSA identity
   seed was `sha256("zxv/vinea/node/v1" + MAC)`; anyone knowing the MAC
   could reproduce the private key. The identity now mixes a persisted
   secret (`identity.seed` on the ZXV_DATA partition) generated from
   hardware entropy on first boot. The MAC-only path warns loudly and is
   bring-up only. Existing identities rotate with the next fleet image.

3. **The browser decoded refusals as success.** `ArrayBuffer[0]` is
   `undefined`, so a refusal byte became `0` (success). Both the key seat
   and the PIN branch now fail closed on non-200, empty or malformed
   replies.

5. **The generated player id could not retrieve its saved key.** The key
   was saved under all 64 hex characters while the card id carried only
   10. The display id now maps to the full key via a stable entry, and the
   actual card index is used.

6. **The Q32.32 exchange had unchecked signed overflow.** The destination
   could wrap int64 with the source debited first. Both legs are now
   checked before either commits; the transfer is rejected atomically with
   `ZCAP_OVERFLOW` (Q32-only; the double-mode build cannot overflow at
   int64 scale). 378 assertions pass in both modes.

## Open, in repair order

2. **The hosted passkey login trusts the caller's assertion.** The server
   must verify a fresh, one-use authenticator challenge; the caller-
   controlled proof flag leaves the public API.
4. **JavaScript strings are passed directly to numeric Wasm exports.**
   Card ids, catalogue names and space names need an explicit
   pointer-plus-length ABI with bounds checks in C.
7. **Vault initialization repeats AES-GCM nonces.** A persisted, crash-
   safe counter or a fresh per-instance key is required.
8. **The published browser artifacts must match one recorded commit.** The
   release pipeline publishes source and artifacts from one commit with a
   signed manifest.
9. **Boot dependency failures must be enforced, not diagnostic.**
10. **The browser boot must demonstrate a complete workflow.** Open a
    document, edit, persist, reload — through the real app runtime.
11. **Canvas accessibility.** A semantic tree synchronized with the
    renderer.
12. **Manifest and bucket must represent one snapshot.**
13. **The bucket root must serve the doorway.** The S3 root needs the
    Filebase static-website toggle; the IPFS gateway root already serves
    `index.html`.
14. **Narrow-screen usability.**
15. **HTTPS for the seed doors and the state bus.**
16. **A concrete key-custody model.** Nonextractable keys or derived
    encryption for exported backups.
17. **Loading and network failures need bounded deadlines and retry.**
18. **Retire XPASS entries; fail strict release checks on open security
    invariants.**
19. **A machine-readable test inventory for CI, runners and the audit.**
20. **Publish a per-platform capability manifest; wire one backend end to
    end.**
21. **The x86 userspace needs the full I/O, FS, IPC and lifecycle
    contracts.**
22. **One maintained release contract; pin third-party actions.**