# Wave 30 task: Heartstring (slot 357), Family care
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (089 wellspring), wave 12 (148 poultice), wave 14 (172 escapement, 174 damper), wave 18 (220 glen).
Manifest: `slot 357`, `layer L3`, `version 1.0.0`, `icon icons/l3/357-heartstring.svg`, `budget 233`, model `apps/heartstring_model.{h,c}`, blocks under kernel/src/apps/blocks/heartstring/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Looks after someone you love: shared care plans, medication reminders, mood check-ins and family updates in the Hoard (`files.read`, `files.write`), the family space (`spaces.get`, `spaces.list`), notices (`notes.list`, `notes.mark_read`), alarms from the home (`log.tail`).
Uses: `use poultice 1` ("dose within the declared limit", care costs, vital trends); `use glen 1` ("this child rule takes effect" (as the care rule) and the availability grid); `use escapement 1` ("job may run" for reminders); `use damper 1` ("raise the alarm"); `use wellspring 1` ("outside a safe range"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `alert_ok`: (critical) dose and alert limits (a missed dose or out-of-range reading alerts the named carers, at most n an hour) in Ada as Eve + APL as KRON witnessed
- `smooth`: (critical) trend smoothing (7-day moving sum of mood and sleep, integers) in FORTRAN as Trebago + APL as KRON witnessed
- `updates`: family update routing (which carer hears which change) in Ada as Eve
- `mood`: mood grid (days x carers' reports) in APL as KRON
- `costs`: care costs (minor units, refusing on overflow) in COBOL as Sutra
- `care_tree`: family care tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): one carer reports "cheerful" and another "withdrawn" for the same day: the day is BOTH; the test asserts state 3 and that both reports stay visible to the family. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory; not a medical device; a clinician decides treatment.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, peers, journal, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Go updates`; `pending IPL care_tree`; `pending Simula_67 smooth`.
Planned (no runtime yet): Go, IPL, Simula 67.
