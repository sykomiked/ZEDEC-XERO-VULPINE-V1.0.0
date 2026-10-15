# Wave 5 task: Lodestone (slot 23), Inspector for developers
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 23, build position 53 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Seedbank (layer 33, wave 1, built before this one: `use seedbank 1`); Trellis (layer 29, wave 1, built before this one: `use trellis 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: methods, models, peers, capabilities (`rpc.discover`, `model.info`, `net.peers`).
Logic: method counts per group in kron + trebago witnessed; mean latency (exact, a vector of ms) in trebago + kron witnessed; min and max latency in kron; peer record in myra.
needs: sys.read, peers.
