# Wave 30 task: Pyrometer (slot 361), Heating and fire risk
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 9 (094 homestead), wave 14 (174 damper), wave 16 (199 furnace), wave 19 (236 levin).
Manifest: `slot 361`, `layer L3`, `version 1.0.0`, `icon icons/l3/361-pyrometer.svg`, `budget 233`, model `apps/pyrometer_model.{h,c}`, blocks under kernel/src/apps/blocks/pyrometer/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Tracks home and building heating, fuel use and fire risk: readings (`sys.info`, `power.status`, `log.tail`, `journal.query`), inspections and bills in the Hoard (`files.read`, `files.write`), contacts rung on the person's request (`call.start`). Boiler, thermostat and smoke-alarm devices have no method: use only existing methods; list what's missing in your report.
Uses: `use damper 1` ("raise the alarm" and temperature rise); `use furnace 1` (energy accounts and "within grid limits"); `use levin 1` ("circuit within its breaker"); `use homestead 1` ("command allowed"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `cutoff`: (critical) temperature cutoffs (any room over its limit, or a rise faster than the declared rate, asks for a shutoff the person confirms) in Ada as Eve + APL as KRON witnessed
- `fuel_bill`: (critical) fuel bills (units x tariff bands, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `heat`: heat transfer model (room heat loss W from area x U value x temperature difference) in FORTRAN as Trebago
- `inspect_due`: inspection due (days since each inspection against its interval) in APL as KRON
- `rooms`: room heat grid in APL as KRON
- `building`: building tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the smoke alarm sounds and the thermometer shows normal: the room is BOTH; the test asserts state 3 and that a check is raised, the alarm never ignored. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory; a shutoff is the person's act; never calls emergency services by itself.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, journal, fs.read, fs.write, peers, location, net, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Bourne_shell inspect_due`.
Planned (no runtime yet): Bourne shell.
