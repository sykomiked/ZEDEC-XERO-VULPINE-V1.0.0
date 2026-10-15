# Wave 27 task: Archivolt (slot 322), Legacy migration
Read ../APP_WAVE_SPEC.md first. This is an L3 app: manifest says `layer L3` and composes its built_from apps with `use`; it may only start after those waves are merged.
Depends on: wave 10 (127 colonnade); wave 13 (165 compositor); wave 20 (241 shallows); wave 22 (269 col, 268 chrysalis).
Manifest: `slot 322`, `layer L3`, `icon icons/l3/322-archivolt.svg`, `budget 233`, one `use <id> 1` per part below. A part exports only what it `provides`: call an L2 part's own entries, and an entry it composed only if it re-exports it (as Murmuration re-exports swarm_shares). If an entry you need is not exported, or the composed logic lines pass ZXAPP_LOGIC_MAX (128) or the uses ZXAPP_USES (8), stop and report it; do not edit the part or raise a limit.
What it does: migrates old systems to ZXV: reads mainframe data and code, translates it, and runs old and new side by side (`files.read`, `files.write`, `play.run`, `xlate.detect`, `net.command`).
Uses: `use shallows 1` (live translation glossary); `use compositor 1` ("outputs agree", job records); `use col 1` (outside gate); `use chrysalis 1` (guest machines); `use colonnade 1` (verdict across witnessed runs). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): `cutover_ok` (every parity check TRUE over the declared run window, owner approved) in Ada as Eve + APL as KRON witnessed; `parity` (old and new numeric results equal, element by element) in FORTRAN as Trebago + COBOL as Sutra witnessed; `record_map` (old fixed records mapped field by field into COBOL copybook form, refusing a value that does not fit) in COBOL as Sutra; `fields` mapping grid in APL as KRON; `legacy` hierarchy in MUMPS as MYRA.
BOTH test: old system total 10450 and new 10405 for batch 7: the result is BOTH and migration continues with that batch held; no silent error. Join with `lpres_conjoin` / `lpres_disjoin` in C; never re-derive the tables.
Cutover is the owner's act; the old system keeps running until then.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: fs.read, fs.write, ai.ask, net, peers, sys.read.
Planned (no runtime yet): OCaml, Python.
Pending: OCaml (parse): no entry here is an obvious fit; put `pending OCaml <entry>` on the one closest to parse and name it in your report; Python (ai): no entry here is an obvious fit; put `pending Python <entry>` on the one closest to ai and name it in your report.
