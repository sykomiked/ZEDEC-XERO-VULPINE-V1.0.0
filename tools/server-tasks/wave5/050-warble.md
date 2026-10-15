# Wave 5 task: Warble (slot 50), Music library and player
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 50, build position 60 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: the library from the Hoard (`files.list`, `files.stat`, `search.query`), playing together in a watch hall (`hall.load`, `hall.play`, `hall.pause`, `hall.status`). Local audio output has no method: use only existing methods; list what's missing in your report.
Logic (critical): shuffle order (a permutation from a seed, every track once) in Rust wasm + kron witnessed; playlist length exact in trebago; volume limits in eve; track index in myra.
needs: fs.read, search, peers.
Planned (no runtime yet): LabVIEW, Julia.
