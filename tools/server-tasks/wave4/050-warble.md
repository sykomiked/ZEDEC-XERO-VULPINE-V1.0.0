# Wave 4 task: Warble (slot 50), Music library and player
Read ../APP_WAVE_SPEC.md first.
What it does: the library from the Hoard (`files.list`, `files.stat`, `search.query`), playing together in a watch hall (`hall.load`, `hall.play`, `hall.pause`, `hall.status`). Local audio output has no method: use only existing methods; list what's missing in your report.
Logic (critical): shuffle order (a permutation from a seed, every track once) in Rust wasm + kron witnessed; playlist length exact in trebago; volume limits in eve; track index in myra.
needs: fs.read, search, peers.
Planned (no runtime yet): LabVIEW, Julia.
