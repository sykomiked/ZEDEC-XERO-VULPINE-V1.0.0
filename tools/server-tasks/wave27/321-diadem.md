# Wave 27 task: Diadem (slot 321), Elections
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (003 threshold, 018 witness), wave 14 (178 assize), wave 20 (242 silt), wave 22 (271 cairn).
Manifest: `slot 321`, `layer L3`, `icon icons/l3/321-diadem.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: runs elections and votes for clubs, co-ops and towns: ballots, eligibility, counting and verifiable results (`spaces.propose`, `spaces.vote`, `spaces.get`, `seats.list`, `journal.query`, `journal.verify`).
Uses: `use cairn 1` (proposals and votes); `use silt 1` (records office); `use assize 1` ("filing accepted" for challenges); `use witness 1` (chain intact); `use threshold 1` (role rights). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `eligible` (on the roll AND not yet voted AND inside the voting window) in Ada as Eve + APL as KRON witnessed; `tally` (counts per option over ballot arrays) in APL as KRON + FORTRAN as Trebago witnessed; `costs` (election costs in minor units) in COBOL as Sutra; `ballots` arrays in APL as KRON; `voter_roll` tree in MUMPS as MYRA.
BOTH test: ballot 330 is valid by the roll and challenged by an observer: it is BOTH, counted provisionally, and resolved openly. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Results are certified by the people running the election.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, net, journal, fs.write, notify, sys.read.
Planned (no runtime yet): Prolog, ALGOL 60, Haskell.
Pending: `pending Prolog eligible` (rules); `pending ALGOL_60 eligible` (proof); `pending Haskell eligible` (proof).
