# Wave 30 task: Shanty (slot 362), Coastal flood watch
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 13 (161 chandler); wave 15 (190 shipwright); wave 16 (197 millrace); wave 19 (239 isobar); wave 7 (088 weatherglass).
Manifest: `slot 362`, `layer L3`, `version 1.0.0`, `icon icons/l3/362-shanty.svg`, `budget 233`, model `apps/shanty_model.{h,c}`, blocks under kernel/src/apps/blocks/shanty/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Forecasts tides, waves and coastal flooding for harbours and shore towns: districts (`place.here`, `place.list`), stations (`net.peers`, `devmesh.devices`), records (`files.read`, `files.write`), alerts sent once a person approves (`pspace.post`), history (`journal.query`). Gauge and buoy readings have no method: use only existing methods; list what's missing in your report.
Uses: `use shipwright 1` (water level ahead and "warn this district"); `use isobar 1` ("accept this reading" and the forecast); `use millrace 1` ("accept more" and flow rate per stage); `use chandler 1` (tide height and tide tables); `use weatherglass 1` (warnings). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `flood_ok`: (critical) flood thresholds (forecast level plus wave run-up at or above a district's threshold) in Ada as Eve + APL as KRON witnessed
- `waves`: (critical) wave models (significant height from wind speed, fetch and duration, integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- `buoy`: buoy raw counts to millimetres with a range check in Forth (twin Cairndrake)
- `send_alert`: alert routing (district to channel) in APL as KRON
- `tides`: tide grids in APL as KRON
- `dues`: harbour dues (minor units, refusing on overflow) in COBOL as Sutra
- `coast`: coastline tree in MUMPS as MYRA
- Forth and BASIC blocks are committed in twin form: the manifest's logic line names the twin (`logic cairndrake ...`, `logic hatchling ...`).
Paraconsistent (BOTH test, in test_zxapp.c): two forecast models give 2.1 m and 2.9 m for the same high water: the forecast is BOTH; the test asserts state 3 and that the range is shown and the district prepares for 2.9 m. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory; never replaces official warnings; an alert needs a person's approval.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, peers, net, fs.read, fs.write, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Elixir send_alert`; `pending Clojure waves`.
Planned (no runtime yet): Elixir, Clojure.
