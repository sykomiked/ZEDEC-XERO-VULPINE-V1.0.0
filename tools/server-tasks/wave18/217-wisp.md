# Wave 18 task: Wisp (slot 217), Outage kit
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (085 waymark, merged); wave 1 (021 brazier); wave 2 (011 lifeboat); wave 11 (143 watchfire).
Manifest: `slot 217`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/217-wisp.svg`, `budget 233`, model `apps/wisp_model.{h,c}`, blocks under kernel/src/apps/blocks/wisp/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Keeps light, offline maps, contacts and battery triage ready when power or network fails: battery, draw and source from `power.status` and `sys.info`; network state from `sys.net`; where the machine is and the saved places (`place.here`, `place.list`); emergency contacts and the medical ID from Watchfire's records (`files.read`); a low-power start with every optional switch off (`features.get`, `features.set`, owner-approved); ring a contact only on the person's request (`call.start`, signalling only). Offline map tiles and outside outage reports have no method: use only existing methods; list what's missing in your report.
Uses: `use brazier 1` (battery x100 and minutes left, temperature limits); `use waymark 1` (`place_ok`); `use watchfire 1` (share-location truth and the cancel window before any ring); `use lifeboat 1` ("safe to start normally" and the next repair step, reused for the low-power start). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "keep this function on" (light, contacts, map, radio, each with its draw in mW) = the reserve after it stays above the floor the owner set for contacts and light, in Ada as Eve + APL as KRON witnessed
- battery runtime per plan (charge in mWh / summed draw, minutes, exact) in FORTRAN as Trebago, checked against Brazier's minutes left
- priority grid (functions x priority rank, the order they are shed) in APL as KRON
- supply list as fixed records (item, count, litres or units), counts refusing on overflow, in COBOL as Sutra
- household medical tree (person, needs, medicines, stored as typed, never inferred) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): mains present from `power.status` (1) and a household outage report (0) give BOTH; the test feeds exactly that pair and asserts state 3, the low-power plan shown, and no switch changed without the person. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety, advisory and human-approved: Wisp never calls emergency services or anyone by itself and says on screen that it cannot reach them; no switch changes without the person.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: sys.read, location, peers, fs.read, notify.
Planned (no runtime yet): Elixir, C#.
