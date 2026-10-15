# Wave 23 task: Lozenge (slot 281), Beekeeping
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 2 (079 fairground), wave 14 (170 sheaf), wave 15 (185 skep), wave 19 (234 neap).
Manifest: `slot 281`, `layer L3`, `icon icons/l3/281-lozenge.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: tracks hives, pollination contracts with growers and honey sales for beekeepers and their co-op (`ledger.entries`, `ledger.post`, `net.offers`, `files.read`, `files.write`).
Uses: `use skep 1` (its colony coordination tree pattern, grant ledger); `use sheaf 1` (farm accounts, "application allowed"); `use neap 1` ("entry may post"); `use fairground 1` (the assurance fee, "offer allowed"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `placement_ok` (a hive at least the declared distance from any field sprayed in the last n days) in Ada as Eve + APL as KRON witnessed; `colony_strength` (frames of bees and brood, weekly trend) in FORTRAN as Trebago + APL as KRON witnessed; `pollination_invoice` (hives x days x rate in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `hives` grid in APL as KRON; `lineage` tree (queen lines) in MUMPS as MYRA.
BOTH test: the grower records hive 14 in field B and the beekeeper in field C: the placement is BOTH until a field check; the contract stays in force. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Financial: invoices and postings are the owner's act.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, fs.read, fs.write, net, peers.
Planned (no runtime yet): Eiffel, Solidity.
Pending: `pending Eiffel placement_ok` (contracts); `pending Solidity placement_ok` (contracts).
