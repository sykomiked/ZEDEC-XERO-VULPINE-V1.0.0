# Wave 3 task: Seedbank (slot 33), IPFS
Read ../APP_WAVE_SPEC.md first.
What it does: pin and publish by CID (`aipi.put`, `aipi.status`, `web4.cid`), fetch (`aipi.get`), record a published space's CID (`spaces.set_cid`). Unpinning and announcing to the network have no method: use only existing methods; list what's missing in your report.
Logic (critical): CID string check in Rust wasm + kron witnessed (the same rule as Ramble's; copy it as a block); size limits (put 1 MiB, get 65536 bytes) in eve; blockstore use x100 from the used_share block (`use used_share 1`); pin record in myra.
needs: net, fs.read, fs.write.
Planned (no runtime yet): Elixir, TeX, Smalltalk.
