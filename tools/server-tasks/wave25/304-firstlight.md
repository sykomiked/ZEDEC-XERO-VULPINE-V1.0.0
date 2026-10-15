# Wave 25 task: Firstlight (slot 304), A child's first computer
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 9 (100 primer, 103 cubby), wave 14 (169 hornbook), wave 17 (216 rill), wave 18 (219 scree).
Manifest: `slot 304`, `layer L3`, `icon icons/l3/304-firstlight.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: a child's first computer: safe play, reading, puzzles and first coding lessons with parents in charge (`screen.check`, `screen.status`, `files.read`, `files.write`, `play.catalogue`, `play.run`).
Uses: `use rill 1` ("age-appropriate", reading level); `use scree 1` (adaptive puzzles); `use hornbook 1` (mastery grid, next lesson step); `use cubby 1` ("may this child open this now"); `use primer 1` ("lesson passed"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `screen_ok` (minutes left today AND inside the parents' hours) in Ada as Eve + APL as KRON witnessed; `pace` (lessons a week at the child's recent pace) in FORTRAN as Trebago + APL as KRON witnessed; `pocket_money` (a play ledger in minor units, never real money) in COBOL as Sutra; `puzzles` grids in APL as KRON; first coding steps in BASIC (twin Hatchling); `child_profile` tree in MUMPS as MYRA.
BOTH test: a child's answer gets the method right and the number wrong: it is BOTH, rewarded for the method and explained; not simply marked wrong. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Twin form: the BASIC block is committed in its twin form (Hatchling) and its manifest logic line names `hatchling`.
Parents set every limit; no contact with strangers; no real money.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, screen.time, sys.read, ai.ask.
Planned (no runtime yet): Mojo, Prolog.
Pending: Mojo (ai): no entry here is an obvious fit; put `pending Mojo <entry>` on the one closest to ai and name it in your report; `pending Prolog screen_ok` (rules).
