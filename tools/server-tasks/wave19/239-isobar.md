# Wave 19 task: Isobar (slot 239), Weather station network
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (029 trellis), wave 6 (040 furrow), wave 8 (088 weatherglass, 094 homestead).
Manifest: `slot 239`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/239-isobar.svg`, `budget 233`, model `apps/isobar_model.{h,c}`, blocks under kernel/src/apps/blocks/isobar/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Runs a local weather station network: stations and sites (`place.here`, `place.list`), station nodes (`devmesh.devices`, `net.peers`), records in the Hoard (`files.read`, `files.write`). Sensor input has no method: use only existing methods; list what's missing in your report.
Uses: `use weatherglass 1` (conversions, warnings, 7-day statistics); `use homestead 1` (device records); `use furrow 1` (column sums); `use trellis 1` (peers per state). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "accept this reading" (within the sensor's range and the step limit since the last) in Ada as Eve + APL as KRON witnessed
- (critical) short forecast from the 3-hour pressure tendency (tenths of a hPa) by rule table in FORTRAN as Trebago + APL as KRON witnessed
- station grid (stations x hours) in APL as KRON
- observation records and the site tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records; observations are MYRA records
Paraconsistent (BOTH test, in test_zxapp.c): two stations 400 m apart read 2.1 C and 7.4 C, both within range, giving BOTH; the test asserts state 3 and that both are kept (a microclimate), not averaged. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Forecasts are advisory and never replace official warnings.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: location, peers, net, fs.read, fs.write, notify.
Planned (no runtime yet): IEC 61131-3 Structured Text, Elixir, Clojure.
