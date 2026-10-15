# Wave 22 task: Lagoon (slot 270), Children's savings
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (142 shepherd), wave 1 (073 coffer), wave 8 (081 heirloom), wave 9 (103 cubby).
Manifest: `slot 270`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/270-lagoon.svg`, `budget 233`, model `apps/lagoon_model.{h,c}`, blocks under kernel/src/apps/blocks/lagoon/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Holds savings and guardianship plans for children until they come of age: accounts (`ledger.open_account`, `ledger.balance`, `ledger.entries`, `ledger.post`), escrow (`ledger.hold`, `ledger.holds`, `ledger.release`), the estate plan and guardians (`estate.status`, `recovery.status`), the child's seat (`seats.list`).
Uses: `use heirloom 1` ("release due" and the split preview); `use coffer 1` (balance and "transfer allowed"); `use cubby 1` ("may this child open this now"); `use shepherd 1` (`left` and `limit_ok`). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "withdrawal allowed" (the child is of age, or k guardians approved, AND within the cap) in Ada as Eve + COBOL as Sutra witnessed
- (critical) trust account balances (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- projected balance from planned deposits (no rate assumed unless the owner enters num/den) exact in FORTRAN as Trebago
- beneficiary grid (children x accounts) in APL as KRON
- family tree in MUMPS as MYRA
- a child's savings lessons, one step at a time, in BASIC (twin Hatchling)
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): a guardian's claim and the books disagree on a withdrawal, giving BOTH; the test (scratch books) asserts state 3 and the amount on `ledger.hold` until the guardians resolve it. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Financial: advisory and human-approved; guardians sign; tests use a scratch books file.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ledger.read, ledger.post, screen.time, sys.read, peers, notify.
Planned (no runtime yet): Simula 67, Oberon.
