# Wave 32 task: Hornpipe (slot 384), Swim coaching
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 14 (172 escapement); wave 15 (188 courser, 190 shipwright); wave 21 (253 fell); wave 7 (089 wellspring).
Manifest: `slot 384`, `layer L3`, `version 1.0.0`, `icon icons/l3/384-hornpipe.svg`, `budget 233`, model `apps/hornpipe_model.{h,c}`, blocks under kernel/src/apps/blocks/hornpipe/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Coaches swimming and water sports: drills and lap times in the Hoard (`files.read`, `files.write`), the clock (`sys.info`), open-water check-ins (`place.here`, `chat.post`).
Uses: `use courser 1` (pace and "over the heart-rate limit"); `use shipwright 1` (water level ahead); `use fell 1` (crew safety); `use escapement 1` (clock offset); `use wellspring 1` ("outside a safe range"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `swim_ok`: (critical) cold and distance limits (water temperature and minutes in it, distance from the shore at most the declared) in Ada as Eve + APL as KRON witnessed
- `stroke`: (critical) stroke analysis (strokes per length, distance per stroke, exact) in FORTRAN as Trebago + APL as KRON witnessed
- `laps`: lap arrays in APL as KRON
- `fees`: club fees (minor units, refusing on overflow) in COBOL as Sutra
- `swimmers`: swimmer tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the watch records 1:12.40 and the coach 1:11.90: the time is BOTH; the test asserts state 3 and that both stand until reviewed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory; the coach decides; never calls emergency services by itself.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, sys.read, location, peers, notify.
