# Wave 2 task: Ramble (slot 25), Web4 viewer
Read ../APP_WAVE_SPEC.md first.
What it does: the owner's envelope feed (`web4.open`, `web4.feed`, `web4.envelope`, `web4.verify`), content by CID (`aipi.get`, `web4.cid`), agent cards (`web4.card_verify`) and Chiglet intents held for consent (`web4.consent`). Pages over HTTP have no method: use only existing methods; list what's missing in your report.
Logic (critical): CID string check (prefix, length, base32 alphabet, as byte codes) in Rust wasm + kron witnessed; "consent covers this" (scope matches, amount_minor within ceiling, not expired) in eve + sutra witnessed; envelope index in myra.
Ramble never grants consent by itself: a grant with money is advisory and the owner's act.
needs: net, ledger.read, notify.
Planned (no runtime yet): Dart, Gleam, Occam.
