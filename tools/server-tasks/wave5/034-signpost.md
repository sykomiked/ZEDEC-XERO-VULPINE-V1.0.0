# Wave 5 task: Signpost (slot 34), Names and addresses
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 34, build position 58 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist). Use the ones built before this one: `use <id> 1` and call `<id>.<entry>`. An idea you need from one built after it: when more than one app needs it, write it here and register it in CONCEPTS.tsv with this app as owner (that layer will use it from you); otherwise list it in your report as that layer's. Report the reuse line `concepts.py` prints for this app.
What it does: CID forms (`web4.cid`), agent IDs (`web4.card_verify`), peers by NodeID (`net.peers`). DNS and DID resolution have no method: use only existing methods; list what's missing in your report.
Logic (critical): name syntax (labels 1..63 bytes, total at most 253, allowed bytes) in Rust wasm + eve witnessed; address class from octets in Rust wasm + kron witnessed; lookup cache record in myra; seconds left before a cached answer lapses in kron.
needs: net, peers.
Planned (no runtime yet): Erlang, SQL, Swift.
