# Wave 13 task: Bindery (slot 168), Library
Read ../APP_WAVE_SPEC.md first. This is an L2 app: manifest says `layer L2` and composes its built_from apps' blocks with `use`; it may only start after those L1 waves are merged.
Depends on: wave 0 (005 scent), wave 6 (039 folio, 042 broadsheet, 044 kindred).
Manifest: `slot 168`, `layer L2`, `icon icons/l2/168-bindery.svg`, `budget 233`, one `use <id> 1` per part below. A part can be used only if its manifest carries `version 1.0.0` and its `provides` lines; the composed logic lines must fit ZXAPP_LOGIC_MAX (128) and the uses ZXAPP_USES (8). If any of these fails, stop and report it; do not raise a limit or edit the part.
What it does: catalogues a home, school or town library in the Hoard (`search.query`, `files.read`, `files.write`) and handles lending and returns, with slips printed by `print.submit`.
Uses: `use scent 1` (`scent.score`); `use folio 1` (page fit for slips and lists); `use broadsheet 1` ("well formed" for imported catalogue files); `use kindred 1` (contact index, field limits, likely duplicates). Call these composed entries; do not re-implement them.
Logic (critical, new decisions only): "may lend" = a copy on the shelf AND the member under their loan limit AND no item overdue, in Ada as Eve + APL as KRON witnessed; loans ledger (loan number, copy, member, due day; loans per member refused on overflow) in COBOL as Sutra + APL as KRON witnessed; demand (loans per title per month, rolling mean floor) exact in FORTRAN as Trebago; loan limits in Ada as Eve; catalogue grid (title by copy state) in APL as KRON; shelf hierarchy in MUMPS as MYRA.
BOTH test: copy 12 is on the shelf by the stock scan and on loan by the ledger: it is BOTH, cannot be lent, and waits for a stock check.
Tests: as the spec, plus a compose test (the app composes against a registry holding its parts, and every part entry it calls resolves) and the BOTH test above.
needs: search, fs.read, fs.write, print.
Now runs (Lingua front-end built): Pascal (twin Tutordrake) for step-by-step teaching: `logic pascal <entry> apps/blocks/bindery/<entry>.pas`, in the twin's canonical form, joined to another language's line for the same entry.
Planned (no runtime yet): Python.
