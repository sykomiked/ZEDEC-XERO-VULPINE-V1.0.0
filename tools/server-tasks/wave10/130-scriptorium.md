# Wave 10 task: Scriptorium (slot 130), Developer documentation
Read ../APP_WAVE_SPEC.md first.
What it does: the developer docs and the daemon's method reference, searchable: `search.query`, `search.refresh`, `files.read`, `rpc.discover` (method list), examples runnable with `play.run`.
Logic (critical): result rank (hits in title, heading and body, weighted) in kron + trebago witnessed; limits (query at most 256 bytes, at most 50 results) in eve; topic index (topic, file, line) in myra.
COBOL owns no decision here; it is left out.
needs: fs.read, search.
Planned (no runtime yet): Icon, OCaml, Bash.
