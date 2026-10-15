# Wave 28 task: Vernier (slot 341), Grading certificates
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 10 (120 gridmark), wave 12 (145 cartulary), wave 13 (167 tintype), wave 14 (177 caliper), wave 20 (246 lode).
Manifest: `slot 341`, `layer L3`, `icon icons/l3/341-vernier.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: inspects and grades finished parts or stones to fine tolerance, issuing a signed certificate for each piece (`files.read`, `files.write`, `journal.query`); the certificate is a Hoard file.
Uses: `use lode 1` (valuables register); `use caliper 1` ("within tolerance"); `use tintype 1` (exposure value, "possibly altered"); `use cartulary 1` (retention days); `use gridmark 1` (QR version for the certificate code). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `cut_ok` (each facet angle and depth inside the declared cut limits) in Ada as Eve + APL as KRON witnessed; `tolerance` (deviation per measurement, micrometres) in FORTRAN as Trebago + APL as KRON witnessed; `certificates` (certificate number, piece, grades, never reused) in COBOL as Sutra; `facets` angle arrays in APL as KRON; `provenance` tree in MUMPS as MYRA.
BOTH test: grader 1 gives grade G and grader 2 grade H: the certificate shows BOTH grades; none is forced. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, camera.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for proofs of correctness: `logic algol60 <entry> apps/blocks/vernier/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): RPG, ML.
Pending: `pending RPG certificates` (report); `pending ML cut_ok` (proof); `pending ALGOL_60 cut_ok` (proof).
