# Wave 20 task: Silt (slot 242), Official records office
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (001 banker), wave 1 (018 witness), wave 6 (042 broadsheet, 047 sealwax).
Manifest: `slot 242`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/242-silt.svg`, `budget 233`, model `apps/silt_model.{h,c}`, blocks under kernel/src/apps/blocks/silt/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Receives, stamps, files and retains official records with a signed trail: records in the Hoard (`files.list`, `files.stat`, `files.read`, `files.write`, `files.mkdir`; disposal only to the Trash with `files.trash` after approval), versions (`backup.versions`), the trail (`journal.query`, `journal.verify`), signed copies checked (`web4.verify`).
Uses: `use sealwax 1` ("ready to sign"); `use broadsheet 1` ("well formed"); `use witness 1` ("chain intact" and counts by kind); `use banker 1` (`share`, `total`, `entry_ok`). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may dispose" (retention period over AND no legal hold AND the records officer approved) in Ada as Eve + APL as KRON witnessed
- backlog forecast (arrivals minus processed per day, days to clear) exact in FORTRAN as Trebago
- filing grid (series x years) in APL as KRON
- register of records (register numbers per year, refusing on overflow) in COBOL as Sutra
- case file tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two records with the same reference and different dates give BOTH; the test asserts state 3 and that both are filed and flagged, never merged. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Disposal is human-approved and goes to the Trash first; nothing is erased by the app.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, journal, backup, notify.
Planned (no runtime yet): ALGOL 60, Haskell.
