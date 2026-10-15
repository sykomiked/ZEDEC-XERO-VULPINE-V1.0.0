# Wave 4 task: Lockbox (slot 9), Keys and passwords
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 9, build position 40 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Coffer (layer 73, wave 4, an earlier wave: `use coffer 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 3 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: ML-DSA identity keys, sealed hardware keys, saved passwords (`pqsec`, `identity`).
Logic: password strength score (length, classes, repeats as integer inputs) in sutra + kron witnessed; key expiry days left in kron + C++ wasm; limits (min length 12, max 1024) in eve. Never log or store secret bytes in tests; inputs are lengths and counts only.
needs: ledger.read, notify.
