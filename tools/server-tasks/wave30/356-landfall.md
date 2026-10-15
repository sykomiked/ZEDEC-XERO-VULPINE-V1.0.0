# Wave 30 task: Landfall (slot 356), Expeditions
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (085 waymark), wave 12 (147 binnacle, 154 chainman), wave 14 (176 verderer), wave 21 (253 fell).
Manifest: `slot 356`, `layer L3`, `version 1.0.0`, `icon icons/l3/356-landfall.svg`, `budget 233`, model `apps/landfall_model.{h,c}`, blocks under kernel/src/apps/blocks/landfall/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans and records expeditions: routes and offline navigation (`place.here`, `place.list`, `place.add`), gear lists in the Hoard (`files.read`, `files.write`), check-ins over the mesh or radio (`chat.post`, `ehop.bands`, `ehop.seal`, `harmonic.encode`), alerts to contacts if a return time is missed (on the person's prior setup).
Uses: `use binnacle 1` (the fix, bearing and distance, "safe heading"); `use verderer 1` (population estimate and "share this place"); `use chainman 1` (parcel area and closure, for permits); `use fell 1` (crew safety and sector priority); `use waymark 1` (`place_ok`). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `turnaround`: (critical) "turn around now" (past the turnaround time OR a weather limit crossed OR a member's limit reached) in Ada as Eve + APL as KRON witnessed
- `overdue`: (critical) "raise overdue" (no check-in by the declared return time plus grace) in Ada as Eve + APL as KRON witnessed; the alert goes only to contacts the person named
- `energy`: route energy model (ascent and distance to kcal and hours, integer tables) in FORTRAN as Trebago
- `waypoints`: waypoint arrays in APL as KRON
- `permits`: permit fees (minor units, refusing on overflow) in COBOL as Sutra
- `roster`: team roster tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the satellite fix and dead reckoning differ by 900 m: the position is BOTH; the test asserts state 3 and that an area covering both is shown, not a single point. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory; never calls emergency services by itself; contacts are alerted only as the person set up.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, peers, net, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Gleam overdue`; `pending Prolog turnaround`; `pending TeX waypoints`.
Planned (no runtime yet): Gleam, Prolog, TeX.
