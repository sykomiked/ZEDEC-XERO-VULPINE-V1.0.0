# Wave 4 task: Parley (slot 68), Speech and translation
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 68, build position 39 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on Murmuration (layer 61, built in wave 0: `use murmuration 1`); Typecase (layer 113, wave 2, built before this one: `use typecase 1`); Nightjar (layer 133, wave 4, built after this one: `use nightjar 1`); the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: which language (`xlate.detect`, `xlate.languages`), translate on this device (`xlate.translate`). Speech recognition and speech output have no method: use only existing methods; list what's missing in your report.
Logic (critical): text sent (valid UTF-8, code points counted, within limit) in Rust wasm + kron witnessed; detection threshold and length limits in eve; language pair record in myra.
Translation is advisory: clinical, legal or safety instructions need a human check.
needs: mic, ai.ask.
Planned (no runtime yet): Mojo, Go, OCaml.
