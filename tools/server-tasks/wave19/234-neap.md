# Wave 19 task: Neap (slot 234), Double-entry books
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (083 reckoner), wave 4 (073 coffer, 078 tallystick), wave 7 (040 furrow).
Manifest: `slot 234`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/234-neap.svg`, `budget 233`, model `apps/neap_model.{h,c}`, blocks under kernel/src/apps/blocks/neap/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Keeps double-entry books, reconciles accounts and prepares tax reports exactly: accounts and entries (`ledger.accounts`, `ledger.balance`, `ledger.entries`, `ledger.post`, `ledger.check`), suspense holds (`ledger.hold`, `ledger.holds`, `ledger.release`), statements (`iso20022.render` camt.053), the jurisdiction (`jurisdiction.get`), reports saved and printed (`files.write`, `print.submit`).
Uses: `use coffer 1` (balance per account and "transfer allowed"); `use tallystick 1` (invoice total and "paid in full"); `use reckoner 1` (period totals, tax estimate, "report allowed"); `use furrow 1` (column sums and money cells). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "entry may post" (debits equal credits, every account open, period open) in Ada as Eve + COBOL as Sutra witnessed
- (critical) A = L + E after every post in COBOL as Sutra + FORTRAN as Trebago witnessed
- depreciation (straight line and declining balance in minor units, the remainder in the last period) in FORTRAN as Trebago + COBOL as Sutra witnessed
- general ledger lines refusing on overflow in COBOL as Sutra
- account grid (accounts x months) in APL as KRON
- chart-of-accounts tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): a statement line of 250.00 and no matching book entry give BOTH; the test (scratch books) asserts state 3, the amount on a suspense `ledger.hold`, and A = L + E still holding. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Tax figures are advisory, not tax advice; a person files. Every post waits for the owner; tests use a scratch books file.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: ledger.read, ledger.post, fs.write, print, notify.
Planned (no runtime yet): RPG, TRAC, TeX.
