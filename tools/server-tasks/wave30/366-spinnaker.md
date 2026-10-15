# Wave 30 task: Spinnaker (slot 366), Flight planning
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 12 (147 binnacle, 152 ratchet); wave 14 (175 tiller); wave 19 (239 isobar); wave 0 (085 waymark, merged).
Manifest: `slot 366`, `layer L3`, `version 1.0.0`, `icon icons/l3/366-spinnaker.svg`, `budget 233`, model `apps/spinnaker_model.{h,c}`, blocks under kernel/src/apps/blocks/spinnaker/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans and logs flights for small aircraft and drones: routes and places (`place.here`, `place.list`, `place.add`), weather from station records, checklists and maintenance in the Hoard (`files.read`, `files.write`), the link (`sys.net`). Airspace data and aircraft telemetry have no method: use only existing methods; list what's missing in your report.
Uses: `use tiller 1` ("command inside the envelope" and "hold safe state"); `use binnacle 1` (the fix and "safe heading"); `use isobar 1` ("accept this reading" and the forecast); `use ratchet 1` ("fault" and hours to service); `use waymark 1` (`place_ok`). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `wb_ok`: (critical) weight and balance (total mass and moment within the envelope) in Ada as Eve + APL as KRON witnessed
- `alt_ok`: (critical) altitude limits (planned altitude within the airspace band and the craft's ceiling) in Ada as Eve + APL as KRON witnessed
- `dynamics`: flight dynamics (take-off distance and climb from integer performance tables by mass and density altitude) in FORTRAN as Trebago
- `airspace`: airspace grid in APL as KRON
- `checklist`: checklist sequencing (items in order, none skipped) in Ada as Eve
- `fees`: landing fees (minor units, refusing on overflow) in COBOL as Sutra
- `airframe`: airframe tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two altimeters read 1200 ft and 1450 ft: altitude is BOTH; the test asserts state 3 and a crosscheck prompt, neither trusted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Advisory: not certified for navigation; the pilot in command decides.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, sys.read, fs.read, fs.write, net, camera, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending IEC_61131-3_Structured_Text alt_ok`; `pending LISP airframe`; `pending Prolog checklist`.
Planned (no runtime yet): IEC 61131-3 Structured Text, LISP, Prolog.
