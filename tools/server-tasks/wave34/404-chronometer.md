# Wave 34 task: Chronometer (slot 404), Shifts across time zones
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (043 solstice), wave 12 (145 cartulary), wave 14 (172 escapement), wave 18 (225 tributary).
Manifest: `slot 404`, `layer L3`, `version 1.0.0`, `icon icons/l3/404-chronometer.svg`, `budget 233`, model `apps/chronometer_model.{h,c}`, blocks under kernel/src/apps/blocks/chronometer/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Coordinates shifts, bookings and deadlines across time zones, with exact time from trusted clocks: the clock (`sys.info`, `devmesh.devices`, `net.peers`), rotas and bookings in the Hoard (`files.read`, `files.write`), records proven by the journal (`journal.query`). A peer clock-offset query has no method (as Escapement): use only existing methods; list what's missing in your report.
Uses: `use escapement 1` (clock offset, "job may run", timesheets); `use cartulary 1` (retention and the record hierarchy); `use tributary 1` (agreeing answers, applied to clocks); `use solstice 1` (day of week, "two events overlap"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `rest_ok`: (critical) rest limits (at least the declared hours between shifts, at most the weekly hours) in Ada as Eve + APL as KRON witnessed
- `drift`: (critical) clock drift correction (offset trend per hour from paired readings) in FORTRAN as Trebago + APL as KRON witnessed
- `parallel`: booking conflicts across people and rooms in APL as KRON
- `rota`: rota grid in APL as KRON
- `billable`: billable hours (minutes x rate, minor units, refusing on overflow) in COBOL as Sutra
- `team`: team tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two trusted clocks differ by 900 ms for one event: the timestamp is BOTH; the test asserts state 3 and that it is held with a window covering both. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, peers, fs.read, fs.write, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Haskell rest_ok`; `pending ML drift`; `pending Occam parallel`.
Planned (no runtime yet): Haskell, ML, Occam.
