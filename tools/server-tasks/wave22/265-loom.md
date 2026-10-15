# Wave 22 task: Loom (slot 265), No-code app composer
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 3 (007 satchel, 132 twelvefold), wave 5 (024 treadle), wave 6 (048 bramble), wave 11 (131 trestle).
Manifest: `slot 265`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/265-loom.svg`, `budget 233`, model `apps/loom_model.{h,c}`, blocks under kernel/src/apps/blocks/loom/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: Fronts app composition (kernel/src/apps/zxapp.c: manifests, `use`, `provides`, at most 8 `use` lines (ZXAPP_USES); kernel/src/modbind; kernel/src/immigration; docs/CAGE.md). Boards saved in the Hoard (`files.read`, `files.write`), test runs on scratch state (`play.run`), a composed app stored and published by CID (`aipi.put`, `aipi.publish`, `web4.cid`), its functions tried in the sandbox (`aipi.execute`). A Cage run and a manifest-check method have no method: use only existing methods; list what's missing in your report.
Uses: `use trestle 1` (row and column sizes and accessibility limits); `use treadle 1` (next run time and "within budget"); `use twelvefold 1` ("conclusion follows"); `use satchel 1` (package size and file-count limits, total size); `use bramble 1` (mind-map layout). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "this wire is valid" (output kind matches input kind, num or truth, and the argument count) in Ada as Eve + APL as KRON witnessed
- (critical) budget total over the wires within the app's budget (refusing on overflow) in FORTRAN as Trebago + COBOL as Sutra witnessed
- board grid (blocks x places) in APL as KRON
- composition log (wires added and removed, counts refusing on overflow) in COBOL as Sutra
- block tree (block, version, CID, wires) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two blocks give different answers on the same wire, giving BOTH; the test asserts state 3 and that the board shows both, no silent pick. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: A composed app runs in the Cage before it is installed; installing is the owner's act.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, swarm, notify.
Planned (no runtime yet): Bourne shell, Racket, B.
