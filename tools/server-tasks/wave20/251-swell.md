# Wave 20 task: Swell (slot 251), Ensemble rehearsal
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 2 (029 trellis), wave 5 (050 warble), wave 6 (057 fiddlehead, 110 sundial).
Manifest: `slot 251`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/251-swell.svg`, `budget 233`, model `apps/swell_model.{h,c}`, blocks under kernel/src/apps/blocks/swell/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Rehearses ensembles: a rehearsal hall (`pspace.create`, `pspace.members`, `hall.load`, `hall.play`, `hall.pause`, `hall.seek`, `hall.status`), players' devices (`devmesh.devices`, `net.peers`), scores and parts in the Hoard (`files.read`, `files.write`), the clock (`sys.info`). Audio in and out has no method (as Fiddlehead): use only existing methods; list what's missing in your report.
Uses: `use fiddlehead 1` (beat grid and pitch); `use warble 1` (playlist length); `use trellis 1` (peers per state); `use sundial 1` (local time). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) click time per device (start + n x 60000000 / bpm microseconds, minus that device's measured latency) in FORTRAN as Trebago + APL as KRON witnessed
- (critical) latency limits (jitter at most 30 ms, else the device is marked out of sync) in Ada as Eve + APL as KRON witnessed
- score matrix (bars x parts) in APL as KRON
- player roster and part tree in MUMPS as MYRA
- Sutra is left out: no money or counted business records; the roster is MYRA's
Paraconsistent (BOTH test, in test_zxapp.c): the conductor's and the section leader's edits to bar 12 differ, giving BOTH; the test asserts state 3 and that both edits are kept until the conductor decides. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: peers, net, fs.read, fs.write, sys.read.
Planned (no runtime yet): Erlang, Simula 67, System/360 assembler.
