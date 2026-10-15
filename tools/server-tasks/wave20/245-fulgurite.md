# Wave 20 task: Fulgurite (slot 245), Self-hosting build
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 4 (121 joinery), wave 5 (019 cellar), wave 6 (125 whetstone, 129 gauntlet), wave 10 (124 graft).
Manifest: `slot 245`, `layer L2`, `version 1.0.0` and its `provides` lines (so L3 apps can use it, C1), `icon icons/l2/245-fulgurite.svg`, `budget 233`, model `apps/fulgurite_model.{h,c}`, blocks under kernel/src/apps/blocks/fulgurite/, one `use <id> 1` per part below. The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit the part.
What it does: ZXV builds ZXV. Fronts build_system/mk_golden_disc.sh, build_system/golden.contents, build_system/golden.models, build_system/test_golden_disc.sh, kernel/src/zxpkg (zxrelease) and kernel/src/provenance. Runs build and test templates (`play.run`), reads sources and writes artefacts and the bill of materials (`files.read`, `files.write`), versions (`backup.versions`), room on disk (`sys.disk`), publishes the bill of materials by CID (`aipi.put`, `web4.cid`), checks for updates (`update.check`). A build-run method and image signing have no method: use only existing methods; list what's missing in your report.
Uses: `use whetstone 1` (overall verdict, counts, durations); `use graft 1` ("history intact" and the four-valued merge); `use joinery 1` (line and column to byte offset, to point at a differing byte); `use cellar 1` (`used_share` and "safe to repair"); `use gauntlet 1` (mutation score and the gate). Call these composed entries; do not re-implement them; each part's manifest is itself the block (C1).
Logic (new decisions only; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- (critical) "reproducible" (two builds' artefact byte vectors equal) in APL as KRON + COBOL as Sutra witnessed (COBOL as Sutra counts differing bytes, refusing on overflow)
- (critical) "may sign" = reproducible AND every test TRUE (joined with `lpres_conjoin` in C) AND the mutation gate passed AND the owner approved, in Ada as Eve + APL as KRON witnessed
- (critical) "refuse an unpinned download" (every dependency names a sha256 content id) in Ada as Eve + APL as KRON witnessed
- build timing (stage ms, totals) exact in FORTRAN as Trebago
- release ledger (release numbers and artefact byte totals, refusing on overflow) in COBOL as Sutra
- bill-of-materials tree (component, version, licence, CID) in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): two builds of the same source whose images differ in one byte give BOTH; the test uses two fixed image files under build/ differing at one offset and asserts state 3 and that the image is not signed. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never touch build_system/keys/; signing is the owner's act with the owner's key. Tests use image files under build/ only.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
needs: fs.read, fs.write, sys.read, backup, net, notify.
Planned (no runtime yet): ML, ALGOL 60, Bash.
