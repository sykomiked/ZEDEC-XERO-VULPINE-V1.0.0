# Wave 28 task: Athenaeum (slot 339), Library
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (042 broadsheet), wave 8 (067 lore), wave 12 (145 cartulary), wave 13 (168 bindery), wave 19 (233 stratum).
Manifest: `slot 339`, `layer L3`, `icon icons/l3/339-athenaeum.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: a community or national library: catalogue, lending, digital collections and reading rooms (`search.query`, `files.read`, `files.write`, `print.submit`).
Uses: `use bindery 1` ("may lend", loans ledger, demand); `use cartulary 1` (retention days); `use stratum 1` (source agreement); `use lore 1` ("may the AI read this file"); `use broadsheet 1` ("well formed"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `loan_ok` (Bindery's "may lend" AND the reading-room seat free for on-site items) in Ada as Eve + APL as KRON witnessed; `demand` (holds per title, next quarter) in FORTRAN as Trebago + APL as KRON witnessed; `fees_fines` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `catalogue` arrays in APL as KRON; reading-room steps for new readers in BASIC (twin Hatchling); `collections` hierarchy in MUMPS as MYRA.
BOTH test: two catalogue records describe one work with different years: both are held as BOTH until a librarian merges them; no data lost. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Twin form: the BASIC block is committed in its twin form (Hatchling) and its manifest logic line names `hatchling`.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: search, fs.read, fs.write, print, ai.ask.
Planned (no runtime yet): Java, Python.
Pending: `pending Java collections` (objects); Python (ai): no entry here is an obvious fit; put `pending Python <entry>` on the one closest to ai and name it in your report.
