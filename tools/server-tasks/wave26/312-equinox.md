# Wave 26 task: Equinox (slot 312), Time capsules
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 6 (043 solstice), wave 8 (081 heirloom), wave 12 (145 cartulary), wave 14 (172 escapement), wave 20 (243 varve).
Manifest: `slot 312`, `layer L3`, `icon icons/l3/312-equinox.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: time capsules and time-locked promises: messages, wills and payments released on a chosen date (`files.write`, `files.read`, `estate.status`, `journal.query`, `sys.info`).
Uses: `use escapement 1` (clock offset); `use varve 1` (timeline of dated events); `use cartulary 1` (retention days); `use solstice 1` (day of week, days in month); `use heirloom 1` ("release due"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `release_date_ok` (today at or after the release day AND Heirloom's release due for a will) in Ada as Eve + APL as KRON witnessed; `days_until` (Gregorian day arithmetic) in FORTRAN as Trebago + APL as KRON witnessed; `scheduled_payments` (amounts in minor units, released only by the owner or executor) in COBOL as Sutra + FORTRAN as Trebago witnessed; `calendar` grid in APL as KRON; `recipients` tree in MUMPS as MYRA.
BOTH test: heir 1 says the capsule opens on 1 May and heir 2 on 1 June: the date is BOTH and the capsule stays sealed until resolved. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Legal and financial: advisory; a will's execution is the executor's act; the app never moves money by itself.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, sys.read, ledger.read, notify.
Planned (no runtime yet): Eiffel, Solidity, Gleam.
Pending: `pending Eiffel release_date_ok` (contracts); `pending Solidity release_date_ok` (contracts); Gleam (messaging): no entry here is an obvious fit; put `pending Gleam <entry>` on the one closest to messaging and name it in your report.
