# Wave 5 task: Pollen (slot 31), Share with nearby devices
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 31, build position 56 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Witness (layer 18, wave 1, built before this one: `use witness 1`). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: offer and fetch on the local network (`net.command` lan, share, want; `net.offers`, `net.peers`), chunk with `freight.pack` and rebuild with `freight.unpack`, read and save in the Hoard (`files.read`, `files.write`), out of a space only through `files.cross`.
Logic (critical): "fits on the receiver" in sutra + kron witnessed; chunk plan (freights and packets for a size and k) exact in trebago + kron witnessed; share limits (file size, files per offer, offers open) in eve; progress x100 from the used_share block (`use used_share 1`); share record in myra.
Each file is shared only on the owner's approval; tests stay on lan or loopback.
needs: peers, fs.read, fs.write, notify.
Planned (no runtime yet): Gleam, JavaScript, Nim.
