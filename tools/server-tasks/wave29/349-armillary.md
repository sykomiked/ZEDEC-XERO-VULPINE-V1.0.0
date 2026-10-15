# Wave 29 task: Armillary (slot 349), Precision machine care
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 3 (128 mockingbird), wave 12 (152 ratchet), wave 14 (172 escapement, 177 caliper), wave 16 (201 gearwright).
Manifest: `slot 349`, `layer L3`, `version 1.0.0`, `icon icons/l3/349-armillary.svg`, `budget 233`, model `apps/armillary_model.{h,c}`, blocks under kernel/src/apps/blocks/armillary/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Calibrates and maintains precision machines: readings from this machine (`sys.info`, `sys.disk`, `power.status`), service logs and plans in the Hoard (`files.read`, `files.write`), service times from the shared clock. Readings from other machines and their instruments have no method: use only existing methods; list what's missing in your report.
Uses: `use caliper 1` ("within tolerance" and stress); `use ratchet 1` ("fault", the next repair step, wear and hours to service); `use escapement 1` ("job may run" and timesheets); `use gearwright 1` ("motion allowed" and end position); `use mockingbird 1` (boot self-check and memory map). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `drift_ok`: (critical) torque and drift limits (torque at most the declared N.mm, drift per 1000 hours at most the bound) in Ada as Eve + APL as KRON witnessed
- `calibrate`: (critical) calibration curve (two-point and multi-point integer correction, reading to true value) in FORTRAN as Trebago + APL as KRON witnessed
- `sensors`: sensor matrices (sensors x axes, alignment offsets) in APL as KRON
- `contracts`: service contracts (visits used, cost in minor units, refusing on overflow) in COBOL as Sutra
- `assembly`: assembly tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two sensors on one axis read 12.004 mm and 12.019 mm with a 5 um tolerance: the axis is BOTH; the test asserts state 3 and that a recalibration is scheduled, neither sensor trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: a person runs every calibration and part change.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, fs.read, fs.write, peers, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending IEC_61131-3_Structured_Text drift_ok`; `pending Simula_67 calibrate`; `pending Scala sensors`.
Planned (no runtime yet): IEC 61131-3 Structured Text, Simula 67, Scala.
