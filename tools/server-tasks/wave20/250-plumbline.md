# Wave 20 task: Plumbline (slot 250), Policy console
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (004 keystone, 008 gatehouse, 018 witness); wave 7 (093 understory); wave 10 (132 twelvefold).
Manifest: `slot 250`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/250-plumbline.svg`, `budget 233`, model `apps/plumbline_model.{h,c}`, blocks under kernel/src/apps/blocks/plumbline/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts the policy engine (kernel/src/policy with policy.c, policy_cobol.c and copybooks; kernel/src/onepolicy; kernel/src/juris with juris.c and juris_vhub.c). Jurisdiction switches (`jurisdiction.get`, `jurisdiction.set`), feature switches and which are regulated (`features.get`, `features.set`), nature-boundary budgets (`power.budget`, `power.status`), the record (`journal.query`). Reading the rulebook and the list of advisory actions have no method: use only existing methods; list what's missing in your report.
Uses: `use keystone 1` (setting limits and budget shares); `use gatehouse 1` ("is this use allowed"); `use understory 1` ("over budget" and energy cost); `use twelvefold 1` ("conclusion follows"); `use witness 1` ("chain intact" and counts). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may this switch change" (a regulated switch needs the owner's session and a confirm; an advisory action never runs by itself) in Ada as Eve + APL as KRON witnessed
- (critical) nature-boundary budget left (rolling totals against the budget) in FORTRAN as Trebago + APL as KRON witnessed
- rule matrix (rules x actions: permit, forbid) in APL as KRON
- rule history (rule versions and change counts, refusing on overflow) in COBOL as Sutra
- jurisdiction records in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): rule A permits an action and rule B forbids it, giving BOTH; the test asserts state 3 and that the action waits for the owner, neither rule winning. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Every change is the owner's; tests never turn a regulated switch on and call `jurisdiction.set` only on a scratch books file.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, journal, ledger.read, notify.
Planned (no runtime yet): Prolog, ALGOL 60, Haskell.
