# Wave 30 task: Capstan (slot 359), Turbine dispatch
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (093 understory), wave 12 (152 ratchet), wave 19 (234 neap, 236 levin, 239 isobar).
Manifest: `slot 359`, `layer L3`, `version 1.0.0`, `icon icons/l3/359-capstan.svg`, `budget 233`, model `apps/capstan_model.{h,c}`, blocks under kernel/src/apps/blocks/capstan/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Monitors and dispatches wind and water turbines: readings from this machine (`power.status`, `power.top`), station records (`place.list`), maintenance plans and output in the Hoard (`files.read`, `files.write`), grid payments in the books (`ledger.post`, `ledger.entries`). Turbine telemetry has no method: use only existing methods; list what's missing in your report.
Uses: `use isobar 1` ("accept this reading" and the short forecast); `use ratchet 1` ("fault" and wear); `use levin 1` ("circuit within its breaker" and voltage drop); `use neap 1` ("entry may post"); `use understory 1` ("over budget" and energy cost). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `rotor_ok`: (critical) rotor speed limits (rpm at most the rating, a cut-out wind speed) in Ada as Eve + APL as KRON witnessed; a shutdown is the operator's
- `grid_pay`: (critical) grid payments (kWh x tariff in minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- `aero`: blade aerodynamics (power from wind speed by the turbine's integer power curve) in FORTRAN as Trebago
- `output`: farm output grid (turbines x hours) in APL as KRON
- `parts`: turbine part tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two vibration sensors on one turbine disagree: the turbine is BOTH; the test asserts state 3 and that an inspection is scheduled, not ignored or shut down. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Grid and dispatch actions are advisory and human-approved; the app never controls the grid.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, location, fs.read, fs.write, ledger.read, ledger.post, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Ladder_logic rotor_ok`.
Planned (no runtime yet): Ladder logic.
