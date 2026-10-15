# Wave 19 task: Isthmus (slot 240), Dispute mediation
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 2 (027 commons), wave 3 (132 twelvefold), wave 6 (047 sealwax).
Manifest: `slot 240`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/240-isthmus.svg`, `budget 233`, model `apps/isthmus_model.{h,c}`, blocks under kernel/src/apps/blocks/isthmus/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Mediates disputes: the case room (`pspace.create`, `pspace.post`, `chat.post`, `chat.thread`), the record (`journal.query`, `files.read`, `files.write`), signed copies checked (`web4.verify`), printed agreements (`print.submit`).
Uses: `use commons 1` ("may I post here"); `use witness 1` ("chain intact" and counts by kind); `use twelvefold 1` ("conclusion follows"); `use sealwax 1` ("ready to sign" and amount fields). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "next step allowed" (both parties heard, cooling-off over, both consented to the proposal) in Ada as Eve + APL as KRON witnessed
- (critical) settlement split of an amount by agreed proportions (largest remainder, exact, minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed
- claim matrix (claims x parties: agreed, disputed) in APL as KRON
- case records (fees and amounts in minor units, refusing on overflow) in COBOL as Sutra
- party tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): party A says the goods were delivered on 3 May and party B says they were not, giving BOTH; the test asserts state 3, neither account marked false, and the other claims can still be agreed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: signing is each party's own act; legal and financial terms are human-approved.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: peers, net, journal, fs.read, fs.write, print, notify.
Planned (no runtime yet): Prolog, Eiffel, Solidity.
