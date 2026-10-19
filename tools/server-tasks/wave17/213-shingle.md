# Wave 17 task: Shingle (slot 213), Slot registry
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 1 (007 satchel, 008 gatehouse, 018 witness), wave 3 (033 seedbank).
Manifest: `slot 213`, `layer L2`, `icon icons/l2/213-shingle.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: registers every program in its slot: checks the signed manifest's slot, layer, capabilities and budget at admission, and publishes and fetches blocks by content id (`aipi.put`, `aipi.get`, `web4.cid`), with admissions on the journal (`journal.query`).
Uses: `use satchel 1` (package record, limits); `use gatehouse 1` (apps per capability); `use witness 1` (entry record, chain intact); `use seedbank 1` (CID check, size limits, pin record). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "admit" (slot in its layer, the same rule as `zxapp_slot_layer`: L1 1-144, L2 145-271, L3 272-407; every need known to `zxapp_cap_known`; budget declared) in Ada as Eve + APL as KRON witnessed, refusing a manifest whose layer does not match; version ordering (major.minor.patch) in FORTRAN as Trebago + APL as KRON witnessed; slot table of 407 in APL as KRON; registry ledger (admission number, slot, never reused) in COBOL as Sutra; manifest records in MUMPS as MYRA.
BOTH test: a block whose content id matches and whose signature does not: it is BOTH and not installed until resolved.
Never copy anything from build_system/keys/.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, net, journal, sys.read.
Planned (no runtime yet): ML, ALGOL 60, TeX.
