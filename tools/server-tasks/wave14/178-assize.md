# Wave 14 task: Assize (slot 178), Dispute cases
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (018 witness), wave 3 (132 twelvefold), wave 6 (042 broadsheet, 047 sealwax).
Manifest: `slot 178`, `layer L2`, `icon icons/l2/178-assize.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: runs dispute cases: filings, evidence, hearings and rulings in the Hoard (`files.read`, `files.write`), all on the signed record (`journal.query`, `journal.verify`).
Uses: `use witness 1` (entry record, chain intact); `use broadsheet 1` ("well formed", annotation record); `use sealwax 1` ("ready to sign", field record); `use twelvefold 1` ("conclusion follows", assignments per value). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "filing accepted" (by a party to the case AND inside the deadline AND within the procedural limits) in Ada as Eve + APL as KRON witnessed; days left to respond (deadline day minus today) in FORTRAN as Trebago + APL as KRON witnessed; case docket (case number, filing sequence, never reused) in COBOL as Sutra; evidence matrix (evidence by claim, four values) in APL as KRON; case tree in MUMPS as MYRA.
BOTH test: witness 1 places the event at site A and witness 2 at site B for one claim: the claim is BOTH and held in escrow until the arbiter rules; the record never collapses.
Rulings are the arbiter's (a person's); the app never decides a case.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, journal, notify.
Now runs (Lingua front-end built): ALGOL 60 (twin Rootwyrm) for proofs of correctness: `logic algol60 <entry> apps/blocks/assize/<entry>.alg`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Prolog, ML.
