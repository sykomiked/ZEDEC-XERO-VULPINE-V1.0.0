# Wave 31 task: Tatting (slot 372), Cold chain
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (150 pannier); wave 14 (176 verderer); wave 16 (194 icehouse); wave 7 (090 caravan).
Manifest: `slot 372`, `layer L3`, `version 1.0.0`, `icon icons/l3/372-tatting.svg`, `budget 233`, model `apps/tatting_model.{h,c}`, blocks under kernel/src/apps/blocks/tatting/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Keeps vaccines, food and samples cold from source to door: consignments and sensor logs in the Hoard (`files.read`, `files.write`), cold archives (`backup.run`, `backup.list`, `backup.verify`), parcels (`freight.pack`, `net.peers`, `peer.status`), places (`place.list`). Temperature loggers have no method: use only existing methods; list what's missing in your report.
Uses: `use icehouse 1` ("thawed copy trusted" and the archive catalogue); `use pannier 1` ("send now" and delivery receipts); `use verderer 1` ("share this place"); `use caravan 1` (charged weight and "within carrier limits"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `deliver_ok`: (critical) "deliver" (every log reading within the product's range AND the time above range at most the declared minutes) in Ada as Eve + APL as KRON witnessed
- `decay`: (critical) thermal decay (remaining shelf minutes from time-temperature exposure, integer table) in FORTRAN as Trebago + APL as KRON witnessed
- `sensors`: sensor arrays (loggers x minutes) in APL as KRON
- `alert`: spoilage alert routing (consignment to its handlers) in Ada as Eve
- `billing`: shipment billing (minor units, refusing on overflow) in COBOL as Sutra
- `consignments`: consignment tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two loggers in one box disagree on an excursion: the shipment is BOTH; the test asserts state 3 and that it is quarantined, neither thrown out nor delivered. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: a person releases or discards every quarantined shipment.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, backup, sys.read, peers, net, location, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending JOVIAL deliver_ok`; `pending Gleam alert`; `pending Scala sensors`.
Planned (no runtime yet): JOVIAL, Gleam, Scala.
