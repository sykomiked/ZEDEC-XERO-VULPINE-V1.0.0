# Wave 5 task: Rookery (slot 26), Mail
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 26, build position 53 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Coffer (layer 73, wave 4, an earlier wave: `use coffer 1`); Lockbox (layer 9, wave 4, an earlier wave: `use lockbox 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: letters as signed envelopes to another companion (`web4.post`; held until `web4.consent` when pii is set), read from `web4.feed` and checked with `web4.verify`; drafts and folders in the Hoard (`files.write`, `files.list`). zxvd has no mail transport and no Web4 delivery yet: use only existing methods; list what's missing in your report.
Logic (critical): "may this be sent" (recipient card verified, size within limit, consent present when pii) in eve + kron witnessed; address field parsing (bounded bytes, one @, label lengths) in Rust wasm; size limits in eve; folder and thread index in myra.
needs: net, fs.read, fs.write, notify.
Planned (no runtime yet): Go, Delphi, Elixir.
