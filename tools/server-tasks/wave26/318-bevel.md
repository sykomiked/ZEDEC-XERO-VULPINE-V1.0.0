# Wave 26 task: Bevel (slot 318), Redaction
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 1 (008 gatehouse), wave 6 (042 broadsheet), wave 12 (145 cartulary), wave 14 (173 warding), wave 20 (242 silt).
Manifest: `slot 318`, `layer L3`, `icon icons/l3/318-bevel.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: finds and redacts personal data in documents and datasets before sharing, keeping only what is needed (`files.read`, `files.write`, `search.query`, `journal.query`).
Uses: `use silt 1` (records office retention); `use warding 1` (key register for sealed originals); `use cartulary 1` (retention days); `use gatehouse 1` ("is this use allowed"); `use broadsheet 1` ("well formed"). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `redact` (a field is redacted unless every rule marks it public) in Ada as Eve + APL as KRON witnessed; `reid_risk` (smallest group size sharing the kept fields, integer) in FORTRAN as Trebago + APL as KRON witnessed; `disclosure_log` (disclosure number, recipient, fields, never reused) in COBOL as Sutra; `sensitivity` grid (field by rule) in APL as KRON; `records` hierarchy in MUMPS as MYRA.
BOTH test: field "town" is personal under rule 2 and public under rule 5: it is BOTH and redacted until reviewed. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Originals are never changed; redacted copies only.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, journal.
Planned (no runtime yet): SQL, Prolog, Icon.
Pending: `pending SQL sensitivity` (query); `pending Prolog sensitivity` (search); Icon (text): no entry here is an obvious fit; put `pending Icon <entry>` on the one closest to text and name it in your report.
