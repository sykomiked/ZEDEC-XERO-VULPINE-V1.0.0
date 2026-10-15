# Wave 27 task: Arcology (slot 325), City services
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 9 (094 homestead), wave 18 (221 watershed), wave 19 (236 levin, 237 weir, 239 isobar).
Manifest: `slot 325`, `layer L3`, `icon icons/l3/325-arcology.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: city services in one place: permits, repairs, transport, utilities and resident requests with public progress (`files.read`, `files.write`, `pspace.post`, `ledger.entries`, `place.list`).
Uses: `use watershed 1` (cooperative projects); `use levin 1` (wiring and load checks); `use weir 1` (water maps, leak alerts); `use isobar 1` (station readings); `use homestead 1` (devices). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `service_level_ok` (a request answered within its declared days) in Ada as Eve + APL as KRON witnessed; `traffic_utility` (daily demand per district from history) in FORTRAN as Trebago + APL as KRON witnessed; `revenue` (city revenue per line in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `districts` grid in APL as KRON; `wards_streets` tree in MUMPS as MYRA.
BOTH test: roads report repair 412 done and parks report it open: it is BOTH and shown to residents until checked. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Advisory; utilities are operated by qualified staff.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, peers, net, ledger.read, location, notify.
Planned (no runtime yet): Elixir, JCL.
Pending: Elixir (faults): no entry here is an obvious fit; put `pending Elixir <entry>` on the one closest to faults and name it in your report; JCL (jobs): no entry here is an obvious fit; put `pending JCL <entry>` on the one closest to jobs and name it in your report.
