# Wave 10 task: Colonnade (slot 127), The language pillars
Read ../APP_WAVE_SPEC.md first.
What it does: write, compile and run code in the pillars (Sutra, Trebago, Eve, KRON, MYRA) and the Lingua languages: `play.catalogue`, `play.run` (scratch state, never the books), `play.draft` (untrusted draft, shown, never run by itself).
Logic (critical): verdict across witnessed runs (all agree, or BOTH with each side kept) in eve + kron witnessed; fuel left (fuel_max - spent, refused below zero) in sutra + kron witnessed; runs per pillar and time totals exact in trebago; run limits (output at most 49152 bytes, at most 60 s) in eve; language to pillar index (from LANGUAGE_FIT.tsv, runtime column) in myra.
A draft from the swarm is checked like anything a person types.
needs: fs.read, ai.ask.
Planned (no runtime yet): OCaml, Bourne shell.
