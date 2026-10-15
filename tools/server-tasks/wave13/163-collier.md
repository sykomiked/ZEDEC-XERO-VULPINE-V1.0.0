# Wave 13 task: Collier (slot 163), Data extraction
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (005 scent), wave 1 (071 gleaner), wave 7 (040 furrow), wave 8 (069 quarry), wave 10 (119 magpie).
Manifest: `slot 163`, `layer L2`, `icon icons/l2/163-collier.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: digs through datasets and files in the Hoard (`search.query`, `files.list`, `files.read`, `files.write`) to extract, clean and label the useful records.
Uses: `use scent 1` (`scent.score`); `use furrow 1` (column sums); `use quarry 1` (sources per topic, citation record); `use gleaner 1` (train and test split, label counts, row filters, size limits); `use magpie 1` ("keep this copy": concealed or private is never kept). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): provenance ledger (each extracted record's source id and offset, numbered without reuse) as fixed records in COBOL as Sutra; column statistics (count, min, max, mean floor) exact in FORTRAN as Trebago; "may this column leave extraction" (not marked personal, or the owner's consent recorded) in Ada as Eve + APL as KRON witnessed; cleaning over column arrays (duplicates by key, values outside a column's range) in APL as KRON; source tree in MUMPS as MYRA.
BOTH test: record 55 has age 34 in source 1 and 43 in source 2: the value is BOTH, labelled, and kept; it is not quietly deleted.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: search, fs.read, fs.write.
Planned (no runtime yet): SQL, PostScript.
