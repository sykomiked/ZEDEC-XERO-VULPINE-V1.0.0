# Wave 3 task: Seedbank (slot 33), IPFS
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 33, build position 34 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Keystone (layer 4, wave 3, an earlier wave: `use keystone 1`); Tideturn (layer 6, wave 1, an earlier wave: `use tideturn 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 12 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: pin and publish by CID (`aipi.put`, `aipi.status`, `web4.cid`), fetch (`aipi.get`), record a published space's CID (`spaces.set_cid`). Unpinning and announcing to the network have no method: use only existing methods; list what's missing in your report.
Logic (critical): CID string check in Rust wasm + kron witnessed (the same rule as Ramble's; copy it as a block); size limits (put 1 MiB, get 65536 bytes) in eve; blockstore use x100 from the used_share block (`use used_share 1`); pin record in myra.
needs: net, fs.read, fs.write.
Planned (no runtime yet): Elixir, TeX, Smalltalk.
