# Wave 4 task: Ferry (slot 12), Move to a new machine
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 12, build position 48 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Caravan (layer 90, wave 2, an earlier wave: `use caravan 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: move files, spaces, keys (owner approves), settings (`files.cross`, `spaces.*`, Keepsake).
Logic: transfer plan totals (bytes, files) exact in trebago; progress x100 from the used_share block (`use used_share 1`); "fits on the target" truth in sutra + kron witnessed; per-space share in kron.
needs: fs.read, fs.write, peers, backup.
