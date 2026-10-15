# Wave 29 task: Gauze (slot 353), Clinic and medic post
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 11 (143 watchfire), wave 12 (148 poultice), wave 14 (180 apothecary), wave 18 (228 rapids), wave 20 (248 sedge).
Manifest: `slot 353`, `layer L3`, `version 1.0.0`, `icon icons/l3/353-gauze.svg`, `budget 233`, model `apps/gauze_model.{h,c}`, blocks under kernel/src/apps/blocks/gauze/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Runs a clinic or field medic post: triage, charts, medicines given and handover notes in the Hoard (`files.read`, `files.write`), the patient's medical ID, location (`place.here`), calls on a person's request (`call.start`). Vital-sign devices have no method: use only existing methods; list what's missing in your report.
Uses: `use apothecary 1` ("may sign this prescription", dose for display, billing codes); `use rapids 1` (triage category and dosage limits); `use poultice 1` ("dose within the declared limit" and vital trends); `use sedge 1` ("may take now" and the interaction matrix); `use watchfire 1` (medical ID records and the cancel window). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `give_ok`: (critical) "may give this dose now" (Apothecary's signature present AND within the hard dose limit AND no allergy reported, from any source) in Ada as Eve + APL as KRON + FORTRAN as Trebago witnessed (three witnesses)
- `dose_curve`: (critical) dosage curves (dose per kg by age band from the clinic's own table, a reduced rational) in FORTRAN as Trebago + APL as KRON witnessed
- `vitals`: vitals arrays (patients x readings by time) in APL as KRON
- `handover`: handover counts (patients per state, open tasks) in APL as KRON
- `billing`: billing (codes and totals in minor units, refusing on overflow) in COBOL as Sutra
- `charts`: patient record tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the medical ID lists no allergy and a carer reports a penicillin reaction: the allergy is BOTH; the test asserts state 3, the warning flagged, and `give_ok` refusing penicillin until a clinician resolves it. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Clinical: advisory and human-approved; a clinician signs every prescription; not a medical device; never calls emergency services by itself.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, location, peers, mic, sys.read, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending AWK handover`.
Planned (no runtime yet): AWK.
