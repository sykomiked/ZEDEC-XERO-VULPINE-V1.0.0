# Wave 20 task: Lode (slot 246), Valuables register
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 3 (060 relief), wave 4 (073 coffer), wave 8 (058 pinhole).
Manifest: `slot 246`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/246-lode.svg`, `budget 233`, model `apps/lode_model.{h,c}`, blocks under kernel/src/apps/blocks/lode/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Appraises and registers precious items with photos, provenance and value records: photos and records in the Hoard (`files.read`, `files.write`, room from `sys.disk`), the trail (`journal.query`), values in the books (`ledger.accounts`, `ledger.balance`, `ledger.post`).
Uses: `use pinhole 1` ("photo fits" and device ready); `use witness 1` ("chain intact"); `use coffer 1` (balance and "transfer allowed"); `use relief 1` (vertex transform and depth effect offsets). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) grade from measured inputs within the table's bands in Ada as Eve + APL as KRON witnessed
- (critical) value recorded (minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- carat and cut metrics (carats x100 = milligrams / 2; proportions x1000) exact in FORTRAN as Trebago
- grade matrix (items x grading inputs) in APL as KRON
- provenance tree (item, holder, from, to, evidence) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two provenance chains name different holders for the same item, giving BOTH; the test asserts state 3 and that both stay on the register, neither erased. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Valuations are advisory; posting a value to the books needs the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: camera, fs.read, fs.write, sys.read, journal, ledger.read, ledger.post.
Planned (no runtime yet): Julia, Haskell, ML.
