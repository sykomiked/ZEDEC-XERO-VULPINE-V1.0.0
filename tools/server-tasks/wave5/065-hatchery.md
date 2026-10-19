# Wave 5 task: Hatchery (slot 65), Build and teach agents
Read ../APP_WAVE_SPEC.md first.
What it does: templates (`play.catalogue`), drafts from the swarm (`play.draft`, untrusted), runs on scratch state (`play.run`), publish and run (`aipi.publish`, `aipi.execute`), energy budgets (`power.budget`), the swarm (`swarm.state`).
Logic (critical): agent limits (fuel_max, energy window, tokens per cycle) in eve + kron witnessed; teaching example counts in trebago; agent record in myra.
An agent runs only on the owner's approval. Joining `play.run`'s four-valued verdict with the other checks uses `lpres_conjoin` in C, never re-derived.
needs: swarm, ai.ask, fs.read, fs.write, notify.
Planned (no runtime yet): Python, Go, JCL.
