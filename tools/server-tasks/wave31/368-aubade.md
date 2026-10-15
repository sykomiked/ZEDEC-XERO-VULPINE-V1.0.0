# Wave 31 task: Aubade (slot 368), Daylight and rhythm
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (148 poultice); wave 14 (172 escapement); wave 16 (199 furnace); wave 18 (224 tarn); wave 7 (088 weatherglass).
Manifest: `slot 368`, `layer L3`, `version 1.0.0`, `icon icons/l3/368-aubade.svg`, `budget 233`, model `apps/aubade_model.{h,c}`, blocks under kernel/src/apps/blocks/aubade/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Tracks daylight, UV and the sleep-wake rhythm to plan outdoor work and healthy light: the place (`place.here`), the clock (`sys.info`), logs in the Hoard (`files.read`, `files.write`), quiet hours (`screen.status`, `notes.list`). UV sensors have no method: use only existing methods; list what's missing in your report.
Uses: `use furnace 1` (expected solar Wh); `use poultice 1` (vital trends); `use escapement 1` ("job may run" and timesheets); `use tarn 1` (breath pacing and session limits); `use weatherglass 1` (warnings). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `uv_ok`: (critical) UV exposure limits (UV index x minutes within the skin type's declared dose) in Ada as Eve + APL as KRON witnessed
- `sun_angle`: (critical) solar elevation by minute (day of year and latitude, integer sine tables) in FORTRAN as Trebago + APL as KRON witnessed
- `daylight`: daylight grid (days x hours) in APL as KRON
- `rhythm`: rhythm plan (light before noon, dim after the declared hour) in Ada as Eve
- `shift_pay`: shift pay (minutes x rate, minor units, refusing on overflow) in COBOL as Sutra
- `workers`: worker profile tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the forecast says UV 4 and the sensor 7: UV is BOTH; the test asserts state 3 and that the warning uses 7 while showing both. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advice only, never a diagnosis.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, sys.read, fs.read, fs.write, screen.time, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Simula_I sun_angle`; `pending LabVIEW daylight`; `pending Modula-2 rhythm`.
Planned (no runtime yet): Simula I, LabVIEW, Modula-2.
