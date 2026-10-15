# Wave 27 task: Spire (slot 323), Geology and mining surveys
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 8 (093 understory), wave 12 (154 chainman), wave 13 (163 collier), wave 14 (179 cupel), wave 21 (256 massif).
Manifest: `slot 323`, `layer L3`, `icon icons/l3/323-spire.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: manages geology and mining surveys: samples, claims, drill logs, reserves and land restoration (`files.read`, `files.write`, `search.query`, `place.list`, `aipi.execute`).
Uses: `use collier 1` (column statistics, provenance ledger); `use massif 1` (distributed integer compute); `use chainman 1` (parcel area, overlaps); `use cupel 1` (integer step, precision bounds); `use understory 1` (land entries). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `extraction_ok` (tonnes at most the permit AND inside the claim's parcel) in Ada as Eve + APL as KRON witnessed; `ore_body` (tonnage x grade from drill intervals, integer) in FORTRAN as Trebago + APL as KRON witnessed; `royalties` (royalty accounts in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `drill` grid in APL as KRON; `claims` hierarchy in MUMPS as MYRA.
BOTH test: lab A assays sample 51 at 3.2 g/t and lab B at 1.9: it is BOTH and reserves are not stated from it until resampled. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Reserve figures are advisory and reviewed by a qualified person.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, search, location, peers, net.
Now runs (Lingua front-end built): Logo (twin Kiteling) for geometry: `logic logo <entry> apps/blocks/spire/<entry>.lg`, in the twin's canonical form, joined to another language's line for the same entry.
Pending: `pending Logo ore_body` (geometry).
