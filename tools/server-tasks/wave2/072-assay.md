# Wave 2 task: Assay (slot 72), Agent testing and scores
Read ../APP_WAVE_SPEC.md first.
Matrix: layer 72, build position 17 of 130 (`../MATRIX_ORDER.tsv`). Its layer leans on no other layer's code yet. 4 other layers lean on this one, so anything here that a later app also needs is a shared entry: provide it, register it in CONCEPTS.tsv with this app as owner, and keep its name general. Report the reuse line `concepts.py` prints for this app.
What it does: run test prompts against the swarm's agents and score them (`swarm.state` agents and audit, `swarm.ask` under the budget gate, `play.run` for scored templates on scratch state).
Logic (critical): pass share x100 per agent in kron + trebago witnessed; "meets the bar" truth (passes, refusals, minimum runs) in eve + kron witnessed; a run's four counts (TRUE FALSE BOTH NEITHER) decided with `use pce_prob 1` (`verdict`, C); ranking over the score vector in kron. Run log stays in the C model (bounded ring).
A score is advisory: it never changes an agent's budget, standing or market share.
needs: swarm, ai.ask, sys.read.
Planned (no runtime yet): Mojo, Occam, Racket, S.
