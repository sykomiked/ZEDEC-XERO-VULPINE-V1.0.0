# Wave 18 task: Tributary (slot 225), Many-model answers
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (061 murmuration), wave 1 (070 retort), wave 2 (072 assay), wave 4 (132 twelvefold).
Manifest: `slot 225`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/225-tributary.svg`, `budget 233`, model `apps/tributary_model.{h,c}`, blocks under kernel/src/apps/blocks/tributary/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Asks several models the same question and joins their answers by witnessed agreement: local models (`model.info`, `swarm.ask`, `swarm.state`), another node's model (`peer.ask`, priced, owner-approved), energy per answer (`power.status`), scored templates (`play.run`).
Uses: `use retort 1` (score totals, latency min and max, run limits); `use murmuration 1` (budget shares (`swarm_shares`)); `use assay 1` (pass share, "meets the bar", the `pce_prob` verdict); `use twelvefold 1` ("conclusion follows"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) agreeing answers counted per normalised answer code in APL as KRON + FORTRAN as Trebago witnessed; the verdict (all agree TRUE, split BOTH, none NEITHER) joined with `lpres_conjoin` in C
- (critical) "may ask another model" (tokens and energy within the owner's cap, peer price within its cap) in Ada as Eve + APL as KRON witnessed
- confidence score x100 per answer (agreeing models / models asked) exact in FORTRAN as Trebago
- answer matrix (models x answer codes) in APL as KRON
- query ledger (token cost per query over the D1-D9 faces of kernel/src/swarm/swarm_denom.h, refusing on overflow) in COBOL as Sutra
- source tree (question, model, answer, time) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): three models answer 42, 42 and 41 with a quorum of 3, giving BOTH; the test asserts state 3 and that both answers are shown with which model gave which, none picked at random. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Answers are suggestions held until accepted (M3); every `peer.ask` is a priced action the owner approves.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ai.ask, swarm, peers, sys.read.
Planned (no runtime yet): Mojo, Haskell, ML.
