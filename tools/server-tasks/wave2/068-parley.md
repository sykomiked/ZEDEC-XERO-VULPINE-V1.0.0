# Wave 2 task: Parley (slot 68), Speech and translation
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 68, build position 15 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Murmuration (layer 61, built in wave 0: `use murmuration 1`); Typecase (layer 113, wave 2, a later wave (a cycle): `use typecase 1`); Nightjar (layer 133, wave 2, an earlier wave: `use nightjar 1`); the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: which language (`xlate.detect`, `xlate.languages`), translate on this device (`xlate.translate`). Speech recognition and speech output have no method: use only existing methods; list what's missing in your report.
Logic (critical): text sent (valid UTF-8, code points counted, within limit) in Rust wasm + kron witnessed; detection threshold and length limits in eve; language pair record in myra.
Translation is advisory: clinical, legal or safety instructions need a human check.
needs: mic, ai.ask.
Planned (no runtime yet): Mojo, Go, OCaml.
