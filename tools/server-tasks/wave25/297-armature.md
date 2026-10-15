# Wave 25 task: Armature (slot 297), Invention notebook
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 10 (124 graft), wave 12 (145 cartulary, 152 ratchet), wave 14 (179 cupel), wave 15 (191 lapidary).
Manifest: `slot 297`, `layer L3`, `icon icons/l3/297-armature.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: an invention lab notebook: experiments, prototypes, parts, results and dated proof of who invented what (`files.read`, `files.write`, `journal.query`, `web4.cid`, `aipi.put`).
Uses: `use ratchet 1` ("fault", wear); `use cupel 1` (integer step, precision bounds); `use cartulary 1` (retention days); `use graft 1` (history intact, commit record); `use lapidary 1` ("may publish", divider and current). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `proof_ok` (an entry is dated proof only when its content id is on the journal and the chain is intact) in Ada as Eve + APL as KRON witnessed; `test_calc` (derived results from measurements, reduced rationals) in FORTRAN as Trebago + APL as KRON witnessed; `prototype_costs` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `results` matrix in APL as KRON; `revisions` tree in MUMPS as MYRA.
BOTH test: test 4 and test 7 of the same prototype give opposite results: both stay in the notebook as BOTH, preserving the anomaly. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Bench safety limits are advisory; the person follows their workshop's rules.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, sys.read.
Planned (no runtime yet): SNOBOL, ALGOL 60, Haskell.
Pending: SNOBOL (text): no entry here is an obvious fit; put `pending SNOBOL <entry>` on the one closest to text and name it in your report; `pending ALGOL_60 proof_ok` (proof); `pending Haskell proof_ok` (proof).
