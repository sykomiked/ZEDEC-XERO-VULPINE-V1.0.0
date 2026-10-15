# Wave 2 task: Rookery (slot 26), Mail
Read ../APP_WAVE_SPEC.md first.
What it does: letters as signed envelopes to another companion (`web4.post`; held until `web4.consent` when pii is set), read from `web4.feed` and checked with `web4.verify`; drafts and folders in the Hoard (`files.write`, `files.list`). zxvd has no mail transport and no Web4 delivery yet: use only existing methods; list what's missing in your report.
Logic (critical): "may this be sent" (recipient card verified, size within limit, consent present when pii) in eve + kron witnessed; address field parsing (bounded bytes, one @, label lengths) in Rust wasm; size limits in eve; folder and thread index in myra.
needs: net, fs.read, fs.write, notify.
Planned (no runtime yet): Go, Delphi, Elixir.
