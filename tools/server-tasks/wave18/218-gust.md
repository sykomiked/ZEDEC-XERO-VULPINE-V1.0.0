# Wave 18 task: Gust (slot 218), Outdoor play planner
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (142 shepherd), wave 3 (097 den), wave 8 (088 weatherglass), wave 9 (103 cubby).
Manifest: `slot 218`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/218-gust.svg`, `budget 233`, model `apps/gust_model.{h,c}`, blocks under kernel/src/apps/blocks/gust/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Plans children's outdoor play: where the child is and the family's safe areas (`place.here`, `place.list`, location shared only after `place.ask`), time left from the child's limits (`screen.check`, `screen.status`), the family's seats (`seats.list`); plans and the activity log in the Hoard (`files.read`, `files.write`). There is no weather source (as Weatherglass): use only existing methods; list what's missing in your report.
Uses: `use cubby 1` ("may this child open this now"); `use weatherglass 1` (unit conversion and frost, heat and wind warnings); `use shepherd 1` (`left`, `limit_ok`, `week`); `use den 1` ("may this start now" with the age rating). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may play outside now" = no warning from Weatherglass's block AND within the time and distance limits AND inside a safe area, in Ada as Eve + APL as KRON witnessed
- wind and UV model (UV index x minutes outside as an exposure total; gust above the limit by hour) exact in FORTRAN as Trebago, witnessed by APL as KRON where it changes the answer above
- time and distance limits (minutes 0..480, metres from home 0..5000, per child) in Ada as Eve
- forecast grid (hour x wind, UV, temperature; the play windows) in APL as KRON
- activity log as fixed records (child, outings, minutes), totals refusing on overflow, in COBOL as Sutra
- family tree (child, guardians, who may approve) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): forecast A wind 25 km/h (under a 40 limit, TRUE) and forecast B 55 km/h (FALSE) give BOTH; both forecasts are shown to the parents, the app neither allows nor blocks, and the test asserts state 3 and that the parent's answer decides. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: the parent decides. A child's place is never shared outside the family and never without `place.ask`.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, screen.time, sys.read, fs.read, fs.write, notify.
Planned (no runtime yet): Simula I, Pascal.
