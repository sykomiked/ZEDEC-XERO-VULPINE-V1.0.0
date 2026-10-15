# Wave 33 task: Heliostat (slot 402), Neighbourhood solar
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 16 (199 furnace); wave 18 (221 watershed); wave 19 (236 levin, 234 neap); wave 7 (093 understory).
Manifest: `slot 402`, `layer L3`, `version 1.0.0`, `icon icons/l3/402-heliostat.svg`, `budget 233`, model `apps/heliostat_model.{h,c}`, blocks under kernel/src/apps/blocks/heliostat/, one `use <id> 1` per part below. Each part's manifest is itself the block (C1): an L2 part exports its own entries and the L1 entries it composed (`<part>.<block>.<entry>`). The composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8); if either fails, stop and report it; do not raise a limit or edit a part.
What it does: Plans, monitors and shares solar power across homes, settling surplus energy credits between neighbours: power (`power.status`, `power.top`, `power.budget`), the books (`ledger.post`, `ledger.entries`, `ledger.hold`, `ledger.holds`, `ledger.release`), votes on shared rules (`spaces.propose`, `spaces.vote`). Inverter and meter readings have no method: use only existing methods; list what's missing in your report.
Uses: `use furnace 1` (energy accounts, expected solar Wh, "within grid limits"); `use levin 1` ("circuit within its breaker"); `use neap 1` ("entry may post"); `use watershed 1` (member shares and "spend allowed"); `use understory 1` ("over budget"). Call these composed entries; do not re-implement them.
Logic (new decisions only; each `name` is the entry; a critical one has two or three `logic` lines for one `provides`, so the witness holds it as BOTH when they disagree):
- `inverter_ok`: (critical) inverter limits (export at most the allowance, battery at least the reserve) in Ada as Eve + APL as KRON witnessed
- `credits`: (critical) energy credits (surplus Wh x price per neighbour, largest remainder, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; settled only on the owners' approval
- `irradiance`: irradiance model (Wh per panel from sun minutes and tilt table) in FORTRAN as Trebago
- `settle_jobs`: settlement schedule (end of each declared period) in Ada as Eve
- `panels`: panel output grid in APL as KRON
- `households`: household tree in MUMPS as MYRA
Paraconsistent (BOTH test, in test_zxapp.c): the meter says 12.4 kWh exported and the inverter 10.9: the export is BOTH; the test (scratch books) asserts state 3 and the credits on `ledger.hold` until checked. Join the sources with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Safety: Grid and money actions are advisory and human-approved; never controls the grid; tests use scratch books.
Integers only: no float or double anywhere; a division that is not whole uses kernel/src/rational in C.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, ledger.read, ledger.post, peers, net, notify.
Pending (P1; the best-fit language has no front-end yet, the block above stands in): `pending Erlang credits`; `pending Bash settle_jobs`; `pending Tcl irradiance`.
Planned (no runtime yet): Erlang, Bash, Tcl.
