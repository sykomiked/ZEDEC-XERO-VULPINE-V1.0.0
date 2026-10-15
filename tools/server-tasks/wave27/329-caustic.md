# Wave 27 task: Caustic (slot 329), Reef restoration
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (071 gleaner), wave 14 (179 cupel), wave 15 (190 shipwright), wave 19 (230 fen), wave 21 (258 atoll).
Manifest: `slot 329`, `layer L3`, `icon icons/l3/329-caustic.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: surveys and restores coral reefs: dive logs, bleaching records, water quality and restoration plots (`files.read`, `files.write`, `search.query`, `place.list`, `ledger.entries`). zxvd has no method for water temperature and quality readings: use only existing methods; list what's missing in your report.
Uses: `use atoll 1` (marine log); `use fen 1` (specimen register); `use cupel 1` (integer step); `use shipwright 1` (water level ahead); `use gleaner 1` (label counts, row filters). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `dive_ok` (depth and time inside the declared no-stop limits, buddy listed) in Ada as Eve + APL as KRON witnessed; `heat_stress` (degree heating weeks from daily temperatures) in FORTRAN as Trebago + APL as KRON witnessed; `grant_accounts` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `plots` grid in APL as KRON; `species` tree in MUMPS as MYRA.
BOTH test: diver 1 records 40 percent bleaching on plot 6 and diver 2 15 percent: it is BOTH until photo review; never averaged. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Dive limits are advisory; divers follow their training and tables.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, location, ledger.read.
Planned (no runtime yet): Elixir, Logo.
Pending: Elixir (faults): no entry here is an obvious fit; put `pending Elixir <entry>` on the one closest to faults and name it in your report; `pending Logo heat_stress` (geometry).
