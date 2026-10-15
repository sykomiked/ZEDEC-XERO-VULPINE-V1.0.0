# Wave 25 task: Elevation (slot 301), Building design
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 5 (060 relief); wave 13 (158 joist); wave 14 (177 caliper); wave 15 (184 trowel); wave 18 (227 cirque).
Manifest: `slot 301`, `layer L3`, `icon icons/l3/301-elevation.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: designs buildings from sketch to construction drawings, with structure checks, materials and cost (`files.read`, `files.write`, `print.submit`).
Uses: `use cirque 1` (building drawings, measurements); `use joist 1` (bill of quantities, "within the safety margin"); `use caliper 1` ("within tolerance"); `use trowel 1` ("safety check overdue"); `use relief 1` (vertex transform, mesh volume). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `code_ok` (each declared code clause met: every clause's result joined with `lpres_conjoin` in C, KRON counting clauses per value) with `clause_check` (each clause as an integer rule) in Ada as Eve + APL as KRON witnessed; `structure` (member forces and deflection, integer) in FORTRAN as Trebago + APL as KRON witnessed; `cost_estimate` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `drawings` grid in APL as KRON; `elements` tree in MUMPS as MYRA.
BOTH test: the stair meets clause 4 (rise) and breaks clause 9 (headroom): the design is BOTH and goes to the engineer; never silently approved. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; a qualified engineer and the building authority approve.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, print, notify.
Planned (no runtime yet): Julia, JCL, Logo.
Pending: `pending Julia structure` (matrix); JCL (jobs): no entry here is an obvious fit; put `pending JCL <entry>` on the one closest to jobs and name it in your report; `pending Logo structure` (geometry).
