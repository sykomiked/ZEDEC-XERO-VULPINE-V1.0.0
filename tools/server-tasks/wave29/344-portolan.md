# Wave 29 task: Portolan (slot 344), Survey maps
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (085 waymark), wave 12 (147 binnacle, 154 chainman), wave 20 (243 varve).
Manifest: `slot 344`, `layer L3`, `version 1.0.0`, `icon icons/l3/344-portolan.svg`, `budget 233`, model `apps/portolan_model.{h,c}`, blocks under kernel/src/apps/blocks/portolan/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Surveys and publishes maps of land, buildings and routes, merging field notes, drone scans and old charts: places (`place.here`, `place.list`, `place.add`), notes and scans in the Hoard (`files.read`, `files.write`, `search.query`), the published map by CID (`aipi.put`, `web4.cid`, `spaces.set_cid`). Drone flight logs and chart image import have no method: use only existing methods; list what's missing in your report.
Uses: `use chainman 1` (parcel area, "closes within tolerance", the parcel grid and its overlaps); `use binnacle 1` (the two-line fix and bearing and distance to waypoints); `use varve 1` (the date shown for an event, to date old charts); `use waymark 1` (`place_ok`). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `merge_ok`: (critical) "merge this source" (its survey closes within tolerance AND its date is known AND its datum is one of the declared ones) in Ada as Eve + APL as KRON witnessed
- `project`: (critical) projection from microdegrees to grid millimetres by an integer table (transverse Mercator series in fixed point) in FORTRAN as Trebago + APL as KRON witnessed
- `no_fly`: no-fly zone limits for drone scans (inside a declared zone is refused, altitude at most the declared metres) in Ada as Eve
- `elevation`: elevation grids (cells x metres, slope per cell) in APL as KRON
- `registry_fee`: parcel registry fees (minor units, refusing on overflow) in COBOL as Sutra
- `places`: place hierarchy (region, parcel, building, route) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): survey A puts a boundary corner at (1000, 2000) mm and survey B at (1000, 2350) for the same parcel, so the strip between is claimed by both: the strip's state is BOTH; the test asserts state 3 and that the map draws both lines and marks the strip as disputed, not redrawn. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Drone scans keep to the no-fly limits; a published map never carries a protected place's exact position.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, search, net.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Logo project`; `pending MATLAB elevation`; `pending Ladder_logic no_fly`.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/portolan/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): MATLAB, Ladder logic.
