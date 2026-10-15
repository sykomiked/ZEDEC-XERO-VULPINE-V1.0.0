# Wave 8 task: Hatchery (slot 65), Build and teach agents
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 65, build position 88 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. Report the reuse line `concepts.py` prints for this app.
What it does: templates (`play.catalogue`), drafts from the swarm (`play.draft`, untrusted), runs on scratch state (`play.run`), publish and run (`aipi.publish`, `aipi.execute`), energy budgets (`power.budget`), the swarm (`swarm.state`).
Logic (critical): agent limits (fuel_max, energy window, tokens per cycle) in eve + kron witnessed; teaching example counts in trebago; agent record in myra.
An agent runs only on the owner's approval. Joining `play.run`'s four-valued verdict with the other checks uses `lpres_conjoin` in C, never re-derived.
needs: swarm, ai.ask, fs.read, fs.write, notify.
Planned (no runtime yet): Python, Go, JCL.
