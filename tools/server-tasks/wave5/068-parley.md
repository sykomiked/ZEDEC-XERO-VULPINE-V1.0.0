# Wave 5 task: Parley (slot 68), Speech and translation
Read ../APP_WAVE_SPEC.md first.
What it does: which language (`xlate.detect`, `xlate.languages`), translate on this device (`xlate.translate`). Speech recognition and speech output have no method: use only existing methods; list what's missing in your report.
Logic (critical): text sent (valid UTF-8, code points counted, within limit) in Rust wasm + kron witnessed; detection threshold and length limits in eve; language pair record in myra.
Translation is advisory: clinical, legal or safety instructions need a human check.
needs: mic, ai.ask.
Planned (no runtime yet): Mojo, Go, OCaml.
