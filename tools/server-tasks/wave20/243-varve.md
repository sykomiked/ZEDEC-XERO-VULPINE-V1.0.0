# Wave 20 task: Varve (slot 243), History timelines
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (010 keepsake, merged); wave 3 (043 solstice); wave 5 (067 lore); wave 8 (102 fieldguide).
Manifest: `slot 243`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/243-varve.svg`, `budget 233`, model `apps/varve_model.{h,c}`, blocks under kernel/src/apps/blocks/varve/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Builds history timelines from archives, sources and dated events: the Hoard (`files.list`, `files.read`, `files.write`, `search.query`), versions (`backup.versions`), the companion's reading only with Lore's leave (`swarm.ask`).
Uses: `use lore 1` ("may the AI read this file"); `use solstice 1` (day of week, days in month, "two events overlap"); `use keepsake 1` (`removed` and `keep_ok`); `use fieldguide 1` (match ranking). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) date shown for an event (each source's earliest and latest day number intersected; circa ranges kept) in FORTRAN as Trebago + APL as KRON witnessed
- source rules (a source counts when dated and cited; primary or secondary marked) in Ada as Eve
- event matrix (events x sources) in APL as KRON
- archive catalogue and era tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records; the catalogue is MYRA's
Paraconsistent (BOTH test, in test_zxapp.c): source A dates an event to 1066 and source B to 1067 with no overlap, giving BOTH; the test asserts state 3 and that both dates stay on the timeline. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, search, ai.ask, backup.
Planned (no runtime yet): JCL, Python, Icon.
