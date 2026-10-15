# Wave 26 task: Collet (slot 309), Data escrow
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 4 (009 lockbox), wave 6 (081 heirloom), wave 12 (145 cartulary, 153 pavise), wave 14 (173 warding).
Manifest: `slot 309`, `layer L3`, `icon icons/l3/309-collet.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: locks sensitive data in escrow that opens only under agreed rules, such as two keyholders or a set date (`recovery.setup`, `recovery.status`, `recovery.approve`, `recovery.veto`, `files.cross`, `journal.query`).
Uses: `use warding 1` ("key active"); `use pavise 1` (combined "allow"); `use cartulary 1` (retention days); `use lockbox 1` (key expiry, limits); `use heirloom 1` ("release due"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `release_ok` (the agreed rule met: k of n keyholders approve, or the set date reached, with no veto) in Ada as Eve + APL as KRON witnessed; `shares` (k-of-n share counts and thresholds, integers only, no key bytes) in FORTRAN as Trebago + APL as KRON witnessed; `escrow_fees` (in minor units) in COBOL as Sutra; `keyholders` matrix in APL as KRON; `policies` tree in MUMPS as MYRA.
BOTH test: keyholder 1 approves release and keyholder 2 refuses: the release is BOTH and the data stays sealed, not destroyed. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Never log key bytes; tests use counts only; never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: peers, journal, notify, fs.read, fs.write, sys.read.
Planned (no runtime yet): Prolog, Eiffel, Solidity.
Pending: `pending Prolog release_ok` (rules); `pending Eiffel release_ok` (contracts); `pending Solidity release_ok` (contracts).
