# Wave 6 task: Assay (slot 72), Agent testing and scores
Read ../APP_WAVE_SPEC.md first.
What it does: run test prompts against the swarm's agents and score them (`swarm.state` agents and audit, `swarm.ask` under the budget gate, `play.run` for scored templates on scratch state).
Logic (critical): pass share x100 per agent in kron + trebago witnessed; "meets the bar" truth (passes, refusals, minimum runs) in eve + kron witnessed; a run's four counts (TRUE FALSE BOTH NEITHER) decided with `use pce_prob 1` (`verdict`, C); ranking over the score vector in kron. Run log stays in the C model (bounded ring).
A score is advisory: it never changes an agent's budget, standing or market share.
needs: swarm, ai.ask, sys.read.
Planned (no runtime yet): Mojo, Occam, Racket, S.
