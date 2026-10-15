# Wave 26 task: Cleave (slot 310), Stress testing
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 10 (129 gauntlet, 125 whetstone); wave 12 (146 swage); wave 13 (165 compositor).
Manifest: `slot 310`, `layer L3`, `icon icons/l3/310-cleave.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: stress tests apps and systems by breaking them on purpose and recording how and where they fail (`play.run` on scratch state, `sys.info`, `files.write`).
Uses: `use swage 1` ("may ship", release ledger); `use compositor 1` ("outputs agree", job records); `use gauntlet 1` (mutation score, "counts add up"); `use whetstone 1` (overall verdict, counts). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `blast_ok` (a test touches only targets in its declared blast radius, never the books, keys or the Hoard root) in Ada as Eve + APL as KRON witnessed; `load_gen` (requests per second ramp over time) in FORTRAN as Trebago + APL as KRON witnessed; `test_budget` (fuel per test run, refused on overflow) in COBOL as Sutra; `failures` matrix in APL as KRON; `components` tree in MUMPS as MYRA.
BOTH test: test 15 passes in run 1 and fails in run 2: it is BOTH and marked flaky; not hidden. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Scratch state only; never against systems outside the declared blast radius.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, fs.read, fs.write, ai.ask, notify.
Planned (no runtime yet): Elixir.
Pending: Elixir (faults): no entry here is an obvious fit; put `pending Elixir <entry>` on the one closest to faults and name it in your report.
