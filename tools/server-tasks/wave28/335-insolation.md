# Wave 28 task: Insolation (slot 335), Energy co-op
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (021 brazier); wave 7 (093 understory); wave 19 (239 isobar, 236 levin, 234 neap).
Manifest: `slot 335`, `layer L3`, `icon icons/l3/335-insolation.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: runs a community wind and solar co-op: generation, storage, shares, mutual-aid grants and grid exports (`power.status`, `ledger.entries`, `ledger.post`, `ledger.hold`, `devmesh.devices`). zxvd has no method for turbine, inverter and meter readings: use only existing methods; list what's missing in your report.
Uses: `use isobar 1` (station readings); `use levin 1` (circuit loads); `use neap 1` ("entry may post"); `use understory 1` (energy cost, "over budget"); `use brazier 1` (battery x100). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `export_ok` (export at most the declared grid allowance AND storage at least its reserve) in Ada as Eve + APL as KRON witnessed; `generation` (expected Wh from wind and sun hours, integer model) in FORTRAN as Trebago + APL as KRON witnessed; `member_grants` (each member's grant by shares, largest remainder, minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `assets` grid (turbine and panel) in APL as KRON; `member_shares` tree in MUMPS as MYRA.
BOTH test: the meter totals 4200 Wh and the inverter 3900: the figure is BOTH and grants are held in escrow until reconciled. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Grid: advisory and human-approved; never switches the grid; grants are posted by the co-op's approved rule and a person.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: sys.read, ledger.read, ledger.post, peers, notify.
Planned (no runtime yet): Erlang.
Pending: Erlang (messaging): no entry here is an obvious fit; put `pending Erlang <entry>` on the one closest to messaging and name it in your report.
