# Wave 10 task: Scriptorium (slot 130), Developer documentation
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 130, build position 120 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: the developer docs and the daemon's method reference, searchable: `search.query`, `search.refresh`, `files.read`, `rpc.discover` (method list), examples runnable with `play.run`.
Logic (critical): result rank (hits in title, heading and body, weighted) in kron + trebago witnessed; limits (query at most 256 bytes, at most 50 results) in eve; topic index (topic, file, line) in myra.
COBOL owns no decision here; it is left out.
needs: fs.read, search.
Planned (no runtime yet): Icon, OCaml, Bash.
