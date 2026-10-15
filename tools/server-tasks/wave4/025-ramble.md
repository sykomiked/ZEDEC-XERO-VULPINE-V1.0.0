# Wave 4 task: Ramble (slot 25), Web4 viewer
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 25, build position 39 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Reckoner (layer 83, wave 2, an earlier wave: `use reckoner 1`); Caravan (layer 90, wave 2, an earlier wave: `use caravan 1`); Seedbank (layer 33, wave 3, an earlier wave: `use seedbank 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 2 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: the owner's envelope feed (`web4.open`, `web4.feed`, `web4.envelope`, `web4.verify`), content by CID (`aipi.get`, `web4.cid`), agent cards (`web4.card_verify`) and Chiglet intents held for consent (`web4.consent`). Pages over HTTP have no method: use only existing methods; list what's missing in your report.
Logic (critical): CID string check (prefix, length, base32 alphabet, as byte codes) in Rust wasm + kron witnessed; "consent covers this" (scope matches, amount_minor within ceiling, not expired) in eve + sutra witnessed; envelope index in myra.
Ramble never grants consent by itself: a grant with money is advisory and the owner's act.
needs: net, ledger.read, notify.
Planned (no runtime yet): Dart, Gleam, Occam.
