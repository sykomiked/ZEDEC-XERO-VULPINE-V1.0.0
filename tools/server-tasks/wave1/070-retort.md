# Wave 1 task: Retort (slot 70), Run and compare models
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 70, build position 3 of 130 (`../MATRIX_ORDER.tsv`). Its layer's code already leans on the core (layer 0: reach it through zxvd methods that exist); Ninefold (layer 80, wave 1, a later wave (a cycle): `use ninefold 1`); Coffer (layer 73, wave 4, a later wave (a cycle): `use coffer 1`). Take every idea those layers own from them (`use` the built ones and call `<id>.<entry>`; for one not built yet, list the entry in your report and do not write it here). 1 other layer lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: the model slot (`model.info`), answers (`swarm.ask`), runs on scratch state (`play.run`), sandboxed functions (`aipi.execute`), energy per run (`power.status`).
Logic (critical): score totals per model exact in trebago + kron witnessed; latency min and max in kron + Rust wasm witnessed; run limits (prompt bytes, fuel, seconds per run) in eve; energy per answer (microjoules) in trebago; run record in myra.
needs: ai.ask, swarm, sys.read.
Planned (no runtime yet): Mojo, Occam, Simula 67.
