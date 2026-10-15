# Wave 28 task: Nymphaeum (slot 334), Drinking water tests
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (093 understory, 094 homestead), wave 12 (148 poultice, 155 alembic), wave 19 (237 weir).
Manifest: `slot 334`, `layer L3`, `icon icons/l3/334-nymphaeum.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: tests household drinking water and filters, warning when quality drops and logging every result (`files.read`, `files.write`, `notes.list`, `journal.query`). zxvd has no method for water test device readings: use only existing methods; list what's missing in your report.
Uses: `use weir 1` (household water map); `use alembic 1` ("step allowed", result tables); `use poultice 1` (care plan escalation); `use understory 1` (rolling-window totals); `use homestead 1` (devices). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `safe_ok` (every measured contaminant at most its declared safe level) in Ada as Eve + APL as KRON witnessed; `contaminant` (trend per contaminant, integer) in FORTRAN as Trebago + APL as KRON witnessed; `testing_costs` (in minor units) in COBOL as Sutra; `samples` grid in APL as KRON; `taps_sources` tree in MUMPS as MYRA.
BOTH test: the strip test reads nitrate 50 mg/L and the lab result 10: it is BOTH and the advice is to boil or use another source until a retest settles it. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Health: advisory; official water authority results take precedence.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, notify, journal.
Planned (no runtime yet): Natural, Go, Icon.
Pending: `pending Natural samples` (query); Go (messaging): no entry here is an obvious fit; put `pending Go <entry>` on the one closest to messaging and name it in your report; Icon (pattern): no entry here is an obvious fit; put `pending Icon <entry>` on the one closest to pattern and name it in your report.
