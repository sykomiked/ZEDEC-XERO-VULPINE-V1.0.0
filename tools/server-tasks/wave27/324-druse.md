# Wave 27 task: Druse (slot 324), Deep cold storage
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 3 (033 seedbank), wave 5 (019 cellar), wave 12 (145 cartulary), wave 17 (210 moraine), wave 21 (263 grotto).
Manifest: `slot 324`, `layer L3`, `icon icons/l3/324-druse.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: deep cold storage for things that must outlive you: offline seed copies of data, keys and documents (`backup.run`, `backup.verify`, `files.cross`, `sys.disk`, `recovery.status`).
Uses: `use moraine 1` ("release now"); `use grotto 1` (hidden compartment); `use cartulary 1` (retention days); `use cellar 1` (used share, "safe to repair"); `use seedbank 1` (CID check). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `access_ok` (opening a cold copy needs Moraine's release AND the owner or guardians present) in Ada as Eve + APL as KRON witnessed; `media_decay` (years left per medium from its declared life and age) in FORTRAN as Trebago + APL as KRON witnessed; `storage_fees` (in minor units) in COBOL as Sutra; `shelf` grid in APL as KRON; `holdings` tree in MUMPS as MYRA.
BOTH test: the cold copy of file 3 has content id 90 and the live copy 91: both are kept as BOTH; corruption never overwrites the original. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Never log key bytes; never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: backup, fs.read, fs.write, sys.read, peers, journal.
Planned (no runtime yet): TRAC.
Pending: TRAC (text): no entry here is an obvious fit; put `pending TRAC <entry>` on the one closest to text and name it in your report.
