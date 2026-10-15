# Wave 34 task: Celesta (slot 406), Mountain trips
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (088 weatherglass), wave 16 (194 icehouse), wave 19 (239 isobar), wave 21 (253 fell, 256 massif).
Manifest: `slot 406`, `layer L3`, `version 1.0.0`, `icon icons/l3/406-celesta.svg`, `budget 233`, model `apps/celesta_model.{h,c}`, blocks under kernel/src/apps/blocks/celesta/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans mountain trips and winter sports with snow reports, avalanche risk, lift passes and group check-ins: places (`place.here`, `place.list`), check-ins (`chat.post`, `pspace.post`), plans in the Hoard (`files.read`, `files.write`), lift pass payments (`ledger.post`). Snow and avalanche feeds have no method: use only existing methods; list what's missing in your report.
Uses: `use massif 1` (work-unit plan, for snowpack runs); `use icehouse 1` ("thawed copy trusted"); `use isobar 1` ("accept this reading" and the forecast); `use fell 1` (sector priority and crew safety); `use weatherglass 1` (warnings). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `slope_ok`: (critical) slope angle limits (no route over the declared degrees at the day's danger level) in Ada as Eve + APL as KRON witnessed
- `snowpack`: (critical) snowpack model (load from new snow and wind, integer tables) in FORTRAN as Trebago + APL as KRON witnessed
- `report`: trip report counts (members, check-ins, alerts) in APL as KRON
- `terrain`: terrain grid in APL as KRON
- `passes`: lift pass sales (minor units, refusing on overflow) in COBOL as Sutra
- `party`: party tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two sources rate the avalanche danger 2 and 4: the rating is BOTH; the test asserts state 3 and that routes use 4 while showing why the sources differ. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory; never replaces official bulletins; never calls emergency services by itself.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, peers, net, fs.read, fs.write, ledger.read, ledger.post, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending PHP report`; `pending Simula_67 snowpack`; `pending Modula-2 party`.
Planned (no runtime yet): PHP, Simula 67, Modula-2.
