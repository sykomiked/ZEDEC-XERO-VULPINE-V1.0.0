# Wave 32 task: Seine (slot 382), Sensor mesh
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (029 trellis), wave 14 (176 verderer), wave 16 (197 millrace), wave 17 (214 updraft), wave 18 (225 tributary).
Manifest: `slot 382`, `layer L3`, `version 1.0.0`, `icon icons/l3/382-seine.svg`, `budget 233`, model `apps/seine_model.{h,c}`, blocks under kernel/src/apps/blocks/seine/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Spreads a mesh of low-power sensors across water or land, gathering readings that survive lost nodes: pairing (`devmesh.invite`, `devmesh.join`, `devmesh.confirm_sas`), relays (`ehop.bands`, `ehop.seal`, `ehop.open`), peers (`net.peers`), readings in the Hoard (`files.read`, `files.write`). Sensor readings have no method: use only existing methods; list what's missing in your report.
Uses: `use tributary 1` (agreeing answers, applied to neighbouring nodes); `use updraft 1` (link margin and "transmit allowed"); `use verderer 1` ("share this place"); `use millrace 1` ("accept more" and flow rate per stage); `use trellis 1` (peers per state and "may this device be revoked"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `battery_ok`: (critical) battery limits (a node sends at most its energy budget allows, sleep below the reserve) in Ada as Eve + APL as KRON witnessed
- `filter`: (critical) signal filtering (median of 5 then a moving sum, integers) in FORTRAN as Trebago + APL as KRON witnessed
- `relay`: relay choice (next hop by link margin) in APL as KRON
- `readings`: reading arrays in APL as KRON
- `credits`: data sharing credits (readings shared, refusing on overflow) in COBOL as Sutra
- `nodes`: node tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two neighbouring sensors read 7 and 19: the cell is BOTH; the test asserts state 3 and that the map shows the anomaly, not an average. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, location, fs.read, fs.write, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Gleam relay`; `pending Ladder_logic battery_ok`; `pending Clojure readings`.
Planned (no runtime yet): Gleam, Ladder logic, Clojure.
