# Wave 21 task: Rainshadow (slot 261), Sky event planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 6 (110 sundial), wave 8 (058 pinhole), wave 9 (101 orrery).
Manifest: `slot 261`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/261-rainshadow.svg`, `budget 233`, model `apps/rainshadow_model.{h,c}`, blocks under kernel/src/apps/blocks/rainshadow/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Plans sky watching: eclipses and transits, where and when, from the place (`place.here`, `place.ask`) and the clock (`sys.info`); photo plans and room (`files.write`, `sys.disk`).
Uses: `use orrery 1` (day number and mean longitudes, "above the horizon"); `use sundial 1` (local time); `use waymark 1` (`place_ok`); `use pinhole 1` ("photo fits" and exposure limits). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "safe to look without a filter" (only inside the totality window at this place; every partial phase needs a certified filter) in Ada as Eve + APL as KRON witnessed
- (critical) contact times at this place (seconds) from the integer ephemeris tables in FORTRAN as Trebago + APL as KRON witnessed
- ephemeris grid (events x places) in APL as KRON
- observation log and event tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records here
Paraconsistent (BOTH test, in test_zxapp.c): model A gives first contact at 14:03:20 and model B at 14:04:10, giving BOTH; the test asserts state 3 and that both times are shown with their margins. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never says looking at the Sun is safe outside totality.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, sys.read, fs.write, camera.
Planned (no runtime yet): Mojo, Simula I, Pascal.
