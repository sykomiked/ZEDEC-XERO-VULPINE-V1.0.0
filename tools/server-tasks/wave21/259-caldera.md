# Wave 21 task: Caldera (slot 259), Natural hazard watch
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (014 wake), wave 1 (029 trellis), wave 8 (088 weatherglass), wave 11 (143 watchfire).
Manifest: `slot 259`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/259-caldera.svg`, `budget 233`, model `apps/caldera_model.{h,c}`, blocks under kernel/src/apps/blocks/caldera/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Watches natural hazards and pushes local alerts: events (`journal.query`, `log.tail`), the place (`place.here`), local alerts to the mesh only after a person confirms (`chat.post`, `pspace.post`), neighbours' nodes (`net.peers`, `devmesh.devices`). Hazard sensor feeds have no method: use only existing methods; list what's missing in your report.
Uses: `use wake 1` (`count` by level); `use weatherglass 1` (warnings); `use watchfire 1` (share-location truth); `use trellis 1` (peers per state). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "raise a local alert" (a reading at or above the evacuation threshold for this region) in Ada as Eve + APL as KRON witnessed, held for a person's confirm
- (critical) local magnitude x10 from amplitude and distance by table in FORTRAN as Trebago + APL as KRON witnessed
- hazard grid (region cells x hazard levels) in APL as KRON
- incident records (incident numbers and counts, refusing on overflow) in COBOL as Sutra
- region tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): sensor A raises an alert and sensor B shows none, giving BOTH; the test asserts state 3 and a "prepare" warning, never an all-clear. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Life-safety: advisory and human-approved; never replaces official warnings; an alert to others needs a person's confirm.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: journal, location, peers, net, notify.
Planned (no runtime yet): Elixir.
