# Wave 1 task: Lockbox (slot 9), Keys and passwords
Read ../APP_WAVE_SPEC.md first.
What it does: ML-DSA identity keys, sealed hardware keys, saved passwords (`pqsec`, `identity`).
Logic: password strength score (length, classes, repeats as integer inputs) in sutra + kron witnessed; key expiry days left in kron + C++ wasm; limits (min length 12, max 1024) in eve. Never log or store secret bytes in tests; inputs are lengths and counts only.
needs: ledger.read, notify.
