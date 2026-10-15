# Wave 33 task: Manometer (slot 395), Plant supervision
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (015 tidewatch), wave 12 (153 pavise), wave 14 (174 damper), wave 16 (199 furnace), wave 19 (236 levin).
Manifest: `slot 395`, `layer L3`, `version 1.0.0`, `icon icons/l3/395-manometer.svg`, `budget 233`, model `apps/manometer_model.{h,c}`, blocks under kernel/src/apps/blocks/manometer/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Supervises a power plant or microgrid: core readings, interlocks, operator logs and regulator reports, from this machine (`sys.info`, `power.status`, `power.top`, `log.tail`, `journal.query`), the perimeter (`features.get`), logs and reports in the Hoard (`files.read`, `files.write`). Plant telemetry and interlock control have no method: use only existing methods; list what's missing in your report.
Uses: `use damper 1` ("raise the alarm"); `use levin 1` ("circuit within its breaker"); `use furnace 1` (energy accounts and "within grid limits"); `use pavise 1` ("allow"); `use tidewatch 1` (`cpu_x100` and `peak`). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `interlock`: (critical) interlock limits (any redundant channel out of range, or any BOTH, calls a safe hold for the operator) in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- `thermal`: (critical) thermal hydraulics (heat balance, flow x temperature rise, integers) in FORTRAN as Trebago + APL as KRON witnessed
- `sensors`: sensor grid in APL as KRON
- `report`: regulator report counts (events per kind per period) in APL as KRON
- `billing`: generation billing (MWh x tariff, minor units, refusing on overflow) in COBOL as Sutra
- `plant`: plant component tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two redundant pressure channels read 15.2 and 16.9 MPa: the channel pair is BOTH; the test asserts state 3 and that a safe hold is proposed, neither channel trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Grid and plant actions are advisory and human-approved; the app never operates the plant.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, journal, fs.read, fs.write, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending IEC_61131-3_Structured_Text interlock`; `pending SAS report`; `pending Simula_I thermal`.
Planned (no runtime yet): IEC 61131-3 Structured Text, SAS, Simula I.
