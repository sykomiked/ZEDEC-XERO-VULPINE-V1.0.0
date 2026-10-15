# Wave 2 task: Nightjar (slot 133), Screen reader
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 133, build position 14 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Lamplight (layer 139, wave 2, an earlier wave: `use lamplight 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: speak what is on screen and where focus is; language of text from `xlate.detect`, simple mode from `ui.get`. zxvd has no accessibility-tree or speech method (kernel/src/speech is not exposed): use only existing methods; list what's missing in your report.
Logic (critical): "interrupt now" (alerts and focus changes cut in, page reading does not) in eve + kron witnessed; speech limits (80..450 words a minute, pitch 50..200) in eve; element record (role, name, row, column, state) in myra; reading order (by row, then column) in kron.
needs: sys.read, notify.
Planned (no runtime yet): OCaml, HyperTalk, Python (SDK host only).
