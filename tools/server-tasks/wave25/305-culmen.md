# Wave 25 task: Culmen (slot 305), Executive view
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 0 (061 murmuration), wave 12 (145 cartulary), wave 14 (172 escapement), wave 19 (234 neap), wave 22 (271 cairn).
Manifest: `slot 305`, `layer L3`, `icon icons/l3/305-culmen.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: one dashboard across an organisation: money, people, risks, projects and AI agents (`ledger.accounts`, `ledger.balance`, `swarm.state`, `spaces.get`, `journal.query`).
Uses: `use cairn 1` (proposals and votes); `use neap 1` (A = L + E, "entry may post"); `use escapement 1` (timesheets, schedule matrix); `use cartulary 1` (retention days); `use murmuration 1` (`murmuration.swarm_shares.cycle_share`, `murmuration.swarm_shares.within_limits`). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `risk_ok` (each risk inside the declared appetite) in Ada as Eve + APL as KRON witnessed; `forecast` (next quarter from the trend) in FORTRAN as Trebago + APL as KRON witnessed; `consolidated` (accounts summed across departments in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `kpis` grid in APL as KRON; `org_chart` tree in MUMPS as MYRA.
BOTH test: department A reports 120 staff and department B's roster gives 112 for the same team: the figure is BOTH and the gap is shown, not hidden in a total. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; decisions are people's.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, swarm, peers, journal, notify.
Planned (no runtime yet): Python, Delphi, PHP.
Pending: Python (ai): no entry here is an obvious fit; put `pending Python <entry>` on the one closest to ai and name it in your report; `pending Delphi org_chart` (structure); `pending PHP consolidated` (report).
