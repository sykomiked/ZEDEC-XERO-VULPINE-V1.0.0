# Wave 24 task: Sanctum (slot 295), Custody
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (009 lockbox); wave 6 (081 heirloom); wave 12 (153 pavise); wave 14 (173 warding); wave 19 (234 neap).
Manifest: `slot 295`, `layer L3`, `icon icons/l3/295-sanctum.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: custody for valuables and digital assets: who holds what, guardians, withdrawal rules and signed audits (`ledger.accounts`, `ledger.balance`, `ledger.holds`, `recovery.status`, `journal.query`, `journal.verify`).
Uses: `use warding 1` ("key active", key register); `use pavise 1` (combined "allow"); `use neap 1` ("entry may post", A = L + E); `use heirloom 1` ("release due", split preview); `use lockbox 1` (key expiry, limits). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `withdraw_ok` (amount at most the per-day limit AND the holder's guardians' approvals met AND no claim open on the asset) in Ada as Eve + COBOL as Sutra witnessed; `valuation` (holdings at the declared prices in minor units) in FORTRAN as Trebago + COBOL as Sutra witnessed; `custody_ledger` (holder, asset, quantity, refused on overflow) in COBOL as Sutra; `holdings` grid in APL as KRON; `beneficiaries` tree in MUMPS as MYRA.
BOTH test: two people claim item 5: the ownership is BOTH, the asset is frozen in escrow until an arbiter or guardian rules. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Financial: every withdrawal is the owner's and guardians' act; never log key bytes; never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: ledger.read, ledger.post, peers, journal, notify.
Planned (no runtime yet): SAS, Haskell, ML.
Pending: `pending SAS custody_ledger` (report); `pending Haskell withdraw_ok` (proof); `pending ML withdraw_ok` (proof).
