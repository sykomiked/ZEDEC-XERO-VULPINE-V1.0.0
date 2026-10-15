# Wave 22 task: Cairn (slot 271), Community governance
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 4 (096 hamlet, 132 twelvefold), wave 7 (047 sealwax).
Manifest: `slot 271`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/271-cairn.svg`, `budget 233`, model `apps/cairn_model.{h,c}`, blocks under kernel/src/apps/blocks/cairn/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Runs community governance: proposals and votes (`spaces.propose`, `spaces.vote`, `spaces.get`, `spaces.set_members`), the circle (`social.post`, `social.feed`), signed decisions checked (`web4.verify`), the trail (`journal.query`), rulings saved (`files.write`).
Uses: `use witness 1` ("chain intact" and counts); `use twelvefold 1` ("conclusion follows"); `use hamlet 1` ("show this post to this seat"); `use sealwax 1` ("ready to sign"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) vote tallies (yes, no, abstain per proposal) in FORTRAN as Trebago + APL as KRON witnessed
- (critical) "decision carried" (quorum met AND the majority threshold) in Ada as Eve + APL as KRON witnessed
- ballot matrix (members x proposals) in APL as KRON
- member register and council tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records; the register is MYRA's
Paraconsistent (BOTH test, in test_zxapp.c): two rulings on one matter contradict, giving BOTH; the test asserts state 3 and that both stand until the arbiter or a majority resolves them. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Signing is each member's own act.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: peers, net, journal, fs.write, notify.
Planned (no runtime yet): Prolog, Haskell, ML.
