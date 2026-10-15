# Wave 17 task: Fellowcraft (slot 207), Twin runs
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 2 (029 trellis), wave 5 (070 retort), wave 10 (132 twelvefold).
Manifest: `slot 207`, `layer L2`, `icon icons/l2/207-fellowcraft.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: runs any job twice, in two languages (`play.run`) or on two machines (`peer.ask`, `aipi.execute`), and compares the results, each on the journal (`journal.query`).
Uses: `use witness 1` (entry record, chain intact); `use trellis 1` (peers per state); `use retort 1` (score totals, run limits, run record); `use twelvefold 1` (assignments per value). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): the overall verdict joins each element's result with `lpres_conjoin` (C, kernel order, never re-derived; say so in the manifest), and the counts per value are in APL as KRON; elements that differ and the largest difference exact in FORTRAN as Trebago + APL as KRON witnessed; contract checks (both runs inside their fuel and output limits) in Ada as Eve; comparison ledger (comparisons, agreements and disagreements, refused on overflow) in COBOL as Sutra; run tree in MUMPS as MYRA.
BOTH test: twin A outputs [1,2,3] and twin B [1,2,4]: the run is BOTH and held for review; neither is picked.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ai.ask, peers, net, journal, fs.read, fs.write.
Planned (no runtime yet): Bash, Racket.
