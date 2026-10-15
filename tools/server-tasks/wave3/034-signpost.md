# Wave 3 task: Signpost (slot 34), Names and addresses
Read ../APP_WAVE_SPEC.md first.
What it does: CID forms (`web4.cid`), agent IDs (`web4.card_verify`), peers by NodeID (`net.peers`). DNS and DID resolution have no method: use only existing methods; list what's missing in your report.
Logic (critical): name syntax (labels 1..63 bytes, total at most 253, allowed bytes) in Rust wasm + eve witnessed; address class from octets in Rust wasm + kron witnessed; lookup cache record in myra; seconds left before a cached answer lapses in kron.
needs: net, peers.
Planned (no runtime yet): Erlang, SQL, Swift.
