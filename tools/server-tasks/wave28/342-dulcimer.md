# Wave 28 task: Dulcimer (slot 342), Jewellery workshop
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 3 (060 relief), wave 12 (146 swage), wave 14 (171 mercer, 172 escapement), wave 20 (246 lode).
Manifest: `slot 342`, `layer L3`, `icon icons/l3/342-dulcimer.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: designs pieces, tracks metal and stones, schedules bench work and sells finished goods (`files.read`, `files.write`, `ledger.entries`, `ledger.post`, `net.offers`).
Uses: `use swage 1` ("may ship" for design files); `use lode 1` (valuables register); `use mercer 1` (stock after a sale, credit sale); `use escapement 1` (schedule matrix); `use relief 1` (mesh volume). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `furnace_ok` (melt temperature at most the furnace's and the alloy's declared maximum) in Ada as Eve + APL as KRON witnessed; `alloy_mix` (grams of each metal for a fineness, exact) in FORTRAN as Trebago + APL as KRON witnessed; `orders` (workshop orders in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `stock` grid in APL as KRON; `pieces` lineage tree in MUMPS as MYRA.
BOTH test: commission 18's spec says 18 carat and its invoice 14 carat: it is BOTH and work proceeds while the client confirms. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; never controls a furnace.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ledger.read, ledger.post, camera, net, peers.
Planned (no runtime yet): Bourne shell, Kotlin.
Pending: Bourne_shell (jobs): no entry here is an obvious fit; put `pending Bourne_shell <entry>` on the one closest to jobs and name it in your report; `pending Kotlin pieces` (objects).
