# Wave 5 task: Cove (slot 32), Private tunnels between your own devices
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 32, build position 55 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). Report the reuse line `concepts.py` prints for this app.
What it does: only between devices on the owner's roster (`devmesh.devices`, `devmesh.status`); frames sealed and opened with `ehop.seal`, `ehop.open` on a band from `ehop.bands`. No carrier is wired to the socket (H7): use only existing methods; list what's missing in your report.
Logic (critical): "may this tunnel open" (both devices active, neither revoked) in eve + kron witnessed; nonce discipline (seq strictly rising per channel, refusal on reuse or overflow) in sutra + Rust wasm witnessed; frame width within the band's largest frame in eve; tunnel record in myra.
Never log or store key bytes; tests use fixed test-vector keys only. Never copy anything from build_system/keys/.
needs: peers, net, notify.
Planned (no runtime yet): Go, System/360 assembler, Objective-C.
