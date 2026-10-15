# Wave 30 task: Argonaut (slot 367), Chambered savings
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (082 larder), wave 14 (171 mercer), wave 17 (212 eddy), wave 19 (234 neap), wave 22 (267 holm).
Manifest: `slot 367`, `layer L3`, `version 1.0.0`, `icon icons/l3/367-argonaut.svg`, `budget 233`, model `apps/argonaut_model.{h,c}`, blocks under kernel/src/apps/blocks/argonaut/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Grows savings in chambered goals that compound over time, with locked portions: accounts (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.open_account`, `ledger.post`), holds for locked chambers (`ledger.hold`, `ledger.holds`, `ledger.release`).
Uses: `use neap 1` ("entry may post" and A = L + E); `use eddy 1` (balance after n periods and "rate within its cap"); `use holm 1` ("may leave hardened mode" reused as the unlock check); `use mercer 1` ("sale on credit allowed" pattern for overdraw refusal); `use larder 1` (spent per category and "over budget"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `withdraw_ok`: (critical) withdrawal limits (an emergency chamber opens only on the declared conditions; a future chamber only after its date) in Ada as Eve + COBOL as Sutra witnessed
- `compound`: (critical) integer compound growth per chamber (balance x (den + num) / den per period, floor, remainder carried) in FORTRAN as Trebago + COBOL as Sutra witnessed
- `ledger`: savings ledger (minor units, refusing on overflow) in COBOL as Sutra
- `goals`: goal grid (chambers x months) in APL as KRON
- `household`: household tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the bank statement says 1,200.00 and the app's books 1,150.00: the gap is BOTH; the test (scratch books) asserts state 3 and the 50.00 held, never written off. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Financial: advisory and human-approved; projections are not promises; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, notify.
