# Wave 34 task: Wardian (slot 405), Greenhouse control
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (149 dibber); wave 19 (237 weir, 239 isobar, 230 fen); wave 7 (094 homestead).
Manifest: `slot 405`, `layer L3`, `version 1.0.0`, `icon icons/l3/405-wardian.svg`, `budget 233`, model `apps/wardian_model.{h,c}`, blocks under kernel/src/apps/blocks/wardian/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs greenhouses, vertical farms or terrariums: climate, water, light, plant health and harvests in the Hoard (`files.read`, `files.write`), stations (`place.list`, `devmesh.devices`), produce sales (`ledger.post`, `ledger.entries`). Climate sensors and actuators have no method: use only existing methods; list what's missing in your report.
Uses: `use dibber 1` (water per bed and "may water now"); `use weir 1` ("leak suspected" and litres from pulses); `use isobar 1` ("accept this reading"); `use fen 1` (growth measures and the specimen register); `use homestead 1` ("command allowed"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `climate_ok`: (critical) humidity and CO2 limits (per cent x10 and ppm within each crop's band; a step change proposed, never forced) in Ada as Eve + APL as KRON witnessed
- `climate`: (critical) climate model (vapour pressure deficit from temperature and humidity tables) in FORTRAN as Trebago + APL as KRON witnessed
- `growth_sim`: harvest date estimate from degree days in FORTRAN as Trebago
- `sensors`: sensor grid in APL as KRON
- `sales`: produce sales (minor units, refusing on overflow) in COBOL as Sutra
- `beds`: bed and plant tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two humidity sensors read 62% and 81%: the zone is BOTH; the test asserts state 3 and that control steers cautiously (no large change) while flagging both. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Actuator changes are human-approved; sales wait for the owner.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, location, peers, net, ledger.read, ledger.post, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Ladder_logic climate_ok`; `pending Simula_I growth_sim`; `pending Delphi beds`.
Planned (no runtime yet): Ladder logic, Simula I, Delphi.
