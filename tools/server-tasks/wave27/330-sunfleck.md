# Wave 27 task: Sunfleck (slot 330), Tree planting and carbon
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 7 (093 understory); wave 12 (149 dibber, 154 chainman); wave 15 (192 coppice); wave 19 (234 neap).
Manifest: `slot 330`, `layer L3`, `icon icons/l3/330-sunfleck.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: plans and verifies tree planting and carbon storage, with growth records that buyers of credits can audit (`place.list`, `files.read`, `files.write`, `ledger.entries`, `ledger.post`, `ledger.hold`).
Uses: `use dibber 1` (planting plans); `use coppice 1` (carbon held, "felling allowed"); `use chainman 1` (parcel area); `use neap 1` ("entry may post"); `use understory 1` (land entries). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `issue_ok` (credits issued at most the verified carbon held, never twice for one tonne) in Ada as Eve + COBOL as Sutra witnessed; `biomass` (carbon per stand from counts and girth, integer) in FORTRAN as Trebago + APL as KRON witnessed; `credit_sales` (in minor units) in COBOL as Sutra + FORTRAN as Trebago witnessed; `plots` grid in APL as KRON; `stands` hierarchy in MUMPS as MYRA.
BOTH test: satellite counts 1200 trees and the ground 950: the stand is BOTH and its credits are held in escrow; none are sold twice or wrongly. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Carbon figures are advisory, not a certified credit; sales are the owner's act.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: location, fs.read, fs.write, ledger.read, ledger.post.
Planned (no runtime yet): Simula 67, Oberon, SAS.
Pending: `pending Simula_67 biomass` (simulation); `pending Oberon stands` (structure); `pending SAS credit_sales` (report).
