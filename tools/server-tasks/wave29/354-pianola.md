# Wave 29 task: Pianola (slot 354), Developer workspace
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 3 (121 joinery), wave 12 (152 ratchet), wave 13 (165 compositor), wave 19 (233 stratum), wave 20 (245 fulgurite).
Manifest: `slot 354`, `layer L3`, `version 1.0.0`, `icon icons/l3/354-pianola.svg`, `budget 233`, model `apps/pianola_model.{h,c}`, blocks under kernel/src/apps/blocks/pianola/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Builds, tests and ships apps in all five pillars from one workspace: scripts and runs (`play.catalogue`, `play.run`, `play.draft`), sources in the Hoard (`files.read`, `files.write`, `search.query`), releases by CID (`aipi.put`, `web4.cid`), versions (`backup.versions`). Build runs and signing have no method: use only existing methods; list what's missing in your report.
Uses: `use compositor 1` ("outputs agree" and job records); `use ratchet 1` ("fault" and the next repair step); `use stratum 1` (claims supported and contradicted, for the change notes); `use fulgurite 1` ("reproducible", "may sign", "refuse an unpinned download"); `use joinery 1` (line and column to byte offset, buffer limits). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `ship_ok`: (critical) "may ship" (every pillar's witnessed run agrees AND Fulgurite's `may sign` TRUE AND the owner approved) in Ada as Eve + APL as KRON witnessed, joined with `lpres_conjoin` in C
- `contract`: (critical) contracts and limits per function (declared input ranges hold on every test row) in Ada as Eve + APL as KRON witnessed
- `kernels`: numeric kernels under test (sums and rationals) in FORTRAN as Trebago
- `tests`: test arrays (functions x languages x rows, disagreements found) in APL as KRON
- `run_jobs`: test run order (modules after their dependencies) in APL as KRON
- `billing`: release billing (build minutes and fees in minor units, refusing on overflow) in COBOL as Sutra
- `modules`: module tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the Sutra and Trebago versions of one function return 1001 and 1000 on the same row: the function is BOTH; the test asserts state 3 and that it is shown, and `ship_ok` refuses. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Never touch build_system/keys/; signing is the owner's act.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, sys.read, ai.ask, backup, net.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Haskell contract`; `pending ML ship_ok`; `pending Bash run_jobs`.
Planned (no runtime yet): Haskell, ML, Bash.
