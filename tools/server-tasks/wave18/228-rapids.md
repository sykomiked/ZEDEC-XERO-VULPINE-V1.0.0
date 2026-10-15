# Wave 18 task: Rapids (slot 228), Emergency crew support
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark, merged); wave 2 (028 halloo); wave 7 (089 wellspring); wave 11 (143 watchfire, 134 hark).
Manifest: `slot 228`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/228-rapids.svg`, `budget 233`, model `apps/rapids_model.{h,c}`, blocks under kernel/src/apps/blocks/rapids/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Supports emergency crews in one place: triage and vitals records in the Hoard (`files.read`, `files.write`), the patient's medical ID through Watchfire's records, location (`place.here`, `place.ask`), calls (`call.start`, `call.status`, `call.hangup`, signalling only), crew devices (`phone.list`). Vital-sign devices have no method: use only existing methods; list what's missing in your report.
Uses: `use watchfire 1` (share-location truth, cancel window, medical ID records); `use wellspring 1` ("outside a safe range"); `use waymark 1` (`place_ok`); `use halloo 1` ("may this call ring now" and call duration); `use hark 1` ("may run by voice"). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) triage category from integer inputs (walking, breathing, rate per minute, capillary refill ms, obeys commands) in APL as KRON + Ada as Eve witnessed; shown as a suggestion the crew confirms
- (critical) dosage limits (dose per kg within the service's own protocol table, entered by the service; refused above it) in Ada as Eve + APL as KRON witnessed
- vitals trends (change per 5 minutes, exact) in FORTRAN as Trebago
- triage grid (patients x category, time seen) in APL as KRON
- incident billing (minutes and charges in minor units, refusing on overflow) in COBOL as Sutra
- patient record tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the medical ID says no allergies and a second record says penicillin allergy, giving BOTH; the test asserts state 3 and that both are shown at the scene and highlighted. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety and clinical: advisory and human-approved; the crew decides every step; never calls emergency services or anyone by itself; not a medical device; no dose is given on the app's word.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, peers, mic, fs.read, fs.write, notify.
Planned (no runtime yet): Elixir.
