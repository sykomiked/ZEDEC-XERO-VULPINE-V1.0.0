# Wave 5 task: Retort (slot 70), Run and compare models
Read ../APP_WAVE_SPEC.md first.
What it does: the model slot (`model.info`), answers (`swarm.ask`), runs on scratch state (`play.run`), sandboxed functions (`aipi.execute`), energy per run (`power.status`).
Logic (critical): score totals per model exact in trebago + kron witnessed; latency min and max in kron + Rust wasm witnessed; run limits (prompt bytes, fuel, seconds per run) in eve; energy per answer (microjoules) in trebago; run record in myra.
needs: ai.ask, swarm, sys.read.
Planned (no runtime yet): Mojo, Occam, Simula 67.
