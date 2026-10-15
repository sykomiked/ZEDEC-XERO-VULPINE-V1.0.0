# Wave 2 task: Lamplight (slot 139), Reading view and read aloud
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 139, build position 13 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Kitling (layer 62, wave 1, an earlier wave: `use kitling 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: a calm reading view of a document in the Hoard (`files.read`), translated on request (`xlate.translate`, `xlate.detect`), read aloud. zxvd has no text-to-speech method: use only existing methods; list what's missing in your report.
Logic (critical): layout limits (line 45..90 characters, text 12..72 px, read-aloud 80..450 words a minute) in eve + kron witnessed; minutes to read (words / rate, rounded up) in kron; bookmark record (file, offset, time) in myra.
needs: fs.read, ai.ask.
Planned (no runtime yet): OCaml, Python (SDK host only), TRAC.
