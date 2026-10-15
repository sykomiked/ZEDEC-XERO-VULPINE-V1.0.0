# Wave 21 task: Atoll (slot 258), Marine log
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark), wave 4 (078 tallystick), wave 8 (088 weatherglass), wave 9 (102 fieldguide).
Manifest: `slot 258`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/258-atoll.svg`, `budget 233`, model `apps/atoll_model.{h,c}`, blocks under kernel/src/apps/blocks/atoll/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Keeps a marine log: tides, catches, reef health and sightings in the Hoard (`files.read`, `files.write`, `search.query`), the place (`place.here`, `place.list`), catch sales (`ledger.post`, `ledger.entries`).
Uses: `use waymark 1` (`place_ok`); `use weatherglass 1` (conversions and warnings); `use fieldguide 1` ("show a caution" and match ranking); `use tallystick 1` (invoice total and "paid in full"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "may land this catch" (season total plus this catch within the quota, size at least the minimum, zone open) in Ada as Eve + APL as KRON witnessed
- (critical) catch sale value (kg x price, minor units, refusing on overflow) in COBOL as Sutra + FORTRAN as Trebago witnessed
- tide height in mm from an integer table of constituents in FORTRAN as Trebago
- zone grid (zones x open, closed) in APL as KRON
- species tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two crew catch reports for one haul differ, giving BOTH; the test asserts state 3 and that both are kept for quota review. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Tides are advisory, not for navigation; quotas are the authority's. A sale waits for the owner's confirmation.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, fs.read, fs.write, search, ledger.read, ledger.post, net.
