# Wave 28 task: Windlass (slot 340), Mine or quarry
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (093 understory); wave 12 (152 ratchet); wave 13 (163 collier); wave 18 (228 rapids); wave 19 (234 neap).
Manifest: `slot 340`, `layer L3`, `icon icons/l3/340-windlass.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: plans, logs and audits a mine or quarry: ore grades, shifts, safety checks and royalty payments (`files.read`, `files.write`, `search.query`, `ledger.entries`, `ledger.post`, `ledger.hold`). zxvd has no method for gas detector readings: use only existing methods; list what's missing in your report.
Uses: `use collier 1` (column statistics); `use ratchet 1` ("fault", next repair step); `use rapids 1` (triage records); `use neap 1` ("entry may post"); `use understory 1` (land entries). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `gas_depth_ok` (gas readings and working depth inside the declared limits before a shift goes in) in Ada as Eve + APL as KRON witnessed; `ore_grade` (grade per block from assays) in FORTRAN as Trebago + APL as KRON witnessed; `royalty_ledger` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `assays` grid in APL as KRON; `crew_health` tree in MUMPS as MYRA.
BOTH test: the assay grade is 2.4 g/t and the survey grade 1.6: the block is BOTH, extraction continues and the disputed royalty sits in escrow (`ledger.hold`). Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Life-safety: advisory; the mine's safety officer decides every entry.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, ledger.read, ledger.post, notify.
Planned (no runtime yet): PHP, Occam, Simula 67.
Pending: `pending PHP royalty_ledger` (report); Occam (concurrency): no entry here is an obvious fit; put `pending Occam <entry>` on the one closest to concurrency and name it in your report; `pending Simula_67 ore_grade` (simulation).
