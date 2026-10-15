# Wave 5 task: Lodestone (slot 23), Inspector for developers
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 23, build position 52 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Seedbank (layer 33, wave 3, an earlier wave: `use seedbank 1`); Trellis (layer 29, wave 2, an earlier wave: `use trellis 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: methods, models, peers, capabilities (`rpc.discover`, `model.info`, `net.peers`).
Logic: method counts per group in kron + trebago witnessed; mean latency (exact, a vector of ms) in trebago + kron witnessed; min and max latency in kron; peer record in myra.
needs: sys.read, peers.
