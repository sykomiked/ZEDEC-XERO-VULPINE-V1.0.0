# Wave 20 task: Sedge (slot 248), Household medicines
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (045 bowline); wave 7 (089 wellspring); wave 8 (102 fieldguide); wave 11 (143 watchfire).
Manifest: `slot 248`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/248-sedge.svg`, `budget 233`, model `apps/sedge_model.{h,c}`, blocks under kernel/src/apps/blocks/sedge/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Tracks household medicines (advice only): doses, schedules, stock and interactions kept in the Hoard (`files.read`, `files.write`, `search.query`), notices (`notes.list`). Posting a reminder has no method (as Bowline): use only existing methods; list what's missing in your report.
Uses: `use wellspring 1` ("outside a safe range" and weekly totals); `use bowline 1` (next due and "overdue"); `use watchfire 1` (medical ID records); `use fieldguide 1` ("show a caution"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may take now" (the last 24 hours plus this dose within the maximum typed from the label, and the minimum interval passed) in Ada as Eve + APL as KRON witnessed
- (critical) next dose time (minutes from the last dose and the interval) in FORTRAN as Trebago + APL as KRON witnessed
- interaction matrix (medicines x medicines, flags from the entered sources) in APL as KRON
- stock records (doses left and days of stock, refusing below zero) in COBOL as Sutra
- patient tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): source A lists no interaction and source B a major one for the same pair, giving BOTH; the test asserts state 3, both shown, and a prompt to check with a pharmacist. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advice only, never a prescription; a pharmacist or clinician decides; limits come from the label as the person typed them.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, search, notify.
Planned (no runtime yet): Bourne shell, PL/I.
